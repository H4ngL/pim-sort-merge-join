// gcc --std=c99 app.c -o app `dpu-pkg-config --cflags --libs dpu`

#include <assert.h>
#include <dpu.h>
#include <dpu_log.h>
#include <stdio.h>
#include <stdlib.h>
#include "timer.h"
#include "common.h"

#ifndef DPU_BINARY_SELECT
#define DPU_BINARY_SELECT "./select"
#endif

#ifndef DPU_BINARY_SORT_DPU
#define DPU_BINARY_SORT_DPU "./sort_dpu"
#endif

#ifndef DPU_BINARY_MERGE_DPU
#define DPU_BINARY_MERGE_DPU "./merge_dpu"
#endif

dpu_result_t dpu_result[NR_DPUS];

void set_csv_size(const char *filename, int *col_num, int *row_num)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        perror("Failed to open file");
        exit(EXIT_FAILURE);
    }

    char line[1024];
    int is_first_line = 1;

    while (fgets(line, sizeof(line), file))
    {
        if (is_first_line)
        {
            is_first_line = 0;
            char *token = strtok(line, ",");
            while (token)
            {
                (*col_num)++;
                token = strtok(NULL, ",");
            }
        }
        (*row_num)++;
    }

    (*row_num)--;
    fclose(file);
}

void load_csv(const char *filename, int col_num, int row_num, int **test_array)
{
    // Allocate memory for the array
    *test_array = (int *)malloc(col_num * row_num * sizeof(int));

    FILE *file = fopen(filename, "r");
    if (!file)
    {
        perror("Failed to open file");
        exit(EXIT_FAILURE);
    }

    char line[1024];
    int row = 0;

    // Skip header line
    fgets(line, sizeof(line), file);

    // Read each row from the file
    while (fgets(line, sizeof(line), file))
    {
        char *token = strtok(line, ",");
        int col = 0;
        while (token)
        {
            (*test_array)[row * col_num + col] = atoi(token);
            token = strtok(NULL, ",");
            col++;
        }
        row++;
    }

    fclose(file);
}

