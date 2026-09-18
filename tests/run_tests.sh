#!/usr/bin/env bash
# End-to-end tests for the jag toolchain (interpreter backend).
# Run via `make test` or directly: ./tests/run_tests.sh
set -u
cd "$(dirname "$0")/.."

JAG=./jag
PASS=0
FAIL=0
SERVER_PIDS=""

cleanup() {
    for pid in $SERVER_PIDS; do kill "$pid" >/dev/null 2>&1; done
}
trap cleanup EXIT

check() {
    local desc="$1"; local got="$2"; local want="$3"
    if [ "$got" = "$want" ]; then
        PASS=$((PASS+1))
    else
        FAIL=$((FAIL+1))
        echo "FAIL: $desc"
        echo "  expected: $want"
        echo "  got:      $got"
    fi
}

check_contains() {
    local desc="$1"; local got="$2"; local want="$3"
    if echo "$got" | grep -qF "$want"; then
        PASS=$((PASS+1))
    else
        FAIL=$((FAIL+1))
        echo "FAIL: $desc"
        echo "  expected to contain: $want"
        echo "  got: $got"
    fi
}

# 0. OOP: classes, fields, methods, constructors, this, inheritance,
#    super(), super.method(), polymorphism, and error paths
OUT=$($JAG run tests/programs/oop_basics.jag 2>&1)
check "oop: fields/methods/this/mutation/display" "$OUT" $'Ada\n30\nHi, I\'m Ada, age 30\n31\nUser {"age": 31, "name": Ada}'

OUT=$($JAG run tests/programs/oop_inheritance.jag 2>&1)
check "oop: extends/super()/super.method()" "$OUT" $'Rex\nLabrador\nRex makes a sound (a bark, specifically, from a Labrador)'

OUT=$($JAG run tests/programs/oop_polymorphism.jag 2>&1)
check "oop: dynamic dispatch through this + 3-level inheritance" "$OUT" $'a shape with area 12.5664\na shape with area 9\nC->B->A'

cat > /tmp/jag_oop_errors.jag << 'EOF'
class X { fun m() { return 1; } }
var x: X = new X();
try { live.on(x.nope()); } catch (err: string) { live.on("caught method"); }
try { live.on(x.nope); } catch (err: string) { live.on("caught field"); }
EOF
OUT=$($JAG run /tmp/jag_oop_errors.jag 2>&1)
check_contains "oop: unknown method is a catchable runtime error" "$OUT" "caught method"
check_contains "oop: unknown field is a catchable runtime error" "$OUT" "caught field"

cat > /tmp/jag_oop_unknown_class.jag << 'EOF'
var bad: Unknown = new Unknown();
EOF
OUT=$($JAG check /tmp/jag_oop_unknown_class.jag 2>&1)
check_contains "oop: unknown class is a compile-time type error" "$OUT" "'Unknown' is not a known class"

# 0b. crash-risk regression: wrong-typed builtin arguments must fail
#     gracefully (a value error), never read the wrong union member
cat > /tmp/jag_type_safety.jag << 'EOF'
var x: data = { "a": 1 };
live.on(x[5]);
env.get(42, "default");
var config: data = { "host": "local" };
config[7] = "oops";
live.on("survived");
EOF
OUT=$($JAG run /tmp/jag_type_safety.jag 2>&1)
check_contains "type safety: wrong-typed args degrade gracefully" "$OUT" "survived"

# 1. demo.jag runs and produces expected output for core language features
OUT=$($JAG run tests/programs/demo.jag 2>&1)
check_contains "demo.jag: if/elif/else" "$OUT" "adult"
check_contains "demo.jag: string interpolation" "$OUT" "status: adult"
check_contains "demo.jag: loop-while" "$OUT" $'0\n1\n2\n3\n4'
check_contains "demo.jag: for-in" "$OUT" $'10\n20\n30'
check_contains "demo.jag: iterate" "$OUT" "item: 20"
check_contains "demo.jag: list methods (append/insert/sort)" "$OUT" "[5, 10, 20, 30, 40]"
check_contains "demo.jag: data literal + bracket/member access" "$OUT" "localhost"
check_contains "demo.jag: functions" "$OUT" "hi, world"
check_contains "demo.jag: return values" "$OUT" $'\n5\n'
check_contains "demo.jag: MixedList" "$OUT" "[1, two, true]"
check_contains "demo.jag: between operator (<<<)" "$OUT" "true"
check_contains "demo.jag: json.parse" "$OUT" "[1, 2, 3]"
check_contains "demo.jag: json.stringify" "$OUT" '{"k":"v","n":5}'

# 2. jag check: valid program
OUT=$($JAG check tests/programs/demo.jag 2>&1)
check_contains "check: valid program passes" "$OUT" "OK"

# 3. typecheck diagnostics
cat > /tmp/jag_test_fixed.jag << 'EOF'
fixed x: num = 5;
x = 10;
EOF
OUT=$($JAG check /tmp/jag_test_fixed.jag 2>&1)
check_contains "typecheck: fixed reassignment rejected" "$OUT" "cannot reassign fixed variable"

