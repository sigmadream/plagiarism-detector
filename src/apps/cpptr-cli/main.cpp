#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../../include/cpptr/dna_service.hpp"
#include "../../include/cpptr/compare_service.hpp"

namespace {

void print_usage(std::ostream& out, const char* program_name) {
    out << "cpptr-cli - C++ Program DNA Generator & Plagiarism Detector\n"
        << "\n"
        << "Usage: " << program_name << " [command] [args...]\n"
        << "\n"
        << "Commands:\n"
        << "  generate <config.ini> <src_file> <dna_dir>\n"
        << "           Generate .DNA file from source\n"
        << "  compare <dna_dir> <alpha> <beta> <gamma> <delta> <mode> <output_dir> <lang>\n"
        << "           Compare all .DNA files in a directory and emit a CSV report\n"
        << "\n"
        << "Options:\n"
        << "  --help       Display this help message\n"
        << "\n"
        << "Phase-2 scope: DNA generation + Adaptive Local Alignment comparison.\n";
}

std::optional<double> parse_finite_number(const char* value) {
    try {
        std::size_t consumed = 0;
        const std::string input(value);
        const double parsed = std::stod(input, &consumed);
        if (consumed == input.size() && std::isfinite(parsed)) {
            return parsed;
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

int handle_generate(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "error: generate expects 3 arguments\n";
        return EXIT_FAILURE;
    }

    const std::filesystem::path config_path(argv[1]);
    const std::filesystem::path source_path(argv[2]);
    const std::filesystem::path dna_directory(argv[3]);

    const cpptr::dna_request request{
        std::move(config_path),
        std::move(source_path),
        std::move(dna_directory),
    };

    auto service = cpptr::make_dna_generation_service();
    const auto result = service->generate(request);

    if (!result.ok()) {
        std::cerr << "error: " << result.error->message << "\n";
        return EXIT_FAILURE;
    }

    std::cout << result.output_path << "\n";
    return EXIT_SUCCESS;
}

int handle_compare(int argc, char* argv[]) {
    if (argc != 9) {
        std::cerr << "error: compare expects 8 arguments\n";
        return EXIT_FAILURE;
    }

    cpptr::compare_request request;
    request.dna_directory = argv[1];

    const auto alpha = parse_finite_number(argv[2]);
    const auto beta = parse_finite_number(argv[3]);
    const auto insertion = parse_finite_number(argv[4]);
    const auto deletion = parse_finite_number(argv[5]);
    if (!alpha || !beta || !insertion || !deletion ||
        *alpha <= 0 || *beta <= 0 || *insertion <= 0 || *deletion <= 0) {
        std::cerr << "error: alpha, beta, gamma, and delta must be positive finite numbers\n";
        return EXIT_FAILURE;
    }
    request.params.alpha = *alpha;
    request.params.beta = *beta;
    request.params.ins = *insertion;
    request.params.del = *deletion;

    const std::string_view mode(argv[6]);
    if (mode == "SC" || mode == "sc") {
        request.params.mode = cpptr::alignment_mode::SCORE_BASED;
    } else if (mode == "FV" || mode == "fv") {
        request.params.mode = cpptr::alignment_mode::FV_BASED;
    } else {
        std::cerr << "error: mode must be SC or FV\n";
        return EXIT_FAILURE;
    }

    const std::string_view language(argv[8]);
    if (language == "C" || language == "c") {
        request.params.language = "C";
    } else if (language == "CPP" || language == "cpp") {
        request.params.language = "CPP";
    } else {
        std::cerr << "error: language must be C or CPP; Java is not supported\n";
        return EXIT_FAILURE;
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(request.dna_directory, ec) || ec) {
        std::cerr << "error: DNA directory does not exist or is not accessible: "
                  << request.dna_directory << "\n";
        return EXIT_FAILURE;
    }

    request.output_path = std::filesystem::path(argv[7]) / "Plag-Detection-Result.csv";
    if (!request.output_path.parent_path().empty()) {
        std::filesystem::create_directories(request.output_path.parent_path(), ec);
        if (ec) {
            std::cerr << "error: failed to create output directory: "
                      << request.output_path.parent_path() << "\n";
            return EXIT_FAILURE;
        }
    }

    auto service = cpptr::make_compare_service();
    auto results = service->run_comparison(request);

    std::ofstream out(request.output_path);
    if (!out) {
        std::cerr << "error: failed to open output file " << request.output_path << "\n";
        return EXIT_FAILURE;
    }

    out << "Program1,Program2,SUM(P1:P2),Score,Begin_P1,End_P1,Begin_P2,End_P2\n";
    for (const auto& r : results) {
        out << r.program1 << "," << r.program2 << "," << r.match_value_sum << ","
            << r.similarity_percent << "," << r.row_start << "," << r.row_end << ","
            << r.col_start << "," << r.col_end << "\n";
    }
    if (!out) {
        std::cerr << "error: failed while writing output file " << request.output_path << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "Comparison complete. Results written to " << request.output_path << "\n";
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(std::cerr, argv[0]);
        std::cerr << "error: missing command\n";
        return EXIT_FAILURE;
    }

    if (std::string_view(argv[1]) == "--help") {
        print_usage(std::cout, argv[0]);
        return EXIT_SUCCESS;
    }

    try {
        std::string_view command(argv[1]);
        if (command == "generate") {
            return handle_generate(argc - 1, argv + 1);
        } else if (command == "compare") {
            return handle_compare(argc - 1, argv + 1);
        } else {
            // Legacy compatibility: if no command, treat as generate
            return handle_generate(argc, argv);
        }
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }
}
