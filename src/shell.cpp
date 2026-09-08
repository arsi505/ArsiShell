#include "shell.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <cstdlib>
#include <cctype>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#endif

// In-memory command history
static std::vector<std::string> g_history;

// Returns path to persistent history file in user's home directory
static std::string get_history_file_path() {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    if (home) {
        std::filesystem::path p(home);
        return (p / ".arsishell_history").string();
    }
    return ".arsishell_history";
}

// Loads persistent history safely on shell startup
void load_history() {
    g_history.clear();
    std::string path = get_history_file_path();
    std::ifstream file(path);
    if (!file.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        std::string trimmed = line.substr(start, end - start + 1);

        if (!trimmed.empty()) {
            if (g_history.size() >= MAX_HISTORY_SIZE) {
                g_history.erase(g_history.begin());
            }
            g_history.push_back(trimmed);
        }
    }
}

// Adds a command to in-memory history and appends to persistent file
void add_to_history(const std::string& command) {
    size_t start = command.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return;
    size_t end = command.find_last_not_of(" \t\r\n");
    std::string trimmed = command.substr(start, end - start + 1);
    if (trimmed.empty()) return;

    if (g_history.size() >= MAX_HISTORY_SIZE) {
        g_history.erase(g_history.begin());
    }
    g_history.push_back(trimmed);

    std::string path = get_history_file_path();
    std::ofstream file(path, std::ios::app);
    if (file.is_open()) {
        file << trimmed << "\n";
    }
}

// Saves full history to persistent storage on exit
void save_history() {
    std::string path = get_history_file_path();
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) return;

    for (const auto& cmd : g_history) {
        file << cmd << "\n";
    }
}

// Expands !! and !n expressions before parsing
bool expand_history(const std::string& line, std::string& expanded_line, std::string& error) {
    expanded_line = line;
    error.clear();

    size_t start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return true;
    }
    size_t end = line.find_last_not_of(" \t\r\n");
    std::string trimmed = line.substr(start, end - start + 1);

    if (trimmed == "!!") {
        if (g_history.empty()) {
            error = "!!: event not found";
            return false;
        }
        expanded_line = g_history.back();
        std::cout << expanded_line << "\n";
        return true;
    }

    if (trimmed.length() > 1 && trimmed[0] == '!' && trimmed[1] != '=') {
        std::string num_str = trimmed.substr(1);
        bool all_digits = true;
        for (char c : num_str) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                all_digits = false;
                break;
            }
        }

        if (!all_digits || num_str.empty()) {
            error = trimmed + ": event not found";
            return false;
        }

        try {
            size_t idx = std::stoul(num_str);
            if (idx >= 1 && idx <= g_history.size()) {
                expanded_line = g_history[idx - 1];
                std::cout << expanded_line << "\n";
                return true;
            } else {
                error = trimmed + ": event not found";
                return false;
            }
        } catch (...) {
            error = trimmed + ": event not found";
            return false;
        }
    }

    return true;
}

// Job table tracking active background jobs
static std::vector<Job> g_jobs;
static int g_next_job_id = 1;

// Helper to parse job identifier argument (%<id> or <id>)
static bool parse_job_id_arg(const std::string& arg, int& job_id) {
    if (arg.empty()) return false;
    std::string s = arg;
    if (s[0] == '%') {
        s = s.substr(1);
    }
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    try {
        job_id = std::stoi(s);
        return job_id > 0;
    } catch (...) {
        return false;
    }
}

// Checks for completed background jobs before prompt, notifies user, and reaps resources
void check_background_jobs() {
    if (g_jobs.empty()) {
        return;
    }

    auto it = g_jobs.begin();
    while (it != g_jobs.end()) {
        bool all_done = true;
#ifdef _WIN32
        for (HANDLE h : it->process_handles) {
            DWORD res = WaitForSingleObject(h, 0);
            if (res != WAIT_OBJECT_0) {
                all_done = false;
                break;
            }
        }
#else
        for (int pid : it->pids) {
            int status = 0;
            pid_t res = waitpid(static_cast<pid_t>(pid), &status, WNOHANG);
            if (res != static_cast<pid_t>(pid)) {
                all_done = false;
                break;
            }
        }
#endif
        if (all_done) {
            std::cout << "[" << it->job_id << "] Done    " << it->command_line << "\n";
#ifdef _WIN32
            for (HANDLE h : it->process_handles) {
                CloseHandle(h);
            }
            it->process_handles.clear();
#endif
            it = g_jobs.erase(it);
        } else {
            ++it;
        }
    }
}

// Built-in 'jobs' command: lists all tracked background jobs
void builtin_jobs() {
    auto it = g_jobs.begin();
    while (it != g_jobs.end()) {
        bool all_done = true;
#ifdef _WIN32
        for (HANDLE h : it->process_handles) {
            DWORD res = WaitForSingleObject(h, 0);
            if (res != WAIT_OBJECT_0) {
                all_done = false;
                break;
            }
        }
#else
        for (int pid : it->pids) {
            int status = 0;
            pid_t res = waitpid(static_cast<pid_t>(pid), &status, WNOHANG);
            if (res != static_cast<pid_t>(pid)) {
                all_done = false;
                break;
            }
        }
#endif
        if (all_done && it->status == JobStatus::Running) {
            it->status = JobStatus::Done;
        }

        std::string status_str = "Running";
        if (it->status == JobStatus::Done) status_str = "Completed";
        else if (it->status == JobStatus::Terminated) status_str = "Terminated";
        else if (it->status == JobStatus::Stopped) status_str = "Stopped";

        std::string pid_str;
        if (it->pids.size() == 1) {
            pid_str = "PID " + std::to_string(it->pids[0]);
        } else {
            pid_str = "PIDs ";
            for (size_t p = 0; p < it->pids.size(); ++p) {
                if (p > 0) pid_str += ", ";
                pid_str += std::to_string(it->pids[p]);
            }
        }

        std::cout << "[" << it->job_id << "] " << status_str << "    " << pid_str << "    " << it->command_line << "\n";

        if (it->status == JobStatus::Done || it->status == JobStatus::Terminated) {
#ifdef _WIN32
            for (HANDLE h : it->process_handles) {
                CloseHandle(h);
            }
            it->process_handles.clear();
#endif
            it = g_jobs.erase(it);
        } else {
            ++it;
        }
    }
}

