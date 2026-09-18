# Networking Runtime in Jaguar 1.1.0

Jaguar 1.1.0 provides HTTP/1.1 and WebSocket RFC 6455 servers and HTTP client capabilities.

## Architecture
```
Networking API
     ↓
Portable Runtime Interface (netutil, reactor)
     ↓
Platform Backend (Linux epoll / macOS kqueue / Windows IOCP audit)
```

## Supported Features
- HTTP/1.1 concurrent connection handling via coroutines over a single-threaded event loop
- WebSocket server (RFC 6455 handshake & framing)
- HTTP client `http.get` / `http.post`
- Planned TLS & WebSocket client support
