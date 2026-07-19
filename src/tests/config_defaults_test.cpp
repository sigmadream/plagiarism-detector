#include "support/configuration_loader_probe.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

class current_path_guard {
public:
    current_path_guard()
        : original_(std::filesystem::current_path()) {}

    ~current_path_guard() {
        std::filesystem::current_path(original_);
    }

private:
    std::filesystem::path original_;
};

}

int main() {
    namespace fs = std::filesystem;

    current_path_guard cwd_guard;

    const auto unique_suffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path isolated_cwd =
        (fs::temp_directory_path() / ("cpptr_config_defaults_" + std::to_string(unique_suffix))).lexically_normal();
    fs::create_directories(isolated_cwd);
    fs::current_path(isolated_cwd);

    const auto defaults = cpptr::tests::support::load_configuration();
    if (!defaults.ok()) {
        return 1;
    }

    const auto default_keyword = defaults.keyword_path;
    if (!fs::is_regular_file(default_keyword)) {
        return 3;
    }

    const fs::path explicit_config = (isolated_cwd / "explicit_config.ini").lexically_normal();
    const fs::path forced_keyword =
        (defaults.config_path.parent_path() / ".." / "keywords" / "c_keyword.tbl").lexically_normal();

    std::ofstream output(explicit_config);
    output << "[COMPILE]\n";
    output << "preprocessor = gcc -E\n";
    output << "input_file_format = S\n";
    output << "[LANGUAGE]\n";
    output << "language = CPP\n";
    output << "[KEYWORD]\n";
    output << "cpp_keyword_path = " << forced_keyword.string() << "\n";
    output.close();

    const auto explicit_loaded = cpptr::tests::support::load_configuration(explicit_config);
    if (!explicit_loaded.ok()) {
        return 4;
    }

    if (fs::weakly_canonical(explicit_loaded.config_path) != fs::weakly_canonical(explicit_config)) {
        return 6;
    }

    if (explicit_loaded.keyword_path.filename() != "c_keyword.tbl") {
        return 7;
    }

    if (!fs::is_regular_file(explicit_loaded.keyword_path)) {
        return 8;
    }

    return 0;
}