// Built-in 'kill' command: terminates all processes in a tracked job
bool builtin_kill(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "ArsiShell: kill: missing job id\n";
        return false;
    }
    if (args.size() > 2) {
        std::cerr << "ArsiShell: kill: too many arguments\n";
        return false;
    }

    int job_id = 0;
    if (!parse_job_id_arg(args[1], job_id)) {
        std::cerr << "ArsiShell: kill: " << args[1] << ": invalid job id\n";
        return false;
    }

    auto it = std::find_if(g_jobs.begin(), g_jobs.end(), [job_id](const Job& j) {
        return j.job_id == job_id;
    });

    if (it == g_jobs.end()) {
        std::cerr << "ArsiShell: kill: " << args[1] << ": no such job\n";
        return false;
    }

#ifdef _WIN32
    for (HANDLE h : it->process_handles) {
        TerminateProcess(h, 1);
        CloseHandle(h);
    }
    it->process_handles.clear();
#else
    for (int pid : it->pids) {
        kill(static_cast<pid_t>(pid), SIGTERM);
        waitpid(static_cast<pid_t>(pid), nullptr, 0);
    }
#endif

    std::cout << "[" << it->job_id << "] terminated\n";
    g_jobs.erase(it);
    return true;
}

// Built-in 'fg' command: brings background job to foreground and waits for completion
bool builtin_fg(const std::vector<std::string>& args) {
    if (args.size() > 2) {
        std::cerr << "ArsiShell: fg: too many arguments\n";
        return false;
    }

    int job_id = 0;
    if (args.size() == 1) {
        if (g_jobs.empty()) {
            std::cerr << "ArsiShell: fg: no current job\n";
            return false;
        }
        job_id = g_jobs.back().job_id;
    } else {
        if (!parse_job_id_arg(args[1], job_id)) {
            std::cerr << "ArsiShell: fg: " << args[1] << ": invalid job id\n";
            return false;
        }
    }

    auto it = std::find_if(g_jobs.begin(), g_jobs.end(), [job_id](const Job& j) {
        return j.job_id == job_id;
    });

    if (it == g_jobs.end()) {
        std::string target = (args.size() >= 2) ? args[1] : "%" + std::to_string(job_id);
        std::cerr << "ArsiShell: fg: " << target << ": no such job\n";
        return false;
    }

    std::cout << it->command_line << "\n";

#ifdef _WIN32
    for (HANDLE h : it->process_handles) {
        WaitForSingleObject(h, INFINITE);
        CloseHandle(h);
    }
    it->process_handles.clear();
#else
    for (int pid : it->pids) {
        int status = 0;
        waitpid(static_cast<pid_t>(pid), &status, 0);
    }
#endif

    g_jobs.erase(it);
    return true;
}

// Built-in 'bg' command: platform-aware background resume / status query
bool builtin_bg(const std::vector<std::string>& args) {
    if (args.size() > 2) {
        std::cerr << "ArsiShell: bg: too many arguments\n";
        return false;
    }

    int job_id = 0;
    if (args.size() == 1) {
        if (g_jobs.empty()) {
            std::cerr << "ArsiShell: bg: no current job\n";
            return false;
        }
        job_id = g_jobs.back().job_id;
    } else {
        if (!parse_job_id_arg(args[1], job_id)) {
            std::cerr << "ArsiShell: bg: " << args[1] << ": invalid job id\n";
            return false;
        }
    }

    auto it = std::find_if(g_jobs.begin(), g_jobs.end(), [job_id](const Job& j) {
        return j.job_id == job_id;
    });

    if (it == g_jobs.end()) {
        std::string target = (args.size() >= 2) ? args[1] : "%" + std::to_string(job_id);
        std::cerr << "ArsiShell: bg: " << target << ": no such job\n";
        return false;
    }

#ifdef _WIN32
    if (it->status == JobStatus::Running) {
        std::cout << "ArsiShell: bg: job [" << it->job_id << "] is already running in background\n";
        return true;
    } else {
        std::cout << "ArsiShell: bg: job [" << it->job_id << "] is not in a stopped state\n";
        return true;
    }
#else
    if (it->status == JobStatus::Stopped) {
        for (int pid : it->pids) {
            kill(static_cast<pid_t>(pid), SIGCONT);
        }
        it->status = JobStatus::Running;
        std::cout << "[" << it->job_id << "] " << it->command_line << " &\n";
        return true;
    } else {
        std::cout << "ArsiShell: bg: job [" << it->job_id << "] already running in background\n";
        return true;
    }
#endif
}

// Tokenizes input string into command arguments, redirection operators, pipe, and background operators.
bool tokenize(const std::string& line, std::vector<std::string>& tokens, std::string& error) {
    tokens.clear();
    error.clear();

    std::string current_token;
    bool in_quotes = false;
    char quote_char = '\0';
    bool in_token = false;

    for (size_t i = 0; i < line.length(); ++i) {
        char c = line[i];

        if (in_quotes) {
            if (c == quote_char) {
                in_quotes = false;
                quote_char = '\0';
            } else {
                current_token += c;
            }
        } else {
            if (c == '"' || c == '\'') {
                in_quotes = true;
                quote_char = c;
                in_token = true;
            } else if (c == ' ' || c == '\t') {
                if (in_token) {
                    tokens.push_back(current_token);
                    current_token.clear();
                    in_token = false;
                }
            } else if (c == '>' || c == '<') {
                if (in_token) {
                    tokens.push_back(current_token);
                    current_token.clear();
                    in_token = false;
                }

                if (c == '>' && i + 1 < line.length() && line[i + 1] == '>') {
                    tokens.push_back(">>");
                    i++;
                } else {
                    tokens.push_back(std::string(1, c));
                }
            } else if (c == '|') {
                if (in_token) {
                    tokens.push_back(current_token);
                    current_token.clear();
                    in_token = false;
                }

                if (i + 1 < line.length() && line[i + 1] == '|') {
                    tokens.push_back("||");
                    i++;
                } else {
                    tokens.push_back("|");
                }
            } else if (c == '&') {
                if (in_token) {
                    tokens.push_back(current_token);
                    current_token.clear();
                    in_token = false;
                }

                if (i + 1 < line.length() && line[i + 1] == '&') {
                    tokens.push_back("&&");
                    i++;
                } else {
                    tokens.push_back("&");
                }
            } else {
                current_token += c;
                in_token = true;
            }
        }
    }

    if (in_quotes) {
        error = "syntax error: unclosed quote";
        return false;
    }

    if (in_token) {
        tokens.push_back(current_token);
    }

    return true;
}

