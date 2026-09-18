#define _POSIX_C_SOURCE 200809L
#include "worker.h"
#include "interp.h"
#include "parser.h"
#include "typecheck.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- single worker handle (spawn / run) ---------------- */

static void *thread_main_func(void *arg) {
    WorkerHandle *h = (WorkerHandle *)arg;
    interp_lock();
    Value *result = interp_invoke(h->interp, h->func, NULL, 0);
    interp_unlock();
    pthread_mutex_lock(&h->mu);
    h->result = result;
    h->result_thread_id = (long)pthread_self();
    h->done = 1;
    pthread_cond_signal(&h->cv);
    pthread_mutex_unlock(&h->mu);
    return NULL;
}

static char *read_whole_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    size_t n = fread(buf, 1, len, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static void *thread_main_file(void *arg) {
    WorkerHandle *h = (WorkerHandle *)arg;
    char *src = read_whole_file(h->file_path);
    if (src) {
        /* Runs on a completely independent Interp/globals - true isolation,
           since worker.run() targets a whole separate .jag file rather than
           a closure that might capture the parent script's state. */
        ParseResult pr = parse_program(src, h->file_path);
        if (!pr.had_error && typecheck_program(pr.stmts, h->file_path) == 0) {
            Interp sub;
            interp_init(&sub);
            interp_lock();
            interp_run(&sub, pr.stmts);
            interp_unlock();
        } else {
            fprintf(stderr, "worker.run: '%s' failed to parse/typecheck\n", h->file_path);
        }
        free(src);
    } else {
        fprintf(stderr, "worker.run: cannot read '%s'\n", h->file_path);
    }
    pthread_mutex_lock(&h->mu);
    h->result = value_data_empty();
    h->result_thread_id = (long)pthread_self();
    h->done = 1;
    pthread_cond_signal(&h->cv);
    pthread_mutex_unlock(&h->mu);
    return NULL;
}

static WorkerHandle *worker_handle_new(void) {
    WorkerHandle *h = calloc(1, sizeof(WorkerHandle));
    pthread_mutex_init(&h->mu, NULL);
    pthread_cond_init(&h->cv, NULL);
    return h;
}

WorkerHandle *worker_spawn_func(Interp *it, Value *func) {
    WorkerHandle *h = worker_handle_new();
    h->func = func;
    h->interp = it;
    pthread_create(&h->thread, NULL, thread_main_func, h);
    h->started = 1;
    return h;
}

WorkerHandle *worker_spawn_file(const char *path) {
    WorkerHandle *h = worker_handle_new();
    h->file_path = strdup(path);
    pthread_create(&h->thread, NULL, thread_main_file, h);
    h->started = 1;
    return h;
}

Value *worker_handle_join(WorkerHandle *h) {
    pthread_mutex_lock(&h->mu);
    while (!h->done) pthread_cond_wait(&h->cv, &h->mu);
    pthread_mutex_unlock(&h->mu);
    if (!h->joined) {
        pthread_join(h->thread, NULL);
        h->joined = 1;
    }
    Value *result = h->result ? h->result : value_data_empty();
    if (result->kind == V_DATA) value_data_set(result, "threadId", value_num(h->result_thread_id));
    return result;
}

/* ---------------- worker pool ---------------- */

typedef struct Job { Value *func; struct Job *next; } Job;
typedef struct ResultNode { Value *value; struct ResultNode *next; } ResultNode;

struct WorkerPool {
    Interp *it;
    int n_threads;
    pthread_t *threads;

    pthread_mutex_t mu;
    pthread_cond_t job_available;
    pthread_cond_t all_done;
    Job *queue_head, *queue_tail;
    int submitted;   /* jobs submitted since the last wait_all() */
    int completed;   /* jobs completed since the last wait_all() */
    ResultNode *results_head, *results_tail;
    int shutdown;
};

static void *pool_thread_main(void *arg) {
    WorkerPool *pool = (WorkerPool *)arg;
    for (;;) {
        pthread_mutex_lock(&pool->mu);
        while (!pool->queue_head && !pool->shutdown)
            pthread_cond_wait(&pool->job_available, &pool->mu);
        if (pool->shutdown && !pool->queue_head) {
            pthread_mutex_unlock(&pool->mu);
            return NULL;
        }
        Job *job = pool->queue_head;
        pool->queue_head = job->next;
        if (!pool->queue_head) pool->queue_tail = NULL;
        pthread_mutex_unlock(&pool->mu);

        interp_lock();
        Value *result = interp_invoke(pool->it, job->func, NULL, 0);
        interp_unlock();
        if (result->kind == V_DATA) value_data_set(result, "threadId", value_num((long)pthread_self()));

        pthread_mutex_lock(&pool->mu);
        ResultNode *rn = calloc(1, sizeof(ResultNode));
        rn->value = result;
        if (pool->results_tail) { pool->results_tail->next = rn; pool->results_tail = rn; }
        else pool->results_head = pool->results_tail = rn;
        pool->completed++;
        if (pool->completed >= pool->submitted) pthread_cond_signal(&pool->all_done);
        pthread_mutex_unlock(&pool->mu);

        free(job);
    }
}

WorkerPool *worker_pool_create(Interp *it, int n_threads) {
    if (n_threads < 1) n_threads = 1;
    WorkerPool *pool = calloc(1, sizeof(WorkerPool));
    pool->it = it;
    pool->n_threads = n_threads;
    pool->threads = calloc(n_threads, sizeof(pthread_t));
    pthread_mutex_init(&pool->mu, NULL);
    pthread_cond_init(&pool->job_available, NULL);
    pthread_cond_init(&pool->all_done, NULL);
    for (int i = 0; i < n_threads; i++) pthread_create(&pool->threads[i], NULL, pool_thread_main, pool);
    return pool;
}

void worker_pool_submit(WorkerPool *pool, Value *func) {
    Job *job = calloc(1, sizeof(Job));
    job->func = func;
    pthread_mutex_lock(&pool->mu);
    if (pool->queue_tail) { pool->queue_tail->next = job; pool->queue_tail = job; }
    else pool->queue_head = pool->queue_tail = job;
    pool->submitted++;
    pthread_cond_signal(&pool->job_available);
    pthread_mutex_unlock(&pool->mu);
}

Value *worker_pool_wait_all(WorkerPool *pool) {
    pthread_mutex_lock(&pool->mu);
    while (pool->completed < pool->submitted) pthread_cond_wait(&pool->all_done, &pool->mu);
    Value *list = value_list_empty();
    ResultNode *rn = pool->results_head;
    while (rn) {
        value_list_append(list, rn->value);
        ResultNode *next = rn->next;
        free(rn);
        rn = next;
    }
    pool->results_head = pool->results_tail = NULL;
    pool->submitted = 0;
    pool->completed = 0;
    pthread_mutex_unlock(&pool->mu);
    return list;
}
