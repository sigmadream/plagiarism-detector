#include "../include/cpptr/dna_service.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::string content{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    content.erase(std::remove(content.begin(), content.end(), '\r'), content.end());
    return content;
}

void write_file(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary);
    output << contents;
}

}

int main() {
    namespace fs = std::filesystem;

    const fs::path source_dir = fs::path(__FILE__).parent_path().parent_path().lexically_normal();
    const fs::path fixture_dir = (source_dir / "tests" / "fixtures" / "source_corpus" / "cpp").lexically_normal();
    const fs::path config_path = (source_dir / "resources" / "config" / "cconfig.ini").lexically_normal();
    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path output_dir =
        (fs::temp_directory_path() / ("cpptr_generate_dna_smoke_" + std::to_string(unique_suffix))).lexically_normal();
    fs::create_directories(output_dir);

    auto service = cpptr::make_dna_generation_service();
    const std::vector<std::string> fixtures{
        "201213119.cpp",
        "201224507.cpp",
    };

    for (std::size_t index = 0; index < fixtures.size(); ++index) {
        const fs::path source_path = (fixture_dir / fixtures[index]).lexically_normal();
        const cpptr::dna_request request{
            config_path,
            source_path,
            output_dir,
        };

        const auto result = service->generate(request);
        if (!result.ok()) {
            return static_cast<int>(10 + index);
        }

        if (result.output_path.filename() != fixtures[index] + ".DNA") {
            return static_cast<int>(20 + index);
        }

        if (!fs::exists(result.output_path)) {
            return static_cast<int>(30 + index);
        }

        const fs::path snapshot_path = result.output_path.string() + ".src";
        if (!fs::is_regular_file(snapshot_path) || read_file(snapshot_path) != read_file(source_path)) {
            return static_cast<int>(40 + index);
        }
    }

    const fs::path comment_source_path = (output_dir / "block_comment_case.c").lexically_normal();
    const fs::path comment_expected_path = (output_dir / "block_comment_case.c.DNA.expected").lexically_normal();

    write_file(
        comment_source_path,
        "int main() {\n"
        "    /* if else return { } % & == = */\n"
        "    if (x % 2 == 0) {\n"
        "        return 1;\n"
        "    }\n"
        "}\n");
    write_file(
        comment_expected_path,
        "INT\t1\t0\n"
        "BLOCK_START\t1\t11\n"
        "IF\t3\t4\n"
        "MOD\t3\t10\n"
        "EQ\t3\t14\n"
        "BLOCK_START\t3\t20\n"
        "RETURN\t4\t8\n"
        "BLOCK_END\t5\t4\n"
        "BLOCK_END\t6\t0\n");

    const cpptr::dna_request comment_request{
        config_path,
        comment_source_path,
        output_dir,
    };
    const auto comment_result = service->generate(comment_request);
    if (!comment_result.ok()) {
        return 100;
    }

    if (comment_result.output_path.filename() != "block_comment_case.c.DNA") {
        return 101;
    }

    if (read_file(comment_result.output_path) != read_file(comment_expected_path)) {
        return 102;
    }

    const fs::path preprocessor_comment_path =
        (output_dir / "preprocessor_comment_case.cpp").lexically_normal();
    write_file(
        preprocessor_comment_path,
        "/*#include <iostream>\n"
        "#include <vector>*/\n"
        "int main() { return 0; }\n");
    const cpptr::dna_request preprocessor_comment_request{
        config_path,
        preprocessor_comment_path,
        output_dir,
    };
    const auto preprocessor_comment_result = service->generate(preprocessor_comment_request);
    if (!preprocessor_comment_result.ok()) {
        return 110;
    }
    if (read_file(preprocessor_comment_result.output_path).find("INT\t3\t0") == std::string::npos) {
        return 111;
    }

    // Classic Mac line endings (bare CR) must still be split into lines.
    const fs::path cr_only_path = (output_dir / "cr_only_case.cpp").lexically_normal();
    write_file(
        cr_only_path,
        "#include <iostream>\r"
        "int main() {\r"
        "    return 0;\r"
        "}\r");
    const cpptr::dna_request cr_only_request{
        config_path,
        cr_only_path,
        output_dir,
    };
    const auto cr_only_result = service->generate(cr_only_request);
    if (!cr_only_result.ok()) {
        return 120;
    }
    if (read_file(cr_only_result.output_path) != "INT\t2\t0\nBLOCK_START\t2\t11\nRETURN\t3\t4\nBLOCK_END\t4\t0\n") {
        return 121;
    }

    return 0;
}