int main(void)
{
    /* ****************** */
    /* select per tasklet */
    /* ****************** */

    // Set variables
    int first_col_num = 0;
    int first_row_num = 0;
    int first_total_row_num = 0;
    int second_col_num = 0;
    int second_row_num = 0;
    int second_total_row_num = 0;

    int *first_test_array = NULL;
    int *second_test_array = NULL;
    int pivot_id = -1;

    // Allocate DPUs
    struct dpu_set_t set, dpu;
    uint32_t dpu_id;
    DPU_ASSERT(dpu_alloc(NR_DPUS, "backend=simulator", &set));
    DPU_ASSERT(dpu_load(set, DPU_BINARY_SELECT, NULL));

    // Set col_num, row_num
    set_csv_size(FILE_NAME_1, &first_col_num, &first_row_num);
    set_csv_size(FILE_NAME_2, &second_col_num, &second_row_num);
    // int row_size = first_row_num / NR_DPUS;
    int row_size = (first_row_num + second_row_num) / NR_DPUS;

    // Set test_array
    load_csv(FILE_NAME_1, first_col_num, first_row_num, &first_test_array);
    load_csv(FILE_NAME_2, second_col_num, second_row_num, &second_test_array);

    // Set timer
    Timer timer;

    // Set input arguments
    dpu_block_t input_args[NR_DPUS];
    int temp_first_row = first_row_num;
    int temp_second_row = second_row_num;
    for (int i = 0; i < NR_DPUS - 1; i++)
    {
        if (temp_first_row > 0)
        {
            input_args[i].table_num = 0;
            input_args[i].col_num = first_col_num;
            if (temp_first_row > row_size)
            {
                input_args[i].row_num = row_size;
                temp_first_row -= row_size;
            }
            else
            {
                input_args[i].row_num = temp_first_row;
                temp_first_row = 0;
                pivot_id = i + 1;
            }
        }
        else
        {
            input_args[i].table_num = 1;
            input_args[i].col_num = second_col_num;
            if (temp_second_row >= row_size)
            {
                input_args[i].row_num = row_size;
                temp_second_row -= row_size;
            }
            else
            {
                input_args[i].row_num = temp_second_row;
                temp_second_row = 0;
            }
        }
    }
    input_args[NR_DPUS - 1].table_num = 1;
    input_args[NR_DPUS - 1].col_num = second_col_num;
    input_args[NR_DPUS - 1].row_num = temp_second_row;

#ifdef DEBUG
    for (int i = 0; i < NR_DPUS; i++)
    {
        printf("DPU %d: table_num %d, col_num %d, row_num %d\n", i, input_args[i].table_num, input_args[i].col_num, input_args[i].row_num);
    }
#endif

    // Transfer input arguments and test_array to DPUs
    start(&timer, 0, 0);
    DPU_FOREACH(set, dpu, dpu_id)
    {
        DPU_ASSERT(dpu_prepare_xfer(dpu, &input_args[dpu_id]));
    }
    DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "bl", 0, sizeof(input_args[0]), DPU_XFER_DEFAULT));

    DPU_FOREACH(set, dpu, dpu_id)
    {
        int transfer_size = input_args[dpu_id].row_num * input_args[dpu_id].col_num * sizeof(int);
        if (input_args[dpu_id].table_num == 0)
        {
            int offset = dpu_id * row_size * first_col_num;
            DPU_ASSERT(dpu_prepare_xfer(dpu, first_test_array + offset));
            DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0, transfer_size, DPU_XFER_DEFAULT));
        }
        else
        {
            int offset = (dpu_id - pivot_id) * row_size * second_col_num;
            DPU_ASSERT(dpu_prepare_xfer(dpu, second_test_array + offset));
            DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0, transfer_size, DPU_XFER_DEFAULT));
        }
    }

    DPU_ASSERT(dpu_launch(set, DPU_SYNCHRONOUS));

    // Retrieve dpu_result from DPUs
    DPU_FOREACH(set, dpu, dpu_id)
    {
        DPU_ASSERT(dpu_prepare_xfer(dpu, &input_args[dpu_id]));
        DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_FROM_DPU, "bl", 0, sizeof(input_args[0]), DPU_XFER_DEFAULT));
        dpu_result[dpu_id].table_num = input_args[dpu_id].table_num;
        dpu_result[dpu_id].dpu_id = dpu_id;
        dpu_result[dpu_id].col_num = input_args[dpu_id].col_num;
        dpu_result[dpu_id].row_num = input_args[dpu_id].row_num;
        if (dpu_result[dpu_id].table_num == 0)
            first_total_row_num += dpu_result[dpu_id].row_num;
        else
            second_total_row_num += dpu_result[dpu_id].row_num;

        int transfer_size = dpu_result[dpu_id].row_num * dpu_result[dpu_id].col_num * sizeof(int);
        dpu_result[dpu_id].arr = (int *)malloc(transfer_size);
        DPU_ASSERT(dpu_prepare_xfer(dpu, dpu_result[dpu_id].arr));
        DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0, transfer_size, DPU_XFER_DEFAULT));
    }

    int *select_array = (int *)malloc(first_col_num * first_total_row_num * sizeof(int));
    int offset = 0;
    for (int i = 0; i < pivot_id; i++)
    {
        int size = dpu_result[i].row_num * dpu_result[i].col_num;
        memcpy(select_array + offset, dpu_result[i].arr, size * sizeof(int));
        offset += size;
    }

    int *select_array2 = (int *)malloc(second_col_num * second_total_row_num * sizeof(int));
    offset = 0;
    for (int i = pivot_id; i < NR_DPUS; i++)
    {
        int size = dpu_result[i].row_num * dpu_result[i].col_num;
        memcpy(select_array2 + offset, dpu_result[i].arr, size * sizeof(int));
        offset += size;
    }

    stop(&timer, 0);

#ifdef DEBUG
    // Print DPU logs
    DPU_FOREACH(set, dpu)
    {
        DPU_ASSERT(dpu_log_read(dpu, stdout));
    }
