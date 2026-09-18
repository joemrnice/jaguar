# Async & Concurrency Model in Jaguar 1.1.0

Jaguar 1.1.0 provides a single-threaded event loop reactor combined with stackful coroutines (`ucontext_t` on Linux / `kqueue` on macOS) and real `pthread` worker threads.

## Core Concepts

### 1. `Task<T>` and `async` / `await`
Functions marked as `async fun` return a `Task<T>`:
```jaguar
async fun fetchData(): Task<string> {
    return "data";
}

var result: string = await fetchData();
```

### 2. `Task.all([...])`
Combines multiple task evaluations and resolves when all complete.

### 3. Timers
- `live.after(ms, callback)`
- `live.every(ms, callback)`
- `live.clear(id)`

### 4. Worker Threads & Worker Pools
- `worker.spawn(fn)`
- `worker.pool(numThreads)`
