# ArsiShell

ArsiShell is a cross-platform command-line shell and lightweight process manager implemented in C++17. Designed as an educational systems programming project for Master's study in Computer Science, it demonstrates core Operating Systems concepts including process lifecycles, inter-process communication (IPC) via pipes, I/O redirection, asynchronous background execution, and job control.

## Why I Built It

Most computer science students encounter operating systems concepts purely in textbooks. I built ArsiShell to bridge theoretical concepts with concrete implementation. By developing a functional shell from scratch without third-party frameworks, I explored:

- How kernels create, isolate, and synchronize processes
- How anonymous pipes transfer byte streams between decoupled programs
- How file descriptors and kernel object handles redirect standard I/O streams
- How shells track asynchronous background execution and prevent zombie processes
- How to design clean, leak-free resource management across disparate kernel architectures (Win32 vs. POSIX)

## Features

- **Interactive REPL:** Clean prompt (`ArsiShell> `) with input normalization and error handling.
- **Robust Command Parsing:**
  - Handles variable whitespace, tabs, and empty inputs safely.
  - Supports single quotes (`'...'`) and double quotes (`"..."`) with escape support.
  - Recognizes shell operators (`<`, `>`, `>>`, `|`, `&`) with or without adjacent spaces.
  - Preserves operators inside quotes as literal characters (e.g., `echo "A | B"` prints `A | B`).
- **Built-in Commands:**
  - `cd [dir]` ¡ª Changes working directory (supports quoted paths; defaults to user home).
  - `pwd` ¡ª Prints the current working directory.
  - `echo [args...]` ¡ª Prints arguments separated by spaces.
  - `history` ¡ª Displays numbered command history.
  - `jobs` ¡ª Lists active and recently completed background jobs.
  - `kill %id` / `kill id` ¡ª Terminates all processes belonging to a tracked job.
  - `fg %id` / `fg id` ¡ª Brings a background job to the foreground and waits for completion.
  - `bg %id` / `bg id` ¡ª Queries or resumes background job state.
  - `clear` ¡ª Clears terminal display buffer across Windows and POSIX.
  - `help` ¡ª Displays command documentation and supported syntax.
  - `exit` ¡ª Reaps active handles and exits cleanly.
- **Command History Expansion:**
  - `!!` ¡ª Repeats the previous command.
  - `!n` ¡ª Re-executes the $n$-th command from history.
  - Persistent storage in `~/.arsishell_history` capped at 1,000 entries.
- **Input and Output Redirection:**
  - `<` ¡ª Redirects standard input from a file.
  - `>` ¡ª Redirects standard output to a file (creates or truncates).
  - `>>` ¡ª Appends standard output to a file.
- **Multi-Stage Pipelines:**
  - Arbitrary pipe chains: `command1 | command2 | command3 ...`
  - Proper pipe inheritance and immediate closure of parent write handles to prevent reader deadlocks.
- **Process & Job Management:**
  - Background execution with `&`.
  - Deterministic sequential job IDs (`[1]`, `[2]`, `[3]...`).
  - Entire pipelines tracked as **one single job** with multiple constituent PIDs.
  - Non-blocking asynchronous reaping before prompts (`[<id>] Done <command>`).
  - Zero process/thread handle leaks verified under 300+ stress operations.

## Example Session

```text
ArsiShell> pwd
D:\Masters-Projects\ArsiShell

ArsiShell> echo "Operating Systems" | findstr Systems
Operating Systems

ArsiShell> hostname > machine.txt
ArsiShell> type machine.txt
DESKTOP-LEGION

ArsiShell> tests\test_helper.exe 3000 &
[1] 14220

ArsiShell> jobs
[1] Running    PID 14220    tests\test_helper.exe 3000

ArsiShell> fg %1
tests\test_helper.exe 3000
(blocks for 3 seconds...)

ArsiShell> tests\test_helper.exe 4000 | tests\test_helper.exe 4000 &
[2] PIDs 15104, 16820

ArsiShell> kill %2
[2] terminated

ArsiShell> history
1  pwd
2  echo "Operating Systems" | findstr Systems
3  hostname > machine.txt
4  type machine.txt
5  tests\test_helper.exe 3000 &
6  jobs
7  fg %1
8  tests\test_helper.exe 4000 | tests\test_helper.exe 4000 &
9  kill %2
10 history

ArsiShell> exit
```

