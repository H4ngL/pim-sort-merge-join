#include <stdio.h>
#include <defs.h>
#include <barrier.h>
#include <handshake.h>
#include <mutex.h>
#include <stdint.h>
#include <string.h>
#include <mram.h>
#include <alloc.h>
#include "common.h"

#define CACHE_SIZE 256

BARRIER_INIT(my_barrier, NR_TASKLETS);
MUTEX_INIT(my_mutex);

__host dpu_block_t bl;

uint32_t message[NR_TASKLETS];
uint32_t message_partial_count;

unsigned int select(int tasklet_id, T *input, T *output, int size, int col_num, int select_col, T select_val)
{
    unsigned int cnt = 0;
    int row_num = size / (col_num * sizeof(T));

    for (int i = 0; i < row_num; i++)
    {
        if (input[i * col_num + select_col] > select_val)
        {
            memcpy(output + cnt * col_num, input + i * col_num, col_num * sizeof(T));
            // printf("T %d - row_num: %d / input: %d / output: %d!!!!!!!!!!!\n", tasklet_id, row_num, input[i * col_num + select_col], output + cnt * col_num);
            cnt++;
        }
    }

    return cnt;
}

unsigned int handshake_sync(unsigned int l_count, unsigned int tasklet_id, bool is_last)
{
    unsigned int p_count;

    if (tasklet_id != 0)
    {
        handshake_wait_for(tasklet_id - 1);
        p_count = message[tasklet_id];
    }
    else
        p_count = 0;

    if (tasklet_id < NR_TASKLETS - 1 && !is_last)
    {
        message[tasklet_id + 1] = p_count + l_count;
        handshake_notify();
    }

    return p_count;
}

int main()
{
    // -------------------- Allocate --------------------

    unsigned int tasklet_id = me();
    int col_num = bl.col_num;
    int row_num = bl.row_num;
    unsigned int select_col = bl.table_num == 0 ? SELECT_COL1 : SELECT_COL2;
    unsigned int select_val = bl.table_num == 0 ? SELECT_VAL1 : SELECT_VAL2;

    int one_row_size = col_num * sizeof(T);
    int cache_size = CACHE_SIZE / one_row_size * one_row_size;
    int input_size = row_num * col_num * sizeof(T);

    // int row_per_tasklet = row_num / NR_TASKLETS;
    // // if(tasklet_id == NR_TASKLETS - 1) row_per_tasklet = row_num - NR_TASKLETS * row_per_tasklet;
    // if(row_per_tasklet * one_row_size < cache_size) cache_size = row_per_tasklet * one_row_size;
    
    int row_per_tasklet = row_num / NR_TASKLETS == 0 ? 1 : row_num / NR_TASKLETS;
    if (cache_size > row_per_tasklet * one_row_size)
        cache_size = row_per_tasklet * one_row_size;


    if (tasklet_id == 0)
    {
        mem_reset();
    }

    barrier_wait(&my_barrier);

    // -------------------- Select --------------------

    uint32_t mram_base_addr = (uint32_t)DPU_MRAM_HEAP_POINTER;
    T *cache_A = (T *)mem_alloc(cache_size);
    T *cache_B = (T *)mem_alloc(cache_size);

    if (tasklet_id == NR_TASKLETS - 1)
        message_partial_count = 0;

    barrier_wait(&my_barrier);

    for (int byte_index = tasklet_id * cache_size; byte_index < input_size; byte_index += cache_size * NR_TASKLETS)
    {
        bool is_last = input_size - byte_index <= cache_size;
        if (is_last)
        {
            cache_size = input_size - byte_index;
        }

        mram_read((__mram_ptr void const *)(mram_base_addr + byte_index), cache_A, cache_size);
        barrier_wait(&my_barrier);
        printf("tasklet %d - byte %d / curnum %ld\n", tasklet_id, byte_index, cache_A[0]);
        uint32_t l_count = select(tasklet_id, cache_A, cache_B, cache_size, col_num, select_col, select_val);
        uint32_t p_count = handshake_sync(l_count, tasklet_id, is_last);

        barrier_wait(&my_barrier);

        mram_write(cache_B, (__mram_ptr void *)(mram_base_addr + (message_partial_count + p_count) * one_row_size), l_count * one_row_size);

        if (tasklet_id == NR_TASKLETS - 1)
        {
            bl.row_num = message_partial_count + p_count + l_count;
            message_partial_count = bl.row_num;
        }
        else if (is_last)
        {
            bl.row_num += p_count + l_count;
            mem_reset();
        }

        barrier_wait(&my_barrier);

        T* total = (T*)mem_alloc(bl.row_num * bl.col_num * sizeof(T));
        if(tasklet_id == NR_TASKLETS - 1) {
            mram_read((__mram_ptr void const *)(mram_base_addr), total, bl.row_num * bl.col_num * sizeof(T));
            for(int r = 0; r < row_num; r++) {
                for(int c = 0; c < bl.col_num; c++) {
                    printf("%ld ", *(total + r * col_num + c));
                }
                printf("\n");
            }
        }

#ifdef DEBUG
        mutex_lock(my_mutex);
        if (is_last)
        {
            printf("Table %d select results: %d\n", bl.table_num, bl.row_num);
        }
        mutex_unlock(my_mutex);
#endif
    }

    barrier_wait(&my_barrier);

    return 0;
}
