# ArsiShell ¡ª Master's Interview & Systems Study Notes

This document contains 20 core Operating Systems and systems programming questions with clear, direct, technically precise answers tailored for graduate admissions interviews and technical exams.

---

### 1. What is a shell?
A shell is a command-line interpreter (CLI) that acts as an interface between the user and the operating system kernel. It reads text commands from standard input, parses them into commands and arguments, resolves built-ins or locates external executable binaries, launches and coordinates child processes, and manages standard input/output streams.

---

### 2. Why must `cd` execute inside the shell process?
Every process has its own private current working directory (CWD) stored in its process control block (PCB). An operating system kernel strictly prevents child processes from altering the internal state or working directory of their parent process. If `cd` were an external binary spawned as a child process, the child's CWD would change, and when the child finished and exited, the parent shell's CWD would remain unchanged. Therefore, `cd` must execute in-process using the runtime API (`std::filesystem::current_path()`).

---

### 3. What is a child process?
A child process is a new, isolated unit of execution created by an existing process (the parent). The child receives its own independent virtual address space, program counter, and kernel handle/descriptor table. It runs concurrently with or asynchronously from the parent, depending on whether the parent blocks to wait for it.

---

### 4. What is the difference between Windows `CreateProcess` and POSIX `fork` + `exec`?
- **POSIX (`fork` + `exec`):** Process creation is split into two distinct steps. `fork()` creates an exact clone of the caller's address space using copy-on-write (COW) memory pages and copies open file descriptors. The child can then configure descriptors or environment before calling `execvp()` to overwrite its memory image with the target executable.
- **Windows (`CreateProcess`):** Process creation is a single, explicit, atomic call. The caller passes the program path, command line arguments, process/thread security attributes, inheritance flags, and a `STARTUPINFO` structure specifying pre-configured standard handles. It immediately returns kernel handles to both the process and its primary thread.

---

### 5. What is an anonymous pipe?
An anonymous pipe is a unidirectional, kernel-buffered data channel used for inter-process communication (IPC). It has two ends:
- A **write end**: where the upstream process writes byte streams.
- A **read end**: where the downstream process reads byte streams.
Data placed in the pipe is held in a FIFO kernel buffer until consumed by the reader.

---

### 6. Why must unused pipe handles be closed immediately?
A reader process reads data from a pipe until it encounters **End-Of-File (EOF)**. The operating system kernel generates EOF on a pipe only when **all open write handles referencing the pipe have been closed**. If the parent shell does not close its own copy of the write handle after spawning the writer child, the write handle count never drops to zero. Consequently, downstream readers will wait forever for more data and will never terminate.

---

### 7. What causes a pipeline to hang (deadlock)?
Two primary causes:
1. **Unclosed Write Handles:** As explained in Question 6, if any process (including the parent shell) keeps an unused write handle open, the downstream reader never receives EOF and blocks forever.
2. **Buffer Overfill:** If an upstream process produces output faster than the downstream process consumes it, the pipe's internal buffer (typically 4KB¨C64KB) fills up. The upstream process then blocks on `write()` until space becomes available. If downstream is waiting on upstream before reading, a deadlock occurs.

---

### 8. What is standard I/O redirection?
By default, a process inherits the standard input (`stdin`), standard output (`stdout`), and standard error (`stderr`) streams connected to the interactive terminal. Redirection is the mechanism of replacing these standard streams with open file handles or pipe ends before the program begins executing, so the program reads from or writes to files without needing special code to do so.

---

### 9. What is the difference between `>` and `>>`?
- **`>` (Truncate / Create):** Redirects standard output to a file, creating it if it does not exist, or clearing/truncating its contents to 0 bytes if it already exists (`CREATE_ALWAYS` on Windows, `O_TRUNC` on POSIX).
- **`>>` (Append):** Redirects standard output to a file, creating it if it does not exist, but preserving any existing content and appending all new output to the end of the file (`OPEN_ALWAYS` with file pointer moved to end on Windows, `O_APPEND` on POSIX).

---

### 10. What is the difference between a foreground and background process?
- **Foreground Process:** The shell launches the process and synchronously halts its own execution (`WaitForSingleObject` / `waitpid`), waiting until the process terminates before displaying the next prompt to the user.
- **Background Process (launched with `&`):** The shell launches the process, records its ID in the job table, and immediately returns control to the interactive prompt, allowing the user to continue typing commands while the process executes concurrently.

---

