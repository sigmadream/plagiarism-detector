#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::string quote_for_shell(const std::filesystem::path& path) {
    std::string value = path.string();
    std::string result = "\"";
    for (char character : value) {
        if (character == '"') {
            result += '\\';
        }
        result += character;
    }
    result += '"';
    return result;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

int run_shell_command(const std::string& command) {
#ifdef _WIN32
    return std::system(("\"" + command + "\"").c_str());
#else
    return std::system(command.c_str());
#endif
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
        (fs::temp_directory_path() / ("cpptr_compare_csv_" + std::to_string(unique_suffix))).lexically_normal();
    const fs::path dna_dir = output_dir / "dna";

    for (const std::string program : {"201213119.cpp", "201224507.cpp"}) {
        const std::string generate_command =
            quote_for_shell(cli_path) + " generate " +
            quote_for_shell(config_path) + " " +
            quote_for_shell(fixture_dir / program) + " " +
            quote_for_shell(dna_dir);
        if (run_shell_command(generate_command) != 0) {
            return 5;
        }
    }

    const std::string compare_command =
        quote_for_shell(cli_path) + " compare " +
        quote_for_shell(dna_dir) + " 1 1 1 1 fv " +
        quote_for_shell(output_dir) + " CPP";

    if (run_shell_command(compare_command) != 0) {
        return 10;
    }

    const fs::path csv_path = output_dir / "Plag-Detection-Result.csv";
    if (!fs::is_regular_file(csv_path)) {
        return 20;
    }
    if (fs::exists(output_dir / "review-bundle")) {
        return 25;
    }

    const std::string csv = read_file(csv_path);
    if (csv.find("Program1,Program2,SUM(P1:P2),Score") == std::string::npos) {
        return 30;
    }
    if (csv.find("201213119.cpp.DNA,201224507.cpp.DNA") == std::string::npos) {
        return 40;
    }

    return 0;
}