# Vibescriber

Vibescriber is a vibe-coded Windows and Linux application for transcribing recorded
conversations. All transcribing is done locally on your machine.

## Build

Vibescriber requires a C++20 compiler and CMake 3.20 or newer. On Windows,
install [vcpkg](https://github.com/microsoft/vcpkg), set `VCPKG_ROOT` to its
installation directory, and start a new terminal before configuring. CMake
uses that variable to load the vcpkg toolchain and install libcurl, libarchive,
and the Vulkan build dependencies from `vcpkg.json`. Set `VCPKG_ROOT` before
the first configure of a build directory; if you configured that directory
without vcpkg, use a fresh build directory. On Linux, install libcurl 8.0 or
newer with HTTPS support and libarchive through your distribution.
The Linux GUI build also needs D-Bus and FLTK's X11 or Wayland development
libraries.
For Vulkan acceleration without vcpkg, install the Vulkan SDK (including
`glslc`) and SPIRV-Headers before configuring. Builds without these dependencies
use the CPU. To require Vulkan and get a configure error if it is unavailable,
pass `-DVIBESCRIBER_REQUIRE_VULKAN=ON` to CMake.
At runtime, transcription uses a Vulkan GPU when one is available and falls back
to the CPU if GPU initialization or transcription fails.
Release packages include both backends, so the build runner does not need a GPU.
On x86 systems, builds include CPU backend variants and select
the fastest one supported by the running CPU, including AVX2 where available.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

For a faster local build targeting only the current machine's CPU, run
`./build.sh` on Linux or `build.bat` on Windows. Both scripts use
`VIBESCRIBER_NATIVE_CPU=ON` and write executables to
`build-native/bin/Release` (on Windows, `build-native\bin\Release`).
The native binaries may not run on another CPU. The release workflow uses the
portable configuration above.

On Linux, `./build.sh` limits compilation to two jobs to avoid exhausting
memory. Set `VIBESCRIBER_BUILD_JOBS` to change that limit. You can also
configure a native build directly:

```sh
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release -DVIBESCRIBER_NATIVE_CPU=ON
cmake --build build-native --config Release --parallel 2
```

## Models

The default uses Whisper `medium.en` (`ggml-medium.en.bin`, 1.53 GB) for the
words and Sortformer v2 for speaker labels. Run it with:

```sh
./build/Release/vibescriber recording.mp4
```

Sortformer identifies up to four speakers and labels transcript paragraphs as
`Speaker 1`, `Speaker 2`, and so on. Its model and NeMo-Speech.cpp runtime are
downloaded once from public URLs. The runtime uses Vulkan when available and
retries on the CPU if GPU inference fails. To use medium transcription without
speaker labels:

```sh
./build/Release/vibescriber --model medium.en recording.mp4
```

The smaller Whisper `small.en` TinyDiarize model
(`ggml-small.en-tdrz.bin`, about 465 MiB) is available with
`--model small.en-tdrz`. `--diarize` uses Sortformer with an explicitly selected
model; it cannot be combined with `--tinydiarize`.

To compare another model, download a whisper.cpp GGML model file from the
[upstream model list](https://github.com/ggml-org/whisper.cpp/blob/master/models/README.md)
and pass its path:

```sh
./build/Release/vibescriber --model /path/to/ggml-base.en.bin recording.mp4
```

## Desktop interface

Builds also produce `vibescriber-gui` beside the CLI. Start it with:

```sh
./build/Release/vibescriber-gui
```
