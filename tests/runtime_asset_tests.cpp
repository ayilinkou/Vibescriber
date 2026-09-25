#include "app_paths.hpp"
#include "runtime_asset.hpp"
#include "sortformer_runtime.hpp"

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
    // std::filesystem interprets absolute paths using the build host's syntax.
    const auto test_root = std::filesystem::temp_directory_path()
                           / "vibescriber-data-directory-tests";
    const auto xdg_data_home = test_root / "xdg";
    const auto home = test_root / "home";

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
                .xdg_data_home = xdg_data_home,
                .home = home,
            })
            == xdg_data_home / "vibescriber",
        "Linux prefers an absolute XDG_DATA_HOME");

    suite.expect(
        vibescriber::resolve_application_data_directory(
            vibescriber::OperatingSystem::linux_host,
            {
                .local_app_data = std::nullopt,
                .xdg_data_home = "relative",
                .home = home,
            })
            == home / ".local" / "share" / "vibescriber",
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

    const vibescriber::RuntimeAsset& medium =
        vibescriber::medium_transcription_model_asset();
    suite.expect(medium.url.find("5359861c739e955e79d9a303bcbc70fb988958b1")
                     != std::string::npos,
                 "the medium model URL is pinned to an immutable revision");
    suite.expect(medium.sha256
                     == "cc37e93478338ec7700281a7ac30a10128929eb8f427dda2e865faa8f6da4356",
                 "the medium model manifest contains its published SHA-256");
    suite.expect(medium.expected_bytes == 1'533'774'781U,
                 "the medium model manifest contains its published size");
    suite.expect(
        vibescriber::runtime_asset_path("/application-data", medium)
            == std::filesystem::path("/application-data/models/ggml-medium.en.bin"),
        "the medium model has a separate cached filename");
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

void test_sortformer_runtime_selection(TestSuite& suite)
{
    TemporaryDirectory temporary_directory;
    const auto& data_directory = temporary_directory.path();
#ifdef _WIN32
    const auto cpu_library = data_directory / "tools"
        / "nemo-speech-0.1.0-windows-x86_64-cpu.zip"
        / "bin/nemo_speech_asr_c.dll";
    const auto vulkan_library = data_directory / "tools"
        / "nemo-speech-0.1.0-windows-x86_64-vulkan.zip"
        / "bin/nemo_speech_asr_c.dll";
#else
    const auto cpu_library = data_directory / "tools"
        / "nemo-speech-0.1.0-linux-x86_64-cpu.tar.gz"
        / "lib/libnemo_speech_asr_c.so.1";
    const auto vulkan_library = data_directory / "tools"
        / "nemo-speech-0.1.0-linux-x86_64-vulkan.tar.gz"
        / "lib/libnemo_speech_asr_c.so.1";
#endif
    std::filesystem::create_directories(cpu_library.parent_path());
    std::filesystem::create_directories(vulkan_library.parent_path());
    write_text(cpu_library, "cpu");
    write_text(vulkan_library, "vulkan");
    suite.expect(vibescriber::ensure_sortformer_library(data_directory, false)
                     == cpu_library,
                 "CPU diarization uses the CPU runtime archive");
    suite.expect(vibescriber::ensure_sortformer_library(data_directory, true)
                     == vulkan_library,
                 "Vulkan diarization uses the Vulkan runtime archive");
}

} // namespace

int main()
{
    TestSuite suite;
    test_data_directories(suite);
    test_model_manifest(suite);
    test_runtime_asset_acquisition(suite);
    test_sortformer_runtime_selection(suite);

    if (suite.failures() != 0) {
        std::cerr << suite.failures() << " runtime asset test assertion(s) failed\n";
        return 1;
    }

    std::cout << "All runtime asset tests passed\n";
    return 0;
}
