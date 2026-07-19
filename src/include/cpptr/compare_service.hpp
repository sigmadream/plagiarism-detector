#pragma once

#include "alignment_types.hpp"
#include <vector>
#include <memory>

namespace cpptr {

class compare_service {
public:
    virtual ~compare_service() = default;

    // 디렉토리 내의 모든 DNA 파일 쌍에 대해 표절 검사 수행
    virtual std::vector<alignment_result> run_comparison(const compare_request& request) = 0;
};

std::unique_ptr<compare_service> make_compare_service();

} // namespace cpptr
