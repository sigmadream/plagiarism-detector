#include "dna_pipeline.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "configuration_loader.hpp"

namespace cpptr::internal {

namespace {

struct scan_result {
    std::vector<dna_event> events;
    std::optional<cpptr::dna_error> error;
};

bool is_identifier_char(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

std::string trim(std::string_view value) {
    const auto begin = value.find_first_not_of(" \t\n\r\f\v");
    if (begin == std::string_view::npos) return "";
    const auto end = value.find_last_not_of(" \t\n\r\f\v");
    return std::string(value.substr(begin, end - begin + 1));
}

cpptr::dna_error make_error(
    cpptr::dna_error_code code,
    std::string message,
    std::filesystem::path path) {
    return cpptr::dna_error{
        code,
        std::move(message),
        std::move(path),
    };
}

std::optional<std::vector<std::string>> read_source_lines(
    const std::filesystem::path& source_path,
    cpptr::dna_error& error) {
    std::ifstream input(source_path);
    if (!input.is_open()) {
        error = make_error(
            cpptr::dna_error_code::preprocessing_failure,
            "source file could not be opened for DNA generation",
            source_path);
        return std::nullopt;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }

    return lines;
}

std::optional<std::unordered_set<std::string>> load_keyword_names(
    const std::filesystem::path& keyword_path,
    cpptr::dna_error& error) {
    std::ifstream input(keyword_path);
    if (!input.is_open()) {
        error = make_error(
            cpptr::dna_error_code::config_load_failure,
            "keyword table could not be opened",
            keyword_path);
        return std::nullopt;
    }

    std::unordered_set<std::string> names;
    for (std::string line, name; std::getline(input, line);) {
        if (std::istringstream(trim(line)) >> name) names.insert(name);
    }

    if (names.empty()) {
        error = make_error(
            cpptr::dna_error_code::config_load_failure,
            "keyword table did not contain any token names",
            keyword_path);
        return std::nullopt;
    }
    return names;
}

std::string strip_literals_and_comments(std::string_view line, bool& in_block_comment) {
    std::string masked(line);
    bool in_string = false, in_char = false, escaped = false;
    for (std::size_t index = 0; index < masked.size(); ++index) {
        char& ch = masked[index];
        const char next = index + 1 < masked.size() ? masked[index + 1] : '\0';
        if (in_block_comment) {
            if (ch == '*' && next == '/') { in_block_comment = false; masked[index++] = ' '; }
            ch = ' '; continue;
        }
        if (in_string || in_char) {
            if (!escaped && ch == (in_string ? '"' : '\'')) in_string = in_char = false;
            escaped = !escaped && ch == '\\'; ch = ' '; continue;
        }
        escaped = false;
        if (ch == '/' && next == '/') { std::fill(masked.begin() + index, masked.end(), ' '); break; }
        if (ch == '/' && next == '*') { in_block_comment = true; ch = masked[index + 1] = ' '; ++index; continue; }
        if (ch == '"' || ch == '\'') { (ch == '"' ? in_string : in_char) = true; ch = ' '; continue; }
    }
    return masked;
}

void add_word_events(
    std::vector<dna_event>& events,
    std::string_view line,
    std::size_t output_line,
    const std::unordered_set<std::string>& keyword_names,
    std::string_view needle,
    std::string_view token_name) {
    if (!keyword_names.contains(std::string(token_name))) return;

    std::size_t pos = line.find(needle);
    while (pos != std::string_view::npos) {
        if ((pos == 0 || !is_identifier_char(line[pos - 1])) &&
            (pos + needle.size() >= line.size() || !is_identifier_char(line[pos + needle.size()]))) {
            events.emplace_back(std::string(token_name), output_line, pos);
        }
        pos = line.find(needle, pos + needle.size());
    }
}

void add_symbol_events(std::vector<dna_event>& events, std::string_view line, std::size_t out, const std::unordered_set<std::string>& keys) {
    for (std::size_t i = 0; i < line.size(); ++i) {
        char ch = line[i], p = i ? line[i - 1] : 0, n = i + 1 < line.size() ? line[i + 1] : 0;
        auto add = [&](const char* t) { if (keys.contains(t)) events.emplace_back(t, out, i); };
        if (ch == '{') add("BLOCK_START");
        else if (ch == '}') add("BLOCK_END");
        else if (ch == '%' && n != '=') add("MOD");
        else if (ch == '&' && n != '&' && n != '=') add("BIT_AND");
        else if (ch == '=' && n == '=') { add("EQ"); ++i; }
        else if (ch == '=' && n != '=' && !std::string_view("=!<>=+-*/%&|^").contains(p)) add("ASSIGN_EQ");
    }
}

scan_result scan_source(
    const std::vector<std::string>& lines,
    const std::unordered_set<std::string>& keyword_names,
    const std::filesystem::path& source_path) {
    scan_result result;
    auto it = std::find_if(lines.begin(), lines.end(), [](const auto& l) {
        auto c = trim(l); return !c.empty() && c[0] != '#';
    });
    const auto base = it != lines.end() ? std::distance(lines.begin(), it) : 0;
    bool in_comment = false;

    for (std::size_t i = base; i < lines.size(); ++i) {
        if (auto c = trim(lines[i]); !c.empty() && c[0] == '#') continue;
        auto masked = strip_literals_and_comments(lines[i], in_comment);
        auto out = i - base;
        for (auto [n, t] : {std::pair{"int", "INT"}, {"if", "IF"}, {"else", "ELSE"}, {"return", "RETURN"}})
            add_word_events(result.events, masked, out, keyword_names, n, t);
        add_symbol_events(result.events, masked, out, keyword_names);
    }

    std::stable_sort(result.events.begin(), result.events.end(), [](const auto& a, const auto& b) {
        if (a.line() != b.line()) return a.line() < b.line();
        if (a.column() != b.column()) return a.column() < b.column();
        return a.token() < b.token();
    });

    if (result.events.empty()) {
        result.error = make_error(
            cpptr::dna_error_code::parse_failure,
            "no DNA tokens were extracted from the source file",
            source_path);
    }

    return result;
}

cpptr::dna_result write_output(
    const std::filesystem::path& source_path,
    const std::filesystem::path& dna_directory,
    const std::vector<dna_event>& events) {
    std::error_code ec; std::filesystem::create_directories(dna_directory, ec);
    if (ec) return cpptr::dna_result::failure(make_error(cpptr::dna_error_code::write_failure, "failed to create DNA output directory", dna_directory));
    const auto out_path = (dna_directory / (source_path.filename().string() + ".DNA")).lexically_normal();
    std::ofstream out(out_path, std::ios::binary);
    if (!out) return cpptr::dna_result::failure(make_error(cpptr::dna_error_code::write_failure, "failed to open DNA output file", out_path));
    for (const auto& e : events) out << e.token() << '\t' << e.line() << '\t' << e.column() << '\n';
    if (!out) return cpptr::dna_result::failure(make_error(cpptr::dna_error_code::write_failure, "failed while writing DNA output file", out_path));

    const auto snapshot_path = std::filesystem::path(out_path.string() + ".src");
    std::filesystem::copy_file(source_path, snapshot_path, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return cpptr::dna_result::failure(make_error(cpptr::dna_error_code::write_failure, "failed to preserve source snapshot", snapshot_path));

    return cpptr::dna_result::success(out_path);
}

class default_file_system_port : public file_system_port {
public:
    std::optional<std::vector<std::string>> read_source_lines(const std::filesystem::path& source_path, cpptr::dna_error& error) override {
        return cpptr::internal::read_source_lines(source_path, error);
    }

    std::optional<std::unordered_set<std::string>> load_keyword_names(const std::filesystem::path& keyword_path, cpptr::dna_error& error) override {
        return cpptr::internal::load_keyword_names(keyword_path, error);
    }

    cpptr::dna_result write_output(const std::filesystem::path& source_path, const std::filesystem::path& dna_directory, const std::vector<dna_event>& events) override {
        return cpptr::internal::write_output(source_path, dna_directory, events);
    }
};

class default_config_loader_port : public config_loader_port {
public:
    configuration_load_result load(const std::filesystem::path& config_path) override {
        return cpptr::internal::configuration_loader::load(config_path);
    }
};

} // namespace

dna_pipeline_service::dna_pipeline_service(
    std::unique_ptr<config_loader_port> config_loader,
    std::unique_ptr<file_system_port> file_system)
    : config_loader_(std::move(config_loader)),
      file_system_(std::move(file_system)) {}

cpptr::dna_result dna_pipeline_service::generate(const cpptr::dna_request& request) {
    const auto loaded = config_loader_->load(request.config_path);
    if (!loaded.ok()) return cpptr::dna_result::failure(*loaded.error);

    cpptr::dna_error io_err, kw_err;
    const auto src = file_system_->read_source_lines(request.source_path, io_err);
    if (!src) return cpptr::dna_result::failure(std::move(io_err));

    const auto knames = file_system_->load_keyword_names(loaded.configuration->keyword_path, kw_err);
    if (!knames) return cpptr::dna_result::failure(std::move(kw_err));

    const auto scanned = scan_source(*src, *knames, request.source_path);
    return scanned.error ? cpptr::dna_result::failure(*scanned.error) : file_system_->write_output(request.source_path, request.dna_directory, scanned.events);
}

std::unique_ptr<dna_generation_service> make_dna_generation_service() {
    return std::make_unique<dna_pipeline_service>(
        std::make_unique<default_config_loader_port>(),
        std::make_unique<default_file_system_port>()
    );
}

}

namespace cpptr {

std::unique_ptr<dna_generation_service> make_dna_generation_service() {
    return internal::make_dna_generation_service();
}

}