## Architecture

Data flows through dedicated processing stages:

```text
               ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
               ©¦   User Input String   ©¦
               ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©Ð©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼
                          ©¦
                          ¨‹
               ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
               ©¦   History Expansion   ©¦ (Expands !! and !n)
               ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©Ð©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼
                          ©¦
                          ¨‹
               ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
               ©¦    Lexical Scanner    ©¦ (Extracts tokens, quotes, operators)
               ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©Ð©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼
                          ©¦
                          ¨‹
               ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
               ©¦   Background Parser   ©¦ (Extracts terminal '&')
               ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©Ð©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼
                          ©¦
                          ¨‹
               ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
               ©¦    Pipeline Parser    ©¦ (Splits on '|' into PipelineStages)
               ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©Ð©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼
                          ©¦
                          ¨‹
               ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
               ©¦  Redirection Parser   ©¦ (Extracts '<', '>', '>>')
               ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©Ð©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼
                          ©¦
                          ¨‹
               ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
               ©¦ Execution Dispatcher  ©¦
               ©¸©¤©¤©¤©¤©¤©Ð©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©Ð©¤©¤©¤©¤©¤©¼
                     ©¦           ©¦
         (Built-in)  ¨‹           ¨‹  (External)
        ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´       ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
        ©¦ In-Process   ©¦       ©¦ Process / Job Manager  ©¦
        ©¦ Built-in API ©¦       ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©Ð©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼
        ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼                   ©¦
                               ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©Ø©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
                               ¨‹                       ¨‹
                        Windows APIs              POSIX APIs
                        CreateProcessA            fork() / execvp()
                        CreatePipe                pipe() / dup2()
                        TerminateProcess          kill()
```

## Operating Systems Concepts Demonstrated

### 1. Process Creation & Isolation
- **Windows:** Process creation is explicit via `CreateProcessA()`, returning process and primary thread handles. The child process receives its own virtual address space, handle table, and environment block.
- **POSIX:** Process creation uses `fork()` (copying address space and descriptor tables via copy-on-write) followed by `execvp()` to load the new executable image.
- **In-Process vs. Out-of-Process:** Built-ins like `cd` must execute within the shell's process address space; executing `cd` in a child process would alter the child's working directory and leave the parent shell unchanged.

### 2. Inter-Process Communication (IPC) via Pipes
- A pipeline connects the standard output of upstream process $i$ to the standard input of downstream process $i+1$ using anonymous byte streams.
- **The EOF Problem & Deadlock Avoidance:** If the parent shell retains open write handles to a pipe, downstream reading processes never receive End-Of-File (EOF) and hang indefinitely waiting for input. ArsiShell explicitly closes its write handles immediately after launching each child stage.
- **Inheritance Control:** Pipe read/write handles are selectively marked inheritable via `SetHandleInformation()` only for the specific child process using them, preventing handle leakage into unrelated child processes.

### 3. I/O Redirection & Handle Duplication
- Uses Win32 `STARTF_USESTDHANDLES` to inject custom `HANDLE` values into `si.hStdInput` and `si.hStdOutput`.
- On POSIX, uses `dup2(fd, STDIN_FILENO)` and `dup2(fd, STDOUT_FILENO)` in the child process before `execvp()`.
- File creation flags strictly distinguish between truncation (`CREATE_ALWAYS` / `O_TRUNC`) for `>` and appending (`OPEN_ALWAYS` / `O_APPEND`) for `>>`.

### 4. Asynchronous Background Execution & Zombie Prevention
- Appending `&` causes the shell to launch child processes without blocking on foreground wait.
- **Non-blocking Reaping:** Before presenting each prompt, `check_background_jobs()` polls active jobs using non-blocking primitives (`WaitForSingleObject(hProcess, 0)` on Windows; `waitpid(pid, &status, WNOHANG)` on POSIX). Completed processes are reaped promptly, preventing resource leaks and zombie processes.

