#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::string quote_for_shell(const std::filesystem::path& path) {
    return "\"" + path.string() + "\"";
}

int run_shell_command(const std::string& command) {
#ifdef _WIN32
    return std::system(("\"" + command + "\"").c_str());
#else
    return std::system(command.c_str());
#endif
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

}

int main(int argc, char* argv[]) {
    namespace fs = std::filesystem;
    if (argc != 2) return 1;

    const fs::path source_dir = fs::path(__FILE__).parent_path().parent_path().lexically_normal();
    const fs::path fixture_dir = source_dir / "tests" / "fixtures" / "source_corpus" / "cpp";
    const fs::path config_path = source_dir / "resources" / "config" / "cconfig.ini";
    const fs::path cli_path = argv[1];
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path output_dir = fs::temp_directory_path() /
        ("cpptr_benchmark_cli_" + std::to_string(suffix));
    const fs::path dna_dir = output_dir / "dna";
    const fs::path result_dir = output_dir / "result";
    const fs::path manifest_path = output_dir / "pairs.csv";
    fs::create_directories(output_dir);

    const std::string generate_command =
        quote_for_shell(cli_path) + " generate-batch " +
        quote_for_shell(config_path) + " " +
        quote_for_shell(fixture_dir) + " " +
        quote_for_shell(dna_dir);
    if (run_shell_command(generate_command) != 0) return 10;
    if (!fs::is_regular_file(dna_dir / "201213119.cpp.DNA")) return 11;
    if (fs::exists(dna_dir / "201213119.cpp.DNA.src")) return 12;

    std::ofstream manifest(manifest_path);
    manifest << "left_dna,right_dna,label,pair_id,kind,split\n"
             << "201213119.cpp.DNA,201224507.cpp.DNA,1,7,same-problem,evaluation\n";
    manifest.close();

    const std::string compare_command =
        quote_for_shell(cli_path) + " compare-manifest " +
        quote_for_shell(dna_dir) + " " +
        quote_for_shell(manifest_path) + " 1 1 1 1 FV " +
        quote_for_shell(result_dir) + " CPP";
    if (run_shell_command(compare_command) != 0) return 20;

    const fs::path csv_path = result_dir / "Benchmark-Result.csv";
    if (!fs::is_regular_file(csv_path)) return 30;
    const std::string csv = read_file(csv_path);
    if (csv.find("PairId,Label,Kind,Split,Program1,Program2") == std::string::npos) {
        return 40;
    }
    if (csv.find("7,1,same-problem,evaluation,201213119.cpp.DNA,201224507.cpp.DNA") ==
        std::string::npos) {
        return 50;
    }
    if (std::count(csv.begin(), csv.end(), '\n') != 2) return 60;
    return 0;
}