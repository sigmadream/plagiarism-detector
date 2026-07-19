#include "../include/cpptr/frequency_service.hpp"
#include "../include/cpptr/dna_loader.hpp"
#include <filesystem>
#include <map>
#include <iostream>

namespace cpptr {

std::unordered_map<int, double> frequency_service::calculate_frequencies(
    const std::filesystem::path& dna_directory,
    dna_loader& loader) {

    std::unordered_map<int, int> counts;
    int total_tokens = 0;

    for (const auto& entry : std::filesystem::directory_iterator(dna_directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".DNA") {
            auto seq = loader.load_sequence(entry.path());
            for (int id : seq) {
                counts[id]++;
                total_tokens++;
            }
        }
    }

    std::unordered_map<int, double> frequencies;
    if (total_tokens == 0) return frequencies;

    for (auto const& [id, count] : counts) {
        // 백분율(%)로 환산
        frequencies[id] = (static_cast<double>(count) / total_tokens) * 100.0;
    }

    return frequencies;
}

} // namespace cpptr
