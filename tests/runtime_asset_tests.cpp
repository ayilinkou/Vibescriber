#include "app_paths.hpp"
#include "runtime_asset.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace {

constexpr std::string_view abc_sha256 =
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

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

    template <typename Exception, typename Function>
    void expect_error(Function&& function, const std::string_view description)
    {
        try {
            function();
            expect(false, description);
        } catch (const Exception&) {
        }
    }

    [[nodiscard]] int failures() const
    {
        return failures_;
    }

private:
    int failures_ = 0;
};

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        const auto unique_value = std::chrono::steady_clock::now()
                                      .time_since_epoch()
                                      .count();
        path_ = std::filesystem::temp_directory_path()
                / ("vibescriber-runtime-asset-tests-" + std::to_string(unique_value));
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

void write_text(const std::filesystem::path& path, const std::string_view text)
{
    std::ofstream output(path, std::ios::binary);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        throw std::runtime_error("failed to write a test file");
    }
}

std::string file_url(const std::filesystem::path& path)
{
#ifdef _WIN32
    return "file:///" + path.generic_string();
#else
    return "file://" + path.generic_string();
#endif
}

void test_data_directories(TestSuite& suite)
{
#ifdef _WIN32
    suite.expect(vibescriber::current_operating_system()
                     == vibescriber::OperatingSystem::windows_host,
                 "the current build selects Windows runtime assets");
#else
    suite.expect(vibescriber::current_operating_system()
                     == vibescriber::OperatingSystem::linux_host,
                 "the current build selects Linux runtime assets");
#endif

    suite.expect(
        vibescriber::resolve_application_data_directory(
            vibescriber::OperatingSystem::windows_host,
            {
                .local_app_data = "C:/Users/Test/AppData/Local",
                .xdg_data_home = std::nullopt,
                .home = std::nullopt,
            })
            == std::filesystem::path("C:/Users/Test/AppData/Local/Vibescriber"),
        "Windows data is stored beneath LOCALAPPDATA");

    suite.expect(
        vibescriber::resolve_application_data_directory(
            vibescriber::OperatingSystem::linux_host,
            {
                .local_app_data = std::nullopt,
                .xdg_data_home = "/data/user",
                .home = "/home/test",
            })
            == std::filesystem::path("/data/user/vibescriber"),
        "Linux prefers an absolute XDG_DATA_HOME");

    suite.expect(
        vibescriber::resolve_application_data_directory(
            vibescriber::OperatingSystem::linux_host,
            {
                .local_app_data = std::nullopt,
                .xdg_data_home = "relative",
                .home = "/home/test",
            })
            == std::filesystem::path("/home/test/.local/share/vibescriber"),
        "Linux falls back to HOME for a relative XDG_DATA_HOME");

    suite.expect_error<std::runtime_error>(
        [] {
            (void)vibescriber::resolve_application_data_directory(
                vibescriber::OperatingSystem::windows_host, {});
        },
        "Windows rejects a missing LOCALAPPDATA");
    suite.expect_error<std::runtime_error>(
        [] {
            (void)vibescriber::resolve_application_data_directory(
                vibescriber::OperatingSystem::linux_host, {});
        },
        "Linux rejects missing data-directory environment variables");
}

void test_model_manifest(TestSuite& suite)
{
    const vibescriber::RuntimeAsset& model = vibescriber::transcription_model_asset();
    suite.expect(model.url.find("d44ba793fc67e509623a88a409723311fa677744")
                     != std::string::npos,
                 "the model URL is pinned to an immutable revision");
    suite.expect(model.sha256
                     == "ceac3ec06d1d98ef71aec665283564631055fd6129b79d8e1be4f9cc33cc54b4",
                 "the model manifest contains its published SHA-256");
    suite.expect(model.expected_bytes == 487'614'184U,
                 "the model manifest contains its published size");
    suite.expect(
        vibescriber::runtime_asset_path("/application-data", model)
            == std::filesystem::path(
                "/application-data/models/ggml-small.en-tdrz.bin"),
        "the model is placed below the application data directory");
}

void test_runtime_asset_acquisition(TestSuite& suite)
{
    TemporaryDirectory temporary_directory;
    const auto source = temporary_directory.path() / "source.bin";
    const auto data_directory = temporary_directory.path() / "data";
    write_text(source, "abc");

    const vibescriber::RuntimeAsset fixture{
        .display_name = "test runtime asset",
        .url = file_url(source),
        .sha256 = std::string(abc_sha256),
        .relative_path = "models/test.bin",
        .expected_bytes = 3U,
    };
    const auto result = vibescriber::ensure_runtime_asset(data_directory, fixture);

    suite.expect(result.status == vibescriber::DownloadStatus::downloaded,
                 "a runtime asset is acquired through the verified downloader");
    suite.expect(std::filesystem::is_regular_file(data_directory / "models/test.bin"),
                 "an acquired runtime asset is installed at its manifest path");

    vibescriber::RuntimeAsset unsafe = fixture;
    unsafe.relative_path = "../outside.bin";
    suite.expect_error<std::invalid_argument>(
        [&] { (void)vibescriber::runtime_asset_path(data_directory, unsafe); },
        "a runtime asset path may not escape the data directory");
}

} // namespace

int main()
{
    TestSuite suite;
    test_data_directories(suite);
    test_model_manifest(suite);
    test_runtime_asset_acquisition(suite);

    if (suite.failures() != 0) {
        std::cerr << suite.failures() << " runtime asset test assertion(s) failed\n";
        return 1;
    }

    std::cout << "All runtime asset tests passed\n";
    return 0;
}
