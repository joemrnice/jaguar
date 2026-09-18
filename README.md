# Jaguar (`jag`)

A toolchain for the Jaguar programming language: lexer → parser → AST →
static checker → tree-walking interpreter, real OOP (classes, `new`,
`this`, `super`, inheritance), real function/method argument and return
type checking, and a real networking/concurrency runtime (event loop,
`ucontext` coroutines, HTTP/1.1, WebSockets, `pthread` workers) — all
exposed through a single `jag` CLI binary, cross-platform on Linux and
macOS.

**Scope of this build:** the full *core* language works end to end, OOP
is real (single inheritance, polymorphism, constructors), the type
checker does real (if deliberately bounded) argument/return validation,
and the networking runtime is real — an HTTP server handling concurrent
connections via coroutines over a single-threaded event loop, a real
WebSocket server (RFC 6455 handshake and framing), and real
`pthread`-backed workers. What's still simplified, missing, or
platform-limited (no TLS, no chunked encoding, no WebSocket client, no
access-control enforcement, no native Windows support, an as-yet-untested
macOS backend) is documented in detail, including *why*, in
[DESIGN_DECISIONS.md](DESIGN_DECISIONS.md) — read that before assuming
anything about behavior this README doesn't spell out. The native/AOT C
backend (`jag build`) is not implemented.

## Build

```sh
make            # builds ./jag (auto-detects Linux/macOS, picks the right reactor backend)
make test       # runs the end-to-end test suite (40 checks)
make platform   # prints which backend this platform will build, without building
```

Requires a C99/C11 compiler (`cc`/`gcc`/`clang`), `make`, and Linux or
macOS. **Native Windows isn't supported — use WSL**, which runs the real,
fully-tested Linux path unmodified; see
[docs/WINDOWS.md](docs/WINDOWS.md) for why and how. No external library
dependencies on any platform.

## Install

**One-liner, straight from GitHub** (builds from source on your own
machine — see [docs/PUBLISHING.md](docs/PUBLISHING.md) for how this is
hosted, and read `get-jaguar.sh` before piping any install script into a
shell, the way you should for anyone's): 
Also make sure you have admin permission activate via password or use : 

```sh
sudo curl -fsSL https://raw.githubusercontent.com/joemrnice/jaguar/main/get-jaguar.sh | bash
```
else you will see Permission de
[docs/PUBLISHING.md](docs/PUBLISHING.md) if you're setting this project
up on GitHub yourself.) Options via environment variables:

```sh
PREFIX=$HOME/.local curl -fsSL .../get-jaguar.sh | bash   # no sudo needed
JAGUAR_REF=v0.2.0   curl -fsSL .../get-jaguar.sh | bash   # pin a release tag instead of main
```

**From a local clone:**

```sh
./install.sh                        # detects your OS and installs to /usr/local/bin
PREFIX=$HOME/.local ./install.sh    # or a custom prefix, no sudo needed
./uninstall.sh
```

