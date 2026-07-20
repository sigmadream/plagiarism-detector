#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace cpptr {

enum class alignment_mode {
    SCORE_BASED, // SC
    FV_BASED,    // FV
    ASYMMETRIC   // ASM
};

struct alignment_parameters {
    double alpha = 0.5;
    double beta = 0.5;
    double ins = 4.0;
    double del = 4.0;
    alignment_mode mode = alignment_mode::FV_BASED;
    std::string language = "CPP";
};

struct alignment_result {
    std::string program1;
    std::string program2;
    double similarity_percent = 0.0;
    double match_value_sum = 0.0;
    double max_match_block = 0.0;
    double shorter_dna_score = 0.0;
    double longer_dna_score = 0.0;
    int match_area_count = 0;

    // 구간 정보 (Absolute Match)
    int row_start = 0;
    int row_end = 0;
    int col_start = 0;
    int col_end = 0;
};

struct comparison_pair {
    std::filesystem::path program1;
    std::filesystem::path program2;
};

struct compare_request {
    std::filesystem::path dna_directory;
    alignment_parameters params;
    std::filesystem::path output_path;
    std::vector<comparison_pair> pairs;
};

} // namespace cpptr
