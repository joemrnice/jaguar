import socket, base64, hashlib, struct, sys, os

HOST, PORT = "127.0.0.1", 9092

def ws_connect(path):
    key = base64.b64encode(os.urandom(16)).decode()
    s = socket.create_connection((HOST, PORT), timeout=3)
    req = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {HOST}:{PORT}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n"
    )
    s.sendall(req.encode())
    resp = s.recv(4096).decode()
    assert "101" in resp.splitlines()[0], f"handshake failed: {resp!r}"
    accept_line = [l for l in resp.split("\r\n") if l.lower().startswith("sec-websocket-accept")][0]
    got_accept = accept_line.split(":", 1)[1].strip()
    expected = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
    assert got_accept == expected, f"accept key mismatch: {got_accept} != {expected}"
    print("handshake OK, Sec-WebSocket-Accept verified")
    return s

def send_text(s, text):
    payload = text.encode()
    mask = os.urandom(4)
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    header = bytes([0x81, 0x80 | len(payload)]) + mask + masked
    s.sendall(header)

def recv_frame(s):
    b0, b1 = s.recv(2)
    opcode = b0 & 0x0F
    plen = b1 & 0x7F
    if plen == 126:
        plen = struct.unpack(">H", s.recv(2))[0]
    elif plen == 127:
        plen = struct.unpack(">Q", s.recv(8))[0]
    payload = b""
    while len(payload) < plen:
        payload += s.recv(plen - len(payload))
    return opcode, payload

s = ws_connect("/chat")
send_text(s, "hello jaguar")
opcode, payload = recv_frame(s)
print("received opcode", hex(opcode), "payload:", payload.decode())
assert payload.decode() == "echo: hello jaguar", payload

send_text(s, "second message")
opcode, payload = recv_frame(s)
print("received opcode", hex(opcode), "payload:", payload.decode())
assert payload.decode() == "echo: second message"

s.close()
print("ALL WS CHECKS PASSED")
