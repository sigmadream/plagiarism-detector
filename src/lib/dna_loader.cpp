#include "../include/cpptr/dna_loader.hpp"
#include <fstream>
#include <sstream>

namespace cpptr {

std::vector<int> dna_loader::load_sequence(const std::filesystem::path& dna_path) {
    std::ifstream input(dna_path);
    if (!input.is_open()) return {};

    std::vector<int> sequence;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '%') continue;

        std::istringstream iss(line);
        std::string token;
        if (iss >> token) {
            sequence.push_back(get_or_create_id(token));
        }
    }
    return sequence;
}

int dna_loader::get_or_create_id(const std::string& token_name) {
    auto it = name_to_id_.find(token_name);
    if (it != name_to_id_.end()) {
        return it->second;
    }

    int id = next_id_++;
    name_to_id_[token_name] = id;
    id_to_name_[id] = token_name;
    return id;
}

const std::unordered_map<std::string, int>& dna_loader::get_name_to_id() const {
    return name_to_id_;
}

const std::unordered_map<int, std::string>& dna_loader::get_id_to_name() const {
    return id_to_name_;
}

} // namespace cpptr