// Validates '&' operator usage: only valid as final token of command line
bool parse_background(std::vector<std::string>& tokens, bool& is_background, std::string& error) {
    is_background = false;
    error.clear();

    if (tokens.empty()) {
        return true;
    }

    for (const auto& token : tokens) {
        if (token == "&&") {
            error = "syntax error near unexpected token '&&'";
            return false;
        }
    }

    if (tokens.front() == "&") {
        error = "syntax error near unexpected token '&'";
        return false;
    }

    for (size_t i = 0; i < tokens.size() - 1; ++i) {
        if (tokens[i] == "&") {
            error = "syntax error near unexpected token '&'";
            return false;
        }
    }

    if (tokens.back() == "&") {
        is_background = true;
        tokens.pop_back();

        if (tokens.empty()) {
            error = "syntax error near unexpected token '&'";
            return false;
        }
    }

    return true;
}

// Parses redirection operators (<, >, >>) and isolates clean command arguments.
bool parse_redirection(
    const std::vector<std::string>& tokens,
    std::vector<std::string>& clean_args,
    RedirectionInfo& redir,
    std::string& error
) {
    clean_args.clear();
    redir = RedirectionInfo();
    error.clear();

    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string& token = tokens[i];

        if (token == ">" || token == ">>") {
            if (redir.has_output) {
                error = "syntax error: multiple output redirections";
                return false;
            }
            if (i + 1 >= tokens.size()) {
                error = "syntax error: missing file for output redirection";
                return false;
            }
            const std::string& next_token = tokens[i + 1];
            if (next_token == ">" || next_token == ">>" || next_token == "<" || next_token == "|" || next_token == "||" || next_token == "&" || next_token == "&&") {
                error = "syntax error: unexpected token near redirection operator";
                return false;
            }

            redir.has_output = true;
            redir.append_output = (token == ">>");
            redir.output_file = next_token;
            i++;
        } else if (token == "<") {
            if (redir.has_input) {
                error = "syntax error: multiple input redirections";
                return false;
            }
            if (i + 1 >= tokens.size()) {
                error = "syntax error: missing file for input redirection";
                return false;
            }
            const std::string& next_token = tokens[i + 1];
            if (next_token == ">" || next_token == ">>" || next_token == "<" || next_token == "|" || next_token == "||" || next_token == "&" || next_token == "&&") {
                error = "syntax error: unexpected token near '<'";
                return false;
            }

            redir.has_input = true;
            redir.input_file = next_token;
            i++;
        } else {
            clean_args.push_back(token);
        }
    }

    if (clean_args.empty()) {
        error = "syntax error: missing command";
        return false;
    }

    return true;
}

// Splits tokens by pipe operators into individual pipeline stages and validates syntax
bool parse_pipeline(
    const std::vector<std::string>& tokens,
    std::vector<PipelineStage>& stages,
    std::string& error
) {
    stages.clear();
    error.clear();

    if (tokens.empty()) {
        return true;
    }

    if (tokens.front() == "|" || tokens.front() == "||") {
        error = "syntax error near unexpected token '" + tokens.front() + "'";
        return false;
    }

    if (tokens.back() == "|" || tokens.back() == "||") {
        error = "syntax error near unexpected token '" + tokens.back() + "'";
        return false;
    }

    PipelineStage current_stage;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string& token = tokens[i];

        if (token == "||") {
            error = "syntax error near unexpected token '||'";
            return false;
        }

        if (token == "|") {
            if (current_stage.raw_tokens.empty()) {
                error = "syntax error: empty pipeline stage";
                return false;
            }
            stages.push_back(current_stage);
            current_stage = PipelineStage();
        } else {
            current_stage.raw_tokens.push_back(token);
        }
    }

    if (current_stage.raw_tokens.empty()) {
        error = "syntax error: empty pipeline stage";
        return false;
    }
    stages.push_back(current_stage);

    for (size_t k = 0; k < stages.size(); ++k) {
        std::string redir_err;
        if (!parse_redirection(stages[k].raw_tokens, stages[k].args, stages[k].redir, redir_err)) {
            error = redir_err;
            return false;
        }

        if (stages.size() > 1) {
            if (stages[k].redir.has_input && k > 0) {
                error = "syntax error: input redirection '<' only allowed on first pipeline stage";
                return false;
            }
            if (stages[k].redir.has_output && k < stages.size() - 1) {
                error = "syntax error: output redirection '>' only allowed on last pipeline stage";
                return false;
            }
        }
    }

    return true;
}

#ifdef _WIN32
static std::string quote_windows_arg(const std::string& arg) {
    if (arg.empty()) {
        return "\"\"";
    }
    if (arg.find_first_of(" \t\"") == std::string::npos) {
        return arg;
    }

    std::string result = "\"";
    for (size_t i = 0; i < arg.length(); ++i) {
        if (arg[i] == '"') {
            result += "\\\"";
        } else if (arg[i] == '\\') {
            size_t count = 1;
            while (i + 1 < arg.length() && arg[i + 1] == '\\') {
                count++;
                i++;
            }
            if (i + 1 == arg.length()) {
                result.append(count * 2, '\\');
            } else if (arg[i + 1] == '"') {
                result.append(count * 2, '\\');
                result += "\\\"";
                i++;
            } else {
                result.append(count, '\\');
            }
        } else {
            result += arg[i];
        }
    }
    result += "\"";
    return result;
}

void clear_terminal() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE) return;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        std::cout << "\033[H\033[2J\033[3J" << std::flush;
        return;
    }

    DWORD cell_count = csbi.dwSize.X * csbi.dwSize.Y;
    DWORD count = 0;
    COORD home = {0, 0};
    FillConsoleOutputCharacterA(hConsole, ' ', cell_count, home, &count);
    FillConsoleOutputAttribute(hConsole, csbi.wAttributes, cell_count, home, &count);
    SetConsoleCursorPosition(hConsole, home);
}
#else
void clear_terminal() {
    std::cout << "\033[H\033[2J\033[3J" << std::flush;
}
#endif

