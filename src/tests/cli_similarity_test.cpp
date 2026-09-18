// Exercises the default two-file similarity mode of cpptr-cli.
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::string quote_for_shell(const std::filesystem::path& path) {
    std::string result = "\"";
    for (char character : path.string()) {
        if (character == '"') result += '\\';
        result += character;
    }
    result += '"';
    return result;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::string content{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
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

}

int main(int argc, char* argv[]) {
    namespace fs = std::filesystem;
    if (argc != 2) return 1;

    const fs::path source_dir = fs::path(__FILE__).parent_path().parent_path().lexically_normal();
    const fs::path fixture_dir = (source_dir / "tests" / "fixtures" / "source_corpus" / "cpp").lexically_normal();
    const fs::path cli_path = argv[1];
    if (!fs::exists(cli_path)) return 1;

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path output_dir = fs::temp_directory_path() / ("cpptr_cli_similarity_" + std::to_string(stamp));
    fs::create_directories(output_dir);
    const fs::path left = fixture_dir / "201213119.cpp";
    const fs::path right = fixture_dir / "201224507.cpp";

    // 1. No command: the two files are compared with the bundled default config.
    const fs::path plain_output = output_dir / "plain.txt";
    if (run_shell_command(quote_for_shell(cli_path) + " " + quote_for_shell(left) + " " +
                          quote_for_shell(right) + " > " + quote_for_shell(plain_output)) != 0) {
        return 10;
    }
    const std::string plain = read_file(plain_output);
    if (plain.find("similarity: 80.896") == std::string::npos) return 11;
    if (plain.find("(FV, CPP)") == std::string::npos) return 12;
    if (plain.find("aligned region: A lines 3-15, B lines 3-13") == std::string::npos) return 13;
    if (plain.find("AVRG") != std::string::npos) return 14; // no distribution statistics for one pair

    // 2. Explicit command with options and CSV output.
    const fs::path csv_output = output_dir / "csv.txt";
    if (run_shell_command(quote_for_shell(cli_path) + " similarity " + quote_for_shell(left) + " " +
                          quote_for_shell(right) + " --mode SC --lang CPP --params 1 1 1 1 --csv > " +
                          quote_for_shell(csv_output)) != 0) {
        return 20;
    }
    const std::string csv = read_file(csv_output);
    if (csv.find("Program1,Program2,Score,Begin_P1,End_P1,Begin_P2,End_P2,"
                 "Line_Begin_P1,Line_End_P1,Line_Begin_P2,Line_End_P2\n") == std::string::npos) {
        return 21;
    }
    if (csv.find(",3,15,3,13\n") == std::string::npos) return 22;

    // 3. Two sources with the same file name must still be comparable.
    const fs::path copy_a = output_dir / "a" / "main.cpp";
    const fs::path copy_b = output_dir / "b" / "main.cpp";
    fs::create_directories(copy_a.parent_path());
    fs::create_directories(copy_b.parent_path());
    fs::copy_file(left, copy_a);
    fs::copy_file(left, copy_b);
    const fs::path same_output = output_dir / "same.txt";
    if (run_shell_command(quote_for_shell(cli_path) + " " + quote_for_shell(copy_a) + " " +
                          quote_for_shell(copy_b) + " > " + quote_for_shell(same_output)) != 0) {
        return 30;
    }
    if (read_file(same_output).find("similarity: 100") == std::string::npos) return 31;

    // 4. Errors: missing file and unknown option.
    const std::string discard = " > " + quote_for_shell(output_dir / "err.txt") + " 2>&1";
    if (run_shell_command(quote_for_shell(cli_path) + " " + quote_for_shell(left) + " " +
                          quote_for_shell(output_dir / "missing.cpp") + discard) == 0) {
        return 40;
    }
    if (run_shell_command(quote_for_shell(cli_path) + " " + quote_for_shell(left) + " " +
                          quote_for_shell(right) + " --bogus" + discard) == 0) {
        return 41;
    }

    std::error_code ec;
    fs::remove_all(output_dir, ec);
    return 0;
}
