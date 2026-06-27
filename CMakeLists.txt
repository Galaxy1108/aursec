cmake_minimum_required(VERSION 3.16)
project(aura LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)

add_executable(aura
    src/main.cpp
    src/config.cpp
    src/arg_parser.cpp
    src/pkgbuild.cpp
    src/ai_reviewer.cpp
    src/executor.cpp
)

target_link_libraries(aura PRIVATE CURL::libcurl nlohmann_json::nlohmann_json)

option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    add_executable(test_aura
        tests/test_main.cpp
        src/ai_reviewer.cpp
        src/arg_parser.cpp
    )
    target_link_libraries(test_aura PRIVATE nlohmann_json::nlohmann_json CURL::libcurl)
    target_compile_definitions(test_aura PRIVATE BUILD_TESTING=1)
endif()
