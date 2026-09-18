# Installing on Windows

**Native Windows (MinGW/MSVC, outside WSL) is not supported**, and won't
be a quick patch — here's exactly why, so it's clear this isn't an
oversight:

- The reactor (the event loop behind `server.listen()`/`socket.listen()`)
  is built on `epoll` (Linux) or `kqueue` (macOS/BSD). Windows has neither
  — its equivalent is **IOCP** (I/O Completion Ports), a genuinely
  different programming model, not a drop-in third backend.
- The coroutine primitive that lets one thread handle many concurrent
  connections is POSIX `ucontext.h` (`makecontext`/`swapcontext`), which
  doesn't exist on Windows at all. The Windows equivalent is **Fibers**,
  again a different API with different semantics.

Porting both of those correctly is a real, standalone engineering effort
— comparable in size to building the Linux backend in the first place —
not something to bolt on as an afterthought. It's an open item, not a
silent gap.

## What to do instead: WSL

**WSL2 (Windows Subsystem for Linux) runs a real Linux kernel**, so the
epoll/ucontext-based build works completely unmodified — this is the
same, fully-tested Linux path, not a compatibility shim.

1. Install WSL if you don't have it (from an admin PowerShell):
   ```powershell
   wsl --install
   ```
   Restart when prompted, then set up your Linux username/password.

2. Open the WSL shell (search "Ubuntu" or "WSL" in the Start menu, or run
   `wsl` from a terminal).

3. Install build tools if needed:
   ```sh
   sudo apt update && sudo apt install build-essential
   ```

4. Get the project into your WSL filesystem (not `/mnt/c/...` — a native
   WSL path like `~/jaguar` performs much better) and build:
   ```sh
   cd ~
   # copy or clone the project here, then:
   cd jaguar
   ./installers/install-linux.sh
   ```

5. If `/usr/local/bin` isn't writable without `sudo`, install to your
   home directory instead:
   ```sh
   PREFIX="$HOME/.local" ./installers/install-linux.sh
   echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
   source ~/.bashrc
   ```

From there, `jag run yourfile.jag` works exactly as documented — ports
you open with `server.on()`/`socket.on()` are reachable from Windows too
(WSL2 forwards `localhost` by default), so you can still use your normal
Windows browser/`curl`/tools against a server running inside WSL.

## If you specifically need a native Windows build

That's a real, scoped project (an IOCP-based reactor + a Fiber-based
coroutine backend, mirroring what `reactor_epoll.c`/`coro.c` do today) —
not something to expect as a side effect of anything else. If you need
it, say so explicitly and treat it as its own effort, the way the
macOS `kqueue` backend was: a dedicated backend behind the same
`reactor.h`/`coro.h` interfaces the rest of the runtime already uses.
