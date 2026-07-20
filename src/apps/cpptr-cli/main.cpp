#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
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
        << "  generate-batch <config.ini> <src_dir> <dna_dir>\n"
        << "           Generate .DNA files recursively in one process\n"
        << "  compare <dna_dir> <alpha> <beta> <gamma> <delta> <mode> <output_dir> <lang>\n"
        << "           Compare all .DNA files in a directory and emit a CSV report\n"
        << "  compare-manifest <dna_dir> <pairs.csv> <alpha> <beta> <gamma> <delta> <mode> <output_dir> <lang>\n"
        << "           Compare only labeled pairs listed in a benchmark manifest\n"
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

bool is_source_file(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension == ".c" || extension == ".cc" ||
           extension == ".cpp" || extension == ".cxx";
}

int handle_generate_batch(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "error: generate-batch expects 3 arguments\n";
        return EXIT_FAILURE;
    }

    const std::filesystem::path config_path(argv[1]);
    const std::filesystem::path source_directory(argv[2]);
    const std::filesystem::path dna_directory(argv[3]);
    std::error_code ec;
    if (!std::filesystem::is_directory(source_directory, ec) || ec) {
        std::cerr << "error: source directory does not exist or is not accessible: "
                  << source_directory << "\n";
        return EXIT_FAILURE;
    }

    std::vector<std::filesystem::path> source_files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(source_directory)) {
        if (entry.is_regular_file() && is_source_file(entry.path())) {
            source_files.push_back(entry.path());
        }
    }
    std::sort(source_files.begin(), source_files.end());
    if (source_files.empty()) {
        std::cerr << "error: source directory contains no C/C++ files\n";
        return EXIT_FAILURE;
    }

    std::unordered_set<std::string> output_names;
    for (const auto& source_file : source_files) {
        const auto output_name = source_file.filename().string() + ".DNA";
        if (!output_names.insert(output_name).second) {
            std::cerr << "error: duplicate source filename would overwrite DNA output: "
                      << source_file.filename() << "\n";
            return EXIT_FAILURE;
        }
    }

    auto service = cpptr::make_dna_generation_service();
    std::size_t generated = 0;
    std::size_t failed = 0;
    for (const auto& source_file : source_files) {
        const cpptr::dna_request request{config_path, source_file, dna_directory};
        const auto result = service->generate(request);
        if (result.ok()) {
            std::error_code remove_error;
            std::filesystem::remove(
                std::filesystem::path(result.output_path.string() + ".src"),
                remove_error);
            if (remove_error) {
                ++failed;
                std::cerr << "error: failed to remove redundant source snapshot for "
                          << source_file << "\n";
                continue;
            }
            ++generated;
        } else {
            ++failed;
            std::cerr << "error: " << source_file << ": " << result.error->message << "\n";
        }
    }

    std::cout << "DNA batch complete. Generated without source snapshots: " << generated
              << ", failed: " << failed << "\n";
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

bool configure_comparison(
    cpptr::compare_request& request,
    char* argv[],
    int first_parameter,
    std::string_view output_filename) {
    const auto alpha = parse_finite_number(argv[first_parameter]);
    const auto beta = parse_finite_number(argv[first_parameter + 1]);
    const auto insertion = parse_finite_number(argv[first_parameter + 2]);
    const auto deletion = parse_finite_number(argv[first_parameter + 3]);
    if (!alpha || !beta || !insertion || !deletion ||
        *alpha <= 0 || *beta <= 0 || *insertion <= 0 || *deletion <= 0) {
        std::cerr << "error: alpha, beta, gamma, and delta must be positive finite numbers\n";
        return false;
    }
    request.params.alpha = *alpha;
    request.params.beta = *beta;
    request.params.ins = *insertion;
    request.params.del = *deletion;

    const std::string_view mode(argv[first_parameter + 4]);
    if (mode == "SC" || mode == "sc") {
        request.params.mode = cpptr::alignment_mode::SCORE_BASED;
    } else if (mode == "FV" || mode == "fv") {
        request.params.mode = cpptr::alignment_mode::FV_BASED;
    } else {
        std::cerr << "error: mode must be SC or FV\n";
        return false;
    }

    const std::string_view language(argv[first_parameter + 6]);
    if (language == "C" || language == "c") {
        request.params.language = "C";
    } else if (language == "CPP" || language == "cpp") {
        request.params.language = "CPP";
    } else {
        std::cerr << "error: language must be C or CPP; Java is not supported\n";
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(request.dna_directory, ec) || ec) {
        std::cerr << "error: DNA directory does not exist or is not accessible: "
                  << request.dna_directory << "\n";
        return false;
    }

    request.output_path = std::filesystem::path(argv[first_parameter + 5]) / output_filename;
    if (!request.output_path.parent_path().empty()) {
        std::filesystem::create_directories(request.output_path.parent_path(), ec);
        if (ec) {
            std::cerr << "error: failed to create output directory: "
                      << request.output_path.parent_path() << "\n";
            return false;
        }
    }
    return true;
}

