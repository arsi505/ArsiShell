#!/usr/bin/env python3
import subprocess
import time
import os
import sys

SHELL_PATH = os.path.abspath('ArsiShell.exe')
TESTS_DIR = os.path.abspath('tests')
TEST_HELPER_EXE = os.path.join(TESTS_DIR, 'test_helper.exe')

class TestRunner:
    def __init__(self):
        self.passed = 0
        self.failed = 0
        self.total = 0

    def assert_test(self, name, condition, details=""):
        self.total += 1
        if condition:
            self.passed += 1
            print(f"  [PASS] {name}")
        else:
            self.failed += 1
            print(f"  [FAIL] {name} -- {details}")

    def run_interactive_session(self, commands):
        proc = subprocess.Popen(
            [SHELL_PATH],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            cwd=os.getcwd()
        )
        for cmd, delay in commands:
            proc.stdin.write(cmd + '\n')
            proc.stdin.flush()
            if delay > 0:
                time.sleep(delay)
        proc.stdin.close()
        out = proc.stdout.read()
        proc.wait(timeout=10)
        return out

def ensure_test_helper():
    if not os.path.exists(TEST_HELPER_EXE):
        cmd = ['g++', '-std=c++17', os.path.join(TESTS_DIR, 'test_helper.cpp'), '-o', TEST_HELPER_EXE]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print("Failed to build test_helper:", res.stderr)
            sys.exit(1)

