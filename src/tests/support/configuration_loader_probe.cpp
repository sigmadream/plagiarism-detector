#include "configuration_loader_probe.hpp"

#include "../../lib/configuration_loader.hpp"

namespace cpptr::tests::support {

configuration_probe_result load_configuration(const std::filesystem::path& explicit_config_path) {
    const auto loaded = cpptr::internal::configuration_loader::load(explicit_config_path);

    configuration_probe_result result;
    if (!loaded.ok()) {
        result.error = loaded.error;
        return result;
    }

    result.config_path = loaded.configuration->config_path;
    result.keyword_path = loaded.configuration->keyword_path;
    return result;
}

}
