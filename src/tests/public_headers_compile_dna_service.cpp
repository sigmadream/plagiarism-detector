#include "../include/cpptr/dna_service.hpp"

class compile_only_service final : public cpptr::dna_generation_service {
public:
    cpptr::dna_result generate(const cpptr::dna_request& request) override {
        return cpptr::dna_result::failure({
            cpptr::dna_error_code::config_load_failure,
            "compile-only",
            request.config_path,
        });
    }
};

int public_headers_compile_dna_service() {
    auto constructed = cpptr::make_dna_generation_service();
    if (!constructed) {
        return 2;
    }

    compile_only_service service;

    const cpptr::dna_request request{
        std::filesystem::path("config.ini"),
        std::filesystem::path("example.cpp"),
        std::filesystem::path("dna-out"),
    };

    return service.generate(request).ok() ? 1 : 0;
}
