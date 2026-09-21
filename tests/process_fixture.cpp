#include <filesystem>
#include <fstream>
#include <string_view>

int main(const int argc, char* argv[])
{
    if (argc < 2) {
        return 2;
    }

    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string_view(argv[index]) == "-i"
            && std::filesystem::path(argv[index + 1]).filename() == "fail input.mp4") {
            return 7;
        }
    }

    std::ofstream output(std::filesystem::path(argv[argc - 1]), std::ios::binary);
    for (int index = 1; index < argc; ++index) {
        output << argv[index] << '\n';
    }
    return output ? 0 : 3;
}
