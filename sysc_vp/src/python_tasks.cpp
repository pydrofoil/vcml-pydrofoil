#include "python_tasks.h"
#include "core.h"

auto create_handlers(PydrofoilCore& core) // core == alias of the PydrofoilCore, we can use it inside the function as it is
    -> std::unordered_map<Funct, std::function<void(PythonTask&)>>
{
    return {
            {
            Funct::Init, [&core](PythonTask &task){  // the lambda keeps a referece of PydrofoilCore
                auto core_type = std::get<const char*>(task.arg);   
                core.cpu = pydrofoil_allocate_cpu(core_type, nullptr); 
                task.result.set_value(0);
            }},
            {
            Funct::SetCb, [&core](PythonTask &task){
                int res = pydrofoil_cpu_set_ram_read_write_callback(core.cpu, read_mem, write_mem, &core);//
                task.result.set_value(res);
            }},
            {
            Funct::GetCycles, [&core](PythonTask &task){
                core.n_cycles = pydrofoil_cpu_cycles(core.cpu);
                task.result.set_value(core.n_cycles);
            }},
            {
            Funct::Simulate, [&core](PythonTask &task){
                auto cycles = std::get<size_t>(task.arg);
                pydrofoil_cpu_simulate(core.cpu, cycles);
                core.n_cycles = pydrofoil_cpu_cycles(core.cpu);
                task.result.set_value(0); 
                core.memtask_cv.notify_one();
            }},
            {
            Funct::SetPc, [&core](PythonTask &task){
                auto pc_value = std::get<size_t>(task.arg);
                int res = pydrofoil_cpu_set_pc(core.cpu, pc_value);
                task.result.set_value(int(res == 0));
            }},
            {
            Funct::ReadPc, [&core](PythonTask &task){
                auto pc_value = pydrofoil_cpu_pc(core.cpu);
                task.result.set_value(pc_value);
            }},
            {
            Funct::FreeCpu, [&core](PythonTask &task){
                pydrofoil_free_cpu(core.cpu);
                task.result.set_value(0);
            }}
    };
}