#include "process.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char* argv[])
{
    if (argc != 2) return 2;
    const auto output = std::filesystem::temp_directory_path()
        / ("vibescriber-process-" + std::to_string(
               std::chrono::steady_clock::now().time_since_epoch().count()) + ".txt");
    std::string captured;
    const std::vector<std::filesystem::path> arguments{
        "--capture", "argument with spaces; and punctuation", output};
    const int result = vibescriber::run_process_capture(argv[1], arguments,
        [&](const std::string_view chunk) { captured.append(chunk); });
    // Windows text streams turn the fixture's '\n' into "\r\n". Keep the
    // standalone '\r' used for progress updates while normalizing line endings.
    std::string normalized;
    normalized.reserve(captured.size());
    for (std::size_t index = 0; index < captured.size(); ++index) {
        if (captured[index] != '\r' || index + 1 == captured.size()
            || captured[index + 1] != '\n') {
            normalized.push_back(captured[index]);
        }
    }
    std::ifstream input(output);
    const std::string recorded{std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>()};
    std::error_code error;
    std::filesystem::remove(output, error);
    const bool progress_found = normalized.find("progress\r42%\n") != std::string::npos;
    const bool warning_found = normalized.find("warning\n") != std::string::npos;
    const bool argument_found =
        recorded.find("argument with spaces; and punctuation\n") != std::string::npos;
    if (result != 0 || !progress_found || !warning_found || !argument_found) {
        std::cerr << "process capture failed: exit=" << result
                  << " progress=" << progress_found
                  << " warning=" << warning_found
                  << " argument=" << argument_found << '\n';
        return 1;
    }
    const std::atomic_bool keep_running{false};
    const int uncancelled_result = vibescriber::run_process_capture(
        argv[1], arguments, [](std::string_view) {}, &keep_running);
    if (uncancelled_result != 0) {
        std::cerr << "cancellable process failed without a cancellation request: "
                  << uncancelled_result << '\n';
        return 1;
    }
    std::atomic_bool cancel_requested{false};
    std::jthread cancel_after_start([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        cancel_requested.store(true);
    });
    const auto started = std::chrono::steady_clock::now();
    const int cancelled_result = vibescriber::run_process_capture(
        argv[1], std::vector<std::filesystem::path>{"--wait", output},
        [](std::string_view) {}, &cancel_requested);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    std::filesystem::remove(output, error);
    if (cancelled_result == 0 || elapsed > std::chrono::seconds(3)) {
        std::cerr << "process cancellation failed: exit=" << cancelled_result
                  << " elapsed_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
                  << '\n';
        return 1;
    }
    return 0;
}
