# The Jaguar Language Guide

A practical, example-driven guide to Jaguar — from your first script to
building a networked, multi-threaded server. Every example in this guide
has been run against the actual `jag` interpreter; where something in the
language is parsed but not fully runnable (classes, `vector`/`matrix`
math, TLS, etc.), that's called out explicitly rather than glossed over.
For the full list of what's simplified or deferred and why, see
[DESIGN_DECISIONS.md](../DESIGN_DECISIONS.md).

## Table of contents

1. [Getting started](#1-getting-started)
2. [Variables, constants, and types](#2-variables-constants-and-types)
3. [Operators](#3-operators)
4. [Control flow](#4-control-flow)
5. [Loops](#5-loops)
6. [Collections](#6-collections)
7. [String templating](#7-string-templating)
8. [Functions and closures](#8-functions-and-closures)
9. [Classes, enums, structs, vectors, matrices](#9-classes-enums-structs-vectors-matrices)
10. [Error handling](#10-error-handling)
11. [Files and directories](#11-files-and-directories)
12. [JSON](#12-json)
13. [Environment variables](#13-environment-variables)
14. [Console I/O and debugging](#14-console-io-and-debugging)
15. [Live mode](#15-live-mode)
16. [Timers](#16-timers)
17. [HTTP server](#17-http-server)
18. [HTTP client](#18-http-client)
19. [WebSockets](#19-websockets)
20. [Workers (threads)](#20-workers-threads)
21. [Async/await and Task](#21-asyncawait-and-task)
22. [Putting it together: a small app](#22-putting-it-together-a-small-app)
23. [CLI reference](#23-cli-reference)
24. [Gotchas and things that look like other languages but aren't](#24-gotchas)

---

## 1. Getting started

Build and install `jag` (see the main [README](../README.md) for details),
then run a file:

```sh
jag run hello.jag
# or just:
jag hello.jag
```

```jaguar
// hello.jag
live.on("Hello, Jaguar!");
```

```
$ jag run hello.jag
Hello, Jaguar!
```

`live.on(...)` is Jaguar's `print` — you'll use it constantly. File
extensions `.jag` and `.ja` are both accepted. Comments are `//` for the
rest of the line, or `/* ... */` for a block.

---

## 2. Variables, constants, and types

Every variable has a declared type. Declare with `var`, or `fixed` for a
constant that can never be reassigned:

```jaguar
var name: string = "Ada";
var age: num = 30;
var score: decimal = 98.5;
var isActive: bool = true;
fixed PI: scifi = 3.14E2;   // 314 — scifi is scientific notation, always fixed-format
```

| Type | Meaning | Example literal |
|---|---|---|
| `string` | text | `"hello"` |
| `num` | integer | `42` |
| `decimal` | floating point | `3.14` |
| `bool` | true/false | `true` |
| `scifi` | scientific-notation literal | `3.14E2` |
| `data` | key:value map | `{ "a": 1 }` |
| `list<T>` | array, single element type | `[1, 2, 3]` |
| `MixedList` | array, mixed element types | `[1, "two", true]` |

A type annotation is **required** on every `var`/`fixed` declaration —
`var y = 5;` without a `: type` is a type error, caught by `jag check`:

```
$ jag check bad.jag
bad.jag:1: type error: 'y' is missing a required type annotation
```

Reassigning a `fixed` is also a type error, and `fixed` requires an
initializer:

```jaguar
fixed x: num = 5;
x = 10;   // type error: cannot reassign fixed variable 'x'
```

**Numeric division always produces a `decimal`**, even for two `num`
operands — this is a deliberate choice (see DESIGN_DECISIONS.md) so
`7 / 2` gives you `3.5` instead of silently truncating:

```jaguar
live.on(7 / 2);   // 3.5
live.on(7 % 2);   // 1  (modulo stays integer when both operands are num)
```

---

## 3. Operators

```jaguar
// arithmetic
+  -  *  /  %  **      // ** is power, right-associative

// augmented assignment
+= -= *= /= %= **=

// comparison
>  <  >=  <=  ==  !=

// logical
&&  ||  !

// bitwise-ish shifts
<<  >>

// the "between" operator — inclusive range check, returns bool
value <<< (low, high)
```

```jaguar
var n: num = 7;
live.on(n <<< (1, 10));   // true — 1 <= n <= 10
```

Ternary expressions work as you'd expect:

```jaguar
var status: string = (age >= 18) ? "adult" : "minor";
```

---

## 4. Control flow

```jaguar
if (age > 18) {
    live.on("adult");
} elif (age == 18) {
    live.on("just turned adult");
} else {
    live.on("minor");
}
```

`elif`, not `else if` — that's a real, easy-to-forget difference from
C-family languages.

---

## 5. Loops

Jaguar has four loop forms:

```jaguar
// while
var i: num = 0;
loop (i < 5) {
    live.on(i);
    i += 1;
}

// do-while
do loop {
    live.on(i);
    i -= 1;
} while (i > 0);

// for-in, over a list
var scores: list<num> = [10, 20, 30];
for (s in scores) {
    live.on(s);
}

// for-in with an optional trailing condition (filters as it iterates)
for (s in scores, s > 15) {
    live.on(s);   // only prints 20, 30
}

// iterate() — works over both list and data collections
iterate(scores, item) {
    live.on(item);
}
var config: data = { "host": "localhost", "port": "8080" };
iterate(config, v) {
    live.on(v);   // yields each *value* — "localhost", then "8080"
                   // (no key access in iterate(); use for-in over
                   // .keys()-style access isn't available either —
                   // if you need keys, index the data value directly:
                   // config["host"], config.host)
}
```

---

## 6. Collections

### `data` (maps)

```jaguar
var config: data = { "host": "localhost", "port": "8080" };
live.on(config["host"]);   // bracket access
live.on(config.host);       // dot access — both work identically
config["timeout"] = "30";   // assign a new key
config.retries = "3";       // dot-assignment also works
```

### `list<T>`

```jaguar
var nums: list<num> = [1, 2, 3];
nums.append(4);       // insert at end       -> [1, 2, 3, 4]
nums.insert(0);        // insert at start     -> [0, 1, 2, 3, 4]
nums.delete(1);         // remove by index     -> [0, 2, 3, 4]
nums.sort();             // sort ascending
nums.concat([9, 9]);     // append another list's items in place
live.on(nums);
live.on(nums[0]);        // index access
```

### `MixedList`

Like `list`, but elements can be any type — no compile-time element-type
checking:

```jaguar
var mixed: MixedList = [1, "two", true];
```

### `vector<T>` / `matrix<T>`

```jaguar
vector<num> position = [3, 4];
matrix<num> grid = [[1, 2], [3, 4]];
```

> **Honest limitation**: `vector`/`matrix` are recognized types that
> typecheck correctly, but at runtime they're backed by ordinary lists
> (lists-of-lists for `matrix`) — there's no dot-product, matrix
> multiplication, or other vector-math built in. If you need that,
> you're writing it yourself with `list` methods and loops for now.

---

## 7. String templating

Two forms of interpolation are supported inside double-quoted strings:

```jaguar
var name: string = "world";
live.on("hi, {{name}}");        // {{ }} form
live.on("hi, ${name}");          // ${ } form — identical behavior
```

> **Important difference from the original language spec**: a *bare*
> single `{expr}` (no double braces, no `$`) does **not** interpolate —
> it's treated as literal text. This is deliberate: a lone `{` is
> extremely common in ordinary strings (embedded JSON being the obvious
> case), and treating every `{...}` as code would make strings like
> `"{\"a\": 1}"` un-writable. See DESIGN_DECISIONS.md for the full
> reasoning. Use `{{ }}` or `${ }` always.

```jaguar
// this is fine — the braces here are just literal text:
var raw: string = "{\"a\": 1, \"b\": [1,2,3]}";
var parsed: data = json.parse(raw);
```

---

## 8. Functions and closures

```jaguar
fun greet(name: string) {
    live.on("hi, {{name}}");
}
greet("world");

fun add(a: num, b: num): num {
    return a + b;
}
live.on(add(2, 3));   // 5
```

Functions are values and close over their defining scope normally:

```jaguar
fun makeCounter() {
    var count: num = 0;
    fun increment(): num {
        count += 1;
        return count;
    }
    return increment;
}
```

Top-level functions can be called before their textual definition
(they're hoisted), so this is fine:

```jaguar
live.on(double(5));   // 10 — works even though double() is defined below
fun double(n: num): num { return n * 2; }
```

`async fun` is also supported syntactically — see
[§21 Async/await and Task](#21-asyncawait-and-task) for what it actually
does in this implementation (spoiler: it runs synchronously, like a
normal function).

---

## 9. Classes and objects (OOP)

Jaguar has real, working classes now — fields, methods, constructors,
`this`, and single inheritance via `extends`/`super`. (The original
language spec defines `class` declarations but never actually shows how
you construct an instance of one anywhere in its own worked examples —
`new`/`this`/`super`/`extends` are this project's resolution of that gap,
designed to feel familiar from JS/Java/Python rather than invent
something new. See DESIGN_DECISIONS.md for the reasoning.)

### Fields, constructors, methods, `this`

```jaguar
class User {
    var name: string;
    var age: num;

    fun constructor(name: string, age: num) {
        this.name = name;
        this.age = age;
    }

    fun greet(): string {
        return "Hi, I'm {{this.name}}, age {{this.age}}";
    }
}

var u: User = new User("Ada", 30);
live.on(u.name);       // Ada
live.on(u.greet());    // Hi, I'm Ada, age 30
u.age = 31;              // fields are mutable
live.on(u);              // User {"age": 31, "name": Ada}
```

A method literally named `constructor` is called automatically by `new`.
A class with no constructor is perfectly legal — `new X()` just leaves
its fields at their declared defaults (void, unless the field has its
own initializer like `var count: num = 0;`).

### Inheritance: `extends`, `super()`, `super.method()`

```jaguar
class Animal {
    var name: string;
    fun constructor(name: string) { this.name = name; }
    fun speak(): string { return "{{this.name}} makes a sound"; }
}

class Dog extends Animal {
    var breed: string;
    fun constructor(name: string, breed: string) {
        super(name);           // calls Animal's constructor
        this.breed = breed;
    }
    fun speak(): string {
        var base: string = super.speak();   // calls Animal's speak()
        return "{{base}} (specifically, a bark, from a {{this.breed}})";
    }
}

var d: Dog = new Dog("Rex", "Labrador");
live.on(d.speak());
// Rex makes a sound (specifically, a bark, from a Labrador)
```

`super(...)` (no dot) calls the parent class's constructor, bound to the
same instance. `super.method(...)` calls a specific method on the parent
class, also bound to the same instance — both work through as many
inheritance levels as you chain `extends` through.

### Polymorphism

Method lookup on `this` always starts from the instance's actual
(most-derived) class, so overriding works the way you'd expect from any
mainstream OOP language:

```jaguar
class Shape {
    fun area(): decimal { return 0.0; }
    fun describe(): string { return "a shape with area {{this.area()}}"; }
}
class Circle extends Shape {
    var radius: decimal;
    fun constructor(radius: decimal) { this.radius = radius; }
    fun area(): decimal { return 3.14159 * this.radius * this.radius; }
}
class Square extends Shape {
    var side: decimal;
    fun constructor(side: decimal) { this.side = side; }
    fun area(): decimal { return this.side * this.side; }
}

var shapes: MixedList = [new Circle(2.0), new Square(3.0)];
iterate(shapes, s) {
    live.on(s.describe());   // each calls its own overridden area()
}
```

### What's not modeled (yet)

- **No access-control enforcement.** `public`/`private` class modifiers
  parse (and the type checker registers the class name either way), but
  nothing stops external code from reading/calling a "private" class's
  fields or methods. Treat the modifiers as documentation for now, not
  an enforced boundary.
- **Single inheritance only** — there's no interface/trait/mixin
  mechanism, only one `extends` parent per class.
- **No static (class-level) fields or methods** — everything is
  per-instance.
- **Field/constructor visibility isn't checked against types as
  thoroughly as top-level functions in every case** — see [§24
  Gotchas](#24-gotchas) for the specific edge the type checker
  deliberately stays conservative about.

### `enum`, `struct`, `vector`, `matrix`

These remain as in the original reference:

```jaguar
enum Direction { North, South, East, West }
struct Point { x: num; y: num; }
vector<num> position = [3, 4];
matrix<num> grid = [[1, 2], [3, 4]];
```

`enum`/`struct` register their name (so referencing them doesn't error)
but, like classes before this version, still have no dedicated
construction syntax of their own — use a `class` (now that it works) or
a `data` literal for anything you need to actually build and pass
around. `vector`/`matrix` are still backed by ordinary lists at runtime,
with no dot-product/matrix-multiplication built in.

---

## 10. Error handling

```jaguar
try {
    var res: data = await http.get("http://unreachable.invalid");
    live.on(res);
} catch (err: string) {
    live.on("request failed: {{err}}");
}
```

Any runtime error inside a `try` block — a failed network call, a
division by zero, an out-of-range list index, an unknown method call —
is caught and bound to the name you declare in `catch (name: string)`.
Errors are always strings; there's no typed error hierarchy.

---

## 11. Files and directories

Jaguar's file/dir API is a **singleton handle** — there's one "current
file" and one "current directory" at a time, opened with `.on()`:

```jaguar
file.on("notes.txt");
var contents: string = file.read();   // reads the whole file as a string
file.close();

dir.on("./logs");
var names: list<string> = dir.read();  // list of entry names (files + dirs)
dir.close();
dir.del();                              // rmdir — only works on an empty dir
```

> **Honest limitation**: `file.loop()` is accepted syntactically but is
> currently a no-op — there's no line-by-line iteration API yet. Read
> the whole file with `file.read()` and split/process the string
> yourself if you need line-by-line behavior.

---

## 12. JSON

```jaguar
var obj: data = json.parse('{"name":"Joe"}');   // note: use \" inside
                                                    // double-quoted strings,
                                                    // or write the JSON with
                                                    // escaped quotes as shown
var text: string = json.stringify(obj);
live.on(text);
```

Jaguar has no single-quoted strings, so JSON text (which itself uses
double quotes) needs escaping when written as a literal:

```jaguar
var raw: string = "{\"name\": \"Joe\", \"tags\": [1, 2, 3]}";
var obj: data = json.parse(raw);
```

`json.stringify` handles `data`, `list`, `string`, `num`, `decimal`,
`bool`, and `void` (→ `null`).

---

## 13. Environment variables

```jaguar
var port: num = env.get("PORT", 8080);   // second argument is the default
```

---

## 14. Console I/O and debugging

```jaguar
live.on("Hello, World!");             // print
var name: string = live.in("Name? "); // prompt + read a line from stdin
live.on("Hi, {{name}}");

// live.deg: a debug type-assertion helper.
// live.deg(typeName, value1, value2, ...) checks each value against the
// named type and prints one of: expectation met / type mismatch / not defined
var greeting: string = "Hi";
live.deg("string", greeting);    // prints: live.deg: expectation met for argument 1
```

`jag check` also statically flags a `live.deg()` call with fewer than 2
arguments (it can't check anything without at least a type name and one
value).

---

## 15. Live mode

Run a script that re-executes every time you save it:

```sh
jag -live=1 hello.jag
```

Jaguar watches the file's modification time and reloads (re-lex, re-parse,
re-typecheck, re-run) on every save. You can also write `live = "1";` as
the first line of the file — it's recognized as a directive, and doesn't
create a variable named `live` (which would otherwise shadow the
`live.on`/`live.in`/`live.deg` namespace for the rest of the script).

> **Note**: live-reload and a running server (`server.listen()`/
> `socket.listen()`) don't currently combine — once a server starts
> serving, the process is busy running the event loop and won't notice
> further file saves. Use `-live=1` for ordinary scripts, and plain
> `jag run` while iterating on a server.

---

## 16. Timers

```jaguar
live.after(1000, fun() {
    live.on("1 second passed");
});

var tickId: num = live.every(500, fun() {
    live.on("tick");
});
live.clear(tickId);   // stops the repeating timer
```

These are real event-loop timers (not `sleep()`-based) — they fire in
due-time order, and a script that only has timers pending (no server)
stays alive until they're all done or cleared.

---

## 17. HTTP server

```jaguar
import "http.jag";

server.on(8080);

server.route("GET", "/", fun(req: data, res: data) {
    res.send("Welcome to Jaguar HTTP server");
});

server.route("GET", "/users/{id}", fun(req: data, res: data) {
    var id: string = req.params["id"];
    res.json({ "id": id, "name": "Joe" });
});

server.route("POST", "/users", fun(req: data, res: data) {
    var body: data = json.parse(req.body);
    res.status(201).json({ "status": "created", "name": body["name"] });
});

server.listen();
```

`req` fields: `method`, `path`, `params` (route `{param}` captures),
`query` (query-string as `data`), `headers` (`data`), `body` (raw
string). `res` methods: `.send(string)`, `.json(data)`,
`.status(num)` (chainable — returns `res` so you can write
`res.status(201).json(...)`), `.header(key, value)`.

**This is a real server**: connections are handled non-blocking via an
`epoll` event loop and coroutines, so many clients can be connected at
once without blocking each other — not a toy single-connection-at-a-time
loop. Every response currently closes the connection afterward (no
keep-alive), and only `Content-Length` bodies are supported (no chunked
transfer encoding). `https://` isn't available — there's no TLS in this
build; plain `http://` only.

Calling `server.listen()` doesn't literally block your script the way a
raw blocking `accept()` loop would — it registers the listener and
returns, and the *process* stays alive afterward because of that pending
listener (the same model Node.js uses). This is what lets one file both
`server.listen()` an HTTP port and `socket.listen()` a WebSocket port
(see [§19](#19-websockets)) and serve both concurrently.

---

## 18. HTTP client

```jaguar
async fun fetchUser(id: num): Task<data> {
    var res: data = await http.get("http://127.0.0.1:8080/users/{{id}}");
    return json.parse(res["body"]);
}

async fun main() {
    var user: data = await fetchUser(42);
    live.on(user);
}
main();
```

`http.get(url)` / `http.post(url, data)` return a `Task<data>` whose
resolved value is `{ "status": num, "headers": data, "body": string }`.
POST bodies are sent as JSON (the `data` you pass is `json.stringify`'d
automatically, with `Content-Type: application/json` set for you).

A failed request (connection refused, DNS failure, malformed response)
surfaces through `await` as a catchable error:

```jaguar
try {
    var res: data = await http.get("http://127.0.0.1:1/");
    live.on(res);
} catch (err: string) {
    live.on("caught: {{err}}");
}
```

> **How this actually works under the hood**: the client is a
> synchronous (blocking) network call, not a coroutine-suspended one —
> see [§21](#21-asyncawait-and-task) for what that means for `await`
> and `Task.all`.

---

## 19. WebSockets

```jaguar
socket.on(9090);

socket.route("/chat", fun(ws: Socket) {
    ws.on("message", fun(msg: string) {
        ws.send("echo: {{msg}}");
    });
    ws.on("close", fun() {
        live.on("client disconnected");
    });
});

socket.listen();
```

This is a real RFC 6455 implementation — a genuine handshake
(`Sec-WebSocket-Accept` computed via SHA-1 + base64) and real frame
parsing/masking, tested against a raw Python socket client, not a
library. `ws.send(text)` sends a text frame; `ws.close()` closes the
connection.

> **Honest limitations**: there's no WebSocket *client* yet
> (`socket.connect()` raises a clear error rather than pretending to
> work), and multi-frame fragmented messages aren't reassembled — each
> frame is delivered to `on("message", ...)` independently. Ordinary
> single-frame text/binary messages (what every WS client sends by
> default for reasonably-sized messages) work correctly.

---

## 20. Workers (threads)

Three ways to run work on a background OS thread:

```jaguar
// 1. a single background job
fun heavyComputation(): data {
    var total: num = 0;
    var i: num = 0;
    loop (i < 500000) { total += i; i += 1; }
    return { "total": total };
}

var w: Worker = worker.spawn(fun(): data {
    return heavyComputation();
});
w.on("done", fun(result: data) {
    live.on(result);   // { "total": ..., "threadId": ... }
});
```

> The runtime attaches a `threadId` field to the result for you, but
> only when the worker function returns a `data` value (as above) — if
> it returns a plain `num`/`string`/etc., you just get that value back
> with no `threadId` wrapper, since there's nowhere to attach it.

```jaguar
// 2. run a whole separate file on its own thread, fully isolated
worker.run("tasks.jag", fun(result: data) {
    live.on("worker.run finished");
});
```

```jaguar
// 3. a fixed pool of threads processing many jobs
var pool: Worker = worker.pool(4);
pool.submit(fun(): data { return heavyComputation(); });
pool.submit(fun(): data { return heavyComputation(); });
pool.onAll(fun(results: list<data>) {
    live.on(results);   // each entry has "total" and "threadId"
});
```

These are real `pthread` OS threads — `worker.pool(4)` genuinely creates
4 threads, verifiable via each result's `threadId` field (again, only
attached when the job returns a `data` value, as here).

> **Important**: interpreter execution across worker threads is
> serialized by a single lock (documented in DESIGN_DECISIONS.md as a
> deliberate GIL-style tradeoff). That means you get real OS threads
> and real thread-safety, but **not** a computational speedup from
> adding more pool threads — total time for N CPU-bound jobs is closer
> to N times one job's time than to one job's time. Use workers to keep
> heavy computation off a request-handling path (see the combined
> example below), not to make CPU-bound code faster.
>
> Worker functions also can't see the local variables of whatever
> called `worker.spawn`/`.pool` — only top-level functions and `fixed`
> constants (this is deliberate: the spec requires workers not to share
> mutable state with the thread that spawned them).

---

## 21. Async/await and Task

This is the part of Jaguar most worth understanding precisely, because
its behavior here is a simplified, honest version of what the full
language spec describes.

**What actually happens**: calling any function — including one declared
`async fun` — runs its body immediately and synchronously, exactly like
a normal function call. There is no coroutine suspension at the
Jaguar-code level. `await expr`:

- if `expr` is a `Task` (e.g. the direct result of `http.get(...)`),
  unwraps it — returning its value if it succeeded, or raising a
  catchable error if it failed;
- otherwise, it's a no-op that just returns `expr` unchanged.

```jaguar
async fun fetchUser(id: num): Task<data> {
    // http.get() already ran to completion (blocking) by the time this
    // line finishes; await here just unwraps its Task wrapper.
    var res: data = await http.get("http://127.0.0.1:8080/users/{{id}}");
    return json.parse(res["body"]);   // fetchUser() itself returns a
                                        // plain `data` value, not a Task
}

async fun main() {
    // fetchUser(1) and fetchUser(2) below each run to completion,
    // one after another, before this list literal is even evaluated —
    // there's no concurrent fan-out actually happening.
    var results: list<data> = await Task.all([fetchUser(1), fetchUser(2)]);
    live.on(results);
}
```

`Task.all([...])` reflects this honestly: since every item in the list
is already a fully-resolved value by the time the list literal is
evaluated, `Task.all` just wraps that already-complete list — it's an
interface-compatibility wrapper (so `.then()`/`.await()` work on it),
not a real concurrent scheduler.

**Where real asynchrony *does* exist**: inside an accepted HTTP or
WebSocket connection (see §17/§19), the connection's own I/O genuinely
suspends via coroutines and the event loop — that's what lets one thread
serve many slow connections. It's the *outbound* client call
(`http.get`/`http.post`) and top-level `async fun`/`await` that are
simplified to synchronous execution. See DESIGN_DECISIONS.md for the
full reasoning if you're extending this.

**`Task` methods**, usable on anything `http.get`/`post` returns:

```jaguar
var task: Task<data> = http.get("http://127.0.0.1:8080/");
task.then(fun(result: data) { live.on(result); });
task.catch(fun(err: string) { live.on("failed: {{err}}"); });
var result: data = task.await();   // equivalent to `await task`
```

---

## 22. Putting it together: a small app

A single process serving HTTP, WebSocket chat, CPU work on a thread
pool, and a heartbeat timer — everything above, combined:

```jaguar
live = "1";
import "http.jag";

fixed PORT: num = env.get("PORT", 8080);
server.on(PORT);

server.route("GET", "/", fun(req: data, res: data) {
    res.send("Welcome to Jaguar HTTP server");
});

server.route("GET", "/users/{id}", fun(req: data, res: data) {
    res.json({ "id": req.params["id"], "name": "Joe" });
});

var pool: Worker = worker.pool(4);
server.route("POST", "/compute", fun(req: data, res: data) {
    pool.submit(fun(): num { return heavyComputation(); });
    pool.onAll(fun(results: list<num>) {
        res.json({ "results": results });
    });
});

fun heavyComputation(): num {
    var total: num = 0;
    var i: num = 0;
    loop (i < 2000000) { total += i; i += 1; }
    return total;
}

socket.on(9090);
socket.route("/chat", fun(ws: Socket) {
    ws.on("message", fun(msg: string) { ws.send("echo: {{msg}}"); });
    ws.on("close", fun() { live.on("client disconnected"); });
});

live.every(10000, fun() { live.on("heartbeat"); });

server.listen();
socket.listen();
```

Run it, then in another terminal:

```sh
curl http://127.0.0.1:8080/users/5
curl -X POST http://127.0.0.1:8080/compute
```

See `examples/server_demo.jag` in this repo for the exact, tested version
of this file.

---

## 23. CLI reference

```
jag <file.jag>            compile+run once (interpreter backend)
jag run <file.jag>        same, explicit
jag -live=1 <file.jag>    live-reload mode (watches the file for saves)
jag check <file.jag>      lex + parse + typecheck only, no execution
jag build <file.jag>      ahead-of-time native compile [not implemented]
jag --version / --help
```

`jag check` is worth running before `jag run` while learning the
language — it catches missing type annotations, fixed-reassignment,
and a few other mistakes with a clear diagnostic instead of a runtime
surprise:

```
$ jag check mistake.jag
mistake.jag:3: type error: cannot reassign fixed variable 'x'
mistake.jag: 1 type error(s)
```

---

## 24. Gotchas

A quick-reference list of things that catch people coming from other
languages:

- **`elif`, not `else if`.**
- **Every `var`/`fixed` needs a type annotation** — no inference.
- **Division always returns `decimal`**, even `num / num`.
- **Bare `{expr}` in a string does not interpolate** — only `{{expr}}`
  and `${expr}` do. This matters most when you're writing literal JSON
  text as a string.
- **`new X()` works for `class` now, but not `struct`/`enum`** — those
  two still only register their name; build record-like values from a
  `class` (with a `constructor`) or a `data` literal instead.
- **`iterate()` over a `data` value yields values only**, not
  key/value pairs.
- **No access-control enforcement** — `public`/`private` on a class
  parse but nothing stops external code from touching a "private"
  class's fields or methods; treat them as documentation, not a wall.
- **Only single inheritance, no static members** — one `extends` parent
  per class, no interfaces/traits/mixins, no class-level (as opposed to
  per-instance) fields or methods.
- **The type checker's argument/return checking is deliberately
  conservative, not exhaustive** — it only flags a *confident* mismatch
  between two known concrete types (e.g. passing a `string` where a
  function declares `num`). It never flags anything involving an
  unknown-typed value (the result of a builtin call, `this`, a class
  field it can't trace), and it doesn't verify that every code path
  through a function actually returns a value, or that every field
  access on `this`/`super` is spelled correctly — those still surface as
  ordinary catchable runtime errors, not compile-time ones. If a program
  passes `jag check`, that means no *confident* mismatch was found, not
  that the program is fully verified.
- **Calling an unknown *class*** (`new NoSuchClass()`) **is a
  compile-time error; calling an unknown *method or field* on a real
  class is a runtime one** — the type checker can resolve class names
  up front, but doesn't reject a specific wrong method/field name
  (that would need exhaustively tracking every value's exact class,
  which it deliberately doesn't attempt - see above).
- **`async fun` is not concurrent** — it runs synchronously, like any
  other function. `await` mostly just unwraps a `Task`.
- **`worker.pool` doesn't speed up CPU-bound code** — worker threads
  are real, but interpreter execution across them is serialized.
- **Only `http://`/`ws://`, no TLS** — `https://`/`wss://` fail with a
  clear error.
- **HTTP responses always close the connection** — no keep-alive.
- **`live` is a reserved namespace** — `live = "1";` (the live-mode
  directive) is recognized specially and won't shadow
  `live.on`/`.in`/`.deg`; the same protection applies to `json`,
  `env`, `file`, `dir`, `Task`, `server`, `socket`, `worker`, `http`.
- **Native Windows isn't supported — use WSL.** The reactor needs
  `epoll` (Linux) or `kqueue` (macOS); Windows has neither. See
  [docs/WINDOWS.md](WINDOWS.md) for why and how to use WSL instead,
  which runs the real, fully-tested Linux path unmodified.
- **The macOS build is untested** — written against the documented
  `kqueue(2)` API and modeled directly on the working Linux backend, but
  there was no macOS machine available during development. Report
  issues if you hit any; see DESIGN_DECISIONS.md.
