#pragma once

#include <filesystem>
#include <unordered_map>
#include <vector>
#include <string>

namespace cpptr {

class frequency_service {
public:
    // DNA 디렉토리 내 모든 파일을 스캔하여 토큰별 빈도(%) 계산
    std::unordered_map<int, double> calculate_frequencies(
        const std::filesystem::path& dna_directory,
        class dna_loader& loader);
};

} // namespace cpptr
