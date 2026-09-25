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
find_package(Vulkan QUIET COMPONENTS glslc)
find_package(SPIRV-Headers CONFIG QUIET)
option(VIBESCRIBER_REQUIRE_VULKAN "Fail configuration if Vulkan build dependencies are missing" OFF)

if(Vulkan_FOUND AND Vulkan_glslc_FOUND AND SPIRV-Headers_FOUND)
    set(VIBESCRIBER_VULKAN ON)
    message(STATUS "Vulkan transcription enabled")
else()
    if(VIBESCRIBER_REQUIRE_VULKAN)
        message(FATAL_ERROR "Vulkan support requires the Vulkan loader, glslc, and SPIRV-Headers")
    endif()
    set(VIBESCRIBER_VULKAN OFF)
    message(STATUS "Vulkan build dependencies unavailable; using CPU transcription")
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86)$")
    set(VIBESCRIBER_CPU_VARIANTS ON)
else()
    set(VIBESCRIBER_CPU_VARIANTS OFF)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/build/$<CONFIG>")
set(BUILD_SHARED_LIBS ${VIBESCRIBER_CPU_VARIANTS} CACHE BOOL "Enable runtime CPU backend selection on x86" FORCE)
set(GGML_BACKEND_DL ${VIBESCRIBER_CPU_VARIANTS} CACHE BOOL "Load CPU backend variants at runtime" FORCE)
set(GGML_CPU_ALL_VARIANTS ${VIBESCRIBER_CPU_VARIANTS} CACHE BOOL "Build CPU variants for runtime selection" FORCE)
if(VIBESCRIBER_CPU_VARIANTS)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/build/$<CONFIG>")
    if(UNIX)
        set(CMAKE_INSTALL_RPATH "$ORIGIN")
    endif()
endif()
set(GGML_CCACHE OFF CACHE BOOL "Do not require a compiler cache" FORCE)
set(GGML_NATIVE OFF CACHE BOOL "Build portable CPU code" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "Avoid an additional OpenMP runtime dependency" FORCE)
set(GGML_SSE42 OFF CACHE BOOL "Use the x86-64 baseline instruction set" FORCE)
set(GGML_AVX OFF CACHE BOOL "Use the x86-64 baseline instruction set" FORCE)
set(GGML_AVX2 OFF CACHE BOOL "Use the x86-64 baseline instruction set" FORCE)
set(GGML_BMI2 OFF CACHE BOOL "Use the x86-64 baseline instruction set" FORCE)
set(GGML_FMA OFF CACHE BOOL "Use the x86-64 baseline instruction set" FORCE)
set(GGML_F16C OFF CACHE BOOL "Use the x86-64 baseline instruction set" FORCE)
set(GGML_VULKAN ${VIBESCRIBER_VULKAN} CACHE BOOL "Enable Vulkan when its build dependencies are available" FORCE)
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

set(FLTK_BUILD_EXAMPLES OFF CACHE BOOL "Build only the FLTK library" FORCE)
set(FLTK_BUILD_FLTK_OPTIONS OFF CACHE BOOL "Skip the FLTK options editor" FORCE)
set(FLTK_BUILD_FLUID OFF CACHE BOOL "Build the GUI directly in C++" FORCE)
set(FLTK_BUILD_FORMS OFF CACHE BOOL "The GUI does not use Forms compatibility" FORCE)
set(FLTK_BUILD_GL OFF CACHE BOOL "The GUI does not use OpenGL" FORCE)
set(FLTK_BUILD_TEST OFF CACHE BOOL "Use Vibescriber's tests" FORCE)
set(FLTK_OPTION_SVG OFF CACHE BOOL "The GUI does not use SVG" FORCE)
FetchContent_Declare(
    fltk
    GIT_REPOSITORY https://github.com/fltk/fltk.git
    GIT_TAG a9b1113516ffd15fc7602a6d425a317df30f4720
    GIT_SHALLOW FALSE
    GIT_PROGRESS TRUE
)
FetchContent_GetProperties(fltk)
if(NOT fltk_POPULATED)
    cmake_policy(PUSH)
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(fltk)
    cmake_policy(POP)
    add_subdirectory(${fltk_SOURCE_DIR} ${fltk_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DBUS1 REQUIRED IMPORTED_TARGET dbus-1)
endif()

if(VIBESCRIBER_CPU_VARIANTS)
    set(vibescriber_shared_targets whisper ggml ggml-base)
    foreach(backend IN LISTS GGML_AVAILABLE_BACKENDS)
        if(TARGET ${backend})
            list(APPEND vibescriber_shared_targets ${backend})
        endif()
    endforeach()
    foreach(shared_target IN LISTS vibescriber_shared_targets)
        set_target_properties(${shared_target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/build/$<CONFIG>"
            LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/build/$<CONFIG>"
        )
    endforeach()
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
    src/sortformer_runtime.cpp
    src/speaker_alignment.cpp
    src/cli.cpp
    src/cpu_profile.cpp
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
target_compile_definitions(vibescriber_core PRIVATE
    VIBESCRIBER_HAS_VULKAN=$<BOOL:${VIBESCRIBER_VULKAN}>
    VIBESCRIBER_DYNAMIC_BACKENDS=$<BOOL:${VIBESCRIBER_CPU_VARIANTS}>
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

add_executable(vibescriber-gui WIN32
    src/gui.cpp
    src/gui_theme.cpp
    src/output_path.cpp
    src/process.cpp
)
target_compile_features(vibescriber-gui PRIVATE cxx_std_20)
target_include_directories(vibescriber-gui PRIVATE src)
target_link_libraries(vibescriber-gui PRIVATE fltk::fltk)
if(UNIX AND NOT APPLE)
    target_link_libraries(vibescriber-gui PRIVATE PkgConfig::DBUS1)
endif()
if(WIN32)
    target_link_libraries(vibescriber-gui PRIVATE Advapi32)
endif()
set_target_properties(vibescriber-gui PROPERTIES CXX_EXTENSIONS OFF)
vibescriber_enable_warnings(vibescriber-gui)

add_executable(vibescriber_sortformer
    src/sortformer_runner.cpp
    src/wav_reader.cpp
)
target_include_directories(vibescriber_sortformer PRIVATE src)
target_compile_features(vibescriber_sortformer PRIVATE cxx_std_20)
set_target_properties(vibescriber_sortformer PROPERTIES CXX_EXTENSIONS OFF)
if(UNIX)
    target_link_libraries(vibescriber_sortformer PRIVATE ${CMAKE_DL_LIBS})
endif()
vibescriber_enable_warnings(vibescriber_sortformer)
add_dependencies(vibescriber vibescriber_sortformer)

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

    add_executable(vibescriber_process_tests tests/process_tests.cpp)
    target_link_libraries(vibescriber_process_tests PRIVATE vibescriber_core)
    set_target_properties(vibescriber_process_tests PROPERTIES CXX_EXTENSIONS OFF)
    vibescriber_enable_warnings(vibescriber_process_tests)
    add_test(NAME vibescriber_process_tests
        COMMAND vibescriber_process_tests $<TARGET_FILE:vibescriber_process_fixture>)

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

install(TARGETS vibescriber vibescriber-gui vibescriber_sortformer
    RUNTIME DESTINATION bin)
if(VIBESCRIBER_CPU_VARIANTS)
    install(TARGETS ${vibescriber_shared_targets}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION bin
    )
endif()
