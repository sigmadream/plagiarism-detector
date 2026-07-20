#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "../lib/dna_pipeline.hpp"
#include "../include/cpptr/dna_service.hpp"

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

} // namespace
} // namespace cpptr::internal