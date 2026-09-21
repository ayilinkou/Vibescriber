#include "cli.hpp"
#include "output_path.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

namespace {

class TestSuite
{
public:
    void expect(const bool condition, const std::string_view description)
    {
        if (!condition) {
            ++failures_;
            std::cerr << "FAIL: " << description << '\n';
        }
    }

    template <typename Function>
    void expect_cli_error(Function&& function, const std::string_view description)
    {
        try {
            function();
            expect(false, description);
        } catch (const vibescriber::CliError&) {
        }
    }

    [[nodiscard]] int failures() const
    {
        return failures_;
    }

private:
    int failures_ = 0;
};

vibescriber::CliParseResult parse(
    const std::initializer_list<std::string_view> arguments)
{
    return vibescriber::parse_cli({arguments.begin(), arguments.size()});
}

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        const auto unique_value = std::chrono::steady_clock::now()
                                      .time_since_epoch()
                                      .count();
        path_ = std::filesystem::temp_directory_path()
                / ("vibescriber-tests-" + std::to_string(unique_value));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void create_empty_file(const std::filesystem::path& path)
{
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("failed to create test file");
    }
}

void test_cli_parsing(TestSuite& suite)
{
    const auto basic = parse({"recording.mp4"});
    suite.expect(basic.action == vibescriber::CliAction::run,
                 "a normal invocation selects the run action");
    suite.expect(basic.options.input_file == "recording.mp4",
                 "the positional argument is the input file");

    const auto configured = parse(
        {"--timestamps", "--force", "--output", "notes.txt", "recording.mp4"});
    suite.expect(configured.options.timestamps, "--timestamps is parsed");
    suite.expect(configured.options.force, "--force is parsed");
    suite.expect(configured.options.output_file == "notes.txt", "--output is parsed");

    suite.expect(parse({"--help"}).action == vibescriber::CliAction::show_help,
                 "--help does not require an input file");
    suite.expect(parse({"--version"}).action == vibescriber::CliAction::show_version,
                 "--version does not require an input file");
    suite.expect(parse({"--", "-recording.mp4"}).options.input_file == "-recording.mp4",
                 "-- permits an input path beginning with a dash");

    suite.expect_cli_error([] { (void)parse({}); },
                           "a missing input file is rejected");
    suite.expect_cli_error([] { (void)parse({"--unknown", "recording.mp4"}); },
                           "an unknown option is rejected");
    suite.expect_cli_error([] { (void)parse({"--output"}); },
                           "a missing output path is rejected");
    suite.expect_cli_error([] { (void)parse({"one.mp4", "two.mp4"}); },
                           "multiple input files are rejected");
    suite.expect_cli_error(
        [] { (void)parse({"-o", "one.txt", "-o", "two.txt", "recording.mp4"}); },
        "multiple output options are rejected");
}

void test_output_paths(TestSuite& suite)
{
    suite.expect(
        vibescriber::default_output_path("somewhere/interview.mp4")
            == std::filesystem::path("somewhere/interview.txt"),
        "the default output replaces the input extension");

    TemporaryDirectory temporary_directory;
    const std::filesystem::path requested = temporary_directory.path() / "interview.txt";

    suite.expect(vibescriber::available_output_path(requested, false) == requested,
                 "an unused output path is preserved");

    create_empty_file(requested);
    create_empty_file(temporary_directory.path() / "interview_1.txt");

    suite.expect(
        vibescriber::available_output_path(requested, false)
            == temporary_directory.path() / "interview_2.txt",
        "numbering begins at one and skips existing outputs");
    suite.expect(vibescriber::available_output_path(requested, true) == requested,
                 "force preserves the requested output path");
}

} // namespace

int main()
{
    TestSuite suite;
    test_cli_parsing(suite);
    test_output_paths(suite);

    if (suite.failures() != 0) {
        std::cerr << suite.failures() << " test assertion(s) failed\n";
        return 1;
    }

    std::cout << "All CLI tests passed\n";
    return 0;
}
