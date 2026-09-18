# Jaguar networking/concurrency examples

These all exercise the real runtime added on top of the core language:
a single-threaded epoll event loop, `ucontext`-based coroutines (used
internally to handle many concurrent HTTP/WebSocket connections without
blocking OS threads), a hand-rolled HTTP/1.1 implementation, RFC 6455
WebSockets, and a `pthread`-backed worker system. See
[../DESIGN_DECISIONS.md](../DESIGN_DECISIONS.md) for exactly what's real
versus simplified (no TLS/HTTPS, no chunked transfer encoding, no
WebSocket fragmentation reassembly, and a couple of concurrency-model
tradeoffs worth knowing about before you build on this).

Build `jag` first (`make` in the repo root), then run any of these with
`jag run <file>` from this directory.

| File | What it shows |
|---|---|
| `oop.jag` | Classes, constructors, `this`, `extends`/`super()`/`super.method()`, and polymorphism |
| `http_server.jag` | HTTP/1.1 server: routing, `{param}` path segments, JSON bodies, status codes |
| `http_client.jag` | Async client: `await http.get(...)`, `Task.all([...])` fan-out. Run `http_server.jag` first. |
| `websocket_chat.jag` | WebSocket echo server: handshake, text frames, close detection |
| `worker_pool.jag` | A fixed pool of real OS threads processing jobs in parallel, collected via `.onAll()` |
| `worker_spawn.jag` | A single background job on its own OS thread, notified via `.on("done", ...)` |
| `worker_run.jag` + `worker_run_task.jag` | Runs an entire separate `.jag` file on its own thread with fully isolated state |
| `timers.jag` | `live.after`/`live.every`, driven by the real event-loop reactor, not run synchronously |
| `server_demo.jag` | Everything above combined in one process: HTTP + WebSocket + worker pool + heartbeat timer sharing one event loop |

## Quick tour

**HTTP server + client** (two terminals):
```sh
jag run http_server.jag
# elsewhere:
curl http://127.0.0.1:8091/users/42
jag run http_client.jag
```

**WebSocket** — any WS client works; `wscat -c ws://127.0.0.1:9092/chat`
if you have it, or see `../tests/ws_check.py` for a ~40-line client using
only Python's standard library (useful as a reference for the handshake
and frame format if you're integrating from another language).

**Workers** — `worker_pool.jag` and `worker_spawn.jag` are runnable
standalone; `worker_run.jag` must be run from inside this directory since
it loads `worker_run_task.jag` by relative path.

**Everything at once**:
```sh
jag run server_demo.jag
# elsewhere:
curl http://127.0.0.1:8080/
curl http://127.0.0.1:8080/users/5
curl -X POST http://127.0.0.1:8080/compute   # runs on the worker pool
```

## A concurrency-model note worth knowing before you build on this

Route handlers, WebSocket callbacks, and timer callbacks all run on the
**main thread**, one at a time (that's what the coroutines buy you:
many concurrent connections without needing many OS threads). Worker
threads run genuinely in parallel with each other and with the main
thread, guarded by a lock so multiple worker threads never execute
interpreter code at the exact same instant. The main thread does **not**
take that lock, which is what makes it safe to, e.g., submit jobs to a
worker pool and block on `.onAll()` from inside an HTTP handler (see
`server_demo.jag`'s `/compute` route) without deadlocking against the
pool's own worker threads. The tradeoff: a worker thread's job function
and the main thread's request handling are not synchronized against each
other, only against other workers. In practice this is a non-issue for
the intended pattern (worker functions should be self-contained
computations, not shared-state mutators - see DESIGN_DECISIONS.md), but
it's worth knowing if you go off that path.
