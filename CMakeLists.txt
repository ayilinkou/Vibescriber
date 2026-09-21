cmake_minimum_required(VERSION 3.20)

project(
    Vibescriber
    VERSION 0.1.0
    DESCRIPTION "Local audio transcription for Windows and Linux"
    LANGUAGES CXX
)

add_executable(vibescriber
    src/main.cpp
)

target_compile_features(vibescriber PRIVATE cxx_std_20)
set_target_properties(vibescriber PROPERTIES
    CXX_EXTENSIONS OFF
)

target_compile_definitions(vibescriber PRIVATE
    VIBESCRIBER_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(vibescriber PRIVATE /W4 /permissive-)
else()
    target_compile_options(vibescriber PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS vibescriber RUNTIME DESTINATION bin)
