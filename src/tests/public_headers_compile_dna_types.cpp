#include "../include/cpptr/dna_types.hpp"

#include <type_traits>

static_assert(std::is_enum_v<cpptr::dna_error_code>);

int public_headers_compile_dna_types() {
    const cpptr::dna_request request{
        std::filesystem::path("config.ini"),
        std::filesystem::path("example.cpp"),
        std::filesystem::path("dna-out"),
    };

    const auto success = cpptr::dna_result::success(request.dna_directory / "example.cpp.DNA");
    const auto failure = cpptr::dna_result::failure({
        cpptr::dna_error_code::parse_failure,
        "compile-only",
        request.source_path,
    });

    return success.ok() && !failure.ok() ? 0 : 1;
}
