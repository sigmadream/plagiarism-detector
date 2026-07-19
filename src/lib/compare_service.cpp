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

        for (size_t i = 0; i < dna_files.size(); ++i) {
            auto seq1 = loader.load_sequence(dna_files[i]);
            std::string name1 = dna_files[i].filename().string();

            for (size_t j = i + 1; j < dna_files.size(); ++j) {
                auto seq2 = loader.load_sequence(dna_files[j]);
                std::string name2 = dna_files[j].filename().string();

                if (seq1.empty() || seq2.empty()) continue;

                results.push_back(engine.compare(name1, seq1, name2, seq2, frequencies));
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
