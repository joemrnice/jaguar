# DESIGN_DECISIONS.md

Honest accounting of what this build actually does, what it simplifies or
defers, and how spec ambiguities were resolved — updated for version 2
(OOP, real static type checking, cross-platform support). Read this
before assuming any behavior, especially around networking, typing, or
platform support — it's the single most important thing to know about
this codebase.

## Version 2 at a glance

Three substantial additions over the version documented below the fold:

1. **Real OOP** — `class`, `new`, `this`, `super`, `extends`, single
   inheritance, polymorphism through overriding.
2. **Real static type checking** — function/method/constructor argument
   and return types are now actually validated (previously: nothing was
   checked beyond "does every var have a type annotation").
3. **Cross-platform reactor** — the event loop now has two real
   backends, `epoll` (Linux) and `kqueue` (macOS), behind one portable
   interface, with separate platform installers. Native Windows remains
   unsupported (WSL is the supported path) — see the reasoning below and
   in `docs/WINDOWS.md`.

Each is covered in depth in its own section immediately below. Everything
under "## What's real" further down is unchanged from the previous
version (the HTTP/WebSocket/worker runtime) unless a section here says
otherwise.

## OOP: design decisions

The original language spec defines `class`/`public class`/`private
class` syntax but never once shows how an instance actually gets built —
no `new`, no object literal, nothing, in any of its own worked examples.
Version 1 left this as a documented gap. Version 2 closes it with a
deliberately familiar design (closer to JS/Java than to anything novel),
since the whole point of this language is to *mask* major languages with
something simpler, not add a fourth thing to learn:

- **`new ClassName(args)`** constructs an instance. Fields (`var`/`fixed`
  members) get their declared defaults (or their own initializer
  expression) first, parent class first if there's an `extends` chain,
  then a method literally named **`constructor`** is called with those
  args, if one exists anywhere in the chain (the most-derived class's own
  constructor if it has one, otherwise the nearest ancestor's - so a
  subclass that doesn't define its own constructor still gets built
  correctly via the parent's). A class with no constructor anywhere in
  its chain is still legal to `new` - fields just keep their defaults.
- **`this`** and **`super`** are bound as ordinary (fixed) local
  variables in a method/constructor's call environment - not special
  syntax, not AST nodes. This was a deliberate implementation choice: it
  means every existing piece of member-access/method-call code
  (`x.field`, `x.method()`) already handles `this.field`/`this.method()`
  and `super.method()` for free, with zero special-casing anywhere except
  at the point where the call environment is first constructed. `super`
  specifically evaluates to a small internal reference (which parent
  class to resolve against, bound to which instance) that's invisible to
  Jaguar code - you can't store it in a variable and inspect it, only
  call through it.
- **Method dispatch is dynamic (virtual) by default** - calling
  `this.method()` from inside any method, including an inherited one,
  always starts lookup from the instance's actual most-derived class, not
  the class that defined the currently-running method. This is what
  makes the polymorphism example in the language guide work (a `Shape`
  base method calling `this.area()` correctly reaches an overriding
  `Circle`/`Square` implementation) and is the standard behavior in every
  mainstream OOP language, so it wasn't worth deviating from for novelty.
- **No access-control enforcement.** `public`/`private` parse and the
  class name still registers either way, but nothing checks them at
  field/method-access time. Implementing this properly would need
  tracking the *calling context's* relationship to the class (same class?
  subclass? unrelated code?) at every single member access, which is a
  real chunk of additional work for a guarantee that, honestly, dynamic
  scripting-adjacent languages usually skip anyway (Python's
  underscore-convention model, JS's pre-`#field` era). Deferred, not
  forgotten.
- **Single inheritance only, no interfaces/traits/mixins, no static
  members.** Multiple inheritance and interfaces bring real design
  questions (diamond resolution, whether an interface can carry default
  method bodies) that didn't fit this pass; static members are a smaller
  gap but still deferred, since fields as designed are always per-instance
  storage (a `Value* fields` on each `Instance`) and static storage would
  need a separate per-*class* storage location.
- **`struct`/`enum` still don't have construction syntax.** Only `class`
  gained `new`. A `struct` is, in every worked spec example, a pure data
  shape with no methods - a `class` (now that it works) already covers
  that shape strictly more capably, so extending `struct` specifically
  felt like effort spent on a strictly-weaker duplicate of something
  that now exists. `enum` similarly still just registers its name and
  values as an identifier list, with no member-access-by-name added.

### A real gotcha this caught: closures vs. worker isolation, again

Wiring `this`/`super` through as ordinary bound variables reused the
exact call machinery that version 1 built for HTTP/WebSocket callbacks -
which meant it inherited version 1's split between `interp_invoke`
(globals-rooted, for worker-thread isolation) and `interp_invoke_closure`
(preserves lexical scope, for everything else). Methods and constructors
correctly use the closure-preserving path, the same way route handlers
do - this was checked explicitly (`this` needs to be visible from a
method's own body, which is exactly the same shape of requirement as `ws`
needing to be visible inside a `ws.on("message", ...)` callback) rather
than assumed to be fine by analogy.

## Static type checking: what's actually checked now, and what isn't

Version 1's typechecker only checked syntax-level things: every
`var`/`fixed` has a type annotation, `fixed` isn't reassigned, `fixed`
has an initializer, `live.deg()` has enough arguments. It checked *zero*
semantic type compatibility - you could pass a `string` to a function
declared to take a `num` and nothing would catch it before the runtime
value flowed somewhere unexpected.

Version 2 adds real, if deliberately bounded, semantic checking:

- **Function call arguments** are checked against the callee's declared
  parameter types, by position, whenever both the parameter's type and
  the argument's *inferred* type are confidently known.
- **`return` statements** are checked against the enclosing function's
  declared return type, the same way.
- **`new ClassName(...)`** is checked against the constructor's
  parameters the same way, and **rejected outright at typecheck time if
  `ClassName` isn't a known class** - this is a genuine compile-time
  error now, not a runtime one (previously, and still for unknown
  methods/fields - see below - such things could only fail at runtime).
- **Instance method calls** (`x.method(args)`) are checked the same way,
  but only when `x`'s *declared* type names a specific, known class (e.g.
  `var x: User = new User(...)`) - there's no attempt to track a
  variable's class through reassignment, ternaries, or anything beyond
  its original declaration.
