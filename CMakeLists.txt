cmake_minimum_required(VERSION 3.20)

project(
    Vibescriber
    VERSION 0.1.0
    DESCRIPTION "Local audio transcription for Windows and Linux"
    LANGUAGES CXX
)

include(CTest)
include(FetchContent)
find_package(CURL 8.0 REQUIRED COMPONENTS HTTPS)
find_package(LibArchive 3.6 REQUIRED)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build dependencies as static libraries" FORCE)
set(GGML_CCACHE OFF CACHE BOOL "Do not require a compiler cache" FORCE)
set(GGML_NATIVE OFF CACHE BOOL "Build portable CPU code" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "Avoid an additional OpenMP runtime dependency" FORCE)
set(GGML_SSE42 OFF CACHE BOOL "Use the x86-64 baseline instruction set" FORCE)
set(GGML_AVX OFF CACHE BOOL "Use the x86-64 baseline instruction set" FORCE)
set(GGML_AVX2 OFF CACHE BOOL "Use the x86-64 baseline instruction set" FORCE)
set(GGML_BMI2 OFF CACHE BOOL "Use the x86-64 baseline instruction set" FORCE)
set(GGML_FMA OFF CACHE BOOL "Use the x86-64 baseline instruction set" FORCE)
set(GGML_F16C OFF CACHE BOOL "Use the x86-64 baseline instruction set" FORCE)
set(WHISPER_ALL_WARNINGS OFF CACHE BOOL "Use project warnings only for project code" FORCE)
set(WHISPER_BUILD_IS_DEV OFF CACHE BOOL "Build the pinned release version" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "Do not build whisper.cpp examples" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "Do not build the whisper.cpp server" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "Do not build whisper.cpp tests" FORCE)

FetchContent_Declare(
    whisper_cpp
    GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
    GIT_TAG 927cfce34f31707e17f2bff35c349632fb9e2c3a
    GIT_SHALLOW FALSE
    GIT_PROGRESS TRUE
)
FetchContent_GetProperties(whisper_cpp)
if(NOT whisper_cpp_POPULATED)
    cmake_policy(PUSH)
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(whisper_cpp)
    cmake_policy(POP)
    add_subdirectory(
        ${whisper_cpp_SOURCE_DIR}
        ${whisper_cpp_BINARY_DIR}
        EXCLUDE_FROM_ALL
    )
endif()

function(vibescriber_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(vibescriber_core STATIC
    src/app_paths.cpp
    src/archive_extract.cpp
    src/audio_conversion.cpp
    src/runtime_asset.cpp
    src/cli.cpp
    src/download.cpp
    src/ffmpeg_runtime.cpp
    src/output_path.cpp
    src/process.cpp
    src/sha256.cpp
    src/transcript_output.cpp
    src/transcription.cpp
    src/wav_reader.cpp
)

target_include_directories(vibescriber_core PUBLIC src)
target_compile_features(vibescriber_core PUBLIC cxx_std_20)
target_link_libraries(vibescriber_core
    PUBLIC CURL::libcurl LibArchive::LibArchive
    PRIVATE whisper
)
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

    add_executable(vibescriber_runtime_asset_tests
        tests/runtime_asset_tests.cpp
    )

    target_link_libraries(vibescriber_runtime_asset_tests PRIVATE vibescriber_core)
    set_target_properties(vibescriber_runtime_asset_tests PROPERTIES CXX_EXTENSIONS OFF)
    vibescriber_enable_warnings(vibescriber_runtime_asset_tests)

    add_test(NAME vibescriber_runtime_asset_tests COMMAND vibescriber_runtime_asset_tests)

    add_executable(vibescriber_ffmpeg_runtime_tests
        tests/ffmpeg_runtime_tests.cpp
    )

    target_link_libraries(vibescriber_ffmpeg_runtime_tests PRIVATE vibescriber_core)
    set_target_properties(vibescriber_ffmpeg_runtime_tests PROPERTIES CXX_EXTENSIONS OFF)
    vibescriber_enable_warnings(vibescriber_ffmpeg_runtime_tests)

    add_test(NAME vibescriber_ffmpeg_runtime_tests COMMAND vibescriber_ffmpeg_runtime_tests)

    add_executable(vibescriber_process_fixture
        tests/process_fixture.cpp
    )
    set_target_properties(vibescriber_process_fixture PROPERTIES CXX_EXTENSIONS OFF)
    target_compile_features(vibescriber_process_fixture PRIVATE cxx_std_20)
    vibescriber_enable_warnings(vibescriber_process_fixture)

    add_executable(vibescriber_audio_conversion_tests
        tests/audio_conversion_tests.cpp
    )

    target_link_libraries(vibescriber_audio_conversion_tests PRIVATE vibescriber_core)
    set_target_properties(vibescriber_audio_conversion_tests PROPERTIES CXX_EXTENSIONS OFF)
    vibescriber_enable_warnings(vibescriber_audio_conversion_tests)

    add_test(
        NAME vibescriber_audio_conversion_tests
        COMMAND vibescriber_audio_conversion_tests $<TARGET_FILE:vibescriber_process_fixture>
    )

    add_executable(vibescriber_transcription_tests
        tests/transcription_tests.cpp
    )

    target_link_libraries(vibescriber_transcription_tests PRIVATE vibescriber_core)
    set_target_properties(vibescriber_transcription_tests PROPERTIES CXX_EXTENSIONS OFF)
    vibescriber_enable_warnings(vibescriber_transcription_tests)

    add_test(NAME vibescriber_transcription_tests COMMAND vibescriber_transcription_tests)

    add_executable(vibescriber_transcript_output_tests
        tests/transcript_output_tests.cpp
    )

    target_link_libraries(vibescriber_transcript_output_tests PRIVATE vibescriber_core)
    set_target_properties(vibescriber_transcript_output_tests PROPERTIES CXX_EXTENSIONS OFF)
    vibescriber_enable_warnings(vibescriber_transcript_output_tests)

    add_test(NAME vibescriber_transcript_output_tests COMMAND vibescriber_transcript_output_tests)
endif()

install(TARGETS vibescriber RUNTIME DESTINATION bin)
