#pragma once

#include "dna_types.hpp"

#include <memory>

namespace cpptr {

class dna_generation_service {
public:
    virtual ~dna_generation_service() = default;

    [[nodiscard]] virtual dna_result generate(const dna_request& request) = 0;
};

[[nodiscard]] std::unique_ptr<dna_generation_service> make_dna_generation_service();

}