// RAII guard for redirecting std::cout of in-process built-in commands.
struct BuiltinRedirGuard {
    std::streambuf* old_cout_buf = nullptr;
    std::ofstream out_file;
    bool active = false;

    BuiltinRedirGuard(const RedirectionInfo& redir, bool& success) {
        success = true;

        if (redir.has_input) {
            std::error_code ec;
            if (!std::filesystem::exists(redir.input_file, ec)) {
                std::cerr << "ArsiShell: " << redir.input_file << ": No such file or directory\n";
                success = false;
                return;
            }
        }

        if (redir.has_output) {
            auto mode = std::ios::out | (redir.append_output ? std::ios::app : std::ios::trunc);
            out_file.open(redir.output_file, mode);
            if (!out_file.is_open()) {
                std::cerr << "ArsiShell: " << redir.output_file << ": Permission denied or cannot open file\n";
                success = false;
                return;
            }
            old_cout_buf = std::cout.rdbuf(out_file.rdbuf());
            active = true;
        }
    }

    ~BuiltinRedirGuard() {
        if (active && old_cout_buf) {
            std::cout.flush();
            std::cout.rdbuf(old_cout_buf);
        }
        if (out_file.is_open()) {
            out_file.close();
        }
    }
};

// Built-in commands execute inside the shell's process.
bool execute_builtin(
    const std::vector<std::string>& tokens,
    bool& should_exit,
    int& exit_code,
    const RedirectionInfo& redir
) {
    if (tokens.empty()) {
        return false;
    }
    const std::string& cmd = tokens[0];

    if (cmd != "exit" && cmd != "pwd" && cmd != "cd" && cmd != "echo" && cmd != "history" && cmd != "clear" && cmd != "help"
        && cmd != "jobs" && cmd != "kill" && cmd != "fg" && cmd != "bg") {
        return false;
    }

    bool redir_ok = true;
    BuiltinRedirGuard redir_guard(redir, redir_ok);
    if (!redir_ok) {
        exit_code = 1;
        return true;
    }

    if (cmd == "exit") {
        should_exit = true;
        exit_code = 0;
        return true;
    }

    if (cmd == "pwd") {
        std::error_code ec;
        auto current_path = std::filesystem::current_path(ec);
        if (ec) {
            std::cerr << "ArsiShell: pwd: " << ec.message() << "\n";
            exit_code = 1;
        } else {
            std::cout << current_path.string() << "\n";
            exit_code = 0;
        }
        return true;
    }

    if (cmd == "cd") {
        if (tokens.size() > 2) {
            std::cerr << "ArsiShell: cd: too many arguments\n";
            exit_code = 1;
            return true;
        }

        std::string target;
        if (tokens.size() < 2) {
#ifdef _WIN32
            const char* home = std::getenv("USERPROFILE");
#else
            const char* home = std::getenv("HOME");
#endif
            if (home) {
                target = home;
            } else {
                std::cerr << "ArsiShell: cd: HOME directory not set\n";
                exit_code = 1;
                return true;
            }
        } else {
            target = tokens[1];
        }

        std::error_code ec;
        if (!std::filesystem::exists(target, ec)) {
            std::cerr << "ArsiShell: cd: " << target << ": No such file or directory\n";
            exit_code = 1;
            return true;
        }
        if (!std::filesystem::is_directory(target, ec)) {
            std::cerr << "ArsiShell: cd: " << target << ": Not a directory\n";
            exit_code = 1;
            return true;
        }

        std::filesystem::current_path(target, ec);
        if (ec) {
            std::cerr << "ArsiShell: cd: " << target << ": " << ec.message() << "\n";
            exit_code = 1;
        } else {
            exit_code = 0;
        }
        return true;
    }

    if (cmd == "echo") {
        for (size_t i = 1; i < tokens.size(); ++i) {
            if (i > 1) {
                std::cout << " ";
            }
            std::cout << tokens[i];
        }
        std::cout << "\n";
        exit_code = 0;
        return true;
    }

    if (cmd == "history") {
        for (size_t i = 0; i < g_history.size(); ++i) {
            std::cout << (i + 1) << "  " << g_history[i] << "\n";
        }
        exit_code = 0;
        return true;
    }

    if (cmd == "jobs") {
        if (tokens.size() > 1) {
            std::cerr << "ArsiShell: jobs: too many arguments\n";
            exit_code = 1;
            return true;
        }
        builtin_jobs();
        exit_code = 0;
        return true;
    }

    if (cmd == "kill") {
        bool ok = builtin_kill(tokens);
        exit_code = ok ? 0 : 1;
        return true;
    }

    if (cmd == "fg") {
        bool ok = builtin_fg(tokens);
        exit_code = ok ? 0 : 1;
        return true;
    }

    if (cmd == "bg") {
        bool ok = builtin_bg(tokens);
        exit_code = ok ? 0 : 1;
        return true;
    }

    if (cmd == "clear") {
        clear_terminal();
        exit_code = 0;
        return true;
    }

    if (cmd == "help") {
        std::cout << "ArsiShell - A Mini Linux Shell / Process Manager\n\n"
                  << "Built-in commands:\n"
                  << "  cd <dir>       Change current directory (defaults to home)\n"
                  << "  pwd            Print current directory\n"
                  << "  echo [...]     Print text\n"
                  << "  history        Show command history\n"
                  << "  !!             Repeat previous command\n"
                  << "  !n             Execute history entry n\n"
                  << "  jobs           List tracked background jobs\n"
                  << "  kill %id       Terminate a background job\n"
                  << "  fg %id         Bring a background job to foreground\n"
                  << "  bg %id         Resume/query background job status\n"
                  << "  clear          Clear terminal\n"
                  << "  help           Show this help\n"
                  << "  exit           Exit ArsiShell\n\n"
                  << "Supported operators:\n"
                  << "  <              Input redirection\n"
                  << "  >              Output redirection\n"
                  << "  >>             Append redirection\n"
                  << "  |              Pipe\n"
                  << "  &              Background execution\n";
        exit_code = 0;
        return true;
    }

    return false;
}

