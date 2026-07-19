#include "../include/cpptr/alignment_engine.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>

namespace cpptr {

namespace {
double log2_safe(double x) {
    if (x <= 0) return 0;
    return std::log2(x);
}
}

alignment_engine::alignment_engine(const alignment_parameters& params)
    : params_(params) {}

alignment_result alignment_engine::compare(
    const std::string& name1, const std::vector<int>& seq1,
    const std::string& name2, const std::vector<int>& seq2,
    const std::unordered_map<int, double>& frequencies) {

    calculate_score_table(seq1, seq2, frequencies);

    double max_score = -1.0;
    int max_r = 0, max_c = 0;

    // 테이블에서 최대값 탐색
    for (int i = 1; i <= seq1.size(); ++i) {
        for (int j = 1; j <= seq2.size(); ++j) {
            if (score_table_[i][j] > max_score) {
                max_score = score_table_[i][j];
                max_r = i;
                max_c = j;
            }
        }
    }

    alignment_result result;
    result.program1 = name1;
    result.program2 = name2;

    if (max_score <= 0) {
        return result;
    }

    auto [start_r, start_c] = trace_back(max_r, max_c);

    result.match_value_sum = max_score;
    result.max_match_block = max_score;
    result.match_area_count = 1;
    result.row_start = start_r;
    result.row_end = max_r;
    result.col_start = start_c;
    result.col_end = max_c;

    // 유사도 계산 (PintCon FV 모드 공식)
    double shorter_score = 0;
    const auto& shorter_seq = (seq1.size() < seq2.size()) ? seq1 : seq2;
    for (int token : shorter_seq) {
        shorter_score += get_match_score(token, frequencies);
    }
    result.shorter_dna_score = shorter_score;

    if (shorter_score > 0) {
        result.similarity_percent = (max_score / shorter_score) * 100.0;
    } else if (max_score > 0) {
        result.similarity_percent = 100.0; // 분모가 0인데 분자가 양수면 (이론상 불가능하지만) 100%
    }

    return result;
}

void alignment_engine::calculate_score_table(
    const std::vector<int>& seq1, const std::vector<int>& seq2,
    const std::unordered_map<int, double>& frequencies) {

    int rows = static_cast<int>(seq1.size());
    int cols = static_cast<int>(seq2.size());

    score_table_.assign(rows + 1, std::vector<double>(cols + 1, 0.0));
    arrow_table_.assign(rows + 1, std::vector<int>(cols + 1, ZERO));

    for (int i = 1; i <= rows; ++i) {
        for (int j = 1; j <= cols; ++j) {
            double diag_score = 0;
            if (seq1[i - 1] == seq2[j - 1]) {
                diag_score = score_table_[i - 1][j - 1] + get_match_score(seq1[i - 1], frequencies);
            } else {
                diag_score = score_table_[i - 1][j - 1] + get_mismatch_score(seq1[i - 1], seq2[j - 1], frequencies);
            }

            double up_score = score_table_[i - 1][j] + get_gap_score(seq1[i - 1], params_.del, frequencies);
            double left_score = score_table_[i][j - 1] + get_gap_score(seq2[j - 1], params_.ins, frequencies);

            if (diag_score >= up_score && diag_score >= left_score && diag_score > 0) {
                score_table_[i][j] = diag_score;
                arrow_table_[i][j] = DIAGONAL;
            } else if (up_score >= left_score && up_score > 0) {
                score_table_[i][j] = up_score;
                arrow_table_[i][j] = UP;
            } else if (left_score > 0) {
                score_table_[i][j] = left_score;
                arrow_table_[i][j] = LEFT;
            } else {
                score_table_[i][j] = 0;
                arrow_table_[i][j] = ZERO;
            }
        }
    }
}

std::pair<int, int> alignment_engine::trace_back(int max_r, int max_c) {
    int cur_r = max_r;
    int cur_c = max_c;

    while (cur_r > 0 && cur_c > 0 && arrow_table_[cur_r][cur_c] != ZERO) {
        int dir = arrow_table_[cur_r][cur_c];
        if (dir == DIAGONAL) {
            cur_r--;
            cur_c--;
        } else if (dir == UP) {
            cur_r--;
        } else if (dir == LEFT) {
            cur_c--;
        } else {
            break;
        }
    }
    return {cur_r, cur_c};
}

double alignment_engine::get_match_score(int token, const std::unordered_map<int, double>& frequencies) {
    if (params_.mode == alignment_mode::SCORE_BASED) {
        return params_.alpha; // FIXED_MATCHING_SCORE = 1.0 * alpha
    }

    auto it = frequencies.find(token);
    double fv = (it != frequencies.end()) ? it->second * 0.01 : 0.0001;
    if (fv <= 0) fv = 0.0001;

    // PintCon: score = -alpha * log2(pi^2)
    return -params_.alpha * log2_safe(fv * fv);
}

double alignment_engine::get_mismatch_score(int token1, int token2, const std::unordered_map<int, double>& frequencies) {
    if (params_.mode == alignment_mode::SCORE_BASED) {
        return -params_.beta; // FIXED_MISMATCHING_SCORE = -1.0 * beta
    }

    auto it1 = frequencies.find(token1);
    auto it2 = frequencies.find(token2);
    double fv1 = (it1 != frequencies.end()) ? it1->second * 0.01 : 0.0001;
    double fv2 = (it2 != frequencies.end()) ? it2->second * 0.01 : 0.0001;
    if (fv1 <= 0) fv1 = 0.0001;
    if (fv2 <= 0) fv2 = 0.0001;

    // PintCon: score = beta * log2(pi * qi)
    return params_.beta * log2_safe(fv1 * fv2);
}

double alignment_engine::get_gap_score(
    int token,
    double penalty,
    const std::unordered_map<int, double>& frequencies) {
    if (params_.mode == alignment_mode::SCORE_BASED) {
        return -penalty;
    }

    auto it = frequencies.find(token);
    double fv = (it != frequencies.end()) ? it->second * 0.01 : 0.0001;
    if (fv <= 0) fv = 0.0001;

    return penalty * log2_safe(fv);
}

} // namespace cpptr
