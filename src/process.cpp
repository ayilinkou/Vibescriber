#include "process.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <exception>
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
        reset();
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    [[nodiscard]] HANDLE get() const { return value_; }
    void reset()
    {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
        value_ = nullptr;
    }

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

int run_process_capture(
    const std::filesystem::path& executable,
    const std::span<const std::filesystem::path> arguments,
    const std::function<void(std::string_view)>& on_output)
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

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE read_raw = nullptr;
    HANDLE write_raw = nullptr;
    if (!CreatePipe(&read_raw, &write_raw, &security, 0)) {
        throw ProcessError("failed to create a process output pipe");
    }
    Handle read_handle(read_raw);
    Handle write_handle(write_raw);
    if (!SetHandleInformation(read_handle.get(), HANDLE_FLAG_INHERIT, 0)) {
        throw ProcessError("failed to configure the process output pipe");
    }
    Handle input_handle(CreateFileW(L"NUL", GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr));
    if (input_handle.get() == INVALID_HANDLE_VALUE) {
        throw ProcessError("failed to open the process input stream");
    }

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdInput = input_handle.get();
    startup_info.hStdOutput = write_handle.get();
    startup_info.hStdError = write_handle.get();
    PROCESS_INFORMATION process_info{};
    if (!CreateProcessW(executable.c_str(), command_line.data(), nullptr, nullptr,
                        TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &startup_info, &process_info)) {
        throw ProcessError("failed to start the process (Windows error "
                           + std::to_string(GetLastError()) + ")");
    }
    Handle process(process_info.hProcess);
    Handle thread(process_info.hThread);
    write_handle.reset();
    input_handle.reset();

    std::array<char, 4096> buffer{};
    std::exception_ptr callback_error;
    DWORD read_count = 0;
    for (;;) {
        if (!ReadFile(read_handle.get(), buffer.data(),
                      static_cast<DWORD>(buffer.size()), &read_count, nullptr)) {
            if (GetLastError() != ERROR_BROKEN_PIPE) {
                throw ProcessError("failed to read process output");
            }
            break;
        }
        if (read_count == 0) break;
        if (!callback_error && on_output) {
            try {
                on_output(std::string_view(buffer.data(), read_count));
            } catch (...) {
                callback_error = std::current_exception();
            }
        }
    }
    if (WaitForSingleObject(process.get(), INFINITE) != WAIT_OBJECT_0) {
        throw ProcessError("failed while waiting for the process");
    }
    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process.get(), &exit_code)) {
        throw ProcessError("failed to read the process exit code");
    }
    if (callback_error) std::rethrow_exception(callback_error);
    return static_cast<int>(exit_code);
#else
    std::vector<std::string> storage;
    storage.reserve(arguments.size() + 1U);
    storage.push_back(executable.native());
    for (const auto& argument : arguments) storage.push_back(argument.native());
    std::vector<char*> argv;
    argv.reserve(storage.size() + 1U);
    for (auto& value : storage) argv.push_back(value.data());
    argv.push_back(nullptr);

    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        throw ProcessError(std::string("failed to create a process output pipe: ")
                           + std::strerror(errno));
    }
    const pid_t child = fork();
    if (child < 0) {
        const int saved_errno = errno;
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        throw ProcessError(std::string("failed to fork the process: ")
                           + std::strerror(saved_errno));
    }
    if (child == 0) {
        close(pipe_fds[0]);
        if (dup2(pipe_fds[1], STDOUT_FILENO) < 0
            || dup2(pipe_fds[1], STDERR_FILENO) < 0) _exit(127);
        close(pipe_fds[1]);
        execv(executable.c_str(), argv.data());
        _exit(127);
    }
    close(pipe_fds[1]);
    std::array<char, 4096> buffer{};
    std::exception_ptr callback_error;
    int read_error = 0;
    for (;;) {
        const ssize_t count = read(pipe_fds[0], buffer.data(), buffer.size());
        if (count == 0) break;
        if (count < 0) {
            if (errno == EINTR) continue;
            read_error = errno;
            break;
        }
        if (!callback_error && on_output) {
            try {
                on_output(std::string_view(buffer.data(), static_cast<std::size_t>(count)));
            } catch (...) {
                callback_error = std::current_exception();
            }
        }
    }
    close(pipe_fds[0]);
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            throw ProcessError(std::string("failed while waiting for the process: ")
                               + std::strerror(errno));
        }
    }
    if (read_error != 0) {
        throw ProcessError(std::string("failed to read process output: ")
                           + std::strerror(read_error));
    }
    if (callback_error) std::rethrow_exception(callback_error);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) {
        throw ProcessError("the process was terminated by signal "
                           + std::to_string(WTERMSIG(status)));
    }
    throw ProcessError("the process ended in an unknown state");
#endif
}

} // namespace vibescriber
