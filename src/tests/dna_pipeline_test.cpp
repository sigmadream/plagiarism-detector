#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "../lib/dna_pipeline.hpp"
#include "../include/cpptr/dna_service.hpp"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;

namespace cpptr::internal {
namespace {

class MockFileSystemPort : public file_system_port {
public:
    MOCK_METHOD(std::optional<std::vector<std::string>>, read_source_lines, (const std::filesystem::path&, cpptr::dna_error&), (override));
    MOCK_METHOD(std::optional<std::unordered_set<std::string>>, load_keyword_names, (const std::filesystem::path&, cpptr::dna_error&), (override));
    MOCK_METHOD(cpptr::dna_result, write_output, (const std::filesystem::path&, const std::filesystem::path&, const std::vector<dna_event>&), (override));
};

class MockConfigLoaderPort : public config_loader_port {
public:
    MOCK_METHOD(configuration_load_result, load, (const std::filesystem::path&), (override));
};

class DnaPipelineTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_config_ptr = new MockConfigLoaderPort();
        mock_fs_ptr = new MockFileSystemPort();

        service = std::make_unique<dna_pipeline_service>(
            std::unique_ptr<config_loader_port>(mock_config_ptr),
            std::unique_ptr<file_system_port>(mock_fs_ptr)
        );

        req.config_path = "test_config.ini";
        req.source_path = "test_source.cpp";
        req.dna_directory = "test_out";

        loaded.config_path = "test_config.ini";
        loaded.keyword_path = "test_keyword.tbl";
    }

    MockConfigLoaderPort* mock_config_ptr;
    MockFileSystemPort* mock_fs_ptr;
    std::unique_ptr<dna_pipeline_service> service;
    cpptr::dna_request req;
    loaded_configuration loaded;
};

TEST_F(DnaPipelineTest, GeneratesSuccessfully) {
    EXPECT_CALL(*mock_config_ptr, load(_)).WillOnce(Return(configuration_load_result::success(loaded)));
    EXPECT_CALL(*mock_fs_ptr, read_source_lines(_, _)).WillOnce(Return(std::vector<std::string>{"int x = 0;"}));
    EXPECT_CALL(*mock_fs_ptr, load_keyword_names(_, _)).WillOnce(Return(std::unordered_set<std::string>{"INT"}));
    EXPECT_CALL(*mock_fs_ptr, write_output(_, _, _)).WillOnce(Return(cpptr::dna_result::success("out.DNA")));

    auto result = service->generate(req);
    EXPECT_TRUE(result.ok());
}

TEST_F(DnaPipelineTest, ReusesConfigurationAndKeywordsAcrossBatchRequests) {
    EXPECT_CALL(*mock_config_ptr, load(_))
        .Times(1)
        .WillOnce(Return(configuration_load_result::success(loaded)));
    EXPECT_CALL(*mock_fs_ptr, load_keyword_names(_, _))
        .Times(1)
        .WillOnce(Return(std::unordered_set<std::string>{"INT"}));
    EXPECT_CALL(*mock_fs_ptr, read_source_lines(_, _))
        .Times(2)
        .WillRepeatedly(Return(std::vector<std::string>{"int x = 0;"}));
    EXPECT_CALL(*mock_fs_ptr, write_output(_, _, _))
        .Times(2)
        .WillRepeatedly(Return(cpptr::dna_result::success("out.DNA")));

    EXPECT_TRUE(service->generate(req).ok());
    req.source_path = "second_source.cpp";
    EXPECT_TRUE(service->generate(req).ok());
}

TEST_F(DnaPipelineTest, FailsWhenConfigLoadFails) {
    cpptr::dna_error err{cpptr::dna_error_code::config_load_failure, "failed", "path"};
    EXPECT_CALL(*mock_config_ptr, load(_)).WillOnce(Return(configuration_load_result::failure(err)));

    auto result = service->generate(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error->code, cpptr::dna_error_code::config_load_failure);
}

TEST_F(DnaPipelineTest, FailsWhenSourceReadFails) {
    EXPECT_CALL(*mock_config_ptr, load(_)).WillOnce(Return(configuration_load_result::success(loaded)));
    EXPECT_CALL(*mock_fs_ptr, read_source_lines(_, _)).WillOnce(Invoke([](const auto&, auto& err) {
        err = {cpptr::dna_error_code::preprocessing_failure, "read failed", "src"};
        return std::nullopt;
    }));

    auto result = service->generate(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error->code, cpptr::dna_error_code::preprocessing_failure);
}

TEST_F(DnaPipelineTest, FailsWhenKeywordTableReadFails) {
    EXPECT_CALL(*mock_config_ptr, load(_)).WillOnce(Return(configuration_load_result::success(loaded)));
    EXPECT_CALL(*mock_fs_ptr, read_source_lines(_, _)).WillOnce(Return(std::vector<std::string>{"int x;"}));
    EXPECT_CALL(*mock_fs_ptr, load_keyword_names(_, _)).WillOnce(Invoke([](const auto&, auto& err) {
        err = {cpptr::dna_error_code::config_load_failure, "read failed", "keywords"};
        return std::nullopt;
    }));

    auto result = service->generate(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error->code, cpptr::dna_error_code::config_load_failure);
}