### 5. Job Control & Job Table Abstraction
- The shell maintains an internal job table mapping human-readable, deterministic sequential IDs (`[1]`, `[2]`, `[3]`) to underlying OS PIDs.
- A multi-stage background pipeline (e.g. `cmd1 | cmd2 &`) is represented as **one cohesive job**. Foregrounding (`fg`) waits on all constituent processes; termination (`kill`) signals all running processes in the pipeline.

## Windows vs. POSIX Implementation Details

| Mechanism | Windows Implementation | POSIX Implementation |
| :--- | :--- | :--- |
| **Process Spawning** | `CreateProcessA()` | `fork()` + `execvp()` |
| **Anonymous Pipes** | `CreatePipe()` | `pipe()` |
| **Handle Inheritance** | `SetHandleInformation(h, HANDLE_FLAG_INHERIT, ...)` | Handled implicitly during `fork()` |
| **Standard Stream Redirection** | `STARTUPINFOA.hStdInput / hStdOutput` | `dup2(fd, STDIN_FILENO / STDOUT_FILENO)` |
| **Non-blocking Polling** | `WaitForSingleObject(hProcess, 0) == WAIT_OBJECT_0` | `waitpid(pid, &status, WNOHANG)` |
| **Foreground Waiting** | `WaitForSingleObject(hProcess, INFINITE)` | `waitpid(pid, &status, 0)` |
| **Process Termination** | `TerminateProcess(hProcess, 1)` | `kill(pid, SIGTERM)` |
| **Job Suspension / Resume** | Not natively supported for console jobs | `kill(pid, SIGSTOP)` / `kill(pid, SIGCONT)` |
| **Handle Cleanup** | `CloseHandle()` on process and thread handles | `close()` on descriptors; kernel reaps on `waitpid()` |

> [!NOTE]
> **POSIX Runtime Validation Status:**
> POSIX implementation is included but runtime validation was not performed because a Linux execution environment was unavailable during development.

## Building

### Prerequisites
- C++17 compatible compiler (MinGW-w64 GCC 9+, Clang 10+, or MSVC 2019+)
- CMake 3.15+ (optional, for CMake builds)
- Ninja (optional)

### Build Option 1: Direct GCC Compilation
```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic src/main.cpp src/shell.cpp -o ArsiShell.exe
```

### Build Option 2: CMake with Ninja
```bash
cmake -S . -B build -G Ninja
cmake --build build
```

## Running Tests

An automated regression test suite and Win32 kernel handle stress test are included in `tests/`:

```bash
# Run comprehensive functional regression tests (40 test assertions)
python tests/run_tests.py

# Run Win32 kernel handle leak verification (measures handles before and after 325 operations)
python tests/verify_handles.py
```

## Current Limitations

- **No Command Chaining:** Logical conditional operators (`&&`, `||`) and command separators (`;`) are not implemented.
- **No Signal-Based Job Suspension on Windows:** Win32 does not provide POSIX `SIGSTOP`/`SIGCONT` semantics. `bg` on Windows reports whether a job is already executing in the background.
- **No Interactive Line Editing:** Tab auto-completion, interactive arrow-key history navigation (readline/ncurses), and environment variable expansion (`$VAR`) are omitted to keep the focus on OS process and IPC primitives.

## Learning Outcomes

1. **Kernel Object Lifecycles:** Gained hands-on experience tracking process and thread handles, ensuring every opened handle is closed exactly once across success and error paths.
2. **IPC Mechanics:** Mastered anonymous pipe creation, inheritance flags, and EOF propagation to avoid deadlock conditions.
3. **Cross-Platform Systems Programming:** Learned how process creation paradigms fundamentally differ between Windows (`CreateProcess`) and UNIX (`fork`/`exec`), and how to write clean, portable C++ code without third-party dependencies.
4. **Defensive Systems Design:** Learned how to safely validate user input, guard against malformed pipes, and restrict process termination exclusively to tracked jobs.

## License

This project is licensed under the [MIT License](LICENSE).
