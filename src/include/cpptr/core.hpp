#pragma once

#include "dna_service.hpp"

#include <string_view>

namespace cpptr {

constexpr std::string_view version() noexcept {
    return "0.0.0-skeleton";
}

std::string_view core_banner() noexcept;

}
