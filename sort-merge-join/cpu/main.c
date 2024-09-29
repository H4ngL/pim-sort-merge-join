#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../timer.h"
#include "../common.h"

#define FILE_1 "../test_data(1).csv"
#define FILE_2 "../test_data(2).csv"
#define STACK_SIZE 100

T *result = NULL;
int result_row_num = 0;
int result_col_num = 0;

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

void load_csv(const char *filename, int col_num, int row_num, T **test_array)
{
    // Allocate memory for the array
    *test_array = (T *)malloc(col_num * row_num * sizeof(T));

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

void select_in_cpu(int col_num, int *row_num, T **test_array, T select_col, T select_val)
{
    int cnt = 0;
    T *original_array = *test_array;

    for (int i = 0; i < *row_num; i++)
    {
        if (original_array[i * col_num + select_col] > select_val)
        {
            cnt++;
        }
    }

    T *result = (T *)malloc(cnt * col_num * sizeof(T));

    for (int i = 0, j = 0; i < *row_num; i++)
    {
        if (original_array[i * col_num + select_col] > select_val)
        {
            for (int k = 0; k < col_num; k++)
            {
                result[j * col_num + k] = original_array[i * col_num + k];
            }
            j++;
        }
    }

    free(*test_array);

    *test_array = result;
    *row_num = cnt;
}

void quick_sort_in_cpu(int col_num, int row_num, int key, T **test_array)
{
    T *original_array = *test_array;
    T *stack = (T *)malloc(STACK_SIZE * sizeof(T));
    int top = -1;

    stack[++top] = 0;
    stack[++top] = row_num - 1;

    while (top >= 0)
    {
        int pRight = stack[top--];
        int pLeft = stack[top--];

        int pivot = original_array[(pRight + pLeft) / 2 * col_num + key];

        int i = pLeft;
        int j = pRight;

        while (i <= j)
        {
            while (original_array[i * col_num + key] < pivot && i <= j)
            {
                i++;
            }

            while (original_array[j * col_num + key] > pivot && i <= j)
            {
                j--;
            }

            if (i <= j)
            {
                for (int k = 0; k < col_num; k++)
                {
                    T temp = original_array[i * col_num + k];
                    original_array[i * col_num + k] = original_array[j * col_num + k];
                    original_array[j * col_num + k] = temp;
                }

                i++;
                j--;
            }
        }

        if (i < pRight)
        {
            stack[++top] = i;
            stack[++top] = pRight;
        }
        if (pLeft < j)
        {
            stack[++top] = pLeft;
            stack[++top] = j;
        }
    }
}

void join_in_cpu(int col_num_1, int row_num_1, T *test_array_1, int col_num_2, int row_num_2, T *test_array_2, int key1, int key2)
{
    int total_col = col_num_1 + col_num_2 - 1;
    int cnt = 0;
    int cur_index1 = 0;
    int cur_index2 = 0;

    while (cur_index1 < row_num_1 && cur_index2 < row_num_2)
    {
        if (test_array_1[cur_index1 * col_num_1 + key1] == test_array_2[cur_index2 * col_num_2 + key2])
        {
            cnt++;
            cur_index1++;
            cur_index2++;
        }
        else if (test_array_1[cur_index1 * col_num_1 + key1] < test_array_2[cur_index2 * col_num_2 + key2])
        {
            cur_index1++;
        }
        else
        {
            cur_index2++;
        }
    }

    result = (T *)malloc(cnt * total_col * sizeof(T));
    result_row_num = cnt;
    result_col_num = total_col;

    cur_index1 = 0;
    cur_index2 = 0;
    int result_index = 0;
    while (cur_index1 < row_num_1 && cur_index2 < row_num_2)
    {
        if (test_array_1[cur_index1 * col_num_1 + key1] == test_array_2[cur_index2 * col_num_2 + key2])
        {
            for (int i = 0; i < col_num_1; i++)
            {
                result[result_index * total_col + i] = test_array_1[cur_index1 * col_num_1 + i];
            }
            for (int i = 0, j = 0; i < col_num_2; i++)
            {
                if (i != key2)
                {
                    result[result_index * total_col + col_num_1 + j] = test_array_2[cur_index2 * col_num_2 + i];
                    j++;
                }
            }

            result_index++;
            cur_index1++;
            cur_index2++;
        }
        else if (test_array_1[cur_index1 * col_num_1 + key1] < test_array_2[cur_index2 * col_num_2 + key2])
        {
            cur_index1++;
        }
        else
        {
            cur_index2++;
        }
    }
}

int main(void)
{
    // Set timer
    Timer timer;

    // Set variables
    int col_num_1 = 0;
    int row_num_1 = 0;
    int total_row_num_1 = 0;
    int col_num_2 = 0;
    int row_num_2 = 0;
    int total_row_num_2 = 0;

    T *test_array_1 = NULL;
    T *test_array_2 = NULL;

    // Set col_num, row_num
    set_csv_size(FILE_1, &col_num_1, &row_num_1);
    set_csv_size(FILE_2, &col_num_2, &row_num_2);
    int row_size = (row_num_1 + row_num_2) / NR_DPUS;

    // Start timer
    start(&timer, 0, 0);

    // Set test_array
    load_csv(FILE_1, col_num_1, row_num_1, &test_array_1);
    load_csv(FILE_2, col_num_2, row_num_2, &test_array_2);

    // select
    select_in_cpu(col_num_1, &row_num_1, &test_array_1, SELECT_COL, SELECT_VAL);
    select_in_cpu(col_num_2, &row_num_2, &test_array_2, SELECT_COL, SELECT_VAL);

    // sort
    quick_sort_in_cpu(col_num_1, row_num_1, JOIN_KEY1, &test_array_1);
    quick_sort_in_cpu(col_num_2, row_num_2, JOIN_KEY2, &test_array_2);

    // join
    join_in_cpu(col_num_1, row_num_1, test_array_1, col_num_2, row_num_2, test_array_2, JOIN_KEY1, JOIN_KEY2);

    // Stop timer
    stop(&timer, 0);

#ifdef DEBUG
    // print debug
    printf("=== SELECT & SORT TABLE 0 ===\n");
    printf("row_num_1: %d\n", row_num_1);
    printf("col_num_1: %d\n\n", col_num_1);
    // for (int i = 0; i < row_num_1; i++)
    // {
    //     for (int j = 0; j < col_num_1; j++)
    //     {
    //         printf("%d ", test_array_1[i * col_num_1 + j]);
    //     }
    //     printf("\n");
    // }
    // printf("\n");

    printf("=== SELECT & SORT TABLE 1 ===\n");
    printf("row_num_2: %d\n", row_num_2);
    printf("col_num_2: %d\n\n", col_num_2);
    // for (int i = 0; i < row_num_2; i++)
    // {
    //     for (int j = 0; j < col_num_2; j++)
    //     {
    //         printf("%d ", test_array_2[i * col_num_2 + j]);
    //     }
    //     printf("\n");
    // }
    // printf("\n");

    printf("=== RESULT TABLE ===\n");
    printf("row_num: %d\n", result_row_num);
    printf("col_num: %d\n", result_col_num);
    print(&timer, 0, 1);
    printf("\n");

    for (int i = 0; i < result_row_num; i++)
    {
        for (int j = 0; j < result_col_num; j++)
        {
            printf("%d ", result[i * result_col_num + j]);
        }
        printf("\n");
    }

    printf("\n");
#endif

    return 0;
}
