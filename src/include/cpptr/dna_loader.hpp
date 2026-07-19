#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>

namespace cpptr {

class dna_loader {
public:
    // 토큰 이름을 ID로 변환하여 시퀀스 로드
    std::vector<int> load_sequence(const std::filesystem::path& dna_path);

    // 토큰 ID와 이름 매핑 정보 반환
    const std::unordered_map<std::string, int>& get_name_to_id() const;
    const std::unordered_map<int, std::string>& get_id_to_name() const;

private:
    int get_or_create_id(const std::string& token_name);

    std::unordered_map<std::string, int> name_to_id_;
    std::unordered_map<int, std::string> id_to_name_;
    int next_id_ = 1;
};

} // namespace cpptr
