param(
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [Parameter(Mandatory = $true)][string]$OutputDir
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build = (Resolve-Path $BuildDir).Path
$output = [IO.Path]::GetFullPath($OutputDir)
# CMakeLists.txt places binaries in the source tree, even for another build dir.
$binaryDir = Join-Path (Join-Path $repo 'build') 'Release'
$versionText = & (Join-Path $binaryDir 'vibescriber.exe') --version
if ($LASTEXITCODE -ne 0 -or $versionText -notmatch '^Vibescriber ([0-9]+\.[0-9]+\.[0-9]+)$') {
    throw "Unexpected application version: $versionText"
}
$version = $Matches[1]
$name = "Vibescriber-v${version}-windows-x86_64"
$stage = Join-Path $output $name
$zip = Join-Path $output "$name.zip"
New-Item -ItemType Directory -Force -Path $output | Out-Null
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
if (Test-Path $zip) { Remove-Item -Force $zip }
& cmake --install $build --config Release --prefix $stage
if ($LASTEXITCODE -ne 0) { throw 'CMake install failed' }

# vcpkg's app-local deployment places third-party runtime DLLs alongside the
# built executables. CMake install already includes the project's own DLLs.
Get-ChildItem -LiteralPath $binaryDir -Filter '*.dll' -File |
    Copy-Item -Destination (Join-Path $stage 'bin') -Force
Copy-Item -LiteralPath (Join-Path $repo 'README.md') -Destination $stage
Compress-Archive -LiteralPath $stage -DestinationPath $zip -CompressionLevel Optimal

$extract = Join-Path $output 'smoke-extracted'
if (Test-Path $extract) { Remove-Item -Recurse -Force $extract }
Expand-Archive -LiteralPath $zip -DestinationPath $extract
$packagedBin = Join-Path (Join-Path $extract $name) 'bin'
$packagedVersion = & (Join-Path $packagedBin 'vibescriber.exe') --version
if ($LASTEXITCODE -ne 0 -or $packagedVersion -ne "Vibescriber $version") {
    throw "Packaged CLI failed: $packagedVersion"
}
foreach ($executable in @('vibescriber-gui.exe', 'vibescriber_sortformer.exe')) {
    if (-not (Test-Path (Join-Path $packagedBin $executable))) {
        throw "Missing packaged executable: $executable"
    }
}
foreach ($backend in @('ggml-vulkan.dll', 'ggml-cpu-x64.dll', 'ggml-cpu-haswell.dll')) {
    if (-not (Test-Path (Join-Path $packagedBin $backend))) {
        throw "Missing packaged backend: $backend"
    }
}
$gui = Start-Process -FilePath (Join-Path $packagedBin 'vibescriber-gui.exe') -PassThru
try {
    Start-Sleep -Seconds 3
    if ($gui.HasExited) {
        throw "Packaged GUI exited during startup with code $($gui.ExitCode)"
    }
} finally {
    if (-not $gui.HasExited) { Stop-Process -Id $gui.Id -Force }
}

# Exercise the packaged helper with the pinned NeMo runtime and model. Checking
# only that the executable exists missed DLL loading and inference failures.
$smokeAssets = Join-Path $output 'sortformer-smoke-assets'
New-Item -ItemType Directory -Force -Path $smokeAssets | Out-Null
$runtimeZip = Join-Path $smokeAssets 'nemo-speech-0.1.0-windows-x86_64-cpu.zip'
$modelFile = Join-Path $smokeAssets 'sortformer-v2-q8_0.gguf'
$assets = @(
    @{
        Path = $runtimeZip
        Url = 'https://github.com/NVIDIA/NeMo-Speech.cpp/releases/download/v0.1.0/nemo-speech-0.1.0-windows-x86_64-cpu.zip'
        Sha256 = '5e4ea81046012edcd77fd8848de8eefb5a4ba38cc26f52eb544ab184695a75d6'
    },
    @{
        Path = $modelFile
        Url = 'https://huggingface.co/nvidia/diar_streaming_sortformer_4spk-v2/resolve/5240a64075176943f677d30fa2171c780229f341/diar_streaming_sortformer_4spk-v2.q8_0.gguf'
        Sha256 = '0679cfeb1ce356d0dea9470b31274f4bfc7eb927497d82005483770666da998a'
    }
)
foreach ($asset in $assets) {
    if (-not (Test-Path $asset.Path) -or
        (Get-FileHash -LiteralPath $asset.Path -Algorithm SHA256).Hash -ne $asset.Sha256) {
        & curl.exe --fail --location --retry 3 --silent --show-error --output $asset.Path $asset.Url
        if ($LASTEXITCODE -ne 0) { throw "Could not download $($asset.Url)" }
    }
    if ((Get-FileHash -LiteralPath $asset.Path -Algorithm SHA256).Hash -ne $asset.Sha256) {
        throw "Sortformer smoke asset has the wrong SHA-256: $($asset.Path)"
    }
}
$runtimeDir = Join-Path $smokeAssets 'runtime'
if (-not (Test-Path (Join-Path $runtimeDir 'bin/nemo_speech_asr_c.dll'))) {
    Expand-Archive -LiteralPath $runtimeZip -DestinationPath $runtimeDir -Force
}
$wav = Join-Path $smokeAssets 'smoke.wav'
$writer = [IO.BinaryWriter]::new([IO.File]::Create($wav))
try {
    $samples = [byte[]]::new(96000)
    $writer.Write([Text.Encoding]::ASCII.GetBytes('RIFF'))
    $writer.Write([int](36 + $samples.Length))
    $writer.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt '))
    $writer.Write([int]16)
    $writer.Write([short]1)
    $writer.Write([short]1)
    $writer.Write([int]16000)
    $writer.Write([int]32000)
    $writer.Write([short]2)
    $writer.Write([short]16)
    $writer.Write([Text.Encoding]::ASCII.GetBytes('data'))
    $writer.Write([int]$samples.Length)
    $writer.Write($samples)
} finally {
    $writer.Dispose()
}
$resultFile = Join-Path $smokeAssets 'smoke.speakers'
& (Join-Path $packagedBin 'vibescriber_sortformer.exe') `
    (Join-Path $runtimeDir 'bin/nemo_speech_asr_c.dll') $modelFile $wav $resultFile cpu
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $resultFile) -or
    -not (Select-String -LiteralPath $resultFile -Pattern '^P\s' -Quiet)) {
    throw "Packaged Sortformer helper failed with exit code $LASTEXITCODE"
}
Remove-Item -Recurse -Force $extract
Remove-Item -Recurse -Force $stage
Write-Output $zip
