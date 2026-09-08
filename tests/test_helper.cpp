#include <iostream>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <string>

// Test helper program for ArsiShell automated tests.
// Usage: test_helper [sleep_ms] [exit_code]
int main(int argc, char* argv[]) {
    int sleep_ms = 1000;
    int exit_code = 0;

    if (argc > 1) {
        sleep_ms = std::atoi(argv[1]);
    }
    if (argc > 2) {
        exit_code = std::atoi(argv[2]);
    }

    if (sleep_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }

    return exit_code;
}
