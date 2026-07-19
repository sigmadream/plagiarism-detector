#include "../include/cpptr/dna_service.hpp"

#include <chrono>
#include <filesystem>

int main() {
    namespace fs = std::filesystem;

    const fs::path source_dir = fs::path(__FILE__).parent_path().parent_path().lexically_normal();
    const fs::path fixture_path = (source_dir / "tests" / "fixtures" / "source_corpus" / "cpp" / "201213119.cpp").lexically_normal();
    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root =
        (fs::temp_directory_path() / ("cpptr_generate_dna_invalid_config_" + std::to_string(unique_suffix))).lexically_normal();
    const fs::path missing_config = (root / "missing.ini").lexically_normal();
    const fs::path output_dir = (root / "dna").lexically_normal();

    auto service = cpptr::make_dna_generation_service();
    const cpptr::dna_request request{
        missing_config,
        fixture_path,
        output_dir,
    };

    const auto result = service->generate(request);
    if (result.ok()) {
        return 1;
    }

    if (!result.error.has_value()) {
        return 2;
    }

    if (result.error->code != cpptr::dna_error_code::config_load_failure) {
        return 3;
    }

    if (result.error->path != missing_config) {
        return 4;
    }

    return 0;
}