- Forward references work: a function can call another function defined
  later in the same block/file and still get checked correctly, because
  function signatures are hoisted into scope before any function body is
  checked (mirroring the interpreter's own hoisting behavior exactly, on
  purpose, to catch the same programs the interpreter would actually
  run).

This is explicitly **not** Rust- or Java-grade type checking, and the
code says so in a comment at the top of `typecheck.c` rather than
implying otherwise. What it deliberately does *not* do, so it never
rejects a valid program:

- It never flags a call/return/`new`/assignment where either side's type
  is **unknown** - the result of any builtin call (`json.parse`,
  `http.get`, etc.), `this`/`super` themselves, list/data element types,
  or anything the checker simply can't trace. A false "unknown" is always
  preferred to a false type error.
- `num`/`decimal`/`scifi` are one interchangeable family (matching the
  interpreter's own arithmetic promotion - `num / num` already produces
  a `decimal` at runtime, so the checker would be actively wrong to treat
  them as incompatible). `list`/`MixedList`/`vector`/`matrix` are
  likewise one family, since a `[...]` literal can't syntactically know
  which of those four it's meant to become - that's purely a function of
  the declared type on the left, and `vector`/`matrix` are backed by
  ordinary lists at runtime anyway.
- It doesn't verify that every code path through a function actually
  returns a value (no control-flow/exhaustiveness analysis), doesn't
  reject calling an unknown *method or field* on an otherwise-known class
  (only argument *types* on methods it can find are checked; a
  misspelled method name is still a runtime error, not a compile-time
  one - correctly resolving that would mean either rejecting perfectly
  dynamic patterns or building a much more thorough exhaustiveness
  checker than fit this pass), and doesn't check binary operator operand
  compatibility at all (e.g. it won't flag anything about `+`'s operands
  individually, only the *result* type it infers for further
  propagation) - the risk of a false positive on legal patterns like
  `"count: " + 5` (string concatenation with an implicit stringify,
  which the interpreter genuinely supports) outweighed the value of
  catching the rare genuine mistake there.

### Bugs this section's implementation caught in itself, via testing

Writing new checks is exactly the kind of change likely to introduce
false positives, so every single code example added to the language
guide for this version was run through `jag check` as part of writing
this document, not just eyeballed. That process caught three real bugs
before they shipped:

