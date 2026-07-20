#pragma once

#include "../include/cpptr/dna_service.hpp"
#include "configuration_loader.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace cpptr::internal {

class dna_event {
public:
    dna_event(std::string token, std::size_t line, std::size_t column)
        : token_(std::move(token)), line_(line), column_(column) {}

    [[nodiscard]] const std::string& token() const noexcept { return token_; }
    [[nodiscard]] std::size_t line() const noexcept { return line_; }
    [[nodiscard]] std::size_t column() const noexcept { return column_; }

private:
    std::string token_;
    std::size_t line_;
    std::size_t column_;
};

class file_system_port {
public:
    virtual ~file_system_port() = default;
    virtual std::optional<std::vector<std::string>> read_source_lines(const std::filesystem::path& source_path, cpptr::dna_error& error) = 0;
    virtual std::optional<std::unordered_set<std::string>> load_keyword_names(const std::filesystem::path& keyword_path, cpptr::dna_error& error) = 0;
    virtual cpptr::dna_result write_output(const std::filesystem::path& source_path, const std::filesystem::path& dna_directory, const std::vector<dna_event>& events) = 0;
};

class config_loader_port {
public:
    virtual ~config_loader_port() = default;
    virtual configuration_load_result load(const std::filesystem::path& config_path) = 0;
};

class dna_pipeline_service : public cpptr::dna_generation_service {
public:
    dna_pipeline_service(
        std::unique_ptr<config_loader_port> config_loader,
        std::unique_ptr<file_system_port> file_system);

    [[nodiscard]] cpptr::dna_result generate(const cpptr::dna_request& request) override;

private:
    std::unique_ptr<config_loader_port> config_loader_;
    std::unique_ptr<file_system_port> file_system_;
    std::filesystem::path cached_config_path_;
    std::optional<loaded_configuration> cached_configuration_;
    std::optional<std::unordered_set<std::string>> cached_keyword_names_;
};

[[nodiscard]] std::unique_ptr<dna_generation_service> make_dna_generation_service();

}
