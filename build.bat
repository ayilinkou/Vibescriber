@echo off
setlocal

set "PROJECT_DIRECTORY=%~dp0"
set "BUILD_DIRECTORY=%PROJECT_DIRECTORY%build-native"

cmake ^
    -S "%PROJECT_DIRECTORY%" ^
    -B "%BUILD_DIRECTORY%" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DVIBESCRIBER_NATIVE_CPU=ON ^
    -DBUILD_TESTING=OFF
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD_DIRECTORY%" --config Release --parallel
if errorlevel 1 exit /b %errorlevel%
