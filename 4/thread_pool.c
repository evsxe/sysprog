#include "thread_pool.h"
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

struct thread_task {
    thread_task_f function;
    void *arg;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    void *result;
    bool finished;
};

struct thread_pool {
    pthread_t *threads;
    int max_thread_count;
    int thread_count;
    struct thread_task **tasks;
    int task_count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool shutdown;
};

static void *thread_func(void *arg) {
    struct thread_pool *pool = (struct thread_pool *)arg;

    while (1) {
        pthread_mutex_lock(&pool->mutex);
        while (pool->task_count == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->cond, &pool->mutex);
        }

        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->mutex);
            pthread_exit(NULL);
        }

        struct thread_task *task = pool->tasks[--pool->task_count];
        pthread_mutex_unlock(&pool->mutex);

        pthread_mutex_lock(&task->mutex);
        task->result = task->function(task->arg);
        task->finished = true;
        pthread_cond_signal(&task->cond);
        pthread_mutex_unlock(&task->mutex);
    }

    return NULL;
}

int thread_pool_new(int max_thread_count, struct thread_pool **pool) {
    if (max_thread_count <= 0 || max_thread_count > TPOOL_MAX_THREADS) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    *pool = (struct thread_pool *)malloc(sizeof(struct thread_pool));
    if (*pool == NULL) {
        return TPOOL_ERR_NOT_IMPLEMENTED;
    }

    (*pool)->max_thread_count = max_thread_count;
    (*pool)->thread_count = 0;
    (*pool)->tasks = (struct thread_task **)malloc(TPOOL_MAX_TASKS * sizeof(struct thread_task *));
    (*pool)->task_count = 0;
    pthread_mutex_init(&(*pool)->mutex, NULL);
    pthread_cond_init(&(*pool)->cond, NULL);
    (*pool)->shutdown = false;

    (*pool)->threads = (pthread_t *)malloc(max_thread_count * sizeof(pthread_t));
    for (int i = 0; i < max_thread_count; i++) {
        pthread_create(&(*pool)->threads[i], NULL, thread_func, (void *)(*pool));
        (*pool)->thread_count++;
    }

    return 0;
}

int thread_pool_thread_count(const struct thread_pool *pool) {
    return pool->thread_count;
}

int thread_pool_delete(struct thread_pool *pool) {
    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = true;
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);

    for (int i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    free(pool->threads);
    free(pool->tasks);
    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->cond);
    free(pool);

    return 0;
}

int thread_pool_push_task(struct thread_pool *pool, struct thread_task *task) {
    pthread_mutex_lock(&pool->mutex);
    if (pool->task_count == TPOOL_MAX_TASKS) {
        pthread_mutex_unlock(&pool->mutex);
        return TPOOL_ERR_TOO_MANY_TASKS;
    }

    pool->tasks[pool->task_count++] = task;
    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);

    return 0;
}

int thread_task_new(struct thread_task **task, thread_task_f function, void *arg) {
    *task = (struct thread_task *)malloc(sizeof(struct thread_task));
    if (*task == NULL) {
        return TPOOL_ERR_NOT_IMPLEMENTED;
    }

    (*task)->function = function;
    (*task)->arg = arg;
    (*task)->finished = false;
    pthread_mutex_init(&(*task)->mutex, NULL);
    pthread_cond_init(&(*task)->cond, NULL);

    return 0;
}

bool thread_task_is_finished(const struct thread_task *task) {
    return task->finished;
}

bool thread_task_is_running(const struct thread_task *task) {
    return !task->finished;
}

int thread_task_join(struct thread_task *task, void **result) {
    pthread_mutex_lock(&task->mutex);
    while (!task->finished) {
        pthread_cond_wait(&task->cond, &task->mutex);
    }
    if (result != NULL) {
        *result = task->result;
    }
    pthread_mutex_unlock(&task->mutex);

    return 0;
}

int thread_task_delete(struct thread_task *task) {
    pthread_mutex_destroy(&task->mutex);
    pthread_cond_destroy(&task->cond);
    free(task);

    return 0;
}

int thread_task_detach(struct thread_task *task) {
    if (task->finished) {
        thread_task_delete(task);
        return 0;
    }

    pthread_mutex_lock(&task->mutex);
    task->finished = true;
    pthread_cond_signal(&task->cond);
    pthread_mutex_unlock(&task->mutex);

    return 0;
}

int thread_task_timed_join(struct thread_task *task, double timeout, void **result) {
    pthread_mutex_lock(&task->mutex);

    if (timeout < 0.000000001) {
        while (!task->finished) {
            pthread_cond_wait(&task->cond, &task->mutex);
        }
    } else {
        struct timespec abs_timeout;
        clock_gettime(CLOCK_REALTIME, &abs_timeout);
        abs_timeout.tv_sec += (time_t)timeout;
        abs_timeout.tv_nsec += (long)((timeout - (double)((long)timeout)) * 1e9);

        if (abs_timeout.tv_nsec >= 1000000000) {
            abs_timeout.tv_sec++;
            abs_timeout.tv_nsec -= 1000000000;
        }

        int condition = 0;
        while (!task->finished && condition != ETIMEDOUT) {
            condition = pthread_cond_timedwait(&task->cond, &task->mutex, &abs_timeout);
        }

        if (condition == ETIMEDOUT) {
            pthread_mutex_unlock(&task->mutex);
            return TPOOL_ERR_TIMEOUT;
        }
    }

    *result = task->result;
    task->finished = true;
    pthread_cond_signal(&task->cond);
    pthread_mutex_unlock(&task->mutex);

    return 0;
}