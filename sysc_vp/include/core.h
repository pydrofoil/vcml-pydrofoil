#ifndef CORE_H
#define CORE_H

#include "vcml.h"
#include <future>
#include <variant>
#include <systemc>
#include "python_tasks.h"


struct PythonTask;
class PydrofoilCore : public vcml::processor{
    public:
        vcml::property<std::string> elf;
        PydrofoilCore(const sc_core::sc_module_name& name,const char* cpu_type);
        ~PydrofoilCore();

        void* cpu;

        bool use_dmi;
        tlm::tlm_dmi dmi_cache;
        bool sim_started = false;
        int n_cycles = 0;

        enum class MemTask {Read, Write};
        struct MemAccess {
            MemTask type;
            size_t addr;
            size_t size;
            size_t* dest; // for reads
            size_t value; // for writes
            std::promise<bool> result;
        };
        std::mutex memtask_mutex;
        std::condition_variable memtask_cv;
        std::queue<MemAccess> memtask_queue;

        // This method gets repeatedly called by the processor class
        // The number of steps/cycles depends on the quantum
        void simulate(size_t cycles) override;
        vcml::u64 cycle_count() const override;
        void reset() override;

        bool write_reg_dbg(size_t reg, const void* buf, size_t len) override;
        bool read_reg_dbg(size_t regno, void* buf, size_t len) override;

    private:
        std::thread python_worker_thread;
        mutable std::queue<PythonTask> task_queue; // mutable is needed to relax the const-correctness compiler check
                                                   // should only have one element
        mutable std::condition_variable task_cv;
        mutable std::mutex task_mutex;
        bool stop_worker = false;

        void set_pc(vcml::u64 value); 
        void python_worker_loop();

    protected:
        virtual void end_of_elaboration() override;
        bool get_dmi_ptr(tlm::tlm_command cmd, uint64_t addr);
};

#endif