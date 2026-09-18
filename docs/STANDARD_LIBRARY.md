# Jaguar Standard Library Reference

The Jaguar 1.1.0 standard library is organized into clean, consistent namespaces under `jag.*`:

## Namespaces & APIs

### `jag.io` / `live`
- `live.on(val)` — print display string of value to stdout
- `live.in(prompt)` — display prompt and read a line from stdin
- `live.after(ms, callback)` — schedule a timer callback after delay `ms`
- `live.every(ms, callback)` — schedule a repeating timer callback
- `live.clear(timerId)` — cancel a scheduled timer

### `jag.fs` / `file` / `dir`
- `file.on(path)` — open file at `path` for appending/reading
- `file.read()` — read file contents
- `file.close()` — close opened file handle
- `dir.on(path)` — select target directory path
- `dir.read()` — list directory entries as `list`
- `dir.del()` — remove directory

### `jag.net` / `http` / `socket` / `server`
- `http.get(url)` — synchronous HTTP GET request
- `http.post(url, body)` — synchronous HTTP POST request
- `server.on(port)` — bind HTTP server to port
- `server.route(method, pattern, handler)` — register HTTP route handler
- `server.listen()` — start serving HTTP requests on reactor event loop
- `socket.on(port)` — bind WebSocket server to port
- `socket.route(path, handler)` — register WebSocket endpoint
- `socket.listen()` — start serving WebSocket connections

### `jag.json` / `json`
- `json.parse(str)` — parse JSON string into `data` / `list` / primitive value
- `json.stringify(val)` — serialize value into formatted JSON string

### `jag.env` / `env`
- `env.get(key, defaultVal)` — retrieve environment variable by name