### 11. What is a zombie process?
On POSIX systems, when a child process terminates, the kernel does not immediately purge its process control block (PCB) because the parent may need to read its exit status code. A terminated process whose parent has not yet called `wait()` or `waitpid()` is called a **zombie process**. If the parent runs indefinitely without reaping children, zombie processes accumulate and exhaust system process table slots (PID exhaustion).

---

### 12. What does `waitpid(pid, &status, WNOHANG)` do?
`waitpid()` with the `WNOHANG` flag tells the kernel to check whether the specified child process has changed state (terminated or stopped) **without blocking**. If the child is still running, it immediately returns `0`, allowing the parent shell to continue executing without delay. If the child has terminated, it reaps the child's exit status and frees its PID.

---

### 13. What is the difference between an OS PID and an ArsiShell Job ID?
- **OS PID:** A global, non-deterministic number assigned by the operating system kernel. PIDs are shared across all processes in the system, change across runs, and can be recycled.
- **ArsiShell Job ID:** A deterministic, sequential integer assigned by the shell (`[1]`, `[2]`, `[3]...`) to represent an execution unit launched by the user. A single job ID can represent an entire multi-process pipeline containing multiple OS PIDs.

---

### 14. How does the `jobs` command work?
`jobs` iterates through the shell's internal `g_jobs` vector. For each tracked job, it checks the kernel status of its constituent process handles. It prints a formatted summary showing the job ID, current state (`Running` or `Completed`), OS PID(s), and the original command string. Finished jobs are displayed and then cleaned up.

---

### 15. How does `fg` work?
`fg %id` locates the specified job in the job table, prints its command line, and transitions it to foreground semantics by synchronously blocking on all constituent process handles (`WaitForSingleObject(..., INFINITE)` / `waitpid(..., 0)`). Once all processes in the job terminate, it reaps their exit codes, closes the handles, and removes the job from the job table.

---

### 16. How does `kill` work in ArsiShell?
`kill %id` takes an ArsiShell job ID and locates the corresponding record in `g_jobs`. For safety, ArsiShell strictly restricts termination to jobs it is actively tracking¡ªpreventing accidental termination of arbitrary system processes. On Windows, it calls `TerminateProcess()` on each process handle in the job; on POSIX, it sends `SIGTERM`. It then closes the handles and prints `[<id>] terminated`.

---

### 17. Why is `bg` behavior different on Windows compared to Linux?
On Linux/POSIX, jobs can be paused by sending `SIGSTOP` and resumed in the background by sending `SIGCONT`. The Win32 console subsystem does not support POSIX signal-based process suspension. ArsiShell takes a technically honest approach: instead of faking unsupported semantics, it accurately reports whether the job is already active in the background on Windows, while preserving `SIGCONT` support for POSIX environments.

---

### 18. What is RAII and why is it essential for OS handles?
RAII (Resource Acquisition Is Initialization) is a C++ idiom where resources (kernel handles, file descriptors, memory) are bound to the lifetime of stack-allocated objects. The constructor acquires the resource, and the destructor automatically releases it when execution leaves the enclosing scope. RAII is essential in systems programming because it guarantees that handles are closed even when errors, early returns, or exceptions occur, preventing kernel resource leaks.

---

### 19. How does ArsiShell ensure zero kernel HANDLE leaks on Windows?
1. **Thread Handles:** Win32 `CreateProcessA` returns both a process handle and a primary thread handle. ArsiShell immediately closes `pi.hThread` upon process creation.
2. **File & Pipe Handles:** Managed via RAII guards (`FileHandleGuard`, `PipeCleanupGuard`) that close handles upon exiting the scope.
3. **Process Handles:** Foreground process handles are closed immediately after `WaitForSingleObject()`. Background handles are retained only in the job table and closed upon natural completion, explicit `kill`, foreground wait, or shell exit.
4. **Empirical Proof:** Verified via Win32 `GetProcessHandleCount()`, maintaining a flat delta of 0 leaked handles across 325 stress operations.

---

### 20. What would you improve or add next?
1. **Interactive Line Editing:** Implement terminal raw mode / curses support for interactive history search (arrow keys) and tab autocompletion.
2. **Command Chaining:** Add support for logical conditional operators (`&&`, `||`) and command sequence separators (`;`).
3. **Environment Variable Expansion:** Add support for expanding `$VARIABLE` expressions from the process environment table.
4. **Signal Handling (POSIX):** Add a full POSIX signal handler for `SIGCHLD` and terminal process group management (`tcsetpgrp()`) for complete job control.
