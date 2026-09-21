#include "download.hpp"
#include "sha256.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace {

constexpr std::string_view abc_sha256 =
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        const auto unique_value = std::chrono::steady_clock::now()
                                      .time_since_epoch()
                                      .count();
        path_ = std::filesystem::temp_directory_path()
                / ("vibescriber-download-tests-" + std::to_string(unique_value));
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
    void expect_download_error(Function&& function, const std::string_view description)
    {
        try {
            function();
            expect(false, description);
        } catch (const vibescriber::DownloadError&) {
        }
    }

    [[nodiscard]] int failures() const
    {
        return failures_;
    }

private:
    int failures_ = 0;
};

void write_text(const std::filesystem::path& path, const std::string_view text)
{
    std::ofstream output(path, std::ios::binary);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        throw std::runtime_error("failed to write a test file");
    }
}

std::string read_text(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string file_url(const std::filesystem::path& path)
{
#ifdef _WIN32
    return "file:///" + path.generic_string();
#else
    return "file://" + path.generic_string();
#endif
}

void test_sha256(TestSuite& suite)
{
    constexpr std::array<std::byte, 0> empty{};
    suite.expect(
        vibescriber::sha256(empty)
            == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "SHA-256 matches the standard empty-input vector");

    constexpr std::array abc = {
        std::byte{'a'},
        std::byte{'b'},
        std::byte{'c'},
    };
    suite.expect(vibescriber::sha256(abc) == abc_sha256,
                 "SHA-256 matches the standard abc vector");

    constexpr std::string_view multi_block_input =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    suite.expect(
        vibescriber::sha256(std::as_bytes(std::span(
            multi_block_input.data(), multi_block_input.size())))
            == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
        "SHA-256 matches a standard multi-block vector");
}

void test_verified_download(TestSuite& suite)
{
    TemporaryDirectory temporary_directory;
    const auto source = temporary_directory.path() / "source.txt";
    const auto destination = temporary_directory.path() / "nested" / "download.txt";
    write_text(source, "abc");

    const auto result = vibescriber::download_verified(
        file_url(source), destination, abc_sha256);
    suite.expect(result.status == vibescriber::DownloadStatus::downloaded,
                 "a valid artifact is downloaded");
    suite.expect(result.bytes == 3U, "the downloaded byte count is reported");
    suite.expect(read_text(destination) == "abc", "the verified file is promoted");
    suite.expect(!std::filesystem::exists(destination.string() + ".part"),
                 "the staging file is absent after success");

    const auto existing_result = vibescriber::download_verified(
        "file:///this-file-does-not-exist", destination, abc_sha256);
    suite.expect(existing_result.status == vibescriber::DownloadStatus::already_present,
                 "an existing verified artifact is reused without transfer");
}

void test_checksum_failure(TestSuite& suite)
{
    TemporaryDirectory temporary_directory;
    const auto source = temporary_directory.path() / "source.txt";
    const auto destination = temporary_directory.path() / "download.txt";
    write_text(source, "abc");

    suite.expect_download_error(
        [&] {
            (void)vibescriber::download_verified(
                file_url(source),
                destination,
                "0000000000000000000000000000000000000000000000000000000000000000");
        },
        "a checksum mismatch rejects the download");
    suite.expect(!std::filesystem::exists(destination),
                 "a checksum mismatch does not create the destination");
    suite.expect(!std::filesystem::exists(destination.string() + ".part"),
                 "a checksum mismatch removes the staging file");
}

} // namespace

int main()
{
    TestSuite suite;
    test_sha256(suite);
    test_verified_download(suite);
    test_checksum_failure(suite);

    if (suite.failures() != 0) {
        std::cerr << suite.failures() << " download test assertion(s) failed\n";
        return 1;
    }

    std::cout << "All download tests passed\n";
    return 0;
}
