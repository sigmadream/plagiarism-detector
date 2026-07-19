#include "../include/cpptr/dna_service.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int main() {
    namespace fs = std::filesystem;

    const fs::path source_dir = fs::path(__FILE__).parent_path().parent_path().lexically_normal();
    const fs::path fixture_dir = (source_dir / "tests" / "fixtures" / "source_corpus" / "c").lexically_normal();
    const fs::path config_path = (source_dir / "resources" / "config" / "cconfig.ini").lexically_normal();
    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path output_dir =
        (fs::temp_directory_path() / ("cpptr_generate_c_corpus_smoke_" + std::to_string(unique_suffix))).lexically_normal();
    fs::create_directories(output_dir);

    auto service = cpptr::make_dna_generation_service();

    std::vector<fs::path> fixture_paths;
    for (const auto& entry : fs::directory_iterator(fixture_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".c") {
            fixture_paths.push_back(entry.path());
        }
    }

    std::sort(fixture_paths.begin(), fixture_paths.end());
    if (fixture_paths.empty()) {
        std::cerr << "No C source fixtures found under: " << fixture_dir << '\n';
        return 1;
    }

    std::size_t generated_count = 0;
    std::size_t generation_failures = 0;
    for (const auto& source_path : fixture_paths) {
        const cpptr::dna_request request{
            config_path,
            source_path,
            output_dir,
        };

        const auto result = service->generate(request);
        if (!result.ok()) {
            ++generation_failures;
            std::cout << source_path.filename().string() << ": generation failed\n";
            continue;
        }

        if (!fs::is_regular_file(result.output_path)) {
            std::cerr << "DNA output was not created: " << result.output_path << '\n';
            return 3;
        }

        const fs::path snapshot_path = result.output_path.string() + ".src";
        if (!fs::is_regular_file(snapshot_path)) {
            std::cerr << "Source snapshot was not created: " << snapshot_path << '\n';
            return 4;
        }
        ++generated_count;
    }

    if (generated_count == 0 || generation_failures > 1) {
        std::cerr << "Unexpected C corpus generation result: "
                  << generated_count << " generated, "
                  << generation_failures << " failed\n";
        return 5;
    }

    std::cout << "C corpus generation: " << generated_count << " succeeded, "
              << generation_failures << " failed across "
              << fixture_paths.size() << " fixtures\n";

    return 0;
}
