#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libcoro.h"
#include <limits.h>
#include <time.h>


struct
my_context
{
    char *coro_name;
    int f_count;
    char **file_names;
    int *f_index;
    int **data;
    int *array;
    int *array_size;

    int sec_start;
    int nsec_start;
    int sec_finish;
    int nsec_finish;
    int sec_total;
    int nsec_total;
};


int
calc_nums_in_file(FILE *file)
{
    int count = 0;
    int numbers;

    rewind(file);
    while (fscanf(file, "%d", &numbers) == 1) {
        count++;
    }

    rewind(file);
    return count;
}

void fill_array(FILE *file, int *array) {
    int i = 0;

    rewind(file);
    while (fscanf(file, "%d", &array[i]) == 1) {
        i++;
    }

    rewind(file);
}



static struct my_context *
my_context_new(char *coro_name, char **file_names, int f_count,
               int *f_index, int **data, int *array_size)
{
    struct my_context *ctx = malloc(sizeof(*ctx));
    ctx->coro_name = strdup(coro_name);
    ctx->file_names = file_names;
    ctx->f_index = f_index;
    ctx->f_count = f_count;
    ctx->data = data;
    ctx->array_size = array_size;
    return ctx;

}

static void my_context_delete(struct my_context *ctx) {
    free(ctx->coro_name);
    free(ctx);
}

static void stop_time(struct my_context *ctx) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    ctx->sec_finish = time.tv_sec;
    ctx->nsec_finish = time.tv_nsec;
}


static void start_time(struct my_context *ctx) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    ctx->sec_start = time.tv_sec;
    ctx->nsec_start = time.tv_nsec;
}

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

void concat(int *arr, int l, int m, int r) {
    int i, j, k;
    int n1 = m - l + 1;
    int n2 = r - m;

    int *L = malloc(sizeof(int) * n1);
    int *R = malloc(sizeof(int) * n2);

    for (i = 0; i < n1; i++)
        L[i] = arr[l + i];
    for (j = 0; j < n2; j++)
        R[j] = arr[m + 1 + j];

    i = 0;
    j = 0;
    k = l;

    while (i < n1 && j < n2) {
        if (L[i] <= R[j]) {
            arr[k] = L[i];
            i++;
        } else {
            arr[k] = R[j];
            j++;
        }
        k++;
    }

    while (i < n1) {
        arr[k] = L[i];
        i++;
        k++;
    }

    while (j < n2) {
        arr[k] = R[j];
        j++;
        k++;
    }

    free(L);
    free(R);
}

void mergeSort(int *array, int l, int r, struct my_context *ctx) {
    if (l < r) {
        int m = l + (r - l) / 2;

        mergeSort(array, l, m, ctx);
        mergeSort(array, m + 1, r, ctx);

        concat(array, l, m, r);

        stop_time(ctx);
        calculate_time(ctx);
        coro_yield();
        start_time(ctx);
    }
}

static int coro_func(void *context)
{
    struct coro *this = coro_this();
    struct my_context *ctx = context;

    start_time(ctx);
    while (*(ctx->f_index) != ctx->f_count) {

        int i = *ctx->f_index;
        char *f_name = ctx->file_names[i];

        printf("Coroutine %s sorting file %s...\n", ctx->coro_name, f_name);

        FILE* file = fopen(f_name, "r");
        if (!file) {
            my_context_delete(ctx);
            return 0;
        }

        int count_of_numbers = calc_nums_in_file(file);

        ctx->array = (int *) malloc(count_of_numbers * sizeof(int ));
        fill_array(file, ctx->array);

        fclose(file);
        ctx->data[*(ctx->f_index)] = ctx->array;
        ctx->array_size[*(ctx->f_index)] = count_of_numbers;

        (*(ctx->f_index))++;
        mergeSort(ctx->array, 0, count_of_numbers - 1, ctx);

    }

    stop_time(ctx);
    calculate_time(ctx);
    printf("%s: количество переключений - %lld, время выполнения: %d\n", ctx->coro_name, coro_switch_count(this),
           ctx->sec_total * 1000000  + ctx->nsec_total / 1000);

    my_context_delete(ctx);
    return 0;
}


int
main(int argc, char **argv)
{
    struct timespec start, end;
    int time;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int count_files = argc - 2;
    int N = atoi(argv[1]);
    int f_indx = 0;

    int *data[count_files];
    int sizes[count_files];

    coro_sched_init();

    for (int i = 0; i < N; i++) {
        char name[16];
        sprintf(name, "coro_%d", i);

        coro_new(coro_func,
                 my_context_new(name, argv + 2, count_files, &f_indx, data, sizes));

    }

    struct coro *c;
    while ((c = coro_sched_wait()) != NULL) {
        if (c != NULL){
            printf("Finished with status %d\n", coro_status(c));
            coro_delete(c);
        }
    }

    int *indx = (int *) calloc(count_files, sizeof(int ));

    FILE *out = fopen("out.txt", "w");

    int min_idx = 0;
    while (min_idx != -1) {

        int curr_min = INT_MAX;
        min_idx = -1;
        for (int i = 0; i < count_files; ++i) {
            if (sizes[i] > indx[i] && data[i][indx[i]] < curr_min) {
                curr_min = data[i][indx[i]];
                min_idx = i;
            }
        }
        if (min_idx != -1) {
            fprintf(out, "%d ", data[min_idx][indx[min_idx]]);
            indx[min_idx] += 1;
        }
    }

    fclose(out);

    for (int i = 0; i < count_files; ++i) {
        free(data[i]);
    }

    free(indx);

    clock_gettime(CLOCK_MONOTONIC, &end);
    time = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_nsec - start.tv_nsec) / 1000;

    printf("Время выполнения программы: %d\n", time);
    return 0;
}