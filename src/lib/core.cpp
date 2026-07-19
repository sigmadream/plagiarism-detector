#include "../include/cpptr/core.hpp"

namespace cpptr {

std::string_view core_banner() noexcept {
    return version();
}

}
