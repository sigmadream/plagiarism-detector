cmake_minimum_required(VERSION 3.20)

project(cpptr
    VERSION 0.2.0
    DESCRIPTION "Program DNA generator and adaptive local alignment comparator for C/C++ sources"
    LANGUAGES CXX
)

option(CPPTR_BUILD_TESTS "Build the cpptr test executables (fetches googletest)" ON)
option(CPPTR_STATIC_RUNTIME "Link the C++ runtime statically (self-contained release binaries)" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)

include(CompilerWarnings)
include(CpptrResources)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CPPTR_STATIC_RUNTIME)
    if(MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    elseif(NOT APPLE)
        add_link_options(-static-libstdc++ -static-libgcc)
    endif()
endif()

add_library(cpptr_core
    lib/core.cpp
    lib/configuration_loader.cpp
    lib/dna_pipeline.cpp
    lib/alignment_engine.cpp
    lib/dna_loader.cpp
    lib/frequency_service.cpp
    lib/compare_service.cpp
)

target_include_directories(cpptr_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(cpptr_core
    PRIVATE
        CPPTR_SOURCE_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
        CPPTR_BUILD_RESOURCES_DIR="${CMAKE_CURRENT_BINARY_DIR}/resources"
        CPPTR_INSTALL_RESOURCES_DIR="${CMAKE_INSTALL_FULL_DATADIR}/cpptr/resources"
)

cpptr_enable_warnings(cpptr_core)

add_executable(cpptr-cli
    apps/cpptr-cli/main.cpp
)

target_link_libraries(cpptr-cli
    PRIVATE
        cpptr_core
)

target_compile_definitions(cpptr-cli
    PRIVATE
        CPPTR_VERSION="${PROJECT_VERSION}"
)

cpptr_enable_warnings(cpptr-cli)

set_target_properties(cpptr-cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/apps/cpptr-cli
)

cpptr_add_resources_target(cpptr_resources
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/resources
    BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/resources
)

add_dependencies(cpptr-cli cpptr_resources)

# Release layout: <prefix>/bin/cpptr-cli, <prefix>/share/cpptr/resources (installed by
# cpptr_add_resources_target), <prefix>/share/doc/cpptr. The executable resolves the default
# config relative to its own location, so the unpacked archive is relocatable.
install(TARGETS cpptr-cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT runtime
)
install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/../README.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/cpptr
    COMPONENT runtime
)

include(CpptrPackaging)

if(CPPTR_BUILD_TESTS)

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
if(CPPTR_STATIC_RUNTIME)
    set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
else()
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
endif()
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(cpptr_dna_pipeline_test
    tests/dna_pipeline_test.cpp
)

target_link_libraries(cpptr_dna_pipeline_test
    PRIVATE
        cpptr_core
        gtest_main
        gmock
)

cpptr_enable_warnings(cpptr_dna_pipeline_test)

add_test(
    NAME cpptr_dna_pipeline_test
    COMMAND cpptr_dna_pipeline_test
)

add_executable(cpptr_alignment_engine_test
    tests/alignment_engine_test.cpp
)

target_link_libraries(cpptr_alignment_engine_test
    PRIVATE
        cpptr_core
        gtest_main
)

cpptr_enable_warnings(cpptr_alignment_engine_test)

add_test(
    NAME cpptr_alignment_engine_test
    COMMAND cpptr_alignment_engine_test
)

add_executable(cpptr_config_defaults_test
    tests/config_defaults_test.cpp
    tests/support/configuration_loader_probe.cpp
)

target_link_libraries(cpptr_config_defaults_test
    PRIVATE
        cpptr_core
)

add_dependencies(cpptr_config_defaults_test cpptr_resources)

cpptr_enable_warnings(cpptr_config_defaults_test)

add_test(
    NAME cpptr_config_defaults
    COMMAND cpptr_config_defaults_test
)

add_executable(cpptr_config_missing_keyword_test
    tests/config_missing_keyword_test.cpp
    tests/support/configuration_loader_probe.cpp
)

target_link_libraries(cpptr_config_missing_keyword_test
    PRIVATE
        cpptr_core
)

add_dependencies(cpptr_config_missing_keyword_test cpptr_resources)

cpptr_enable_warnings(cpptr_config_missing_keyword_test)

add_test(
    NAME cpptr_config_missing_keyword
    COMMAND cpptr_config_missing_keyword_test
)

add_library(cpptr_public_headers_compile OBJECT
    tests/public_headers_compile_core.cpp
    tests/public_headers_compile_dna_types.cpp
    tests/public_headers_compile_dna_service.cpp
)

target_include_directories(cpptr_public_headers_compile
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

cpptr_enable_warnings(cpptr_public_headers_compile)

add_test(
    NAME cpptr_public_headers_compile
    COMMAND ${CMAKE_COMMAND} --build . --target cpptr_public_headers_compile
)

set_tests_properties(cpptr_public_headers_compile PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_executable(cpptr_generate_dna_smoke_test
    tests/generate_dna_smoke.cpp
)

target_link_libraries(cpptr_generate_dna_smoke_test
    PRIVATE
        cpptr_core
)

add_dependencies(cpptr_generate_dna_smoke_test cpptr_resources)

cpptr_enable_warnings(cpptr_generate_dna_smoke_test)

add_test(
    NAME cpptr_generate_dna_smoke
    COMMAND cpptr_generate_dna_smoke_test
)

add_executable(cpptr_generate_dna_invalid_config_test
    tests/generate_dna_invalid_config.cpp
)

target_link_libraries(cpptr_generate_dna_invalid_config_test
    PRIVATE
        cpptr_core
)

add_dependencies(cpptr_generate_dna_invalid_config_test cpptr_resources)

cpptr_enable_warnings(cpptr_generate_dna_invalid_config_test)

add_test(
    NAME cpptr_generate_dna_invalid_config
    COMMAND cpptr_generate_dna_invalid_config_test
)

add_executable(cpptr_generate_c_corpus_smoke_test
    tests/generate_c_corpus_smoke.cpp
)

target_link_libraries(cpptr_generate_c_corpus_smoke_test
    PRIVATE
        cpptr_core
)

add_dependencies(cpptr_generate_c_corpus_smoke_test cpptr_resources)

cpptr_enable_warnings(cpptr_generate_c_corpus_smoke_test)

add_test(
    NAME cpptr_generate_c_corpus_smoke
    COMMAND cpptr_generate_c_corpus_smoke_test
)

add_executable(cpptr_cli_smoke_test
    tests/cli_smoke_test.cpp
)

target_link_libraries(cpptr_cli_smoke_test
    PRIVATE
        cpptr_core
)

add_dependencies(cpptr_cli_smoke_test cpptr_resources cpptr-cli)

cpptr_enable_warnings(cpptr_cli_smoke_test)

add_test(
    NAME cpptr_cli_smoke
    COMMAND cpptr_cli_smoke_test $<TARGET_FILE:cpptr-cli>
)

add_executable(cpptr_cli_similarity_test
    tests/cli_similarity_test.cpp
)

target_link_libraries(cpptr_cli_similarity_test
    PRIVATE
        cpptr_core
)

add_dependencies(cpptr_cli_similarity_test cpptr_resources cpptr-cli)

cpptr_enable_warnings(cpptr_cli_similarity_test)

add_test(
    NAME cpptr_cli_similarity
    COMMAND cpptr_cli_similarity_test $<TARGET_FILE:cpptr-cli>
)

add_executable(cpptr_cli_usage_error_test
    tests/cli_usage_error_test.cpp
)

target_link_libraries(cpptr_cli_usage_error_test
    PRIVATE
        cpptr_core
)

add_dependencies(cpptr_cli_usage_error_test cpptr_resources cpptr-cli)

cpptr_enable_warnings(cpptr_cli_usage_error_test)

add_test(
    NAME cpptr_cli_usage_error
    COMMAND cpptr_cli_usage_error_test $<TARGET_FILE:cpptr-cli>
)

add_executable(cpptr_compare_csv_test
    tests/compare_csv_test.cpp
)

target_link_libraries(cpptr_compare_csv_test
    PRIVATE
        cpptr_core
)

add_dependencies(cpptr_compare_csv_test cpptr_resources cpptr-cli)

cpptr_enable_warnings(cpptr_compare_csv_test)

add_test(
    NAME cpptr_compare_csv
    COMMAND cpptr_compare_csv_test $<TARGET_FILE:cpptr-cli>
)

add_executable(cpptr_benchmark_cli_test
    tests/benchmark_cli_test.cpp
)

target_link_libraries(cpptr_benchmark_cli_test
    PRIVATE
        cpptr_core
)

add_dependencies(cpptr_benchmark_cli_test cpptr_resources cpptr-cli)

cpptr_enable_warnings(cpptr_benchmark_cli_test)

add_test(
    NAME cpptr_benchmark_cli
    COMMAND cpptr_benchmark_cli_test $<TARGET_FILE:cpptr-cli>
)

endif() # CPPTR_BUILD_TESTS
