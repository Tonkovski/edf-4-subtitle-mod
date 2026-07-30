@echo off
setlocal

set "PROJECT_ROOT=%~dp0"
if "%PROJECT_ROOT:~-1%"=="\" set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

where cmake.exe >nul 2>nul
if errorlevel 1 (
    if exist "D:\bin\mingw64\bin\cmake.exe" (
        set "PATH=D:\bin\mingw64\bin;%PATH%"
    )
)

where g++.exe >nul 2>nul
if errorlevel 1 (
    echo [ERROR] g++.exe was not found. Add the MinGW-w64 bin directory to PATH.
    exit /b 1
)

where cmake.exe >nul 2>nul
if errorlevel 1 (
    echo [ERROR] cmake.exe was not found.
    exit /b 1
)

where ninja.exe >nul 2>nul
if errorlevel 1 (
    echo [ERROR] ninja.exe was not found.
    exit /b 1
)

echo [INFO] Configuring %CONFIG% build...
cmake.exe -S "%PROJECT_ROOT%" -B "%PROJECT_ROOT%\build\%CONFIG%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG%
if errorlevel 1 exit /b 1

echo [INFO] Building...
cmake.exe --build "%PROJECT_ROOT%\build\%CONFIG%"
if errorlevel 1 exit /b 1

echo [INFO] Running core tests...
ctest.exe --test-dir "%PROJECT_ROOT%\build\%CONFIG%" --output-on-failure
if errorlevel 1 exit /b 1

echo [INFO] Preparing package...
cmake.exe --install "%PROJECT_ROOT%\build\%CONFIG%" --prefix "%PROJECT_ROOT%\out"
if errorlevel 1 exit /b 1

echo [OK] Package ready under "%PROJECT_ROOT%\out\Mods\Plugins".
