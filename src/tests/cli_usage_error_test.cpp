#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

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

int run_shell_command(const std::string& command) {
#ifdef _WIN32
    return std::system(("\"" + command + "\"").c_str());
#else
    return std::system(command.c_str());
#endif
}

int capture_output(const std::string& cmd, std::string& stdout_content, std::string& stderr_content, int& exit_code) {
    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / ("cpptr_test_" + std::to_string(unique_suffix));
    std::filesystem::create_directories(temp_dir);

    const std::filesystem::path stdout_file = temp_dir / "stdout.txt";
    const std::filesystem::path stderr_file = temp_dir / "stderr.txt";

    const std::string full_cmd = cmd + " > " + quote_for_shell(stdout_file) + " 2> " + quote_for_shell(stderr_file);
    exit_code = run_shell_command(full_cmd);

    auto read_file = [](const std::filesystem::path& path) -> std::string {
        std::ifstream input(path, std::ios::binary);
        return std::string(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
    };

    stdout_content = read_file(stdout_file);
    stderr_content = read_file(stderr_file);

    std::filesystem::remove(stdout_file);
    std::filesystem::remove(stderr_file);
    std::filesystem::remove(temp_dir);

    return 0;
}

}

int main(int argc, char* argv[]) {
    namespace fs = std::filesystem;

    if (argc != 2) {
        return 1;
    }

    const fs::path cli_path = argv[1];

    if (!fs::exists(cli_path)) {
        return 1;
    }

    std::string stdout_content;
    std::string stderr_content;
    int exit_code = 0;

    const std::string no_args_cmd = quote_for_shell(cli_path);
    capture_output(no_args_cmd, stdout_content, stderr_content, exit_code);

    if (exit_code == 0) {
        return 10;
    }

    const bool has_usage = stderr_content.find("Usage:") != std::string::npos;
    const bool has_error_msg = stderr_content.find("error:") != std::string::npos;

    if (!has_usage || !has_error_msg) {
        return 20;
    }

    stdout_content.clear();
    stderr_content.clear();
    exit_code = 0;

    const std::string one_arg_cmd = quote_for_shell(cli_path) + " /some/config.ini";
    capture_output(one_arg_cmd, stdout_content, stderr_content, exit_code);

    if (exit_code == 0) {
        return 30;
    }

    if (stderr_content.find("error:") == std::string::npos) {
        return 40;
    }

    stdout_content.clear();
    stderr_content.clear();
    exit_code = 0;

    const std::string two_args_cmd = quote_for_shell(cli_path) + " /some/config.ini /some/source.cpp";
    capture_output(two_args_cmd, stdout_content, stderr_content, exit_code);

    if (exit_code == 0) {
        return 50;
    }

    if (stderr_content.find("error:") == std::string::npos) {
        return 60;
    }

    const fs::path source_dir = fs::path(__FILE__).parent_path().parent_path().lexically_normal();
    const fs::path output_dir = fs::temp_directory_path() / "cpptr_cli_validation";

    auto fails_with = [&](const std::string& command, const std::string& expected) {
        stdout_content.clear();
        stderr_content.clear();
        exit_code = 0;
        capture_output(command, stdout_content, stderr_content, exit_code);
        return exit_code != 0 && stderr_content.find(expected) != std::string::npos;
    };

    const std::string invalid_number_cmd =
        quote_for_shell(cli_path) + " compare " + quote_for_shell(fs::current_path()) +
        " nope 1 1 1 FV " + quote_for_shell(output_dir) + " CPP";
    if (!fails_with(invalid_number_cmd, "positive finite numbers")) {
        return 70;
    }

    const std::string invalid_mode_cmd =
        quote_for_shell(cli_path) + " compare " + quote_for_shell(fs::current_path()) +
        " 1 1 1 1 adaptive " + quote_for_shell(output_dir) + " CPP";
    if (!fails_with(invalid_mode_cmd, "mode must be SC or FV")) {
        return 80;
    }

    const std::string invalid_language_cmd =
        quote_for_shell(cli_path) + " compare " + quote_for_shell(fs::current_path()) +
        " 1 1 1 1 FV " + quote_for_shell(output_dir) + " JAVA";
    if (!fails_with(invalid_language_cmd, "Java is not supported")) {
        return 90;
    }

    const fs::path missing_dir = output_dir / "missing-dna";
    const std::string missing_directory_cmd =
        quote_for_shell(cli_path) + " compare " + quote_for_shell(missing_dir) +
        " 1 1 1 1 FV " + quote_for_shell(output_dir) + " CPP";
    if (!fails_with(missing_directory_cmd, "DNA directory does not exist")) {
        return 100;
    }

    const fs::path java_config = output_dir / "unsupported-java.ini";
    fs::create_directories(output_dir);
    {
        std::ofstream config(java_config);
        config << "[LANGUAGE]\n";
        config << "language=JAVA\n";
    }
    const fs::path source_file = source_dir / "tests" / "fixtures" / "source_corpus" / "cpp" / "201213119.cpp";
    const std::string java_generate_cmd =
        quote_for_shell(cli_path) + " generate " + quote_for_shell(java_config) + " " +
        quote_for_shell(source_file) + " " + quote_for_shell(output_dir);
    if (!fails_with(java_generate_cmd, "unsupported language")) {
        return 110;
    }

    return 0;
}
