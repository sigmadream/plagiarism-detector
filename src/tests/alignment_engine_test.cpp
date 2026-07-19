#include "../include/cpptr/alignment_engine.hpp"

#include <gtest/gtest.h>

#include <unordered_map>
#include <vector>

namespace {

cpptr::alignment_result compare_with(
    cpptr::alignment_parameters parameters,
    const std::vector<int>& left,
    const std::vector<int>& right,
    const std::unordered_map<int, double>& frequencies = {}) {
    cpptr::alignment_engine engine(parameters);
    return engine.compare("left", left, "right", right, frequencies);
}

TEST(AlignmentEngineTest, DeletionPenaltyChangesScoreBasedAlignment) {
    cpptr::alignment_parameters parameters;
    parameters.mode = cpptr::alignment_mode::SCORE_BASED;
    parameters.alpha = 2.0;
    parameters.beta = 10.0;
    parameters.del = 0.25;

    const auto low_penalty = compare_with(parameters, {1, 2, 3}, {1, 3});
    parameters.del = 10.0;
    const auto high_penalty = compare_with(parameters, {1, 2, 3}, {1, 3});

    EXPECT_GT(low_penalty.match_value_sum, high_penalty.match_value_sum);
}

TEST(AlignmentEngineTest, InsertionPenaltyChangesFrequencyAlignment) {
    cpptr::alignment_parameters parameters;
    parameters.mode = cpptr::alignment_mode::FV_BASED;
    parameters.alpha = 1.0;
    parameters.beta = 10.0;
    parameters.ins = 0.25;
    const std::unordered_map<int, double> frequencies{{1, 10.0}, {2, 10.0}, {3, 10.0}};

    const auto low_penalty = compare_with(parameters, {1, 3}, {1, 2, 3}, frequencies);
    parameters.ins = 10.0;
    const auto high_penalty = compare_with(parameters, {1, 3}, {1, 2, 3}, frequencies);

    EXPECT_GT(low_penalty.match_value_sum, high_penalty.match_value_sum);
}

} // namespace