TEST_F(DnaPipelineTest, FailsWhenNoTokensExtracted) {
    EXPECT_CALL(*mock_config_ptr, load(_)).WillOnce(Return(configuration_load_result::success(loaded)));
    EXPECT_CALL(*mock_fs_ptr, read_source_lines(_, _)).WillOnce(Return(std::vector<std::string>{"// only comment"}));
    EXPECT_CALL(*mock_fs_ptr, load_keyword_names(_, _)).WillOnce(Return(std::unordered_set<std::string>{"INT"}));

    auto result = service->generate(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error->code, cpptr::dna_error_code::parse_failure);
}

// ---------------------------------------------------------------------------
// Scanner coverage: the full keyword table and static call tracing
// ---------------------------------------------------------------------------

using Tokens = std::vector<std::string>;

class DnaScannerTest : public ::testing::Test {
protected:
    // Runs the pipeline on the given source lines and returns the emitted DNA token names.
    Tokens scan(std::vector<std::string> lines, std::unordered_set<std::string> table = full_table()) {
        auto* config = new MockConfigLoaderPort();
        auto* fs = new MockFileSystemPort();
        dna_pipeline_service service{
            std::unique_ptr<config_loader_port>(config),
            std::unique_ptr<file_system_port>(fs)};

        loaded_configuration loaded;
        loaded.config_path = "test_config.ini";
        loaded.keyword_path = "test_keyword.tbl";

        Tokens tokens;
        EXPECT_CALL(*config, load(_)).WillOnce(Return(configuration_load_result::success(loaded)));
        EXPECT_CALL(*fs, read_source_lines(_, _)).WillOnce(Return(std::move(lines)));
        EXPECT_CALL(*fs, load_keyword_names(_, _)).WillOnce(Return(std::move(table)));
        EXPECT_CALL(*fs, write_output(_, _, _)).WillOnce(Invoke(
            [&](const auto&, const auto&, const std::vector<dna_event>& events) {
                for (const auto& event : events) tokens.push_back(event.token());
                return cpptr::dna_result::success("out.DNA");
            }));

        cpptr::dna_request req;
        req.config_path = "test_config.ini";
        req.source_path = "test_source.cpp";
        req.dna_directory = "test_out";
        EXPECT_TRUE(service.generate(req).ok());
        return tokens;
    }

    static std::unordered_set<std::string> full_table() {
        return {
            "ASSIGN_EQ", "TIMES_EQ", "DIV_EQ", "MOD_EQ", "PLUS_EQ", "MINUS_EQ", "SHL_EQ", "SHR_EQ",
            "BITAND_EQ", "BITXOR_EQ", "BITOR_EQ", "OR", "AND", "BIT_OR", "BIT_XOR", "BIT_AND",
            "NOT_EQ", "EQ", "LT", "GT", "LT_EQ", "GT_EQ", "SHL", "SHR", "PLUS", "MINUS", "MULTIPLE",
            "DIVIDE", "MOD", "INCR", "DECR", "NOT", "VOID", "INT", "CHAR", "BOOL", "SHORT", "FLOAT",
            "DOUBLE", "SIGNED", "LONG", "UNSIGNED", "DO", "TRUE", "FALSE", "STRUCT", "STATIC",
            "CONTINUE", "SWITCH", "CASE", "DEFAULT", "BREAK", "RETURN", "GOTO", "NULL", "TYPEDEF",
            "SIZEOF", "CONST", "VOLATILE", "INLINE", "PUBLIC", "PRIVATE", "FRIEND", "PROTECTED", "NEW",
            "DELETE", "VIRTUAL", "TRY", "CATCH", "THROW", "CLASS", "NAMESPACE", "IF", "ELSE", "FOR",
            "WHILE", "BLOCK_START", "BLOCK_END", "FUNC_CALL", "FUNC_END", "UNTRACKABLE_FUNC_CALL",
        };
    }
};

TEST_F(DnaScannerTest, EmitsControlKeywordsAndOperators) {
    const auto tokens = scan({
        "int main() {",
        "    for (int i = 0; i < n; i++) { sum += a[i] * 2; }",
        "    while (x != y && !done || flag) { x <<= 1; y >>= 1; }",
        "    switch (k) { case 1: break; default: continue; }",
        "    do { x--; } while (x >= 0);",
        "    return x % 3 == 0 ? 1 : 0;",
        "}",
    });
    const Tokens expected{
        "INT", "BLOCK_START",
        "FOR", "INT", "ASSIGN_EQ", "LT", "INCR", "BLOCK_START", "PLUS_EQ", "MULTIPLE", "BLOCK_END",
        "WHILE", "NOT_EQ", "AND", "NOT", "OR", "BLOCK_START", "SHL_EQ", "SHR_EQ", "BLOCK_END",
        "SWITCH", "BLOCK_START", "CASE", "BREAK", "DEFAULT", "CONTINUE", "BLOCK_END",
        "DO", "BLOCK_START", "DECR", "BLOCK_END", "WHILE", "GT_EQ",
        "RETURN", "MOD", "EQ",
        "BLOCK_END",
    };
    EXPECT_EQ(tokens, expected);
}

