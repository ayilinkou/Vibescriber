# Vibescriber

Vibescriber is a vibe-coded Windows and Linux application for transcribing recorded
conversations. All transcribing is done locally on your machine.

## Build

Vibescriber requires a C++20 compiler, CMake 3.20 or newer, and libcurl 8.0 or
newer with HTTPS support.
The Linux GUI build also needs D-Bus and FLTK's X11 or Wayland development
libraries; the CI workflow lists the required Ubuntu packages.
For Vulkan acceleration, install the Vulkan SDK (including `glslc`) and
SPIRV-Headers before configuring. Builds without these dependencies use the CPU.
At runtime, transcription uses a Vulkan GPU when one is available and falls back
to the CPU if GPU initialization or transcription fails.
On x86 systems, the release build includes CPU backend variants and selects
the fastest one supported by the running CPU, including AVX2 where available.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On Linux, `./build.sh` configures the same Release build and limits compilation
to two jobs to avoid exhausting memory. Set `VIBESCRIBER_BUILD_JOBS` to change
that limit if your machine has more memory.

Run the tests with:

```sh
ctest --test-dir build --output-on-failure
```

## Models

The default transcription model is Whisper `small.en` with TinyDiarize speaker
turn detection (`ggml-small.en-tdrz.bin`, about 465 MiB). During setup,
Vibescriber also downloads and caches Whisper `medium.en` (`ggml-medium.en.bin`,
1.53 GB) for comparison when using a built-in model. The medium model does not
detect speaker turns. Select it with:

```sh
./build/Release/vibescriber --model medium.en recording.mp4
```

To compare another model, download a whisper.cpp GGML model file from the
[upstream model list](https://github.com/ggml-org/whisper.cpp/blob/master/models/README.md)
and pass its path:

```sh
./build/Release/vibescriber --model /path/to/ggml-base.en.bin recording.mp4
```

`tiny.en` and `base.en` are smaller English models to try. A custom model is used
as-is and is not downloaded by Vibescriber. Add `--tinydiarize` when the
custom model supports TinyDiarize speaker turns. Standard Whisper models do not
provide those speaker-turn markers. TinyDiarize marks speaker changes but does
not assign persistent speaker identities. Transcription currently uses English.

Run the development executable on Linux with:

```sh
./build/Release/vibescriber
```

## Desktop interface

Builds also produce `vibescriber-gui` beside the CLI. Start it with:

```sh
./build/Release/vibescriber-gui
```

Choose a recording and the Output transcript field fills with the same path
and a `.txt` extension. You can edit the output name or choose another folder.
The GUI runs the CLI beside it and shows progress. If the requested output
already exists, Vibescriber adds `_1`, `_2`, and so on rather than replacing it;
the status line shows the file that was actually written.

The Appearance menu offers **Auto**, **Light**, and **Dark**. Auto is the default
and follows the Windows app color setting or the Linux XDG desktop portal's
color-scheme setting, checking again while the window is open. If the desktop
does not publish a preference, Auto uses Light. The selected menu option is
saved between sessions. The file chooser may use the desktop's own theme.
