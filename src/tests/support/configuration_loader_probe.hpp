#pragma once

#include "../../include/cpptr/dna_types.hpp"

#include <filesystem>
#include <optional>

namespace cpptr::tests::support {

struct configuration_probe_result {
    std::filesystem::path config_path;
    std::filesystem::path keyword_path;
    std::optional<dna_error> error;

    [[nodiscard]] bool ok() const noexcept {
        return !error.has_value();
    }
};

[[nodiscard]] configuration_probe_result load_configuration(
    const std::filesystem::path& explicit_config_path = {});

}
