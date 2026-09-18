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
    if (csv.find("Line_Begin_P1") != std::string::npos) {
        return 45;
    }

    // --lines appends the source line range of each aligned region.
    const fs::path lines_dir = output_dir / "lines";
    const std::string lines_command =
        quote_for_shell(cli_path) + " compare " +
        quote_for_shell(dna_dir) + " 1 1 1 1 fv " +
        quote_for_shell(lines_dir) + " CPP --lines";
    if (run_shell_command(lines_command) != 0) {
        return 50;
    }
    const std::string lines_csv = read_file(lines_dir / "Plag-Detection-Result.csv");
    if (lines_csv.find("End_P2,Line_Begin_P1,Line_End_P1,Line_Begin_P2,Line_End_P2") == std::string::npos) {
        return 51;
    }
    // Both fixtures start with `#include` on line 1 and `int main()` on line 3, so the
    // aligned region must begin at source line 3 for both programs.
    if (lines_csv.find(",3,") == std::string::npos) {
        return 52;
    }
    const auto row_start = lines_csv.find("201213119.cpp.DNA,201224507.cpp.DNA");
    const auto row_end = lines_csv.find('\n', row_start);
    const std::string row = lines_csv.substr(row_start, row_end - row_start);
    int commas = 0;
    for (char character : row) {
        if (character == ',') ++commas;
    }
    if (commas != 11) {
        return 53;
    }

    return 0;
}