cmake_minimum_required(VERSION 3.20)

project(
    Vibescriber
    VERSION 0.1.0
    DESCRIPTION "Local audio transcription for Windows and Linux"
    LANGUAGES CXX
)

include(CTest)
find_package(CURL 8.0 REQUIRED COMPONENTS HTTPS)

function(vibescriber_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(vibescriber_core STATIC
    src/cli.cpp
    src/download.cpp
    src/output_path.cpp
    src/sha256.cpp
)

target_include_directories(vibescriber_core PUBLIC src)
target_compile_features(vibescriber_core PUBLIC cxx_std_20)
target_link_libraries(vibescriber_core PUBLIC CURL::libcurl)
set_target_properties(vibescriber_core PROPERTIES CXX_EXTENSIONS OFF)
vibescriber_enable_warnings(vibescriber_core)

add_executable(vibescriber
    src/main.cpp
)

target_link_libraries(vibescriber PRIVATE vibescriber_core)
set_target_properties(vibescriber PROPERTIES CXX_EXTENSIONS OFF)

target_compile_definitions(vibescriber PRIVATE
    VIBESCRIBER_VERSION="${PROJECT_VERSION}"
)
vibescriber_enable_warnings(vibescriber)

if(BUILD_TESTING)
    add_executable(vibescriber_tests
        tests/cli_tests.cpp
    )

    target_link_libraries(vibescriber_tests PRIVATE vibescriber_core)
    set_target_properties(vibescriber_tests PROPERTIES CXX_EXTENSIONS OFF)
    vibescriber_enable_warnings(vibescriber_tests)

    add_test(NAME vibescriber_tests COMMAND vibescriber_tests)

    add_executable(vibescriber_download_tests
        tests/download_tests.cpp
    )

    target_link_libraries(vibescriber_download_tests PRIVATE vibescriber_core)
    set_target_properties(vibescriber_download_tests PROPERTIES CXX_EXTENSIONS OFF)
    vibescriber_enable_warnings(vibescriber_download_tests)

    add_test(NAME vibescriber_download_tests COMMAND vibescriber_download_tests)
endif()

install(TARGETS vibescriber RUNTIME DESTINATION bin)
