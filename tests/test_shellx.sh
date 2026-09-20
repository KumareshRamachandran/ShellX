#!/bin/bash
# ShellX automated test script
# Usage: bash tests/test_shellx.sh ./build/shellx

SHELLX="${1:-./build/shellx}"
PASS=0
FAIL=0
TOTAL=0
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

# --- Test helper ---
# Runs shellx with the given input and checks if expected substring appears in combined output
run_test() {
    local name="$1"
    local input="$2"
    local expected="$3"

    TOTAL=$((TOTAL + 1))

    # Feed input to shellx, capture all output
    local actual
    actual=$(echo "$input" | "$SHELLX" 2>&1 || true)

    if echo "$actual" | grep -qF "$expected"; then
        echo "  PASS: $name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $name"
        echo "    Expected (substring): $expected"
        echo "    Got: $actual"
        FAIL=$((FAIL + 1))
    fi
}

# Test that checks exit code
run_test_exit_code() {
    local name="$1"
    local input="$2"
    local expected_code="$3"

    TOTAL=$((TOTAL + 1))

    set +e
    echo "$input" | "$SHELLX" > /dev/null 2>&1
    local code=$?
    set -e

    if [ "$code" -eq "$expected_code" ]; then
        echo "  PASS: $name (exit code $code)"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $name"
        echo "    Expected exit code: $expected_code"
        echo "    Got: $code"
        FAIL=$((FAIL + 1))
    fi
}

echo "============================================"
echo "  ShellX Test Suite"
echo "  Binary: $SHELLX"
echo "============================================"
echo ""

# --- Basic commands ---
echo "[Basic Commands]"
run_test "echo hello" "echo hello" "hello"
run_test "echo with multiple args" "echo hello world" "hello world"
run_test "pwd" "pwd" "/"

# --- Built-ins ---
echo ""
echo "[Built-in Commands]"

# cd + pwd
TOTAL=$((TOTAL + 1))
actual=$(printf 'cd /tmp\npwd\n' | "$SHELLX" 2>&1 || true)
if echo "$actual" | grep -qF "/tmp"; then
    echo "  PASS: cd /tmp + pwd"
    PASS=$((PASS + 1))
else
    echo "  FAIL: cd /tmp + pwd"
    echo "    Got: $actual"
    FAIL=$((FAIL + 1))
fi

# cd with no args (goes to $HOME)
TOTAL=$((TOTAL + 1))
actual=$(printf 'cd\npwd\n' | "$SHELLX" 2>&1 || true)
if echo "$actual" | grep -qF "$HOME"; then
    echo "  PASS: cd (no args) goes to \$HOME"
    PASS=$((PASS + 1))
else
    echo "  FAIL: cd (no args) goes to \$HOME"
    echo "    Expected to contain: $HOME"
    echo "    Got: $actual"
    FAIL=$((FAIL + 1))
fi

# cd to nonexistent dir
run_test "cd to nonexistent dir" "cd /nonexistent_dir_xyz" "No such file or directory"

# exit
run_test_exit_code "exit returns 0" "exit" 0

# --- Redirection ---
echo ""
echo "[I/O Redirection]"

# > (truncate)
TOTAL=$((TOTAL + 1))
actual=$(printf "echo hello > $TMPDIR/out.txt\ncat $TMPDIR/out.txt\n" | "$SHELLX" 2>&1 || true)
if echo "$actual" | grep -qF "hello"; then
    echo "  PASS: echo hello > file"
    PASS=$((PASS + 1))
else
    echo "  FAIL: echo hello > file"
    echo "    Got: $actual"
    FAIL=$((FAIL + 1))
fi

# >> (append)
TOTAL=$((TOTAL + 1))
actual=$(printf "echo hello > $TMPDIR/append.txt\necho world >> $TMPDIR/append.txt\ncat $TMPDIR/append.txt\n" | "$SHELLX" 2>&1 || true)
if echo "$actual" | grep -qF "hello" && echo "$actual" | grep -qF "world"; then
    echo "  PASS: echo >> file (append)"
    PASS=$((PASS + 1))
else
    echo "  FAIL: echo >> file (append)"
    echo "    Got: $actual"
    FAIL=$((FAIL + 1))
fi

# < (input redirection)
TOTAL=$((TOTAL + 1))
echo "input_content" > "$TMPDIR/input.txt"
actual=$(printf "cat < $TMPDIR/input.txt\n" | "$SHELLX" 2>&1 || true)
if echo "$actual" | grep -qF "input_content"; then
    echo "  PASS: cat < file"
    PASS=$((PASS + 1))
else
    echo "  FAIL: cat < file"
    echo "    Got: $actual"
    FAIL=$((FAIL + 1))
fi

# Missing input file
run_test "redirect from nonexistent file" "cat < /nonexistent_file_xyz" "No such file or directory"

# --- Pipes ---
echo ""
echo "[Pipes]"

run_test "two-stage pipe" "echo hello | cat" "hello"

# Multi-stage pipe
TOTAL=$((TOTAL + 1))
printf "banana\napple\ncherry\napple\n" > "$TMPDIR/fruits.txt"
actual=$(printf "cat $TMPDIR/fruits.txt | sort | uniq\n" | "$SHELLX" 2>&1 || true)
if echo "$actual" | grep -qF "apple" && echo "$actual" | grep -qF "banana" && echo "$actual" | grep -qF "cherry"; then
    echo "  PASS: multi-stage pipe (cat | sort | uniq)"
    PASS=$((PASS + 1))
else
    echo "  FAIL: multi-stage pipe (cat | sort | uniq)"
    echo "    Got: $actual"
    FAIL=$((FAIL + 1))
fi

# Pipe with grep
run_test "pipe with grep" "echo hello world | grep hello" "hello world"

# --- Background ---
echo ""
echo "[Background Jobs]"

# Background job prints [N] pid
TOTAL=$((TOTAL + 1))
actual=$(printf 'sleep 0.1 &\n' | "$SHELLX" 2>&1 || true)
if echo "$actual" | grep -qE '\[1\] [0-9]+'; then
    echo "  PASS: background job prints [N] pid"
    PASS=$((PASS + 1))
else
    echo "  FAIL: background job prints [N] pid"
    echo "    Got: $actual"
    FAIL=$((FAIL + 1))
fi

# --- Failure cases ---
echo ""
echo "[Error Handling]"

run_test "nonexistent command" "nonexistent_command_xyz" "No such file or directory"
run_test "unsupported &&" "echo hello && echo world" "not supported"
run_test "unsupported ||" "echo hello || echo world" "not supported"
run_test "empty pipe segment" "| ls" "syntax error"
run_test "trailing pipe" "ls |" "syntax error"
run_test "backgrounded pipeline rejected" "echo hello | cat &" "backgrounded pipelines"

# --- Quoted strings ---
echo ""
echo "[Quoted Strings]"
run_test "double-quoted string" 'echo "hello world"' "hello world"

# --- Summary ---
echo ""
echo "============================================"
echo "  Results: $PASS passed, $FAIL failed, $TOTAL total"
echo "============================================"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
