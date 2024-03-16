#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include "libcoro.h"

struct my_context {
    char *name;
    char **file_list;
    int file_count;
    int *file_idx;
    int **arr_p;
    int *arr;
    int *size_p;
    int sec_start;
    int nsec_start;
    int sec_finish;
    int nsec_finish;
    int sec_total;
    int nsec_total;
};

// Allocate and initialize the context object
static struct my_context *my_context_new(const char *name, char **file_list, int file_count,
                                         int *idx, int **data_p, int *size_p) {
    struct my_context *ctx = malloc(sizeof(*ctx));
    ctx->name = strdup(name);
    ctx->file_list = file_list;
    ctx->file_idx = idx;
    ctx->file_count = file_count;
    ctx->arr_p = data_p;
    ctx->size_p = size_p;
    return ctx;
}

// Deallocate the context and its fields
static void my_context_delete(struct my_context *ctx) {
    free(ctx->name);
    free(ctx);
}

// Stop the timer and update the finish time in the context
static void stop_timer(struct my_context *ctx) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    ctx->sec_finish = time.tv_sec;
    ctx->nsec_finish = time.tv_nsec;
}

// Start the timer and update the start time in the context
static void start_timer(struct my_context *ctx) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    ctx->sec_start = time.tv_sec;
    ctx->nsec_start = time.tv_nsec;
}

// Calculate the total context time, considering nanoseconds overflow
static void calculate_time(struct my_context *ctx) {
    long long diff_sec = ctx->sec_finish - ctx->sec_start;
    long long diff_nsec = ctx->nsec_finish - ctx->nsec_start;

    if (diff_sec > 0 && diff_nsec < 0) {
        diff_sec--;
        diff_nsec += 1000000000;
    }

    ctx->sec_total = diff_sec;
    ctx->nsec_total += diff_nsec;
}

// Swap two integers
static void swap(int *a, int *b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}

// Perform quicksort on the array using coroutines without a separate partition function
void quick_sort(int *array, int left, int right, struct my_context *ctx) {
    int stack[right - left + 1];
    int top = -1;

    stack[++top] = left;
    stack[++top] = right;

    while (top >= 0) {
        right = stack[top--];
        left = stack[top--];

        int pivot = array[right];
        int i = left;

        for (int j = left; j < right; j++) {
            if (array[j] <= pivot) {
                swap(&array[i], &array[j]);
                i++;
            }
        }

        swap(&array[i], &array[right]);
        int pi = i;

        if (pi - 1 > left) {
            stack[++top] = left;
            stack[++top] = pi - 1;
        }

        if (pi + 1 < right) {
            stack[++top] = pi + 1;
            stack[++top] = right;
        }

        stop_timer(ctx);
        calculate_time(ctx);
        coro_yield();
        start_timer(ctx);
    }
}

// Coroutine function
static int coroutine_func_f(void *context) {
    struct coro *this = coro_this();
    struct my_context *ctx = (struct my_context *) context;
    start_timer(ctx);

    while (*(ctx->file_idx) != ctx->file_count) {
        char *filename = ctx->file_list[*(ctx->file_idx)];
        FILE *in = fopen(filename, "r");
        if (!in) {
            my_context_delete(ctx);
            return 1;
        }
        int size = 0;
        int cap = 100;
        ctx->arr = (int *) malloc(cap * sizeof(int));

        // Read data from the text file
        // Reallocate the array if the capacity has run out
        while (fscanf(in, "%d", ctx->arr + size) == 1) {
            ++size;
            if (size == cap) {
                cap *= 2;
                ctx->arr = (int *) realloc(ctx->arr, cap * sizeof(int));
            }
        }

        // Shrink the array to fit
        cap = size;
        ctx->arr = (int *) realloc(ctx->arr, cap * sizeof(int));

        fclose(in);

        // Store the address of the allocated array and its size
        ctx->arr_p[*(ctx->file_idx)] = ctx->arr;
        ctx->size_p[*(ctx->file_idx)] = size;

        // Move to the next file
        (*(ctx->file_idx))++;

        quick_sort(ctx->arr, 0, size - 1, ctx);
    }

    stop_timer(ctx);
    calculate_time(ctx);

    printf("%s info:\nswitch count %lld\nworked %d us\n\n",
           ctx->name,
           coro_switch_count(this),
           ctx->sec_total * 1000000 + ctx->nsec_total / 1000
    );

    my_context_delete(ctx);
    return 0;
}

// Merge the sorted arrays
static int merge(int **data, int *size, int *idx, int cnt) {
    int min_idx = -1;
    int curr_min = INT_MAX;

    for (int i = 0; i < cnt; ++i) {
        if (size[i] > idx[i] && data[i][idx[i]] < curr_min) {
            curr_min = data[i][idx[i]];
            min_idx = i;
        }
    }

    return min_idx;
}

int main(int argc, char **argv) {
    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    coro_sched_init();

    int file_count = argc - 2;
    int coroutine_count = atoi(argv[1]);

    if (!coroutine_count || !file_count) {
        fprintf(stderr, "Incorrect input format.\n"
                        "\tExample: N test1.txt test2.txt test3.txt test4.txt test5.txt test6.txt\n");
        return 1;
    }

    int *p[file_count];
    int s[file_count];
    int file_idx = 0;

    for (int i = 0; i < coroutine_count; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "coro_%d", i);
        coro_new(coroutine_func_f,
                 my_context_new(name, argv + 2, file_count, &file_idx, p, s));
    }
    struct coro *c;
    while ((c = coro_sched_wait()) != NULL) {
        coro_delete(c);
    }

    int idx[file_count]; // Array of current indexes for merging arrays
    for (int i = 0; i < file_count; ++i) {
        idx[i] = 0;
    }

    FILE *out = fopen("out.txt", "w");

    int min_idx = 0;
    while (min_idx != -1) {
        min_idx = merge(p, s, idx, file_count);
        if (min_idx != -1) {
            fprintf(out, "%d ", p[min_idx][idx[min_idx]]);
            idx[min_idx] += 1;
        }
    }
    fclose(out);

    for (int i = 0; i < file_count; ++i) {
        free(p[i]);
    }

    struct timespec finish_time;
    clock_gettime(CLOCK_MONOTONIC, &finish_time);

    printf("total time: %ld us\n",
           (finish_time.tv_sec - start_time.tv_sec) * 1000000 + (finish_time.tv_nsec - start_time.tv_nsec) / 1000);

    return 0;
}