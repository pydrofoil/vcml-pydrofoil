#include "pydrofoilcapi.h"
#include <stdio.h>
#include <stdlib.h>

/*
 * Organised as a linked list of memory blocks
 */

struct block {
  uint64_t block_id;
  uint8_t *mem;
  struct block *next;
};

struct mem {
    struct block *first_block;
};

uint64_t BLOCK_MASK = 0xFFFFFFul;

int get_dma_region(void* cpu, uint64_t guest_addr, pydrofoil_dma_region_t* region, void* payload) {
    struct mem* mem = (struct mem*) payload;
    uint64_t block_id = guest_addr & ~BLOCK_MASK;

    // Search for existing block
    struct block *current = mem->first_block;
    while (current != NULL) {
        if (current->block_id == block_id) {
            region->host_ptr = (void*)current->mem;
            region->guest_base = block_id;
            region->size = BLOCK_MASK + 1;
            printf("DMA request for 0x%llx -> returning existing block at 0x%llx, size %llu bytes\n",
                   (unsigned long long)guest_addr, (unsigned long long)block_id,
                   (unsigned long long)region->size);
            return 0;
        }
        current = current->next;
    }

    // Allocate new block on-demand
    struct block *new_block = (struct block *)malloc(sizeof(struct block));
    new_block->block_id = block_id;
    new_block->mem = (uint8_t *)calloc(BLOCK_MASK + 1, 1);
    new_block->next = mem->first_block;
    mem->first_block = new_block;

    region->host_ptr = (void*)new_block->mem;
    region->guest_base = block_id;
    region->size = BLOCK_MASK + 1;
    printf("DMA request for 0x%llx -> allocated NEW block at 0x%llx, size %llu bytes\n",
           (unsigned long long)guest_addr, (unsigned long long)block_id,
           (unsigned long long)region->size);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        // Print usage information if not enough arguments are provided
        printf("Usage: %s <riscv binary> <number of steps to run>\n", argv[0]);
        return -1; // Exit with an error code
    }
    int steps = atoi(argv[2]);
    void* cpu = pydrofoil_allocate_cpu("rv64", argv[1]);
    if (cpu == NULL) {
        printf("Failed to allocate CPU for the provided binary.\n");
        return -1; // Exit with an error code
    }
    struct mem mem;
    mem.first_block = NULL;
    int res = pydrofoil_cpu_set_dma_callback(cpu, get_dma_region, &mem);
    pydrofoil_cpu_simulate(cpu, steps);
    uint64_t cycles = pydrofoil_cpu_cycles(cpu);
    printf("Simulation completed. Total cycles: %llu\n", (unsigned long long)cycles);
    printf("reset\n");
    pydrofoil_cpu_reset(cpu);
    printf("running quietly\n");
    pydrofoil_cpu_set_verbosity(cpu, 0);
    printf("Reset PC %ld\n", pydrofoil_cpu_pc(cpu));
    res = pydrofoil_cpu_set_pc(cpu, 4096);
    if (res != 0) {
        printf("setting pc failed\n");
        return -1;
    }
    pydrofoil_cpu_simulate(cpu, steps);
    cycles = pydrofoil_cpu_cycles(cpu);
    printf("Simulation completed. Total cycles: %llu\n", (unsigned long long)cycles);
    uint64_t pc = pydrofoil_cpu_pc(cpu);
    printf("current pc: %llu\n", (unsigned long long)pc);
    if (pydrofoil_free_cpu(cpu) != 0) {
        printf("Failed to free CPU resources.\n");
        return -1; // Exit with an error code
    }
    printf("freed successfully\n");
    return 0; // Success
}