1. **A double-error-emission bug.** The first draft had `infer_type`
   (compute a best-effort type for an expression) and `check_expr`
   (walk the tree emitting diagnostics) tangled together, so asking for
   an expression's type as a side-channel (e.g. to check a `var`
   declaration's initializer against its annotation) could re-trigger
   the *same* diagnostic `check_expr` had already emitted for that exact
   expression - a `new UnknownClass()` inside a `var` declaration printed
   its "not a known class" error twice. Fixed by making `infer_type`
   strictly pure (no diagnostics, ever) and moving all error emission
   into `check_expr`/`check_stmt`, called exactly once per node.
2. **`MixedList` flagged as incompatible with a list literal.** `var
   mixed: MixedList = [1, "two", true];` - straight from the language
   guide itself - failed typecheck, because a list literal's inferred
   type was hardcoded to plain `list` with no way to know it was meant
   to be mixed. Fixed by treating `list`/`MixedList` as one family (see
   above).
3. **`vector`/`matrix` flagged as incompatible with a list literal**, the
   same bug in a different spot - `vector<num> position = [3, 4];`,
   also straight from the guide, also failed. Same fix, extended to
   include `vector`/`matrix` in the same family.

All three were caught specifically *because* every guide example is
executed, not just read, as part of writing documentation for this
project - a policy worth keeping for anything added later.

## Cross-platform support: what changed, and Windows specifically

The event loop (`reactor.c` in version 1) was Linux-only, hardcoded to
`epoll`. Version 2 splits it into:

- `reactor_common.c` - OS-agnostic timer management and bookkeeping
  (shared by both backends, so neither duplicates it)
- `reactor_epoll.c` - the Linux backend (unchanged behavior, just moved
  and using the new portable `REACTOR_READABLE`/`REACTOR_WRITABLE` flags
  instead of raw `EPOLLIN`/`EPOLLOUT` at every call site)
- `reactor_kqueue.c` - a new macOS/BSD backend

The `Makefile` picks exactly one of the two backend files to compile,
based on `uname -s`, so there's never a symbol clash between them.

**On the macOS backend specifically: it is untested.** It was written
carefully, against the documented `kqueue(2)`/`kevent(2)` API, mirroring
the working, tested Linux backend's structure line-for-line where the
two APIs correspond (same `EV_ONESHOT`-per-await model as epoll's
register-then-unregister-on-fire pattern, same timer/`reactor_has_work`
plumbing reused unchanged from `reactor_common.c`). Its syntax was
verified against a hand-written stub of the kqueue API's actual shape
(struct layout, `EV_SET`, function signatures) since no macOS machine was
available during development - but a syntax-clean compile against a stub
is not the same as a real build and a passing `make test` on real
hardware. If you're on macOS and something in the networking runtime
misbehaves, this backend is the first place to look; the core language,
OOP, and type checking are entirely platform-independent and just as
tested as on Linux.

**Native Windows (outside WSL) is explicitly not supported, and this
isn't a small gap**: `epoll`/`kqueue` have no Windows equivalent at all
(Windows uses IOCP, a different I/O model), and the coroutine primitive
(`ucontext.h`) doesn't exist on Windows either (the equivalent is
Fibers, again a different API). Porting either correctly is roughly
comparable in scope to building the macOS backend was, not a quick
`#ifdef`. WSL2 runs a real Linux kernel, so the existing, tested
`epoll`/`ucontext` path works completely unmodified there - see
`docs/WINDOWS.md` for setup steps. A genuine native Windows port (IOCP +
Fibers, behind the same `reactor.h`/`coro.h` interfaces the rest of the
runtime already uses) remains a well-scoped but real future project, not
something silently promised by "cross-platform."