cat > /tmp/jag_test_notype.jag << 'EOF'
var y = 5;
EOF
OUT=$($JAG check /tmp/jag_test_notype.jag 2>&1)
check_contains "typecheck: missing type annotation rejected" "$OUT" "missing a required type annotation"

cat > /tmp/jag_test_deg.jag << 'EOF'
live.deg("string");
EOF
OUT=$($JAG check /tmp/jag_test_deg.jag 2>&1)
check_contains "typecheck: live.deg arity checked" "$OUT" "arity mismatch"

# 4. try/catch around a failing (unreachable host) networking call: must be
#    catchable via await rather than crashing the process.
cat > /tmp/jag_test_net.jag << 'EOF'
try {
    var res: data = await http.get("http://127.0.0.1:1");
    live.on(res);
} catch (err: string) {
    live.on("caught");
}
live.on("done");
EOF
OUT=$($JAG run /tmp/jag_test_net.jag 2>&1)
check "failed http.get() is catchable via await" "$OUT" $'caught\ndone'

# 6. HTTP server + client round trip (real sockets, real epoll reactor)
timeout 15 "$JAG" run tests/programs/http_server.jag >/tmp/jag_http_server.log 2>&1 &
SERVER_PIDS="$SERVER_PIDS $!"
sleep 0.4
OUT=$(curl -s http://127.0.0.1:8091/)
check "http server: GET / " "$OUT" "Welcome to Jaguar HTTP server"
OUT=$(curl -s http://127.0.0.1:8091/users/42)
check "http server: route params" "$OUT" '{"id":"42","name":"Joe"}'
OUT=$(curl -s -X POST -d '{"name":"Ada"}' http://127.0.0.1:8091/users)
check "http server: POST + json body" "$OUT" '{"status":"created","name":"Ada"}'
cat > /tmp/jag_http_client_test.jag << 'EOF'
async fun fetchUser(id: num): Task<data> {
    var res: data = await http.get("http://127.0.0.1:8091/users/{{id}}");
    return json.parse(res["body"]);
}
async fun main() {
    var user: data = await fetchUser(7);
    live.on(user["id"]);
    var results: list<data> = await Task.all([fetchUser(1), fetchUser(2)]);
    live.on(results);
}
main();
EOF
OUT=$($JAG run /tmp/jag_http_client_test.jag 2>&1)
check_contains "http client: await + fetchUser" "$OUT" "7"
check_contains "http client: Task.all fan-out" "$OUT" '"id": 1'
check_contains "http client: Task.all fan-out (2)" "$OUT" '"id": 2'

# 7. WebSocket echo test, including handshake verification (real RFC 6455
#    frames over a raw socket - see tests/ws_check.py)
timeout 15 "$JAG" run tests/programs/websocket_chat.jag >/tmp/jag_ws_server.log 2>&1 &
SERVER_PIDS="$SERVER_PIDS $!"
sleep 0.4
OUT=$(python3 tests/ws_check.py 2>&1)
check_contains "websocket: handshake + frame echo" "$OUT" "ALL WS CHECKS PASSED"

# 8. worker pool test: N jobs, all results present, and at least one ran on
#    a different OS thread than the main process (pthread_self() check)
( timeout 30 "$JAG" run tests/programs/worker_pool.jag >/tmp/jag_pool.log 2>&1 )
OUT=$(cat /tmp/jag_pool.log)
check_contains "worker pool: all 4 results present" "$OUT" "19999900000, 19999900000, 19999900000, 19999900000"

# 9. timers: live.after/live.every actually scheduled via the reactor (not
#    run synchronously) - all ticks must fire (exact order between same-
#    due-time timers isn't guaranteed, so check membership, not sequence)
OUT=$(timeout 3 "$JAG" run tests/programs/timers.jag 2>&1)
check_contains "timers: tick 1 fires" "$OUT" "tick 1"
check_contains "timers: tick 2 fires" "$OUT" "tick 2"
check_contains "timers: tick 3 fires" "$OUT" "tick 3"
check_contains "timers: live.after fires" "$OUT" "1 second passed"
check_contains "timers: live.clear stops further ticks" "$OUT" "tick 3"
[ "$(echo "$OUT" | grep -c 'tick 4')" -eq 0 ] && PASS=$((PASS+1)) || { FAIL=$((FAIL+1)); echo "FAIL: timers: no tick 4 after live.clear"; }

# 5. install.sh smoke test (dry run into a temp prefix)
TMP_PREFIX=$(mktemp -d)
PREFIX="$TMP_PREFIX" ./install.sh >/tmp/jag_install.log 2>&1
if [ -x "$TMP_PREFIX/bin/jag" ]; then
    PASS=$((PASS+1))
else
    FAIL=$((FAIL+1))
    echo "FAIL: install.sh installs a working binary"
    cat /tmp/jag_install.log
fi
rm -rf "$TMP_PREFIX"

echo ""
echo "== $PASS passed, $FAIL failed =="
[ "$FAIL" -eq 0 ]
