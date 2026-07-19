#include "../include/cpptr/core.hpp"

#include <string_view>
#include <type_traits>

static_assert(noexcept(cpptr::version()));
static_assert(std::is_same_v<decltype(cpptr::version()), std::string_view>);

int public_headers_compile_core() {
    return cpptr::version().empty() ? 1 : 0;
}
