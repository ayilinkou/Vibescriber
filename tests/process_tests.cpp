#include "process.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
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
    std::ifstream input(output);
    const std::string recorded{std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>()};
    std::error_code error;
    std::filesystem::remove(output, error);
    if (result != 0 || captured.find("progress\r42%\n") == std::string::npos
        || captured.find("warning\n") == std::string::npos
        || recorded.find("argument with spaces; and punctuation\n") == std::string::npos) {
        std::cerr << "process output capture or argument handling failed\n";
        return 1;
    }
    return 0;
}