// Spawns an external command in a new child process with input/output redirection.
int execute_external(
    const std::vector<std::string>& tokens,
    const RedirectionInfo& redir,
    bool is_background,
    const std::string& raw_cmd_line
) {
    if (tokens.empty()) {
        return 0;
    }

#ifdef _WIN32
    if (redir.has_input) {
        std::error_code ec;
        if (!std::filesystem::exists(redir.input_file, ec)) {
            std::cerr << "ArsiShell: " << redir.input_file << ": No such file or directory\n";
            return 1;
        }
    }

    SECURITY_ATTRIBUTES sa;
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hIn = INVALID_HANDLE_VALUE;
    HANDLE hOut = INVALID_HANDLE_VALUE;

    struct FileHandleGuard {
        HANDLE& h;
        ~FileHandleGuard() {
            if (h != INVALID_HANDLE_VALUE && h != NULL) {
                CloseHandle(h);
                h = INVALID_HANDLE_VALUE;
            }
        }
    } in_guard{hIn}, out_guard{hOut};

    if (redir.has_input) {
        hIn = CreateFileA(
            redir.input_file.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            &sa,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        if (hIn == INVALID_HANDLE_VALUE) {
            std::cerr << "ArsiShell: " << redir.input_file << ": Cannot open input file\n";
            return 1;
        }
    }

    if (redir.has_output) {
        hOut = CreateFileA(
            redir.output_file.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &sa,
            redir.append_output ? OPEN_ALWAYS : CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        if (hOut == INVALID_HANDLE_VALUE) {
            std::cerr << "ArsiShell: " << redir.output_file << ": Access denied or cannot open file\n";
            return 1;
        }
        if (redir.append_output) {
            SetFilePointer(hOut, 0, NULL, FILE_END);
        }
    }

    std::string command_line;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) command_line += " ";
        command_line += quote_windows_arg(tokens[i]);
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    bool needs_handles = redir.has_input || redir.has_output;
    if (needs_handles) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdInput = (hIn != INVALID_HANDLE_VALUE) ? hIn : GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = (hOut != INVALID_HANDLE_VALUE) ? hOut : GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    }

    BOOL success = CreateProcessA(
        NULL,
        command_line.data(),
        NULL,
        NULL,
        needs_handles ? TRUE : FALSE,
        0,
        NULL,
        NULL,
        &si,
        &pi
    );

    if (hIn != INVALID_HANDLE_VALUE) {
        CloseHandle(hIn);
        hIn = INVALID_HANDLE_VALUE;
    }
    if (hOut != INVALID_HANDLE_VALUE) {
        CloseHandle(hOut);
        hOut = INVALID_HANDLE_VALUE;
    }

    if (!success) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            std::cerr << "ArsiShell: command not found: " << tokens[0] << "\n";
        } else if (err == ERROR_ACCESS_DENIED) {
            std::cerr << "ArsiShell: permission denied: " << tokens[0] << "\n";
        } else {
            LPSTR buffer = nullptr;
            size_t size = FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&buffer, 0, NULL
            );
            if (size > 0 && buffer) {
                std::string msg(buffer, size);
                while (!msg.empty() && (msg.back() == '\r' || msg.back() == '\n')) {
                    msg.pop_back();
                }
                std::cerr << "ArsiShell: " << tokens[0] << ": " << msg << "\n";
                LocalFree(buffer);
            } else {
                std::cerr << "ArsiShell: failed to execute " << tokens[0] << " (error " << err << ")\n";
            }
        }
        return -1;
    }

    CloseHandle(pi.hThread);

    if (!is_background) {
        WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD exit_code = 0;
        if (GetExitCodeProcess(pi.hProcess, &exit_code)) {
            CloseHandle(pi.hProcess);
            return static_cast<int>(exit_code);
        }
        CloseHandle(pi.hProcess);
        return 0;
    } else {
        Job job;
        job.job_id = g_next_job_id++;
        job.pids.push_back(static_cast<int>(pi.dwProcessId));
        job.command_line = raw_cmd_line;
        job.status = JobStatus::Running;
        job.is_pipeline = false;
        job.process_handles.push_back(pi.hProcess);
        g_jobs.push_back(job);

        std::cout << "[" << job.job_id << "] " << pi.dwProcessId << "\n";
        return 0;
    }

#else
    if (redir.has_input) {
        std::error_code ec;
        if (!std::filesystem::exists(redir.input_file, ec)) {
            std::cerr << "ArsiShell: " << redir.input_file << ": No such file or directory\n";
            return 1;
        }
    }

    pid_t pid = fork();

    if (pid < 0) {
        std::cerr << "ArsiShell: fork failed: " << strerror(errno) << "\n";
        return -1;
    }

    if (pid == 0) {
        if (redir.has_input) {
            int fd_in = open(redir.input_file.c_str(), O_RDONLY);
            if (fd_in < 0) {
                std::cerr << "ArsiShell: " << redir.input_file << ": " << strerror(errno) << "\n";
                _exit(1);
            }
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }

        if (redir.has_output) {
            int flags = O_WRONLY | O_CREAT | (redir.append_output ? O_APPEND : O_TRUNC);
            int fd_out = open(redir.output_file.c_str(), flags, 0644);
            if (fd_out < 0) {
                std::cerr << "ArsiShell: " << redir.output_file << ": " << strerror(errno) << "\n";
                _exit(1);
            }
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }

        std::vector<char*> argv;
        for (const auto& token : tokens) {
            argv.push_back(const_cast<char*>(token.c_str()));
        }
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        if (errno == ENOENT) {
            std::cerr << "ArsiShell: command not found: " << tokens[0] << "\n";
        } else if (errno == EACCES) {
            std::cerr << "ArsiShell: permission denied: " << tokens[0] << "\n";
        } else {
            std::cerr << "ArsiShell: " << tokens[0] << ": " << strerror(errno) << "\n";
        }
        _exit(127);
    } else {
        if (!is_background) {
            int status = 0;
            waitpid(pid, &status, 0);
            if (WIFEXITED(status)) {
                return WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                return 128 + WTERMSIG(status);
            }
            return status;
        } else {
            Job job;
            job.job_id = g_next_job_id++;
            job.pids.push_back(static_cast<int>(pid));
            job.command_line = raw_cmd_line;
            job.status = JobStatus::Running;
            job.is_pipeline = false;
            g_jobs.push_back(job);

            std::cout << "[" << job.job_id << "] " << pid << "\n";
            return 0;
        }
    }