#endif

    DPU_ASSERT(dpu_free(set));

    /* ************************ */
    /*     sort in each DPU     */
    /* ************************ */

    // Allocate DPUs
    struct dpu_set_t set1, dpu1;
    DPU_ASSERT(dpu_alloc(NR_DPUS, "backend=simulator", &set1));
    DPU_ASSERT(dpu_load(set1, DPU_BINARY_SORT_DPU, NULL));

    // Set input arguments
    row_size = first_total_row_num / pivot_id;
    for (int i = 0; i < pivot_id - 1; i++)
    {
        input_args[i].col_num = first_col_num;
        input_args[i].row_num = row_size;
        dpu_result[i].row_num = row_size;
    }
    input_args[pivot_id - 1].col_num = first_col_num;
    input_args[pivot_id - 1].row_num = first_total_row_num - (pivot_id - 1) * row_size;
    dpu_result[pivot_id - 1].row_num = first_total_row_num - (pivot_id - 1) * row_size;

    int temp_row_size = second_total_row_num / (NR_DPUS - pivot_id);
    for (int i = pivot_id; i < NR_DPUS - 1; i++)
    {
        input_args[i].col_num = second_col_num;
        input_args[i].row_num = temp_row_size;
        dpu_result[i].row_num = temp_row_size;
    }
    input_args[NR_DPUS - 1].col_num = second_col_num;
    input_args[NR_DPUS - 1].row_num = second_total_row_num - (NR_DPUS - pivot_id - 1) * temp_row_size;
    dpu_result[NR_DPUS - 1].row_num = second_total_row_num - (NR_DPUS - pivot_id - 1) * temp_row_size;

    // Transfer input arguments and test_array to DPUs
    start(&timer, 1, 0);
    DPU_FOREACH(set1, dpu1, dpu_id)
    {
        DPU_ASSERT(dpu_prepare_xfer(dpu1, &input_args[dpu_id]));
    }
    DPU_ASSERT(dpu_push_xfer(set1, DPU_XFER_TO_DPU, "bl", 0, sizeof(input_args[0]), DPU_XFER_DEFAULT));

    DPU_FOREACH(set1, dpu1, dpu_id)
    {
        int transfer_size = input_args[dpu_id].row_num * input_args[dpu_id].col_num * sizeof(int);
        dpu_result[dpu_id].arr = (int *)malloc(transfer_size);
        if (input_args[dpu_id].table_num == 0)
            DPU_ASSERT(dpu_prepare_xfer(dpu1, select_array + dpu_id * row_size * first_col_num));
        else
            DPU_ASSERT(dpu_prepare_xfer(dpu1, select_array2 + (dpu_id - pivot_id) * temp_row_size * second_col_num));
        DPU_ASSERT(dpu_push_xfer(set1, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0, transfer_size, DPU_XFER_DEFAULT));
    }

    DPU_ASSERT(dpu_launch(set1, DPU_SYNCHRONOUS));

    // Retrieve dpu_result from DPUs
    DPU_FOREACH(set1, dpu1, dpu_id)
    {
        int transfer_size = input_args[dpu_id].row_num * input_args[dpu_id].col_num * sizeof(int);
        DPU_ASSERT(dpu_prepare_xfer(dpu1, dpu_result[dpu_id].arr));
        DPU_ASSERT(dpu_push_xfer(set1, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0, transfer_size, DPU_XFER_DEFAULT));
    }
    stop(&timer, 1);

#ifdef DEBUG
    // Print DPU logs
    DPU_FOREACH(set1, dpu1)
    {
        DPU_ASSERT(dpu_log_read(dpu1, stdout));
    }

    // Print result
    printf("===============\n");
    for (int d = 0; d < NR_DPUS; d++)
    {
        printf("Table %d DPU %d sort results:\n", dpu_result[d].table_num, dpu_result[d].dpu_id);
        printf("Rows: %u\n", dpu_result[d].row_num);
        for (int i = 0; i < dpu_result[d].row_num; i++)
        {
            for (int j = 0; j < dpu_result[d].col_num; j++)
            {
                printf("%d ", dpu_result[d].arr[i * dpu_result[d].col_num + j]);
            }
            printf("\n");
        }
        printf("---------------\n");
    }
    printf("total_row_num: %d %d\n", first_total_row_num, second_total_row_num);
    print(&timer, 0, 1);
    printf("\n");
