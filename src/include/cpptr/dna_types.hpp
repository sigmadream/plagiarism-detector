#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>

namespace cpptr {

enum class dna_error_code {
    config_load_failure,
    preprocessing_failure,
    parse_failure,
    write_failure,
};

struct dna_request {
    std::filesystem::path config_path;
    std::filesystem::path source_path;
    std::filesystem::path dna_directory;
};

struct dna_error {
    dna_error_code code;
    std::string message;
    std::filesystem::path path;
};

struct dna_result {
    std::filesystem::path output_path;
    std::optional<dna_error> error;

    [[nodiscard]] bool ok() const noexcept {
        return !error.has_value();
    }

    [[nodiscard]] static dna_result success(std::filesystem::path output_path) {
        return dna_result{
            std::move(output_path),
            std::nullopt,
        };
    }

    [[nodiscard]] static dna_result failure(dna_error error) {
        return dna_result{
            {},
            std::move(error),
        };
    }
};

}
