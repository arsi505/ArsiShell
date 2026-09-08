#ifndef ARSI_SHELL_H
#define ARSI_SHELL_H

#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#endif

// Maximum number of entries retained in command history
constexpr size_t MAX_HISTORY_SIZE = 1000;

// Job lifecycle state
enum class JobStatus {
    Running,
    Done,
    Terminated,
    Stopped
};

// Represents an active or completed background job
struct Job {
    int job_id = 0;                        // ArsiShell sequential job ID (1, 2, 3...)
    std::vector<int> pids;                 // OS process IDs
    std::string command_line;              // Command line string
    JobStatus status = JobStatus::Running; // Current state
    bool is_pipeline = false;              // Multi-stage pipeline flag

#ifdef _WIN32
    std::vector<HANDLE> process_handles;   // Process handles for reaping
#endif
};

// Stores redirection configuration for a command
struct RedirectionInfo {
    std::string input_file;     // Target file for '<'
    std::string output_file;    // Target file for '>' or '>>'
    bool append_output = false; // true for '>>', false for '>'
    bool has_input = false;
    bool has_output = false;
};

// Represents a single command stage within a pipeline
struct PipelineStage {
    std::vector<std::string> raw_tokens; // Tokens before redirection extraction
    std::vector<std::string> args;       // Command name and clean arguments
    RedirectionInfo redir;               // Redirection for this stage
};

// Tokenizes an input line into command, argument, redirection, and pipe/background operator tokens.
bool tokenize(const std::string& line, std::vector<std::string>& tokens, std::string& error);

// Checks if the command should execute in the background (&).
bool parse_background(std::vector<std::string>& tokens, bool& is_background, std::string& error);

// Expands history operators (!! and !n).
bool expand_history(const std::string& line, std::string& expanded_line, std::string& error);

// Adds a command string to in-memory history and appends to persistent file.
void add_to_history(const std::string& command);

// Loads persistent command history from the user's home directory.
void load_history();

// Saves the entire in-memory history to persistent storage on shell exit.
void save_history();

// Splits tokens into pipeline stages and validates redirection rules.
bool parse_pipeline(
    const std::vector<std::string>& tokens,
    std::vector<PipelineStage>& stages,
    std::string& error
);

// Separates redirection operators and filenames from command arguments.
bool parse_redirection(
    const std::vector<std::string>& tokens,
    std::vector<std::string>& clean_args,
    RedirectionInfo& redir,
    std::string& error
);

// Executes built-in commands (cd, pwd, echo, history, clear, help, jobs, kill, fg, bg, exit).
bool execute_builtin(
    const std::vector<std::string>& tokens,
    bool& should_exit,
    int& exit_code,
    const RedirectionInfo& redir
);

// Executes an external program in a separate child process with redirection.
int execute_external(
    const std::vector<std::string>& tokens,
    const RedirectionInfo& redir,
    bool is_background,
    const std::string& raw_cmd_line
);

// Executes a multi-stage pipeline of commands connected via pipes.
int execute_pipeline(
    const std::vector<PipelineStage>& stages,
    bool is_background,
    bool& should_exit,
    const std::string& raw_cmd_line
);

// Checks for completed background jobs and reaps their resources.
void check_background_jobs();

// Built-in job management commands
void builtin_jobs();
bool builtin_kill(const std::vector<std::string>& args);
bool builtin_fg(const std::vector<std::string>& args);
bool builtin_bg(const std::vector<std::string>& args);

// Main shell Read-Eval-Print Loop (REPL).
void run_shell();

#endif // ARSI_SHELL_H
