#ifndef JAG_WORKER_H
#define JAG_WORKER_H
#include <pthread.h>
#include "value.h"
#include "interp.h"

/* A single background job (worker.spawn / worker.run): runs once on its own
   OS thread. The caller polls/blocks via worker_handle_join(). */
typedef struct WorkerHandle {
    pthread_t thread;
    int started;
    int joined;
    int done;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    Value *result;        /* set by the thread when finished */
    long result_thread_id; /* pthread_self() cast to long, for verification */
    /* what to run - exactly one of these is set */
    Value *func;           /* FuncValue to call with no args (worker.spawn) */
    char *file_path;        /* .jag file to run (worker.run) */
    Interp *interp;  /* interpreter to call `func` under, if set */
} WorkerHandle;

WorkerHandle *worker_spawn_func(Interp *it, Value *func);
WorkerHandle *worker_spawn_file(const char *path);
/* Blocks until the handle's thread finishes, then returns its result
   (a `data` value with at least a "threadId" field for verification). */
Value *worker_handle_join(WorkerHandle *h);

/* A fixed-size pool of N worker threads pulling jobs off a shared queue.
   submit() enqueues a zero-arg FuncValue; wait_all() blocks until every job
   submitted since the last wait_all() call has completed, then returns
   their results as a Jaguar list (in completion order). */
typedef struct WorkerPool WorkerPool;

WorkerPool *worker_pool_create(Interp *it, int n_threads);
void worker_pool_submit(WorkerPool *pool, Value *func);
Value *worker_pool_wait_all(WorkerPool *pool);

#endif
