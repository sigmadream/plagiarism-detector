#include "support/configuration_loader_probe.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

int main() {
    namespace fs = std::filesystem;

    const auto unique_suffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root =
        (fs::temp_directory_path() / ("cpptr_missing_keyword_" + std::to_string(unique_suffix))).lexically_normal();
    const fs::path config_dir = (root / "config").lexically_normal();
    fs::create_directories(config_dir);

    const fs::path config_path = (config_dir / "cconfig.ini").lexically_normal();
    std::ofstream output(config_path);
    output << "[COMPILE]\n";
    output << "input_file_format = S\n";
    output << "[LANGUAGE]\n";
    output << "language = CPP\n";
    output << "[KEYWORD]\n";
    output << "cpp_keyword_path = ../keywords/does_not_exist.tbl\n";
    output.close();

    const auto loaded = cpptr::tests::support::load_configuration(config_path);
    if (loaded.ok()) {
        return 1;
    }

    if (!loaded.error.has_value()) {
        return 2;
    }

    if (loaded.error->code != cpptr::dna_error_code::config_load_failure) {
        return 3;
    }

    if (loaded.error->path.filename() != "does_not_exist.tbl") {
        return 4;
    }

    return 0;
}
