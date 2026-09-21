# Vibescriber

Vibescriber is a vibe-coded Windows and Linux application for transcribing recorded
conversations. All transcribing is done locally on your machine.

## Build

Vibescriber requires a C++20 compiler, CMake 3.20 or newer, and libcurl 8.0 or
newer with HTTPS support.

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
./build/vibescriber
```
