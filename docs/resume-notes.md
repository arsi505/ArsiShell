# ArsiShell ¡ª R¨¦sum¨¦ & Portfolio Guide

This guide provides polished, accurate r¨¦sum¨¦ entries, technical bullet points, and an elevator pitch for Master's admissions applications and technical pair-programming interviews.

---

## 1. Project Title & Overview

**Title:**  
**ArsiShell ¡ª Cross-Platform Mini Shell & Process Manager**

**One-Line Description:**  
*A lightweight command-line shell and job control system built in C++17 demonstrating process lifecycles, anonymous pipe IPC, stream redirection, and leak-free Win32/POSIX resource management.*

---

## 2. R¨¦sum¨¦ Bullet Points (Choose 3 for CV)

* **Bullet 1 (Core Systems & Process Architecture):**  
  *Architected a cross-platform command-line shell in C++17 implementing custom lexical parsing, standard I/O redirection (`<`, `>`, `>>`), and in-process execution for state-altering commands like `cd` and `history`.*

* **Bullet 2 (IPC & Pipeline Synchronization):**  
  *Engineered an inter-process communication (IPC) engine supporting multi-stage pipelines (`|`) using Windows anonymous pipes and POSIX streams, eliminating reader deadlocks by enforcing strict handle inheritance and immediate parent write-handle closure.*

* **Bullet 3 (Job Control & Resource Safety):**  
  *Implemented an asynchronous background process manager with job control (`jobs`, `kill`, `fg`, `bg`), decoupling shell job IDs from OS PIDs, and validated zero kernel handle leaks across 300+ stress operations via RAII guards and Win32 process APIs.*

---

## 3. The 30¨C45 Second Interview Elevator Pitch

When a professor or interviewer asks:  
**"Tell me about this project."**

> *"I built **ArsiShell** to bridge textbook Operating Systems theory with hands-on systems programming in C++. It's a cross-platform mini shell and process manager that handles everything from lexical parsing and quote-aware tokenization to low-level process creation using both Windows `CreateProcess` and POSIX `fork/exec` architectures.*
> 
> *A major challenge I tackled was preventing pipe deadlocks in multi-stage pipelines by managing kernel handle inheritance and ensuring EOF is properly propagated to downstream readers. I also implemented an asynchronous job-control system with background execution, non-blocking process reaping, and verified zero handle leaks across hundreds of stress cycles.*
> 
> *Building it taught me how kernels actually isolate processes, manage file descriptor tables, and why resource lifetimes require strict RAII discipline."*

---

## 4. Key Technical Keywords for Systems Profiling

- **Languages & Standards:** C++17, Modern C++ (RAII, smart cleanup guards, STL algorithms).
- **Operating Systems Concepts:** Process Control Blocks (PCB), Process Synchronization, Inter-Process Communication (IPC), Anonymous Pipes, File Descriptors, Kernel Object Handles, Standard Stream Redirection, Deadlock Prevention, Asynchronous Reaping, Zombie Processes.
- **APIs & Kernel Primitives:**
  - *Windows:* `CreateProcessA`, `CreatePipe`, `SetHandleInformation`, `WaitForSingleObject`, `TerminateProcess`, `GetProcessHandleCount`, `CloseHandle`.
  - *POSIX:* `fork`, `execvp`, `pipe`, `dup2`, `waitpid` (`WNOHANG`), `kill` (`SIGTERM`, `SIGCONT`).
- **Tooling & Build Systems:** CMake, Ninja, GCC / MinGW-w64, Python automated testing.
