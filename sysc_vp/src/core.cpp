#include "core.h"
#include <cstdio>


PydrofoilCore::PydrofoilCore(const sc_core::sc_module_name& name, const char* core_type):
vcml::processor(name,"riscv"),
elf("elf","")
{
    python_worker_thread = std::thread(&PydrofoilCore::python_worker_loop, this);

    PythonTask task;
    task.py_funct = Funct::Init;
    task.arg = core_type;
    std::future<uint64_t> done = task.result.get_future();

    {
        std::lock_guard lock(task_mutex);
        task_queue.push(std::move(task));
    }
    task_cv.notify_one(); // notify the waiting thread
    done.get(); // Wait for the result

    define_cpureg_rw(0, "pc",8);
}


PydrofoilCore::~PydrofoilCore()
{
    if(cpu){
        PythonTask task;
        task.py_funct = Funct::FreeCpu;
        std::future<uint64_t> done = task.result.get_future();

        {
            std::lock_guard lock(task_mutex);
            task_queue.push(std::move(task));
            stop_worker = true;
        }
        task_cv.notify_one();
        done.get();
    }

    python_worker_thread.join();
}


bool PydrofoilCore::write_reg_dbg(size_t reg, const void* buf, size_t len)
{
    if(reg == 0 && len==8){
        PythonTask task;
        task.py_funct = Funct::SetPc;
        task.arg = *reinterpret_cast<const vcml::u64*>(buf);
        std::future<uint64_t> done = task.result.get_future();

        {
            std::lock_guard lock(task_mutex);
            task_queue.push(std::move(task));
        }
        task_cv.notify_one(); // notify the waiting thread

        return done.get(); // Wait for the result
    }
    return false;
}


bool PydrofoilCore::read_reg_dbg(size_t regno, void* buf, size_t len){
    if(regno == 0 && len==8){
        PythonTask task;
        task.py_funct = Funct::ReadPc;
        std::future<uint64_t> done = task.result.get_future();

        {
            std::lock_guard lock(task_mutex);
            task_queue.push(std::move(task));
        }
        task_cv.notify_one(); // notify the waiting thread

        *reinterpret_cast<vcml::u64*>(buf) = done.get(); // Wait for the result
        return true;
    }
    return false;
}

// Called from a coroutine
void PydrofoilCore::simulate(size_t cycles)
{
    PythonTask task;
    task.py_funct = Funct::Simulate;
    task.arg = cycles;
    std::future<uint64_t> done = task.result.get_future();

    {
        std::lock_guard lock(task_mutex);
        task_queue.push(std::move(task));
    }
    sim_started = true;
    task_cv.notify_one(); // notify the waiting thread
    

    while(done.wait_for(std::chrono::seconds(0)) != std::future_status::ready){
        
        MemAccess memtask;

        {
            std::unique_lock<std::mutex> lock(memtask_mutex);
            memtask_cv.wait(lock, [&]{return !memtask_queue.empty() ||
                                                (done.wait_for(std::chrono::seconds(0)) == std::future_status::ready);});
            
            if(!memtask_queue.empty()){
                memtask = std::move(memtask_queue.front());
                memtask_queue.pop();
            }
            else
                continue;
            
        }

        bool success = false;
        if(memtask.type == MemTask::Read){
            success = (data.read(memtask.addr, memtask.dest, memtask.size, vcml::SBI_NONE) == tlm::TLM_OK_RESPONSE);
            memset(memtask.dest,0x297,8); // To be removed once the 0x1000 initial accesses are fixed
        }
        else
            success = (data.write(memtask.addr, &memtask.value, memtask.size, vcml::SBI_NONE) == tlm::TLM_OK_RESPONSE);
        
        memtask.result.set_value(success);
    }
    sim_started = false;
}


// Called from a coroutine
vcml::u64 PydrofoilCore::cycle_count() const
{   
    if(sim_started)
        return n_cycles;
    
    PythonTask task;
    task.py_funct = Funct::GetCycles;
    std::future<uint64_t> done = task.result.get_future();

    {
        std::lock_guard lock(task_mutex);
        task_queue.push(std::move(task));
    }
    task_cv.notify_one(); // notify the waiting thread
    return done.get(); // Wait for the result
}


void PydrofoilCore::reset() 
{
    //pydrofoil_cpu_reset(cpu);
}

void PydrofoilCore::set_pc(vcml::u64 value)
{
    PythonTask task;
    task.py_funct = Funct::SetPc;
    task.arg = value;
    std::future<uint64_t> done = task.result.get_future();

    {
        std::lock_guard lock(task_mutex);
        task_queue.push(std::move(task));
    }
    task_cv.notify_one(); // notify the waiting thread

    done.get(); // Wait for the result
}

/* How it would look like without the std::future
   Pros: faster (see profiling)
   Cons: error prone
   --> Unless in the profiling we see that it's the bottleneck we stick with it
void PydrofoilCore::set_pc(vcml::u64 value)
{
    auto task = std::make_shared<PythonTask>(); //both threads refer to the same object
    task->py_funct = Funct::SetPc;
    task->arg = value;

    {
        std::lock_guard lock(task_mutex);
        task_queue.push(task);  // Now we're copying a pointer to the struct
    }
    task_cv.notify_one(); // notify the waiting thread

    {
        std::unique_lock lock(task->done_mutex);
        task->done_cv.wait(lock, [&] { return task->done; });
    }
    return done.value;
}
*/


void PydrofoilCore::python_worker_loop(){
    std::unordered_map<Funct, std::function<void(PythonTask&)>> handlers = create_handlers(*this);

    while(true) {
        PythonTask task;

        {   // We need unique_lock because:
            // 1. we need wait()
            // 2. wait can temporarely release the lock and reacquire once notified
            // Neither 1. nor 2. are supported by lock_guard
            std::unique_lock<std::mutex> lock(task_mutex); 
            task_cv.wait(lock, [this]{ return !task_queue.empty() || stop_worker;});

            if (stop_worker && task_queue.empty())
                break;
            
            task = std::move(task_queue.front()); // PythonTask has std::promise, not copyable!
            task_queue.pop();                      // pop: reason not to use eg vectors
        }   // --> lock released (out of scope)

        auto it = handlers.find(task.py_funct);
        if(it != handlers.end())
            it->second(task);
    }
}


bool PydrofoilCore::get_dmi_ptr(tlm::tlm_command cmd, uint64_t addr)
{
    tlm::tlm_generic_payload tx;
    tx.set_command(cmd);
    tx.set_address(addr);
    tx.set_data_length(1);
    tx.set_dmi_allowed(true);

    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    if (data.get_interface()) { // it ensures that the socket is actually bound to something 
        if (data->get_direct_mem_ptr(tx, dmi_cache))
            return true;
    }
    return false;
}


void PydrofoilCore::end_of_elaboration()
{
    processor::end_of_elaboration();
    //use_dmi = get_dmi_ptr(tlm::TLM_READ_COMMAND, 0x80000000); // Remove magic numbers
    //use_dmi &= get_dmi_ptr(tlm::TLM_READ_COMMAND, 0x1000); // Remove magic numbers

    PythonTask task;
    task.py_funct = Funct::SetCb;
    std::future<uint64_t> done = task.result.get_future();

    {
        std::lock_guard lock(task_mutex);
        task_queue.push(std::move(task));
    }
    task_cv.notify_one(); // notify the waiting thread
    done.get(); // Wait for the result
}