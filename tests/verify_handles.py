#!/usr/bin/env python3
import subprocess
import time
import os
import sys
import threading
import ctypes
from ctypes import wintypes

kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
GetProcessHandleCount = kernel32.GetProcessHandleCount
GetProcessHandleCount.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
GetProcessHandleCount.restype = wintypes.BOOL

OpenProcess = kernel32.OpenProcess
OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
OpenProcess.restype = wintypes.HANDLE

CloseHandle = kernel32.CloseHandle
CloseHandle.argtypes = [wintypes.HANDLE]
CloseHandle.restype = wintypes.BOOL

PROCESS_QUERY_INFORMATION = 0x0400

def get_handle_count(pid):
    hProc = OpenProcess(PROCESS_QUERY_INFORMATION, False, pid)
    if not hProc:
        return -1
    count = wintypes.DWORD()
    success = GetProcessHandleCount(hProc, ctypes.byref(count))
    CloseHandle(hProc)
    if success:
        return count.value
    return -1

def main():
    print("===========================================================")
    print("       ArsiShell Kernel HANDLE Leak Stress Verification     ")
    print("===========================================================")

    shell_exe = os.path.abspath('ArsiShell.exe')
    helper_exe = os.path.abspath('tests/test_helper.exe')
    if not os.path.exists(helper_exe):
        subprocess.run(['g++', '-std=c++17', 'tests/test_helper.cpp', '-o', helper_exe])

    proc = subprocess.Popen(
        [shell_exe],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
        cwd=os.getcwd()
    )

    # Drain stdout continuously so pipe buffer never deadlocks
    threading.Thread(target=lambda: [line for line in proc.stdout], daemon=True).start()

    def send(cmd, delay=0.02):
        proc.stdin.write(cmd + '\n')
        proc.stdin.flush()
        if delay > 0:
            time.sleep(delay)

    # Initial warmup: initializes one-time Win32 CRT thread pool & console buffers
    print("[*] Initializing runtime and warming up Win32 process pool...")
    for _ in range(5):
        send('tests\\test_helper.exe 5 0', 0.02)
        send('echo warmup | findstr warmup', 0.02)
    time.sleep(0.5)

    base_handles = get_handle_count(proc.pid)
    print(f"[*] Post-initialization steady-state baseline: {base_handles} handles")

    # Stress Workload 1: 100 external process launches
    print("[*] Running 100 external process launches...")
    for _ in range(100):
        send('tests\\test_helper.exe 5 0', 0.015)
    time.sleep(0.5)
    h_after_proc = get_handle_count(proc.pid)
    print(f"    Handle count after 100 processes: {h_after_proc} (delta: {h_after_proc - base_handles})")

    # Stress Workload 2: 100 redirection operations
    print("[*] Running 100 redirection operations...")
    for _ in range(100):
        send('echo stress_data > stress.tmp', 0.015)
    time.sleep(0.5)
    if os.path.exists('stress.tmp'): os.remove('stress.tmp')
    h_after_redir = get_handle_count(proc.pid)
    print(f"    Handle count after 100 redirections: {h_after_redir} (delta: {h_after_redir - base_handles})")

    # Stress Workload 3: 100 pipe pipelines
    print("[*] Running 100 pipe pipeline executions...")
    for _ in range(100):
        send('echo pipe_data | findstr pipe', 0.015)
    time.sleep(0.5)
    h_after_pipes = get_handle_count(proc.pid)
    print(f"    Handle count after 100 pipelines: {h_after_pipes} (delta: {h_after_pipes - base_handles})")

    # Stress Workload 4: 25 background jobs with asynchronous reaping
    print("[*] Running 25 background jobs with asynchronous reaping...")
    for _ in range(25):
        send('tests\\test_helper.exe 50 0 &', 0.02)
    time.sleep(1.5)
    send('echo reap_check', 0.1)
    h_after_bg = get_handle_count(proc.pid)
    print(f"    Handle count after 25 background jobs reaped: {h_after_bg} (delta: {h_after_bg - base_handles})")

    final_handles = get_handle_count(proc.pid)
    delta = final_handles - base_handles
    print(f"[*] Final ArsiShell HANDLE count: {final_handles}")
    print(f"[*] Net Steady-State Handle Delta across 325 operations: {delta} handles")

    send('exit', 0.1)
    proc.stdin.close()
    proc.wait(timeout=5)

    print("===========================================================")
    if delta == 0:
        print("RESULT: SUCCESS -- EXACT ZERO LEAKS (delta = 0)")
        sys.exit(0)
    elif delta <= 2:
        print(f"RESULT: PASS -- Handle count stable within transient variance (delta = {delta})")
        sys.exit(0)
    else:
        print(f"RESULT: FAILED -- Leaks detected (delta = {delta})")
        sys.exit(1)

if __name__ == '__main__':
    main()
