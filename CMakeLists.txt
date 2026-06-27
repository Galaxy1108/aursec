cmake_minimum_required(VERSION 3.16)
project(aursec VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig QUIET)

add_executable(aursec
    src/main.cpp
    src/config.cpp
    src/arg_parser.cpp
    src/pkgbuild.cpp
    src/ai_reviewer.cpp
    src/executor.cpp
)

target_compile_definitions(aursec PRIVATE AURSEC_VERSION="${PROJECT_VERSION}")
target_link_libraries(aursec PRIVATE CURL::libcurl nlohmann_json::nlohmann_json OpenSSL::Crypto)

if(PkgConfig_FOUND)
    pkg_check_modules(LIBSECRET libsecret-1 IMPORTED_TARGET)
    if(TARGET PkgConfig::LIBSECRET)
        target_link_libraries(aursec PRIVATE PkgConfig::LIBSECRET)
        target_compile_definitions(aursec PRIVATE HAVE_LIBSECRET=1)
        message(STATUS "libsecret found - keyring support enabled")
    endif()
endif()

option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    add_executable(test_aursec
        tests/test_main.cpp
        src/ai_reviewer.cpp
        src/arg_parser.cpp
    )
    target_link_libraries(test_aursec PRIVATE nlohmann_json::nlohmann_json CURL::libcurl)
    target_compile_definitions(test_aursec PRIVATE BUILD_TESTING=1)
endif()
