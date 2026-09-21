#include "process.hpp"

#include <cerrno>
#include <cstring>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace vibescriber {
namespace {

#ifdef _WIN32
[[nodiscard]] std::wstring quote_windows_argument(const std::wstring& argument)
{
    std::wstring result;
    result.push_back(L'"');
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            result.append(backslashes * 2U + 1U, L'\\');
            result.push_back(L'"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(character);
    }
    result.append(backslashes * 2U, L'\\');
    result.push_back(L'"');
    return result;
}

class Handle
{
public:
    explicit Handle(HANDLE value)
        : value_(value)
    {
    }

    ~Handle()
    {
        if (value_ != nullptr) {
            CloseHandle(value_);
        }
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

private:
    HANDLE value_;
};
#endif

} // namespace

int run_process(
    const std::filesystem::path& executable,
    const std::span<const std::filesystem::path> arguments)
{
    if (executable.empty()) {
        throw ProcessError("the process executable path may not be empty");
    }

#ifdef _WIN32
    std::wstring command_line = quote_windows_argument(executable.native());
    for (const auto& argument : arguments) {
        command_line.push_back(L' ');
        command_line += quote_windows_argument(argument.native());
    }

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};
    const BOOL created = CreateProcessW(
        executable.c_str(),
        command_line.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &startup_info,
        &process_info);
    if (created == FALSE) {
        throw ProcessError("failed to start the process (Windows error "
                           + std::to_string(GetLastError()) + ")");
    }

    Handle process(process_info.hProcess);
    Handle thread(process_info.hThread);
    if (WaitForSingleObject(process_info.hProcess, INFINITE) != WAIT_OBJECT_0) {
        throw ProcessError("failed while waiting for the process (Windows error "
                           + std::to_string(GetLastError()) + ")");
    }

    DWORD exit_code = 0;
    if (GetExitCodeProcess(process_info.hProcess, &exit_code) == FALSE) {
        throw ProcessError("failed to read the process exit code (Windows error "
                           + std::to_string(GetLastError()) + ")");
    }
    return static_cast<int>(exit_code);
#else
    std::vector<std::string> storage;
    storage.reserve(arguments.size() + 1U);
    storage.push_back(executable.native());
    for (const auto& argument : arguments) {
        storage.push_back(argument.native());
    }

    std::vector<char*> argv;
    argv.reserve(storage.size() + 1U);
    for (auto& value : storage) {
        argv.push_back(value.data());
    }
    argv.push_back(nullptr);

    const pid_t child = fork();
    if (child < 0) {
        throw ProcessError(std::string("failed to fork the process: ")
                           + std::strerror(errno));
    }
    if (child == 0) {
        execv(executable.c_str(), argv.data());
        _exit(127);
    }

    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            throw ProcessError(std::string("failed while waiting for the process: ")
                               + std::strerror(errno));
        }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        throw ProcessError("the process was terminated by signal "
                           + std::to_string(WTERMSIG(status)));
    }
    throw ProcessError("the process ended in an unknown state");
#endif
}

} // namespace vibescriber