def main():
    print("===========================================================")
    print("       ArsiShell Comprehensive Step 1-7 Test Suite         ")
    print("===========================================================")

    ensure_test_helper()
    runner = TestRunner()
    helper_cmd = 'tests\\test_helper.exe'

    # --- CATEGORY 1: BASICS ---
    print("\n[1. Basics]")
    out = runner.run_interactive_session([
        ('pwd', 0.1),
        ('echo hello basics', 0.1),
        ('cd ..', 0.1),
        ('pwd', 0.1),
        ('cd "' + os.getcwd() + '"', 0.1),
        ('pwd', 0.1),
        ('invalid_cmd_xyz_123', 0.1),
        ('exit', 0.1)
    ])
    runner.assert_test("pwd prints current path", os.getcwd().lower() in out.lower(), "current directory missing")
    runner.assert_test("echo outputs arguments", "hello basics" in out, "echo output missing")
    runner.assert_test("cd navigates parent and back", "ArsiShell>" in out, "cd prompt failed")
    runner.assert_test("invalid command reports error", "command not found: invalid_cmd_xyz_123" in out, "missing error")

    # --- CATEGORY 2: PARSING ---
    print("\n[2. Parsing & Quotes]")
    out = runner.run_interactive_session([
        ('echo    multiple    spaces   between    args', 0.1),
        ('echo "double quote test with spaces"', 0.1),
        ('echo \'single quote test with spaces\'', 0.1),
        ('echo "nested \'single\' in double"', 0.1),
        ('echo "unmatched quote test', 0.1),
        ('exit', 0.1)
    ])
    runner.assert_test("multiple spaces collapsed", "multiple spaces between args" in out, "spacing mismatch")
    runner.assert_test("double quotes preserved", "double quote test with spaces" in out, "double quote mismatch")
    runner.assert_test("single quotes preserved", "single quote test with spaces" in out, "single quote mismatch")
    runner.assert_test("nested quotes handled", "nested \'single\' in double" in out, "nested quote mismatch")
    runner.assert_test("unmatched quote error", "syntax error: unclosed quote" in out, "missing quote syntax error")

    # --- CATEGORY 3: REDIRECTION ---
    print("\n[3. Input / Output Redirection]")
    test_f1 = 'tests_out1.tmp'
    test_in = 'tests_in1.tmp'
    with open(test_in, 'w') as f:
        f.write("line_alpha\nline_beta\n")
    out = runner.run_interactive_session([
        (f'echo first_line > {test_f1}', 0.1),
        (f'echo second_line >> {test_f1}', 0.1),
        (f'sort.exe < {test_in}', 0.1),
        ('sort.exe < nonexistent_file_404.tmp', 0.1),
        ('echo "test > in_quotes"', 0.1),
        (f'echo attached_operator>{test_f1}', 0.1),
        ('exit', 0.1)
    ])
    f1_content = open(test_f1).read() if os.path.exists(test_f1) else ""
    runner.assert_test("output redirection creates and writes file", "attached_operator" in f1_content, f1_content)
    runner.assert_test("input redirection feeds stdin", "line_alpha" in out and "line_beta" in out)
    runner.assert_test("nonexistent input file error", "No such file or directory" in out)
    runner.assert_test("operator inside quotes is literal", "test > in_quotes" in out)
    for p in [test_f1, test_in]:
        if os.path.exists(p): os.remove(p)

    # --- CATEGORY 4: PIPES ---
    print("\n[4. Inter-Process Communication (Pipes)]")
    test_pout = 'tests_pout.tmp'
    out = runner.run_interactive_session([
        ('echo apple banana orange | findstr banana', 0.1),
        ('echo one | echo two | echo three', 0.1),
        (f'echo "pipeline to file" | findstr pipeline > {test_pout}', 0.1),
        ('echo "pipe | inside | quotes"', 0.1),
        ('echo broken || findstr broken', 0.1),
        ('| broken_pipe', 0.1),
        ('exit', 0.1)
    ])
    pout_content = open(test_pout).read() if os.path.exists(test_pout) else ""
    runner.assert_test("single pipe transfers data between processes", "banana" in out)
    runner.assert_test("multi-stage pipeline executes sequentially", "three" in out)
    runner.assert_test("pipeline with output redirection", "pipeline to file" in pout_content)
    runner.assert_test("pipe operator inside quotes is literal", "pipe | inside | quotes" in out)
    runner.assert_test("syntax error detected on '||'", "syntax error near unexpected token '||'" in out)
    runner.assert_test("syntax error detected on leading '|'", "syntax error near unexpected token '|'" in out)
    if os.path.exists(test_pout): os.remove(test_pout)

    # --- CATEGORY 5: BACKGROUND PROCESSES ---
    print("\n[5. Background Execution (&)]")
    out = runner.run_interactive_session([
        (f'{helper_cmd} 1000 &', 0.2),
        ('echo immediate_prompt', 0.1),
        (f'{helper_cmd} 300 | {helper_cmd} 300 &', 0.2),
        ('echo waiting_reap', 1.5),
        ('echo after_reap', 0.2),
        ('exit', 0.1)
    ])
    runner.assert_test("background returns prompt immediately", "immediate_prompt" in out)
    runner.assert_test("background job assigned [id] and PID", "[1]" in out)
    runner.assert_test("background pipeline assigned [id] and PIDs", "[2] PIDs" in out)
    runner.assert_test("asynchronous completion reaped before prompt", "Done" in out)

    # --- CATEGORY 6: COMMAND HISTORY ---
    print("\n[6. Command History]")
    out = runner.run_interactive_session([
        ('pwd', 0.1),
        ('echo hist_item', 0.1),
        ('history', 0.1),
        ('!!', 0.1),
        ('!2', 0.1),
        ('!9999', 0.1),
        ('!abc', 0.1),
        ('exit', 0.1)
    ])
    runner.assert_test("history lists sequential commands", "1  pwd" in out and "2  echo hist_item" in out)
    runner.assert_test("!! repeats previous command", "history" in out)
    runner.assert_test("!n repeats nth command", "hist_item" in out)
    runner.assert_test("out of bounds history error", "!9999: event not found" in out)
    runner.assert_test("malformed history specifier error", "!abc: event not found" in out)

    # --- CATEGORY 7: JOB MANAGEMENT ---
    print("\n[7. Process and Job Management]")
    out = runner.run_interactive_session([
        (f'{helper_cmd} 3000 &', 0.2),
        (f'{helper_cmd} 3000 &', 0.2),
        ('jobs', 0.2),
        ('kill %1', 0.2),
        ('kill %999', 0.2),
        ('kill abc', 0.2),
        ('kill', 0.2),
        ('jobs', 0.2),
        ('fg %2', 3.2),
        ('fg %999', 0.2),
        ('fg', 0.2),
        (f'{helper_cmd} 2000 &', 0.2),
        ('bg %3', 0.2),
        ('bg %999', 0.2),
        ('kill %3', 0.2),
        (f'{helper_cmd} 2500 | {helper_cmd} 2500 &', 0.2),
        ('jobs', 0.2),
        ('kill %4', 0.2),
        ('jobs', 0.2),
        ('exit', 0.1)
    ])
    runner.assert_test("jobs lists active jobs as Running", "Running" in out)
    runner.assert_test("kill %id terminates job", "[1] terminated" in out)
    runner.assert_test("kill error on invalid job", "kill: %999: no such job" in out)
    runner.assert_test("kill error on non-numeric", "kill: abc: invalid job id" in out)
    runner.assert_test("kill error on missing arg", "kill: missing job id" in out)
    runner.assert_test("fg brings job to foreground", "test_helper" in out)
    runner.assert_test("fg error on missing job", "fg: %999: no such job" in out)
    runner.assert_test("bg reports honest Windows status", "already running in background" in out)
    runner.assert_test("pipeline tracked as one job", "[4] Running" in out or "[4] PIDs" in out)
    runner.assert_test("kill terminates background pipeline", "[4] terminated" in out)

    # --- CATEGORY 8: RELIABILITY & CLEAN EXIT ---
    print("\n[8. Reliability & Stress Tests]")
    rapid_cmds = []
    for _ in range(8):
        rapid_cmds.append((f'{helper_cmd} 150 &', 0.05))
    rapid_cmds.append(('echo rapid_done', 1.0))
    rapid_cmds.append(('jobs', 0.2))
    rapid_cmds.append(('exit', 0.1))
    out = runner.run_interactive_session(rapid_cmds)
    runner.assert_test("rapid job creation and completion", "rapid_done" in out and "Done" in out)
    runner.assert_test("clean shell exit without crash", "ArsiShell>" in out)

    print("\n===========================================================")
    print(f"TEST SUMMARY: Total: {runner.total} | Passed: {runner.passed} | Failed: {runner.failed}")
    print("===========================================================")

    if runner.failed > 0:
        sys.exit(1)
    else:
        sys.exit(0)

if __name__ == '__main__':
    main()
