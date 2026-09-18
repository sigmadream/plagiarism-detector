#include <cstdlib>
#include <algorithm>
#include <chrono>
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

#ifndef CPPTR_VERSION
#define CPPTR_VERSION "unknown"
#endif

void print_usage(std::ostream& out, const char* program_name) {
    out << "cpptr-cli " << CPPTR_VERSION << " - C++ Program DNA Generator & Plagiarism Detector\n"
        << "\n"
        << "Usage: " << program_name << " <src_file1> <src_file2> [options]\n"
        << "       " << program_name << " <command> [args...]\n"
        << "\n"
        << "Default (no command): print the similarity of two C/C++ source files.\n"
        << "  similarity <src_file1> <src_file2> [--config <ini>] [--mode SC|FV] [--lang C|CPP]\n"
        << "             [--params <alpha> <beta> <gamma> <delta>] [--csv]\n"
        << "           Defaults: bundled config, FV, language from file extension, params 1 1 1 1\n"
        << "\n"
        << "Commands:\n"
        << "  generate <config.ini> <src_file> <dna_dir>\n"
        << "           Generate .DNA file from source\n"
        << "  generate-batch <config.ini> <src_dir> <dna_dir>\n"
        << "           Generate .DNA files recursively in one process\n"
        << "  compare <dna_dir> <alpha> <beta> <gamma> <delta> <mode> <output_dir> <lang> [--lines]\n"
        << "           Compare all .DNA files in a directory and emit a CSV report\n"
        << "  compare-manifest <dna_dir> <pairs.csv> <alpha> <beta> <gamma> <delta> <mode> <output_dir> <lang> [--lines]\n"
        << "           Compare only labeled pairs listed in a benchmark manifest\n"
        << "\n"
        << "Arguments:\n"
        << "  <config.ini>  Path to a config file, or '-' to use the bundled default config\n"
        << "  <mode>        SC or FV\n"
        << "  <lang>        C or CPP\n"
        << "  --lines       Append the source line range of each aligned region to the CSV\n"
        << "\n"
        << "Options:\n"
        << "  --help       Display this help message\n"
        << "  --version    Print the version and exit\n";
}

// '-' selects the default config bundled with the executable (share/cpptr/resources).
std::filesystem::path config_argument(const char* value) {
    return std::string_view(value) == "-" ? std::filesystem::path{} : std::filesystem::path(value);
}

// Consumes a trailing "--lines" flag so the positional argument count stays unchanged.
bool take_lines_flag(int& argc, char* argv[]) {
    if (argc >= 2 && std::string_view(argv[argc - 1]) == "--lines") {
        --argc;
        return true;
    }
    return false;
}