#endif
}

// Executes an N-stage pipeline of commands connected via pipes (foreground or background)
int execute_pipeline(
    const std::vector<PipelineStage>& stages,
    bool is_background,
    bool& should_exit,
    const std::string& raw_cmd_line
) {
    size_t num_stages = stages.size();
    if (num_stages == 0) return 0;

    if (num_stages == 1) {
        const std::string& cmd = stages[0].args[0];
        if (cmd == "cd" || cmd == "exit") {
            if (is_background) {
                std::cerr << "ArsiShell: note: '" << cmd << "' in background has no effect on parent shell\n";
                return 0;
            }
        }
        int exit_code = 0;
        if (execute_builtin(stages[0].args, should_exit, exit_code, stages[0].redir)) {
            if (is_background && (cmd == "echo" || cmd == "pwd" || cmd == "history" || cmd == "help" || cmd == "jobs")) {
                std::cout << "[background task finished]\n";
            }
            return exit_code;
        }
        return execute_external(stages[0].args, stages[0].redir, is_background, raw_cmd_line);
    }

#ifdef _WIN32
    struct PipePair {
        HANDLE hRead = INVALID_HANDLE_VALUE;
        HANDLE hWrite = INVALID_HANDLE_VALUE;
    };
    std::vector<PipePair> pipes(num_stages - 1);

    SECURITY_ATTRIBUTES sa;
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    for (size_t k = 0; k < num_stages - 1; ++k) {
        if (!CreatePipe(&pipes[k].hRead, &pipes[k].hWrite, &sa, 0)) {
            std::cerr << "ArsiShell: failed to create pipe\n";
            for (size_t j = 0; j < k; ++j) {
                if (pipes[j].hRead != INVALID_HANDLE_VALUE) CloseHandle(pipes[j].hRead);
                if (pipes[j].hWrite != INVALID_HANDLE_VALUE) CloseHandle(pipes[j].hWrite);
            }
            return 1;
        }
        SetHandleInformation(pipes[k].hRead, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(pipes[k].hWrite, HANDLE_FLAG_INHERIT, 0);
    }

    struct PipeCleanupGuard {
        std::vector<PipePair>& p;
        ~PipeCleanupGuard() {
            for (auto& pair : p) {
                if (pair.hRead != INVALID_HANDLE_VALUE) {
                    CloseHandle(pair.hRead);
                    pair.hRead = INVALID_HANDLE_VALUE;
                }
                if (pair.hWrite != INVALID_HANDLE_VALUE) {
                    CloseHandle(pair.hWrite);
                    pair.hWrite = INVALID_HANDLE_VALUE;
                }
            }
        }
    } pipe_cleanup{pipes};

    std::vector<PROCESS_INFORMATION> child_procs;
    std::vector<DWORD> spawned_pids;
    bool stage_failed = false;

    for (size_t i = 0; i < num_stages; ++i) {
        const auto& stage = stages[i];
        const std::string& cmd = stage.args[0];

        HANDLE hIn = INVALID_HANDLE_VALUE;
        HANDLE hOpenedIn = INVALID_HANDLE_VALUE;
        if (i == 0) {
            if (stage.redir.has_input) {
                std::error_code ec;
                if (!std::filesystem::exists(stage.redir.input_file, ec)) {
                    std::cerr << "ArsiShell: " << stage.redir.input_file << ": No such file or directory\n";
                    return 1;
                }
                hOpenedIn = CreateFileA(
                    stage.redir.input_file.c_str(),
                    GENERIC_READ,
                    FILE_SHARE_READ,
                    &sa,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL
                );
                if (hOpenedIn == INVALID_HANDLE_VALUE) {
                    std::cerr << "ArsiShell: " << stage.redir.input_file << ": Cannot open input file\n";
                    return 1;
                }
                hIn = hOpenedIn;
            } else {
                hIn = GetStdHandle(STD_INPUT_HANDLE);
            }
        } else {
            hIn = pipes[i - 1].hRead;
            SetHandleInformation(hIn, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        }

        HANDLE hOut = INVALID_HANDLE_VALUE;
        HANDLE hOpenedOut = INVALID_HANDLE_VALUE;
        if (i == num_stages - 1) {
            if (stage.redir.has_output) {
                hOpenedOut = CreateFileA(
                    stage.redir.output_file.c_str(),
                    GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    &sa,
                    stage.redir.append_output ? OPEN_ALWAYS : CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL
                );
                if (hOpenedOut == INVALID_HANDLE_VALUE) {
                    std::cerr << "ArsiShell: " << stage.redir.output_file << ": Access denied or cannot open file\n";
                    if (hOpenedIn != INVALID_HANDLE_VALUE) CloseHandle(hOpenedIn);
                    return 1;
                }
                if (stage.redir.append_output) {
                    SetFilePointer(hOpenedOut, 0, NULL, FILE_END);
                }
                hOut = hOpenedOut;
            } else {
                hOut = GetStdHandle(STD_OUTPUT_HANDLE);
            }
        } else {
            hOut = pipes[i].hWrite;
            SetHandleInformation(hOut, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        }

        if (cmd == "echo" || cmd == "pwd" || cmd == "history" || cmd == "help" || cmd == "jobs") {
            std::string output_str;
            if (cmd == "echo") {
                for (size_t a = 1; a < stage.args.size(); ++a) {
                    if (a > 1) output_str += " ";
                    output_str += stage.args[a];
                }
                output_str += "\r\n";
            } else if (cmd == "pwd") {
                output_str = std::filesystem::current_path().string() + "\r\n";
            } else if (cmd == "history") {
                for (size_t h = 0; h < g_history.size(); ++h) {
                    output_str += std::to_string(h + 1) + "  " + g_history[h] + "\r\n";
                }
            } else if (cmd == "help") {
                output_str = "ArsiShell built-in commands: cd, pwd, echo, history, clear, help, jobs, kill, fg, bg, exit\r\n";
            } else if (cmd == "jobs") {
                for (const auto& job : g_jobs) {
                    std::string status_str = "Running";
                    if (job.status == JobStatus::Done) status_str = "Completed";
                    else if (job.status == JobStatus::Terminated) status_str = "Terminated";
                    else if (job.status == JobStatus::Stopped) status_str = "Stopped";

                    std::string pid_str;
                    if (job.pids.size() == 1) {
                        pid_str = "PID " + std::to_string(job.pids[0]);
                    } else {
                        pid_str = "PIDs ";
                        for (size_t p = 0; p < job.pids.size(); ++p) {
                            if (p > 0) pid_str += ", ";
                            pid_str += std::to_string(job.pids[p]);
                        }
                    }
                    output_str += "[" + std::to_string(job.job_id) + "] " + status_str + "    " + pid_str + "    " + job.command_line + "\r\n";
                }
            }

            DWORD bytes_written = 0;
            WriteFile(hOut, output_str.data(), static_cast<DWORD>(output_str.size()), &bytes_written, NULL);

            if (i < num_stages - 1) {
                CloseHandle(pipes[i].hWrite);
                pipes[i].hWrite = INVALID_HANDLE_VALUE;
            }
            if (i > 0 && pipes[i - 1].hRead != INVALID_HANDLE_VALUE) {
                CloseHandle(pipes[i - 1].hRead);
                pipes[i - 1].hRead = INVALID_HANDLE_VALUE;
            }
            if (hOpenedIn != INVALID_HANDLE_VALUE) CloseHandle(hOpenedIn);
            if (hOpenedOut != INVALID_HANDLE_VALUE) CloseHandle(hOpenedOut);
            continue;
        } else if (cmd == "cd" || cmd == "exit" || cmd == "kill" || cmd == "fg" || cmd == "bg") {
            std::cerr << "ArsiShell: note: '" << cmd << "' inside pipeline has no effect on parent shell\n";
            if (i < num_stages - 1) {
                CloseHandle(pipes[i].hWrite);
                pipes[i].hWrite = INVALID_HANDLE_VALUE;
            }
            if (i > 0 && pipes[i - 1].hRead != INVALID_HANDLE_VALUE) {
                CloseHandle(pipes[i - 1].hRead);
                pipes[i - 1].hRead = INVALID_HANDLE_VALUE;
            }
            if (hOpenedIn != INVALID_HANDLE_VALUE) CloseHandle(hOpenedIn);
            if (hOpenedOut != INVALID_HANDLE_VALUE) CloseHandle(hOpenedOut);
            continue;
        }

        std::string command_line;
        for (size_t a = 0; a < stage.args.size(); ++a) {
            if (a > 0) command_line += " ";
            command_line += quote_windows_arg(stage.args[a]);
        }

        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdInput = hIn;
        si.hStdOutput = hOut;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        ZeroMemory(&pi, sizeof(pi));

        BOOL success = CreateProcessA(
            NULL,
            command_line.data(),
            NULL,
            NULL,
            TRUE,
            0,
            NULL,
            NULL,
            &si,
            &pi
        );

        if (i < num_stages - 1) {
            CloseHandle(pipes[i].hWrite);
            pipes[i].hWrite = INVALID_HANDLE_VALUE;
        }
        if (i > 0 && pipes[i - 1].hRead != INVALID_HANDLE_VALUE) {
            CloseHandle(pipes[i - 1].hRead);
            pipes[i - 1].hRead = INVALID_HANDLE_VALUE;
        }
        if (hOpenedIn != INVALID_HANDLE_VALUE) CloseHandle(hOpenedIn);
        if (hOpenedOut != INVALID_HANDLE_VALUE) CloseHandle(hOpenedOut);

        if (!success) {
            stage_failed = true;
            DWORD err = GetLastError();
            if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
                std::cerr << "ArsiShell: command not found: " << stage.args[0] << "\n";
            } else if (err == ERROR_ACCESS_DENIED) {
                std::cerr << "ArsiShell: permission denied: " << stage.args[0] << "\n";
            } else {
                std::cerr << "ArsiShell: failed to execute " << stage.args[0] << " (error " << err << ")\n";
            }
            break;
        } else {
            CloseHandle(pi.hThread);
            child_procs.push_back(pi);
            spawned_pids.push_back(pi.dwProcessId);
        }
    }

    if (stage_failed) {
        for (const auto& pi : child_procs) {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hProcess);
        }
        return 1;
    }

    if (!is_background) {
        int final_exit_code = 0;
        for (const auto& pi : child_procs) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD code = 0;
            GetExitCodeProcess(pi.hProcess, &code);
            final_exit_code = static_cast<int>(code);
            CloseHandle(pi.hProcess);
        }
        return final_exit_code;
    } else {
        if (!spawned_pids.empty()) {
            Job job;
            job.job_id = g_next_job_id++;
            for (DWORD pid : spawned_pids) {
                job.pids.push_back(static_cast<int>(pid));
            }
            job.command_line = raw_cmd_line;
            job.status = JobStatus::Running;
            job.is_pipeline = true;
            for (const auto& pi : child_procs) {
                job.process_handles.push_back(pi.hProcess);
            }
            g_jobs.push_back(job);

            std::cout << "[" << job.job_id << "] PIDs ";
            for (size_t p = 0; p < job.pids.size(); ++p) {
                if (p > 0) std::cout << ", ";
                std::cout << job.pids[p];
            }
            std::cout << "\n";
        }
        return 0;
    }

#else
    std::vector<int[2]> pipefds(num_stages - 1);
    for (size_t k = 0; k < num_stages - 1; ++k) {
        if (pipe(pipefds[k]) < 0) {
            std::cerr << "ArsiShell: pipe creation failed: " << strerror(errno) << "\n";
            return 1;
        }
    }

    std::vector<pid_t> pids;
    bool stage_failed = false;
    for (size_t i = 0; i < num_stages; ++i) {
        const auto& stage = stages[i];

        pid_t pid = fork();
        if (pid < 0) {
            std::cerr << "ArsiShell: fork failed: " << strerror(errno) << "\n";
            stage_failed = true;
            break;
        }

        if (pid == 0) {
            if (i == 0) {
                if (stage.redir.has_input) {
                    int fd_in = open(stage.redir.input_file.c_str(), O_RDONLY);
                    if (fd_in < 0) {
                        std::cerr << "ArsiShell: " << stage.redir.input_file << ": " << strerror(errno) << "\n";
                        _exit(1);
                    }
                    dup2(fd_in, STDIN_FILENO);
                    close(fd_in);
                }
            } else {
                dup2(pipefds[i - 1][0], STDIN_FILENO);
            }

            if (i == num_stages - 1) {
                if (stage.redir.has_output) {
                    int flags = O_WRONLY | O_CREAT | (stage.redir.append_output ? O_APPEND : O_TRUNC);
                    int fd_out = open(stage.redir.output_file.c_str(), flags, 0644);
                    if (fd_out < 0) {
                        std::cerr << "ArsiShell: " << stage.redir.output_file << ": " << strerror(errno) << "\n";
                        _exit(1);
                    }
                    dup2(fd_out, STDOUT_FILENO);
                    close(fd_out);
                }
            } else {
                dup2(pipefds[i][1], STDOUT_FILENO);
            }

            for (size_t k = 0; k < num_stages - 1; ++k) {
                close(pipefds[k][0]);
                close(pipefds[k][1]);
            }

            const std::string& cmd = stage.args[0];
            if (cmd == "echo") {
                for (size_t a = 1; a < stage.args.size(); ++a) {
                    if (a > 1) std::cout << " ";
                    std::cout << stage.args[a];
                }
                std::cout << "\n";
                _exit(0);
            } else if (cmd == "pwd") {
                std::cout << std::filesystem::current_path().string() << "\n";
                _exit(0);
            } else if (cmd == "history") {
                for (size_t h = 0; h < g_history.size(); ++h) {
                    std::cout << (h + 1) << "  " << g_history[h] << "\n";
                }
                _exit(0);
            } else if (cmd == "help") {
                std::cout << "ArsiShell built-in commands: cd, pwd, echo, history, clear, help, jobs, kill, fg, bg, exit\n";
                _exit(0);
            } else if (cmd == "jobs") {
                builtin_jobs();
                _exit(0);
            } else if (cmd == "cd" || cmd == "exit" || cmd == "kill" || cmd == "fg" || cmd == "bg") {
                _exit(0);
            }

            std::vector<char*> argv;
            for (const auto& arg : stage.args) {
                argv.push_back(const_cast<char*>(arg.c_str()));
            }
            argv.push_back(nullptr);

            execvp(argv[0], argv.data());
            std::cerr << "ArsiShell: command not found: " << stage.args[0] << "\n";
            _exit(127);
        } else {
            pids.push_back(pid);
        }
    }

    for (size_t k = 0; k < num_stages - 1; ++k) {
        close(pipefds[k][0]);
        close(pipefds[k][1]);
    }

    if (stage_failed) {
        for (pid_t p : pids) {
            kill(p, SIGTERM);
            waitpid(p, nullptr, 0);
        }
        return 1;
    }

    if (!is_background) {
        int last_status = 0;
        for (pid_t p : pids) {
            int status = 0;
            waitpid(p, &status, 0);
            last_status = status;
        }

        if (WIFEXITED(last_status)) {
            return WEXITSTATUS(last_status);
        }
        return 0;
    } else {
        if (!pids.empty()) {
            Job job;
            job.job_id = g_next_job_id++;
            for (pid_t pid : pids) {
                job.pids.push_back(static_cast<int>(pid));
            }
            job.command_line = raw_cmd_line;
            job.status = JobStatus::Running;
            job.is_pipeline = true;
            g_jobs.push_back(job);

            std::cout << "[" << job.job_id << "] PIDs ";
            for (size_t p = 0; p < job.pids.size(); ++p) {
                if (p > 0) std::cout << ", ";
                std::cout << job.pids[p];
            }
            std::cout << "\n";
        }
        return 0;
    }
#endif
}

