#include "../include/cpptr/compare_service.hpp"
#include "../include/cpptr/alignment_engine.hpp"
#include "../include/cpptr/dna_loader.hpp"
#include "../include/cpptr/frequency_service.hpp"

#include <filesystem>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>

namespace cpptr {

class compare_pipeline_service final : public compare_service {
public:
    std::vector<alignment_result> run_comparison(const compare_request& request) override {
        dna_loader loader;
        frequency_service freq_svc;

        auto frequencies = freq_svc.calculate_frequencies(request.dna_directory, loader);

        std::vector<std::filesystem::path> dna_files;
        for (const auto& entry : std::filesystem::directory_iterator(request.dna_directory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".DNA") {
                dna_files.push_back(entry.path());
            }
        }
        std::sort(dna_files.begin(), dna_files.end());

        alignment_engine engine(request.params);
        std::vector<alignment_result> results;
        std::unordered_map<std::filesystem::path, std::vector<int>> sequences;

        auto load = [&](const std::filesystem::path& path) -> const std::vector<int>& {
            auto [entry, inserted] = sequences.try_emplace(path);
            if (inserted) {
                entry->second = loader.load_sequence(path);
            }
            return entry->second;
        };

        auto compare_pair = [&](const std::filesystem::path& path1, const std::filesystem::path& path2) {
            const auto& seq1 = load(path1);
            const auto& seq2 = load(path2);
            if (seq1.empty() || seq2.empty()) return;
            results.push_back(engine.compare(
                path1.filename().string(), seq1,
                path2.filename().string(), seq2,
                frequencies));
        };

        if (request.pairs.empty()) {
            for (size_t i = 0; i < dna_files.size(); ++i) {
                for (size_t j = i + 1; j < dna_files.size(); ++j) {
                    compare_pair(dna_files[i], dna_files[j]);
                }
            }
        } else {
            results.reserve(request.pairs.size());
            for (const auto& pair : request.pairs) {
                compare_pair(
                    request.dna_directory / pair.program1,
                    request.dna_directory / pair.program2);
            }
        }

        if (!results.empty()) {
            calculate_statistics(results);
        }

        return results;
    }

private:
    void calculate_statistics(const std::vector<alignment_result>& results) {
        double sum = 0;
        for (const auto& r : results) {
            sum += r.similarity_percent;
        }
        double avg = sum / results.size();

        double ss = 0;
        for (const auto& r : results) {
            ss += std::pow(r.similarity_percent - avg, 2);
        }
        double stdv = std::sqrt(ss / results.size());

        double beta = (stdv * std::sqrt(6.0) / std::numbers::pi);
        double mu = avg - (0.57721 * beta);

        std::cout << "> AVRG = " << avg << "\n"
                  << "> STDV = " << stdv << "\n"
                  << "> BETA = " << beta << "\n"
                  << "> MU   = " << mu << "\n";
    }
};

std::unique_ptr<compare_service> make_compare_service() {
    return std::make_unique<compare_pipeline_service>();
}

} // namespace cpptr