Or run the platform-specific installer directly:
[`installers/install-linux.sh`](installers/install-linux.sh) /
[`installers/install-macos.sh`](installers/install-macos.sh) (the macOS
one notes its backend's testing status — see DESIGN_DECISIONS.md).

## Learning the language

[docs/LANGUAGE_GUIDE.md](docs/LANGUAGE_GUIDE.md) is a full, practical
guide from basic syntax through OOP and the networking/worker runtime —
every example in it has been run against the actual interpreter,
including its type checker. Start there if you're new to Jaguar; use
this README for build/install/CLI details.

## CLI

```
jag <file.jag>            compile+run once (interpreter backend)
jag run <file.jag>        same, explicit
jag -live=1 <file.jag>    live-reload mode (watches the file for saves)
jag check <file.jag>      lex + parse + typecheck only, no execution
jag build <file.jag>      ahead-of-time native compile [not yet implemented]
jag --version / --help
```

## What works

**Core language** — everything in the language reference's core section:
`var`/`fixed` declarations and all scalar/collection types, `if`/`elif`/
`else` and the ternary operator, all four loop forms, list methods
(`.append` `.insert` `.delete` `.sort` `.concat`), functions and closures,
every operator including `<<< (low, high)`, `{{}}`/`${}` string
interpolation, `try`/`catch`, `file.*`/`dir.*` I/O, `json.parse`/
`stringify`, `env.get`, `live.on`/`in`/`deg`, and `-live=1` file-watch
reload.

**OOP** — `class`, `new ClassName(args)`, `this`, `constructor` methods,
`extends`/`super()`/`super.method()` for single inheritance, and dynamic
dispatch (so overriding a method and calling it through a base-typed
reference works the way you'd expect). No access-control enforcement, no
static members, no multiple inheritance — see
[DESIGN_DECISIONS.md](DESIGN_DECISIONS.md) for the reasoning.
`struct`/`enum`/`vector<T>`/`matrix<T>` remain as before (parsed and
typechecked; `vector`/`matrix` are backed by ordinary lists at runtime).

**Static type checking** — function, method, and constructor calls are
checked against declared parameter types; `return` statements are
checked against the declared return type; `new` on an unknown class is a
compile-time error. Deliberately conservative (never flags anything
involving an unknown/untraceable type, so it won't reject a valid
program) — not Rust/Java-grade exhaustive checking, and the code says so.
See DESIGN_DECISIONS.md for exactly what is and isn't caught.

**Networking/concurrency runtime** — see [examples/](examples/) for
working, runnable programs and [examples/README.md](examples/README.md)
for a guided tour:

- `server.on/route/listen` — real HTTP/1.1 server: `{param}` routes,
  JSON bodies, chainable `res.status().json()`, concurrent connections
  handled via coroutines over one event loop (not one thread per
  connection)
- `http.get/post` — HTTP client, usable with `await`
- `socket.on/route/listen` — real WebSocket server (RFC 6455 handshake +
  framing), `ws.on("message"/"close", fn)`, `ws.send()`
- `worker.spawn/run/pool` — real OS threads via `pthread`, with a
  worker-pool `.submit()`/`.onAll()` verified distributing jobs across
  4 real threads and collecting all results correctly (note: interpreter
  execution itself is serialized by a lock across worker threads — see
  "the concurrency-safety model" in DESIGN_DECISIONS.md for what that
  does and doesn't buy you)
- `live.after`/`live.every` — real timers driven by the event loop, not
  run synchronously
- `Task<T>`, `.then()`/`.catch()`/`.await()`, `await`, `Task.all([...])`
  — see DESIGN_DECISIONS.md for exactly how these behave (function calls,
  including `async fun`, still run synchronously to completion; `await`
  is mostly a Task-unwrapper rather than a real suspension point, except
  for the I/O inside an accepted server/WS connection, which genuinely
  suspends via the reactor)

**Cross-platform**: Linux (`epoll`, fully tested) and macOS (`kqueue`,
written and syntax-verified but not yet run on real hardware — see
DESIGN_DECISIONS.md). Native Windows is not supported; use WSL
([docs/WINDOWS.md](docs/WINDOWS.md)).

Not implemented: TLS (`https://`/`wss://` fail with a clear, catchable
error), chunked transfer encoding, WebSocket fragmented-message
reassembly, a WebSocket *client* (`socket.connect()`), access-control
enforcement for `public`/`private` classes, and the native/AOT backend
(`jag build`). All documented in DESIGN_DECISIONS.md.

## Repository layout

```
include/          public headers
src/lexer/        hand-written lexer
src/parser/       recursive-descent parser (incl. string-interpolation splitting)
src/ast/          AST node constructors
src/typecheck/    static checker: type annotations, fixed-reassignment,
                   function/method/constructor argument and return type checking
src/interp/       tree-walking interpreter + runtime value system (value.c),
                   including OOP (classes, instances, method dispatch, super)
src/runtime/      networking/concurrency runtime:
                    reactor_common.c - OS-agnostic timers/bookkeeping
                    reactor_epoll.c  - Linux backend
                    reactor_kqueue.c - macOS/BSD backend
                    coro.c           - ucontext-based coroutines
                    netutil.c        - socket setup + coroutine-aware I/O
                    http.c           - HTTP/1.1 server + client
                    ws.c             - WebSocket (RFC 6455) server
                    worker.c         - pthread worker/pool subsystem
                    json.c           - JSON parse/stringify (shared by interp + http)
                    native.c         - tagged native-value wrappers (Task/Worker/Socket/super)
                    sha1.c, base64.c - vendored, for the WS handshake only
src/main.c        CLI entry point
installers/       platform-specific installers (install-linux.sh, install-macos.sh)
docs/             LANGUAGE_GUIDE.md, WINDOWS.md, PUBLISHING.md, index.html (static docs site)
get-jaguar.sh     one-line remote installer (curl | bash from GitHub - see docs/PUBLISHING.md)
examples/         runnable networking/concurrency examples + guided README
tests/            end-to-end test suite (tests/run_tests.sh) + sample .jag programs
```

## Testing

`make test` runs `tests/run_tests.sh` (40 checks): every core-language
construct, OOP (fields/methods/inheritance/`super`/polymorphism and their
error paths), `jag check`'s diagnostics (including the new argument/
return type checking), a type-safety regression test, a full HTTP
server+client round trip (path params, JSON bodies, `Task.all` fan-out)
against a real running server, a WebSocket handshake+echo test against a
real running server, a worker-pool test, and a timer-ordering test. Runs
on Linux; not yet run on macOS hardware (see DESIGN_DECISIONS.md).
