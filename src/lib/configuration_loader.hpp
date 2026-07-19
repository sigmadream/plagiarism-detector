#pragma once

#include "../include/cpptr/dna_types.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>

namespace cpptr::internal {

struct loaded_configuration {
    std::filesystem::path config_path;
    std::string preprocessor;
    bool source_file_input = true;
    std::string language;
    std::filesystem::path keyword_path;
};

struct configuration_load_result {
    std::optional<loaded_configuration> configuration;
    std::optional<dna_error> error;

    [[nodiscard]] bool ok() const noexcept {
        return configuration.has_value() && !error.has_value();
    }

    [[nodiscard]] static configuration_load_result success(loaded_configuration value) {
        return configuration_load_result{
            std::move(value),
            std::nullopt,
        };
    }

    [[nodiscard]] static configuration_load_result failure(dna_error value) {
        return configuration_load_result{
            std::nullopt,
            std::move(value),
        };
    }
};

class configuration_loader {
public:
    [[nodiscard]] static configuration_load_result load(const std::filesystem::path& explicit_config_path = {});
};

}
