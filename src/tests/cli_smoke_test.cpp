#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string quote_for_shell(const std::filesystem::path& p) {
    std::string s = p.string();
    std::string result = "\"";
    for (char c : s) {
        if (c == '"') {
            result += '\\';
        }
        result += c;
    }
    result += '"';
    return result;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::string content{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    content.erase(std::remove(content.begin(), content.end(), '\r'), content.end());
    return content;
}

int run_shell_command(const std::string& command) {
#ifdef _WIN32
    return std::system(("\"" + command + "\"").c_str());
#else
    return std::system(command.c_str());
#endif
}

int run_cli(
    const std::filesystem::path& cli_path,
    const std::filesystem::path& config_path,
    const std::filesystem::path& source_path,
    const std::filesystem::path& dna_dir) {

    const std::string cmd =
        quote_for_shell(cli_path) + " generate " +
        quote_for_shell(config_path) + " " +
        quote_for_shell(source_path) + " " +
        quote_for_shell(dna_dir);

    return run_shell_command(cmd);
}

}

int main(int argc, char* argv[]) {
    namespace fs = std::filesystem;

    if (argc != 2) {
        return 1;
    }

    const fs::path source_dir = fs::path(__FILE__).parent_path().parent_path().lexically_normal();
    const fs::path fixture_dir = (source_dir / "tests" / "fixtures" / "source_corpus" / "cpp").lexically_normal();
    const fs::path config_path = (source_dir / "resources" / "config" / "cconfig.ini").lexically_normal();
    const fs::path cli_path = argv[1];

    if (!fs::exists(cli_path)) {
        return 1;
    }

    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path output_dir =
        (fs::temp_directory_path() / ("cpptr_cli_smoke_" + std::to_string(unique_suffix))).lexically_normal();
    fs::create_directories(output_dir);

    const fs::path source_path = (fixture_dir / "201213119.cpp").lexically_normal();

    const int exit_code = run_cli(cli_path, config_path, source_path, output_dir);
    if (exit_code != 0) {
        return 10;
    }

    const fs::path output_file = (output_dir / "201213119.cpp.DNA").lexically_normal();
    if (!fs::exists(output_file)) {
        return 20;
    }

    const fs::path snapshot_file = output_file.string() + ".src";
    if (!fs::is_regular_file(snapshot_file) || read_file(snapshot_file) != read_file(source_path)) {
        return 30;
    }

    return 0;
}