int handle_compare(int argc, char* argv[]) {
    if (argc != 9) {
        std::cerr << "error: compare expects 8 arguments\n";
        return EXIT_FAILURE;
    }

    cpptr::compare_request request;
    request.dna_directory = argv[1];
    if (!configure_comparison(request, argv, 2, "Plag-Detection-Result.csv")) {
        return EXIT_FAILURE;
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

std::vector<std::string> split_manifest_row(const std::string& line) {
    std::vector<std::string> fields;
    std::istringstream stream(line);
    std::string field;
    while (std::getline(stream, field, ',')) {
        fields.push_back(std::move(field));
    }
    return fields;
}

struct benchmark_pair_record {
    cpptr::comparison_pair pair;
    std::string label;
    std::string pair_id;
    std::string kind;
    std::string split;
};

std::optional<std::vector<benchmark_pair_record>> load_manifest(
    const std::filesystem::path& manifest_path,
    const std::filesystem::path& dna_directory) {
    std::ifstream input(manifest_path);
    if (!input) {
        std::cerr << "error: failed to open pair manifest: " << manifest_path << "\n";
        return std::nullopt;
    }

    std::string line;
    if (!std::getline(input, line)) {
        std::cerr << "error: pair manifest is empty\n";
        return std::nullopt;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != "left_dna,right_dna,label,pair_id,kind,split") {
        std::cerr << "error: pair manifest has an unsupported header\n";
        return std::nullopt;
    }

    std::vector<benchmark_pair_record> records;
    std::size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        auto fields = split_manifest_row(line);
        if (fields.size() != 6 || fields[0].empty() || fields[1].empty()) {
            std::cerr << "error: invalid pair manifest row " << line_number << "\n";
            return std::nullopt;
        }

        const std::filesystem::path left(fields[0]);
        const std::filesystem::path right(fields[1]);
        if (left.has_parent_path() || right.has_parent_path()) {
            std::cerr << "error: pair manifest DNA names must not contain directories\n";
            return std::nullopt;
        }
        if (!std::filesystem::is_regular_file(dna_directory / left) ||
            !std::filesystem::is_regular_file(dna_directory / right)) {
            std::cerr << "error: pair manifest row " << line_number
                      << " references missing DNA\n";
            return std::nullopt;
        }
        records.push_back({
            {left, right},
            std::move(fields[2]),
            std::move(fields[3]),
            std::move(fields[4]),
            std::move(fields[5]),
        });
    }
    if (records.empty()) {
        std::cerr << "error: pair manifest contains no pairs\n";
        return std::nullopt;
    }
    return records;
}

int handle_compare_manifest(int argc, char* argv[]) {
    if (argc != 10) {
        std::cerr << "error: compare-manifest expects 9 arguments\n";
        return EXIT_FAILURE;
    }

    cpptr::compare_request request;
    request.dna_directory = argv[1];
    if (!configure_comparison(request, argv, 3, "Benchmark-Result.csv")) {
        return EXIT_FAILURE;
    }
    const auto records = load_manifest(argv[2], request.dna_directory);
    if (!records) return EXIT_FAILURE;
    request.pairs.reserve(records->size());
    for (const auto& record : *records) request.pairs.push_back(record.pair);

    auto service = cpptr::make_compare_service();
    const auto results = service->run_comparison(request);
    if (results.size() != records->size()) {
        std::cerr << "error: one or more manifest DNA sequences are empty\n";
        return EXIT_FAILURE;
    }

    std::ofstream out(request.output_path);
    if (!out) {
        std::cerr << "error: failed to open output file " << request.output_path << "\n";
        return EXIT_FAILURE;
    }
    out << "PairId,Label,Kind,Split,Program1,Program2,SUM(P1:P2),Score,"
           "Begin_P1,End_P1,Begin_P2,End_P2\n";
    for (std::size_t index = 0; index < results.size(); ++index) {
        const auto& record = (*records)[index];
        const auto& result = results[index];
        out << record.pair_id << ',' << record.label << ',' << record.kind << ','
            << record.split << ',' << result.program1 << ',' << result.program2 << ','
            << result.match_value_sum << ',' << result.similarity_percent << ','
            << result.row_start << ',' << result.row_end << ','
            << result.col_start << ',' << result.col_end << '\n';
    }
    if (!out) {
        std::cerr << "error: failed while writing output file " << request.output_path << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "Manifest comparison complete. Pairs: " << results.size()
              << ". Results written to " << request.output_path << "\n";
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
        } else if (command == "generate-batch") {
            return handle_generate_batch(argc - 1, argv + 1);
        } else if (command == "compare") {
            return handle_compare(argc - 1, argv + 1);
        } else if (command == "compare-manifest") {
            return handle_compare_manifest(argc - 1, argv + 1);
        } else {
            // Legacy compatibility: if no command, treat as generate
            return handle_generate(argc, argv);
        }
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }
}
