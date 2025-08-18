#include "core.h"
#include <cstdio>

extern "C" {
    #include "pydrofoilcapi.h" 
}


// C++ member functions cannot be used as callbacks, we need to define C-style functions
// (not member of the class), but they still need to get access to the class fields
// so we misuse the payload pointer to pass this as argument
// get a dmi pointer check if I can do a dmi access and if yes do the transaction from here
// the out socket has a method o chec for dmi. If the dmi fails then we go the slow way otherwise we're done
// vcml::success(data.access_dmi(
//            tx.is_read ? tlm::TLM_READ_COMMAND : tlm::TLM_WRITE_COMMAND,
//            tx.addr, tx.data, tx.size, info)))
int write_mem(void* cpu, uint64_t address, int size, uint64_t value, void* payload) {
    auto core = reinterpret_cast<PydrofoilCore*>(payload);
    PydrofoilCore::MemAccess memtask;

    memtask.type = PydrofoilCore::MemTask::Write;
    memtask.addr = address;
    memtask.size = size;
    memtask.value = value;

    std::future<bool> res = memtask.result.get_future();

    {
        std::lock_guard lock(core->memtask_mutex);
        core->memtask_queue.push(std::move(memtask));
    }
    core->memtask_cv.notify_one();

    if (res.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready)
        return res.get()? 0:1;
    else
        return 0; // we timed out, but now we get weird callback calls at the beginning --> we return 0
}


// The debug leads to a debug transaction avoid timig annotation --> no wait --> we dont have to be in a sc_thread
// Should be changed, NO access when callbacks are being set!!!
int read_mem(void* cpu, uint64_t address, int size, uint64_t* destination, void* payload) {
    auto core = reinterpret_cast<PydrofoilCore*>(payload);
    PydrofoilCore::MemAccess memtask;

    memtask.type = PydrofoilCore::MemTask::Read;
    memtask.addr = address;
    memtask.size = size;
    memtask.dest = destination;

    std::future<bool> res = memtask.result.get_future();

    {
        std::lock_guard lock(core->memtask_mutex);
        core->memtask_queue.push(std::move(memtask));
    }
    core->memtask_cv.notify_one();

    if (res.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready)
        return res.get()? 0:1;
    else
        return 0; // we timed out, but now we get weird callback calls at the beginning --> we return 0
}


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


bool PydrofoilCore::write_reg_dbg(size_t reg, const void* buf, size_t len){
    if(reg == 0 && len==8)
        return pydrofoil_cpu_set_pc(cpu, *reinterpret_cast<const vcml::u64*>(buf)) == 0;
    return false;
}


bool PydrofoilCore::read_reg_dbg(size_t regno, void* buf, size_t len){
    if(regno == 0 && len==8){
        *reinterpret_cast<vcml::u64*>(buf) = pydrofoil_cpu_pc(cpu);
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
    task_cv.notify_one(); // notify the waiting thread

    // Using done.wait() or done.get() would block us until ready 
    while(done.wait_for(std::chrono::seconds(0)) != std::future_status::ready){
        sc_core::wait(sc_core::SC_ZERO_TIME);
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
        if(memtask.type == MemTask::Read)
            success = (this->data.read(memtask.addr, memtask.dest, memtask.size, vcml::SBI_NONE) == tlm::TLM_OK_RESPONSE);
        else
            success = (this->data.write(memtask.addr, &memtask.value, memtask.size, vcml::SBI_NONE) == tlm::TLM_OK_RESPONSE);
        
        memtask.result.set_value(success);
    }
    std::cout<< "simulate done" << std::endl;
}


// Called from a coroutine
vcml::u64 PydrofoilCore::cycle_count() const
{   
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


void PydrofoilCore::python_worker_loop(){
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

        // Ugly, needs to be improved!!!
        if (task.py_funct == Funct::Simulate){
            auto cycles = std::get<size_t>(task.arg);
            pydrofoil_cpu_simulate(cpu, cycles);
            task.result.set_value(0); //lock the mutex!
            memtask_cv.notify_one();
        } else if (task.py_funct == Funct::GetCycles){
            auto n_cycles = pydrofoil_cpu_cycles(cpu);
            task.result.set_value(n_cycles);
        } else if (task.py_funct == Funct::SetPc){
            auto pc_value = std::get<size_t>(task.arg);
            pydrofoil_cpu_set_pc(cpu, pc_value);
            task.result.set_value(0);
        } else if (task.py_funct == Funct::SetCb){
            int res = pydrofoil_cpu_set_ram_read_write_callback(cpu, read_mem, write_mem, this);
            task.result.set_value(res);
        } else if (task.py_funct == Funct::Init){
            auto core_type = std::get<const char*>(task.arg);
            cpu = pydrofoil_allocate_cpu(core_type, elf.c_str());
            task.result.set_value(0);
        } else if (task.py_funct == Funct::FreeCpu){
            pydrofoil_free_cpu(cpu);
            task.result.set_value(0);
        }
    }
}


void PydrofoilCore::end_of_elaboration()
{
    processor::end_of_elaboration();
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