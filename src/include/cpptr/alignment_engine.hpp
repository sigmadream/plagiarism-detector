#pragma once

#include "alignment_types.hpp"
#include <vector>
#include <string>
#include <unordered_map>

namespace cpptr {

class alignment_engine {
public:
    explicit alignment_engine(const alignment_parameters& params);

    // 두 시퀀스를 비교하여 결과 반환
    alignment_result compare(
        const std::string& name1, const std::vector<int>& seq1,
        const std::string& name2, const std::vector<int>& seq2,
        const std::unordered_map<int, double>& frequencies);

private:
    // Smith-Waterman 기반 스코어링 테이블 계산
    void calculate_score_table(
        const std::vector<int>& seq1, const std::vector<int>& seq2,
        const std::unordered_map<int, double>& frequencies);

    // 백트래킹을 통한 매칭 영역 추출 (Row/Col Start 좌표 반환)
    std::pair<int, int> trace_back(int max_r, int max_c);

    double get_match_score(int token, const std::unordered_map<int, double>& frequencies);
    double get_mismatch_score(int token1, int token2, const std::unordered_map<int, double>& frequencies);
    double get_gap_score(int token, double penalty, const std::unordered_map<int, double>& frequencies);

    alignment_parameters params_;
    std::vector<std::vector<double>> score_table_;
    std::vector<std::vector<int>> arrow_table_;

    // 방향 상수 (PintCon과 동일)
    static constexpr int ZERO = 0;
    static constexpr int UP = 1;
    static constexpr int LEFT = 2;
    static constexpr int DIAGONAL = 3;
};

} // namespace cpptr
