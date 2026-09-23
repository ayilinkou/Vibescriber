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
detect speaker turns by itself. Select it with:

```sh
./build/Release/vibescriber --model medium.en recording.mp4
```

For speaker identities, add `--diarize`. This transcribes with Whisper
`medium.en` by default, then uses Sortformer v2 to identify up to four speakers.
The transcript labels paragraphs as `Speaker 1`, `Speaker 2`, and so on, and
shows a separate diarization progress indicator. The Sortformer model and
NeMo-Speech.cpp runtime are downloaded once from public URLs; no account,
token, or Python installation is needed. The runtime uses Vulkan when
available and retries on the CPU if GPU inference fails.

```sh
./build/Release/vibescriber --diarize recording.mp4
```

TinyDiarize remains available in the default mode. `--diarize` and
`--tinydiarize` cannot be combined. You can pair `--diarize` with a custom
Whisper GGML model via `--model`.

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