#endif

    DPU_ASSERT(dpu_free(set1));
    free(first_test_array);
    free(second_test_array);
    free(select_array);

    //     /* ********************** */
    //     /* add & sort DPU results */
    //     /* ********************** */

    //     // merge each dpu results
    //     struct dpu_set_t set2, dpu2; // modify label later
    //     int cur_dpus = NR_DPUS;

    //     while (cur_dpus > 1)
    //     {
    //         cur_dpus /= 2;
    //         DPU_ASSERT(dpu_alloc(cur_dpus, "backend=simulator", &set2));
    //         DPU_ASSERT(dpu_load(set2, DPU_BINARY_MERGE_DPU, NULL));

    //         DPU_FOREACH(set2, dpu2, dpu_id)
    //         {
    //             int pair_index = dpu_id * 2;

    //             if (pair_index + 1 < 2) // cur_dpus
    //             {
    //                 DPU_ASSERT(dpu_prepare_xfer(dpu2, &input_args[pair_index]));
    //                 DPU_ASSERT(dpu_push_xfer(set2, DPU_XFER_TO_DPU, "bl1", 0, sizeof(dpu_block_t), DPU_XFER_DEFAULT));
    //                 DPU_ASSERT(dpu_prepare_xfer(dpu2, &input_args[pair_index + 1]));
    //                 DPU_ASSERT(dpu_push_xfer(set2, DPU_XFER_TO_DPU, "bl2", 0, sizeof(dpu_block_t), DPU_XFER_DEFAULT));

    //                 uint32_t first_size = input_args[pair_index].row_num * input_args[pair_index].col_num * sizeof(int);
    //                 uint32_t second_size = input_args[pair_index + 1].row_num * input_args[pair_index + 1].col_num * sizeof(int);
    //                 DPU_ASSERT(dpu_prepare_xfer(dpu2, dpu_result[pair_index].arr));
    //                 DPU_ASSERT(dpu_push_xfer(set2, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0, first_size, DPU_XFER_DEFAULT));
    //                 DPU_ASSERT(dpu_prepare_xfer(dpu2, dpu_result[pair_index + 1].arr));
    //                 DPU_ASSERT(dpu_push_xfer(set2, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, first_size + second_size, second_size, DPU_XFER_DEFAULT));
    //             }
    //         }

    //         DPU_ASSERT(dpu_launch(set2, DPU_SYNCHRONOUS));

    //         DPU_FOREACH(set2, dpu2, dpu_id)
    //         {
    //             int pair_index = dpu_id * 2;
    //             if (pair_index + 1 < cur_dpus)
    //             {
    //                 DPU_ASSERT(dpu_prepare_xfer(dpu2, dpu_result + pair_index));
    //                 DPU_ASSERT(dpu_push_xfer(dpu2, DPU_XFER_FROM_DPU, "dpu_result", 0, sizeof(int), DPU_XFER_DEFAULT));
    //                 DPU_ASSERT(dpu_prepare_xfer(dpu2, dpu_result + pair_index + 1));
    //                 DPU_ASSERT(dpu_push_xfer(dpu2, DPU_XFER_FROM_DPU, "dpu_result", 0, sizeof(int), DPU_XFER_DEFAULT));
    //             }
    //         }

    //         DPU_ASSERT(dpu_free(set2));

    //         int next_dpus = (cur_dpus + 1) / 2;

    //         for (int i = 0; i < next_dpus; ++i)
    //         {
    //             int result_id = i * 2;
    //             if (result_id + 1 < cur_dpus) // Pair exists
    //             {
    //                 input_args[i] = input_args[result_id];
    //                 dpu_result[i] = dpu_result[result_id];
    //             }
    //             else
    //             {
    //                 input_args[i] = input_args[result_id];
    //                 dpu_result[i] = dpu_result[result_id];
    //             }
    //         }

    //         cur_dpus = next_dpus;
    //     }

    // #ifdef DEBUG
    //     // Print result
    //     printf("===============\n");
    //     printf("Table 0 results:\n");
    //     for (int i = 0; i < total_row_num; i++)
    //     {
    //         for (int j = 0; j < first_col_num; j++)
    //         {
    //             printf("%d ", dpu_result[0].arr[i * first_col_num + j]);
    //         }
    //         printf("\n");
    //     }
    //     printf("---------------\n");
    // #endif

    return 0;
}
