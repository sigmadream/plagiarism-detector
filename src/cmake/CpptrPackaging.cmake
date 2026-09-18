# CPack configuration for relocatable CLI archives.
#
#   cmake -S src -B build/release -DCMAKE_BUILD_TYPE=Release \
#         -DCPPTR_BUILD_TESTS=OFF -DCPPTR_STATIC_RUNTIME=ON
#   cmake --build build/release --config Release
#   cpack --config build/release/CPackConfig.cmake -C Release
#
# The archive contains bin/cpptr-cli, share/cpptr/resources (config and keyword tables) and
# share/doc/cpptr/README.md. Benchmark scripts, corpora and the JPlag jar are never packaged.

set(CPACK_PACKAGE_NAME "cpptr")
set(CPACK_PACKAGE_VENDOR "cppTR")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_STRIP_FILES ON)
set(CPACK_VERBATIM_VARIABLES ON)

# Only the runtime component goes into the archive (no static library, no tests).
set(CPACK_COMPONENTS_ALL runtime)
set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)
set(CPACK_MONOLITHIC_INSTALL ON)

# cpptr-0.2.0-windows-x86_64.zip, cpptr-0.2.0-linux-x86_64.tar.gz, cpptr-0.2.0-darwin-arm64.tar.gz
string(TOLOWER "${CMAKE_SYSTEM_NAME}" _cpptr_system)
set(_cpptr_arch "${CMAKE_SYSTEM_PROCESSOR}")
if(_cpptr_arch MATCHES "^(AMD64|amd64|x64)$")
    set(_cpptr_arch "x86_64")
elseif(_cpptr_arch MATCHES "^(ARM64|aarch64)$")
    set(_cpptr_arch "arm64")
endif()
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${_cpptr_system}-${_cpptr_arch}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/package")

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

# Source archive for people who want to build themselves.
set(CPACK_SOURCE_GENERATOR "ZIP")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-source")
set(CPACK_SOURCE_IGNORE_FILES
    "/\\\\.git/"
    "/\\\\.venv/"
    "/build/"
    "/benchmark/"
    "/ref/"
    "\\\\.jar$"
    "\\\\.parquet$"
    "\\\\.duckdb$"
)

include(CPack)