// Main shell Read-Eval-Print Loop (REPL)
void run_shell() {
    // Load persistent history from home directory
    load_history();

    std::string line;
    bool should_exit = false;
    int last_exit_code = 0;
    (void)last_exit_code;

    while (!should_exit) {
        // 1. Check for completed background jobs and reap them before displaying prompt
        check_background_jobs();

        // 2. Display Prompt
        std::cout << "ArsiShell> ";
        std::cout.flush();

        // 3. Read command line
        if (!std::getline(std::cin, line)) {
            std::cout << "\n";
            break;
        }

        // 4. Expand history operators (!! and !n)
        std::string expanded_line;
        std::string expand_error;
        if (!expand_history(line, expanded_line, expand_error)) {
            std::cerr << "ArsiShell: " << expand_error << "\n";
            continue;
        }

        // 5. Tokenize input
        std::vector<std::string> raw_tokens;
        std::string parse_error;
        if (!tokenize(expanded_line, raw_tokens, parse_error)) {
            std::cerr << "ArsiShell: " << parse_error << "\n";
            continue;
        }

        // 6. Ignore empty input
        if (raw_tokens.empty()) {
            continue;
        }

        // 7. Add expanded command to history
        add_to_history(expanded_line);

        // 8. Parse background operator (&)
        bool is_background = false;
        std::string bg_error;
        if (!parse_background(raw_tokens, is_background, bg_error)) {
            std::cerr << "ArsiShell: " << bg_error << "\n";
            continue;
        }

        if (raw_tokens.empty()) {
            continue;
        }

        std::string clean_cmd_line = expanded_line;
        if (is_background) {
            size_t ampersand_pos = clean_cmd_line.find_last_of('&');
            if (ampersand_pos != std::string::npos) {
                clean_cmd_line = clean_cmd_line.substr(0, ampersand_pos);
                size_t last_char = clean_cmd_line.find_last_not_of(" \t\r\n");
                if (last_char != std::string::npos) {
                    clean_cmd_line = clean_cmd_line.substr(0, last_char + 1);
                }
            }
        }

        // 9. Parse pipeline stages and redirection
        std::vector<PipelineStage> stages;
        std::string pipe_error;
        if (!parse_pipeline(raw_tokens, stages, pipe_error)) {
            std::cerr << "ArsiShell: " << pipe_error << "\n";
            continue;
        }

        if (stages.empty()) {
            continue;
        }

        // 10. Execute pipeline (foreground or background)
        last_exit_code = execute_pipeline(stages, is_background, should_exit, clean_cmd_line);
    }

    // Save history on shell exit
    save_history();

#ifdef _WIN32
    // Clean up any remaining background process handles when the shell exits
    for (auto& job : g_jobs) {
        for (HANDLE h : job.process_handles) {
            CloseHandle(h);
        }
        job.process_handles.clear();
    }
#endif
    g_jobs.clear();
}
