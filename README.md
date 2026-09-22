# Vibescriber

Vibescriber is a vibe-coded Windows and Linux application for transcribing recorded
conversations. All transcribing is done locally on your machine.

## Build

Vibescriber requires a C++20 compiler, CMake 3.20 or newer, and libcurl 8.0 or
newer with HTTPS support.
For Vulkan acceleration, install the Vulkan SDK (including `glslc`) and
SPIRV-Headers before configuring. Builds without these dependencies use the CPU.
At runtime, transcription uses a Vulkan GPU when one is available and falls back
to the CPU if GPU initialization or transcription fails.
On x86 systems, the release build includes CPU backend variants and selects
the fastest one supported by the running CPU, including AVX2 where available.

```sh
cmake -S . -B build
cmake --build build
```

Run the tests with:

```sh
ctest --test-dir build --output-on-failure
```

Run the development executable on Linux with:

```sh
./build/bin/vibescriber
```
