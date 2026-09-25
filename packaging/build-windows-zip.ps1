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
$gui = Start-Process -FilePath (Join-Path $packagedBin 'vibescriber-gui.exe') -PassThru
try {
    Start-Sleep -Seconds 3
    if ($gui.HasExited) {
        throw "Packaged GUI exited during startup with code $($gui.ExitCode)"
    }
} finally {
    if (-not $gui.HasExited) { Stop-Process -Id $gui.Id -Force }
}
Remove-Item -Recurse -Force $extract
Remove-Item -Recurse -Force $stage
Write-Output $zip