TEST_F(DnaScannerTest, EmitsTypeAndClassKeywords) {
    const auto tokens = scan({
        "struct Node { unsigned long value; Node* next; };",
        "class Stack {",
        "public:",
        "    virtual ~Stack() {}",
        "private:",
        "    static const double ratio;",
        "};",
        "typedef char byte;",
        "namespace app { bool ok = true; float f = 1.5e-3; }",
    });
    const Tokens expected{
        "STRUCT", "BLOCK_START", "UNSIGNED", "LONG", "MULTIPLE", "BLOCK_END",
        "CLASS", "BLOCK_START", "PUBLIC", "VIRTUAL", "BLOCK_START", "BLOCK_END",
        "PRIVATE", "STATIC", "CONST", "DOUBLE", "BLOCK_END",
        "TYPEDEF", "CHAR",
        "NAMESPACE", "BLOCK_START", "BOOL", "ASSIGN_EQ", "TRUE", "FLOAT", "ASSIGN_EQ", "BLOCK_END",
    };
    EXPECT_EQ(tokens, expected);
}

TEST_F(DnaScannerTest, IgnoresStringsCommentsAndPreprocessorLines) {
    const auto tokens = scan({
        "#include <cstdio>",
        "#define SQUARE(x) ((x) * (x))",
        "int main() {",
        "    printf(\"if (a % b == 0) { return; }\"); // if else while",
        "    char c = '{'; /* for (;;) */",
        "    return 0;",
        "}",
    });
    const Tokens expected{"INT", "BLOCK_START", "UNTRACKABLE_FUNC_CALL", "CHAR", "ASSIGN_EQ", "RETURN", "BLOCK_END"};
    EXPECT_EQ(tokens, expected);
}

TEST_F(DnaScannerTest, InlinesUserDefinedFunctionsAtCallSites) {
    const auto tokens = scan({
        "int add(int a, int b) { return a + b; }",
        "int main() {",
        "    int r = add(1, 2);",
        "    printf(\"%d\", r);",
        "    return 0;",
        "}",
    });
    const Tokens expected{
        "INT", "BLOCK_START",
        "INT", "ASSIGN_EQ", "FUNC_CALL",
        "INT", "INT", "INT", "BLOCK_START", "RETURN", "PLUS", "BLOCK_END",
        "FUNC_END",
        "UNTRACKABLE_FUNC_CALL",
        "RETURN", "BLOCK_END",
    };
    EXPECT_EQ(tokens, expected);
}

TEST_F(DnaScannerTest, StopsInliningOnRecursion) {
    const auto tokens = scan({
        "int fact(int n) {",
        "    if (n <= 1) return 1;",
        "    return n * fact(n - 1);",
        "}",
        "int main() { return fact(5); }",
    });
    const Tokens expected{
        "INT", "BLOCK_START", "RETURN", "FUNC_CALL",
        "INT", "INT", "BLOCK_START",
        "IF", "LT_EQ", "RETURN",
        "RETURN", "MULTIPLE", "FUNC_CALL", "MINUS", "FUNC_END",
        "BLOCK_END",
        "FUNC_END", "BLOCK_END",
    };
    EXPECT_EQ(tokens, expected);
}

TEST_F(DnaScannerTest, EmitsUncalledFunctionsInPlaceAndSkipsPrototypes) {
    const auto tokens = scan({
        "int helper(int);",
        "int main() { return 0; }",
        "int helper(int x) { return x; }",
    });
    const Tokens expected{
        "INT", "BLOCK_START", "RETURN", "BLOCK_END",
        "INT", "INT", "BLOCK_START", "RETURN", "BLOCK_END",
    };
    EXPECT_EQ(tokens, expected);
}

TEST_F(DnaScannerTest, TracesMethodCallsAndConstructorInitializers) {
    const auto tokens = scan({
        "class Counter {",
        "public:",
        "    Counter(int start) : value(start) {}",
        "    int next() { return ++value; }",
        "    int peek() const;",
        "private:",
        "    int value;",
        "};",
        "int main() {",
        "    Counter c(0);",
        "    return c.next();",
        "}",
    });
    const Tokens expected{
        "CLASS", "BLOCK_START",
        "PUBLIC", "INT", "BLOCK_START", "BLOCK_END",
        "PRIVATE", "INT",
        "BLOCK_END",
        "INT", "BLOCK_START",
        "RETURN", "FUNC_CALL", "INT", "BLOCK_START", "RETURN", "INCR", "BLOCK_END", "FUNC_END",
        "BLOCK_END",
    };
    EXPECT_EQ(tokens, expected);
}

TEST_F(DnaScannerTest, RespectsKeywordTableFilter) {
    const auto tokens = scan({"int main() { for (;;) { return 0; } }"}, {"FOR", "RETURN"});
    EXPECT_EQ(tokens, (Tokens{"FOR", "RETURN"}));
}

} // namespace
} // namespace cpptr::internal