**Separate installers per platform**, as requested: `installers/
install-linux.sh`, `installers/install-macos.sh`, and `docs/WINDOWS.md`
(a guide, since there's no native installer to ship). The top-level
`install.sh` is a thin dispatcher that detects the OS and calls the right
one, kept for convenience so `./install.sh` still "just works" without
needing to know which platform script to reach for.

## A robustness pass: crash-risk hardening found via review

Separately from the three additions above, testing OOP's new error paths
surfaced a real, pre-existing class of bug worth fixing on its own
merits: several builtins (`env.get`, `file.on`, `dir.on`, `server.route`,
`socket.route`, `worker.run`, bracket-index access/assignment on `data`
values, `res.header()`) read a `Value`'s `->as.string` field without
first checking that the value was actually a string. If it wasn't - a
wrong-typed argument (`env.get(42, ...)`) or a value that had already
degraded to `void` because an *earlier* part of the same expression threw
an error - this read the wrong member of the value's union, which is
undefined behavior in C (concretely: treating a `num`'s 64-bit integer as
if it were a `char*` pointer). This was found by deliberately testing
`x[5]` on a `data` value (a non-string bracket key) rather than by a
crash actually occurring in normal use, and fixed with a small
`safe_str()` helper (returns the string, or `""` if the value isn't
actually one) used at every such call site, plus a matching fix in
`http.c`'s `res.header()`. `tests/run_tests.sh` now has a dedicated
regression test exercising exactly this. It's the kind of bug that's easy
to introduce piecemeal (each individual builtin looked locally
reasonable) and easy to miss without specifically testing wrong-typed
arguments, not just missing ones - worth being deliberate about if this
codebase keeps growing.

---

## What's real (version 1 baseline — the networking/worker runtime)

- **Core language**: lexer → parser → AST → static checker → tree-walking
  interpreter for everything in Part 1's "Core Language Reference" —
  unchanged from the first pass, still 100% real.
- **Event loop**: a genuine single-threaded reactor driving all server
  I/O and timers, with a real backend on both Linux (`epoll`) and macOS
  (`kqueue`) as of version 2 — see the cross-platform section above for
  what changed and the macOS backend's testing status.
- **Coroutines**: genuine stackful coroutines via `ucontext.h`
  (`src/runtime/coro.c`, `makecontext`/`swapcontext`). Every accepted HTTP
  or WebSocket connection runs on its own coroutine, suspending at
  `reactor_await_fd()` whenever it would otherwise block on `recv`/`send`,
  and resuming when the reactor sees the fd ready. This is what lets one
  OS thread serve many concurrent slow connections without blocking each
  other — verified in testing with 10 concurrent requests against a
  single-threaded server.
- **HTTP/1.1**: hand-written incremental parser (request line, headers,
  `Content-Length` body across multiple reads) — not a wrapped framework,
  per the original brief. Server: `server.on/route/listen`, `{param}`
  path segments, `req.method/path/params/query/headers/body`,
  `res.send/json/status/header` (chainable). Client: `http.get/post`.
- **WebSockets**: real RFC 6455 handshake (hand-rolled SHA-1 + base64,
  verified byte-for-byte against Python's `hashlib` during testing) and
  real frame parsing/masking/serialization. `socket.on/route/listen`,
  connection objects with `.on("message"/"close", fn)`, `.send()`,
  `.close()`.
- **Workers**: real `pthread`-backed `worker.spawn`, `worker.run`, and
  `worker.pool(n)` / `.submit()` / `.onAll()`. Verified with a 4-thread
  pool actually computing on 4 threads in parallel.
- **Timers**: `live.after`/`live.every` are genuine reactor timers (a
  sorted list checked against `epoll_wait`'s timeout), not run
  synchronously — verified firing in due-time order.

## What's simplified or not there, and why

- **No TLS.** `https://`/`wss://` fail with a clear, catchable error
  naming this file; plain `http://`/`ws://` work fully. Adding OpenSSL or
  mbedTLS is a contained, well-scoped follow-up (wrap the same
  read/write points in `netutil.c`), but it's a real dependency and
  build-system change that didn't fit this pass.
- **No chunked transfer encoding.** Only `Content-Length` bodies are
  handled, on both the server and client side. Chunked request/response
  bodies aren't parsed.
- **No WebSocket fragmented-message reassembly.** Each frame is
  delivered to `on("message", ...)` independently, including
  continuation frames (`fin=0`) — multi-frame logical messages are not
  reassembled into one callback invocation. Single-frame text/binary
  messages (the overwhelmingly common case, and what every WS client
  library produces by default for normal-sized messages) work correctly.
- **No WebSocket *client*.** `socket.connect()` raises a clear error.
  The server side (`socket.on/route/listen`) is the real, tested half;
  building the client is mechanically similar (same handshake, same
  frame code, initiated instead of accepted) but didn't fit this pass.
- **`await` doesn't suspend a coroutine for `http.get`/`post`.** The
  client is a synchronous (blocking) call — see `net_read_some`/
  `net_write_all` in `netutil.c`, which fall back to a blocking retry
  loop when called outside a coroutine. This was a deliberate scope cut:
  making the *client* coroutine-aware would mean every top-level script
  and every `async fun` body would need to run as a coroutine too (not
  just accepted server connections), which is a much bigger structural
  change for a benefit (concurrent outbound requests) that most scripts
  using `await http.get(...)` sequentially don't need. The spec itself
  sanctions a blocking fallback for non-event-loop contexts, which is
  what this is throughout, not just in "batch scripts" as originally
  scoped.
- **Function calls (including `async fun`) still run synchronously to
  completion**, exactly as in the first pass — calling one just runs its
  body immediately. Combined with the point above, this means `await`
  is mostly a structural no-op / Task-unwrapper (see `E_AWAIT` in
  `interp.c`) rather than a real suspension point, *except* inside an
  accepted server/WebSocket connection's coroutine, where the
  connection's own I/O genuinely suspends via the reactor even though
  the Jaguar-level `await` expressions inside its handler don't.
- **`Task.all(list)`** is therefore just a wrapper: since every task in
  the list literal already resolved by the time the list is evaluated
  (function calls aren't lazy), it wraps the already-complete list in a
  `Task` for interface consistency with `.then()`/`.catch()`/`.await()`,
  rather than doing real concurrent fan-out.
- **`server.listen()`/`socket.listen()` don't literally block the
  script.** They register the listening socket's accept loop with the
  reactor and return immediately; the *process* stays alive afterward
  because `main.c` enters the reactor loop once the top-level script's
  statements are exhausted, as long as something registered work
  (a listener or a timer). This means multiple `.listen()` calls in one
  file (as in `server_demo.jag`, which listens on both an HTTP and a
  WebSocket port) all end up served concurrently, which is what you
  want — but it does mean `.listen()` returning doesn't mean "done
  serving," it means "now serving, alongside whatever else is
  listening." This is deliberately the same model Node.js uses (a
  `server.listen()` call returns immediately; the process stays alive
  because of the pending listener, not because the call itself blocks)
  rather than a literal per-call block, which would make it impossible
  for a single script to serve on two ports at once without its own
  manual threading.
- **HTTP responses always close the connection** (`Connection: close`
  is always sent, and there's no keep-alive/pipelining). Simpler and
  fully correct per HTTP/1.1 semantics, just not maximally efficient for
  high-throughput use.
- **`worker.spawn`/`.on("done")`** and **`worker.pool`/`.onAll()`** block
  the *calling* thread until the relevant work finishes (a real
  `pthread_join` or condition-variable wait), rather than notifying
  asynchronously via the reactor. This means a worker's OS thread
  genuinely runs in the background while other things could happen, but
  nothing currently *does* happen concurrently on the calling side while
  it's waiting at `.on()`/`.onAll()` — those calls are "do this in the
  background, then block until it's back" rather than fire-and-forget.
  Wiring worker completion into the reactor (via an eventfd/self-pipe)
  would make `.onAll()` a non-blocking registration instead; this
  version prioritized getting real threads, real parallelism, and a real
  pool working correctly over that last mile.
- **`worker.run(path, cb)`** spawns a real OS thread for the target file,
  but then immediately joins it before returning — so from the calling
  script's point of view it's synchronous, even though the work
  genuinely happened on a separate thread (verifiable via the
  `threadId` field on results). True fire-and-forget (script keeps
  running while the file executes) isn't implemented.

## The concurrency-safety model, and a deadlock this pass found and fixed

Worker execution (`worker.c`'s thread entry points) is guarded by a
single process-wide lock (`interp_lock`/`interp_unlock` in `interp.c`),
so multiple worker OS threads never execute interpreter code at the
literal same instant — call it a GIL for worker threads specifically.
Concretely: `worker.pool(4)` gives you 4 real OS threads (verifiable via
distinct thread IDs, and useful for isolating blocking I/O or just
organizing concurrent work), but CPU-bound Jaguar *code* running on those
threads is serialized by this lock, so total wall-clock time for N
CPU-bound jobs is closer to N times one job's duration than to one job's
duration - there's no interpreter-level speedup from adding more pool
threads. Size any timeout around a worker-pool script accordingly.

The first version of this runtime also wrapped *every* HTTP/WebSocket
handler invocation in that same lock, on the theory that the main thread
should be included in the same mutual exclusion as worker threads. That
was wrong and caused a real deadlock, caught during testing: a
`server.route()` handler that submits jobs to a `worker.pool` and then
calls `.onAll()` would hold the lock for its entire (locked) invocation
while blocking on the pool's condition variable — but the pool's own
worker threads need that same lock to actually execute the submitted job
function, so they could never make progress, and `.onAll()` would never
return. `curl -X POST /compute` against `server_demo.jag` hung
indefinitely until this was found and fixed by removing the lock from
the main thread's handler-invocation path entirely (`http.c`/`ws.c` no
longer call `interp_lock`/`interp_unlock` around calling into Jaguar
code). The main thread is single-threaded by construction (it's the only
thread driving the reactor and all coroutines), so it never needs
protection against *itself*; it only ever needed protection against
worker threads, and removing its participation in the lock fixes the
deadlock without reintroducing the race the lock was for. The residual,
accepted risk: a worker thread's job function and the main thread's
request handling are not synchronized against *each other* (only workers
against other workers) — a worker function that mutates shared global
state while a handler concurrently reads or writes the same state has no
protection. Given the spec's own stated worker-isolation intent ("workers
do not share memory... reject captured mutable references crossing a
worker boundary"), well-formed worker functions shouldn't be doing that
in the first place; this is documented here rather than silently risked.

Related to this: worker calls (`interp_invoke`) deliberately run the
job function's closure rooted at `it->globals` rather than the closure
it was actually defined with, so a worker can see top-level functions
and constants but not another function's local variables — an
approximation of the spec's "no captured mutable references" rule
enforced at the value level rather than by the type checker (which still
doesn't do this statically, as noted below). HTTP/WebSocket handlers and
callbacks, by contrast, use `interp_invoke_closure`, which preserves the
normal lexical closure — this had to be a separate code path because a
`socket.route()` handler's `ws.on("message", fun(msg){ ws.send(...) })`
genuinely depends on the inner function seeing the outer `ws` parameter,
which the globals-only path would break (and did, until this was caught
in testing).

## Ambiguities in the spec, and how they were resolved

- **`Task` is both a type keyword and an expression namespace.** The
  spec uses `Task<T>` as a type and `Task.all(...)` as a callable
  namespace in the same worked example. The lexer emits a dedicated
  `TOK_TYPE_TASK` for `Task` (needed so `Task<data>` return-type
  annotations parse), which meant `Task.all(...)` failed to parse as an
  expression until the parser was taught to also accept that token as an
  identifier in expression position. Caught by testing the spec's own
  `fetchAll()` example verbatim.
- **Single-brace string interpolation `{expr}` vs. literal braces**,
  **`import ... from ...`**, **the `live = "1"` directive not shadowing
  the `live` namespace**, **`for`'s optional trailing condition**,
  **`live.deg`'s semantics**, **`await` legality outside `async fun`**,
  **class instantiation not being defined anywhere in the spec**, and
  **numeric division always producing a decimal** were all resolved in
  the first pass and are unchanged here; see the core-language sections
  of this codebase (`parser.c`, `typecheck.c`) for where each is
  implemented.
- **Which callbacks get the caller's closure vs. isolated globals** was
  not something the spec addresses at all (it doesn't model closures
  crossing thread boundaries explicitly beyond the one worker-isolation
  sentence quoted above) — resolved per the concurrency section above:
  worker job bodies are isolated, everything else (HTTP/WS/timer
  callbacks) behaves like an ordinary Jaguar closure.

## Testing

`make test` runs 40 checks: everything from the first pass (core
language, typecheck diagnostics, install smoke test) plus a full HTTP
server+client round trip (including path params, JSON bodies, and
`Task.all` fan-out) against a real running server, a WebSocket
handshake+echo test against a real running server (`tests/ws_check.py`,
using only Python's standard library so it doubles as a from-scratch
reference client), a worker-pool test asserting all results arrive, a
timer-ordering test, OOP tests (fields/methods/`this`/inheritance/
`super`/polymorphism, plus their runtime and compile-time error paths),
and a type-safety regression test for the wrong-typed-argument hardening
described above. It's stable across repeated back-to-back runs (an
earlier version was flaky because backgrounded test servers weren't
being cleaned up between runs — fixed with a `trap EXIT` in
`tests/run_tests.sh`). All of it runs on Linux; the macOS build has not
been run through this suite on real hardware (see the cross-platform
section above).