void write_line_columns(std::ostream& out, const cpptr::alignment_result& result, bool enabled) {
    if (!enabled) return;
    out << ',' << result.row_line_start << ',' << result.row_line_end << ','
        << result.col_line_start << ',' << result.col_line_end;
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

    const std::filesystem::path config_path = config_argument(argv[1]);
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

    const std::filesystem::path config_path = config_argument(argv[1]);
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
    const bool with_lines = take_lines_flag(argc, argv);
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

    out << "Program1,Program2,SUM(P1:P2),Score,Begin_P1,End_P1,Begin_P2,End_P2";
    if (with_lines) out << ",Line_Begin_P1,Line_End_P1,Line_Begin_P2,Line_End_P2";
    out << "\n";
    for (const auto& r : results) {
        out << r.program1 << "," << r.program2 << "," << r.match_value_sum << ","
            << r.similarity_percent << "," << r.row_start << "," << r.row_end << ","
            << r.col_start << "," << r.col_end;
        write_line_columns(out, r, with_lines);
        out << "\n";
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
    const bool with_lines = take_lines_flag(argc, argv);
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
           "Begin_P1,End_P1,Begin_P2,End_P2";
    if (with_lines) out << ",Line_Begin_P1,Line_End_P1,Line_Begin_P2,Line_End_P2";
    out << "\n";
    for (std::size_t index = 0; index < results.size(); ++index) {
        const auto& record = (*records)[index];
        const auto& result = results[index];
        out << record.pair_id << ',' << record.label << ',' << record.kind << ','
            << record.split << ',' << result.program1 << ',' << result.program2 << ','
            << result.match_value_sum << ',' << result.similarity_percent << ','
            << result.row_start << ',' << result.row_end << ','
            << result.col_start << ',' << result.col_end;
        write_line_columns(out, result, with_lines);
        out << '\n';
    }
    if (!out) {
        std::cerr << "error: failed while writing output file " << request.output_path << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "Manifest comparison complete. Pairs: " << results.size()
              << ". Results written to " << request.output_path << "\n";
    return EXIT_SUCCESS;
}

// Removes a temporary directory tree; errors are ignored because the result is already known.
struct temporary_directory {
    std::filesystem::path path;
    ~temporary_directory() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

// similarity <src1> <src2> [--config <ini>] [--mode SC|FV] [--lang C|CPP]
//            [--params a b g d] [--csv]
int handle_similarity(int argc, char* argv[]) {
    std::vector<std::filesystem::path> sources;
    std::filesystem::path config_path;
    cpptr::compare_request request;
    request.params.alpha = request.params.beta = request.params.ins = request.params.del = 1.0;
    request.params.mode = cpptr::alignment_mode::FV_BASED;
    request.params.language.clear();
    request.report_statistics = false;
    bool csv = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view arg(argv[index]);
        if (arg == "--config" && index + 1 < argc) {
            config_path = config_argument(argv[++index]);
        } else if (arg == "--mode" && index + 1 < argc) {
            const std::string_view mode(argv[++index]);
            if (mode == "SC" || mode == "sc") {
                request.params.mode = cpptr::alignment_mode::SCORE_BASED;
            } else if (mode == "FV" || mode == "fv") {
                request.params.mode = cpptr::alignment_mode::FV_BASED;
            } else {
                std::cerr << "error: --mode must be SC or FV\n";
                return EXIT_FAILURE;
            }
        } else if (arg == "--lang" && index + 1 < argc) {
            const std::string_view language(argv[++index]);
            if (language == "C" || language == "c") {
                request.params.language = "C";
            } else if (language == "CPP" || language == "cpp") {
                request.params.language = "CPP";
            } else {
                std::cerr << "error: --lang must be C or CPP\n";
                return EXIT_FAILURE;
            }
        } else if (arg == "--params" && index + 4 < argc) {
            double* targets[] = {&request.params.alpha, &request.params.beta,
                                 &request.params.ins, &request.params.del};
            for (double* target : targets) {
                const auto value = parse_finite_number(argv[++index]);
                if (!value || *value <= 0) {
                    std::cerr << "error: --params values must be positive finite numbers\n";
                    return EXIT_FAILURE;
                }
                *target = *value;
            }
        } else if (arg == "--csv") {
            csv = true;
        } else if (arg.starts_with("--")) {
            std::cerr << "error: unknown option " << arg << "\n";
            return EXIT_FAILURE;
        } else {
            sources.emplace_back(arg);
        }
    }

    if (sources.size() != 2) {
        std::cerr << "error: similarity expects exactly two source files\n";
        return EXIT_FAILURE;
    }
    for (const auto& source : sources) {
        if (!std::filesystem::is_regular_file(source)) {
            std::cerr << "error: source file does not exist: " << source << "\n";
            return EXIT_FAILURE;
        }
    }
    if (request.params.language.empty()) {
        const auto is_c = [](const std::filesystem::path& path) {
            auto extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return extension == ".c" || extension == ".h";
        };
        request.params.language = is_c(sources[0]) && is_c(sources[1]) ? "C" : "CPP";
    }

    // DNA files are generated into a private temporary directory as A.DNA and B.DNA so that
    // two sources with the same file name can be compared.
    std::error_code ec;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    temporary_directory workspace{
        std::filesystem::temp_directory_path(ec) / ("cpptr-similarity-" + std::to_string(stamp))};
    if (ec) {
        std::cerr << "error: no temporary directory available\n";
        return EXIT_FAILURE;
    }
    request.dna_directory = workspace.path / "dna";
    std::filesystem::create_directories(request.dna_directory, ec);
    if (ec) {
        std::cerr << "error: failed to create temporary directory " << workspace.path << "\n";
        return EXIT_FAILURE;
    }

    auto generator = cpptr::make_dna_generation_service();
    const char* labels[] = {"A", "B"};
    for (std::size_t index = 0; index < sources.size(); ++index) {
        const auto scratch = workspace.path / labels[index];
        const cpptr::dna_request dna_request{config_path, sources[index], scratch};
        const auto generated = generator->generate(dna_request);
        if (!generated.ok()) {
            std::cerr << "error: " << sources[index] << ": " << generated.error->message << "\n";
            return EXIT_FAILURE;
        }
        std::filesystem::rename(
            generated.output_path, request.dna_directory / (std::string(labels[index]) + ".DNA"), ec);
        if (ec) {
            std::cerr << "error: failed to stage DNA for " << sources[index] << "\n";
            return EXIT_FAILURE;
        }
    }

    request.pairs.push_back({std::filesystem::path("A.DNA"), std::filesystem::path("B.DNA")});
    const auto results = cpptr::make_compare_service()->run_comparison(request);
    if (results.size() != 1) {
        std::cerr << "error: one of the sources produced an empty DNA sequence\n";
        return EXIT_FAILURE;
    }
    const auto& result = results.front();

    if (csv) {
        std::cout << "Program1,Program2,Score,Begin_P1,End_P1,Begin_P2,End_P2,"
                     "Line_Begin_P1,Line_End_P1,Line_Begin_P2,Line_End_P2\n"
                  << sources[0].string() << ',' << sources[1].string() << ','
                  << result.similarity_percent << ',' << result.row_start << ',' << result.row_end << ','
                  << result.col_start << ',' << result.col_end;
        write_line_columns(std::cout, result, true);
        std::cout << '\n';
    } else {
        const char* mode = request.params.mode == cpptr::alignment_mode::SCORE_BASED ? "SC" : "FV";
        std::cout << "A: " << sources[0].string() << "\n"
                  << "B: " << sources[1].string() << "\n"
                  << "similarity: " << result.similarity_percent << " (" << mode << ", "
                  << request.params.language << ")\n"
                  << "aligned region: A lines " << result.row_line_start << "-" << result.row_line_end
                  << ", B lines " << result.col_line_start << "-" << result.col_line_end << "\n";
    }
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
    if (std::string_view(argv[1]) == "--version") {
        std::cout << "cpptr-cli " << CPPTR_VERSION << "\n";
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
        } else if (command == "similarity") {
            return handle_similarity(argc - 1, argv + 1);
        } else {
            // Default: the arguments are two source files to compare.
            return handle_similarity(argc, argv);
        }
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }
}
