#include "configuration_loader.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cpptr::internal::detail {

std::string trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(begin, end - begin));
}

std::string uppercase(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return value;
}

std::string strip_inline_comment(std::string value) {
    const auto semicolon = value.find(';');
    const auto hash = value.find('#');
    const auto cut = std::min(semicolon, hash);
    if (cut != std::string::npos) {
        value.erase(cut);
    }
    return trim(value);
}

std::unordered_map<std::string, std::string> parse_ini_file(const std::filesystem::path& config_path) {
    std::unordered_map<std::string, std::string> values;
    std::ifstream input(config_path);
    std::string line;

    while (std::getline(input, line)) {
        const auto trimmed = trim(line);
        if (trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';') {
            continue;
        }

        if (trimmed.front() == '[' && trimmed.back() == ']') {
            continue;
        }

        const auto equals = trimmed.find('=');
        if (equals == std::string::npos) {
            continue;
        }

        auto key = trim(trimmed.substr(0, equals));
        auto value = strip_inline_comment(trimmed.substr(equals + 1));
        values[std::move(key)] = std::move(value);
    }

    return values;
}

std::vector<std::filesystem::path> default_resource_roots() {
    std::vector<std::filesystem::path> roots;

#ifdef CPPTR_SOURCE_RESOURCES_DIR
    roots.emplace_back(CPPTR_SOURCE_RESOURCES_DIR);
#endif

#ifdef CPPTR_BUILD_RESOURCES_DIR
    roots.emplace_back(CPPTR_BUILD_RESOURCES_DIR);
#endif

#ifdef CPPTR_INSTALL_RESOURCES_DIR
    roots.emplace_back(CPPTR_INSTALL_RESOURCES_DIR);
#endif

    return roots;
}

std::optional<std::filesystem::path> resolve_existing(const std::filesystem::path& candidate) {
    if (candidate.empty()) {
        return std::nullopt;
    }

    if (std::filesystem::exists(candidate)) {
        return candidate.lexically_normal();
    }

    return std::nullopt;
}

std::optional<std::filesystem::path> resolve_default_config() {
    for (const auto& root : default_resource_roots()) {
        const auto candidate = root / "config" / "cconfig.ini";
        if (const auto resolved = resolve_existing(candidate); resolved.has_value()) {
            return resolved;
        }
    }

    return std::nullopt;
}

std::filesystem::path resolve_keyword_from_ini(const std::filesystem::path& config_path, const std::string& value) {
    std::filesystem::path keyword(value);
    if (keyword.empty() || keyword.is_absolute()) {
        return keyword;
    }

    return (config_path.parent_path() / keyword).lexically_normal();
}

dna_error make_config_error(std::string message, std::filesystem::path path) {
    return dna_error{
        dna_error_code::config_load_failure,
        std::move(message),
        std::move(path),
    };
}

}

namespace cpptr::internal {

configuration_load_result configuration_loader::load(const std::filesystem::path& explicit_config_path) {
    std::filesystem::path config_path;

    if (!explicit_config_path.empty()) {
        const auto resolved = detail::resolve_existing(explicit_config_path);
        if (!resolved.has_value()) {
            return configuration_load_result::failure(detail::make_config_error(
                "explicit config path was provided but file does not exist",
                explicit_config_path));
        }
        config_path = *resolved;
    } else {
        const auto resolved = detail::resolve_default_config();
        if (!resolved.has_value()) {
            return configuration_load_result::failure(detail::make_config_error(
                "default config file was not found in source/build/install resource roots",
                {}));
        }
        config_path = *resolved;
    }

    if (!std::filesystem::is_regular_file(config_path)) {
        return configuration_load_result::failure(detail::make_config_error(
            "config path is not a regular file",
            config_path));
    }

    const auto values = detail::parse_ini_file(config_path);

    loaded_configuration loaded;
    loaded.config_path = config_path;

    if (const auto preprocessor_it = values.find("preprocessor"); preprocessor_it != values.end()) {
        loaded.preprocessor = preprocessor_it->second;
    } else if (const auto legacy_it = values.find("proprecessor"); legacy_it != values.end()) {
        loaded.preprocessor = legacy_it->second;
    }

    if (const auto input_it = values.find("input_file_format"); input_it != values.end()) {
        loaded.source_file_input = detail::uppercase(input_it->second) == "S";
    }

    loaded.language = "CPP";
    if (const auto language_it = values.find("language"); language_it != values.end()) {
        loaded.language = detail::uppercase(language_it->second);
    }
    if (loaded.language != "C" && loaded.language != "CPP") {
        return configuration_load_result::failure(detail::make_config_error(
            "unsupported language; only C and CPP are implemented",
            config_path));
    }

    const char* keyword_key = loaded.language == "C" ? "c_keyword_path" : "cpp_keyword_path";
    const auto keyword_it = values.find(keyword_key);
    if (keyword_it == values.end()) {
        return configuration_load_result::failure(detail::make_config_error(
            "keyword table key is missing from config",
            config_path));
    }

    loaded.keyword_path = detail::resolve_keyword_from_ini(config_path, keyword_it->second);
    if (loaded.keyword_path.empty()) {
        return configuration_load_result::failure(detail::make_config_error(
            "keyword table path is empty",
            config_path));
    }

    if (!std::filesystem::is_regular_file(loaded.keyword_path)) {
        return configuration_load_result::failure(detail::make_config_error(
            "keyword table file does not exist",
            loaded.keyword_path));
    }

    return configuration_load_result::success(std::move(loaded));
}

}
