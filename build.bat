@echo off
REM Unified configure + build script for Goemon64Recompiled.
REM
REM   build.bat          - configure (only if build-cmake has no cache) then build
REM   build.bat clean    - delete the CMake cache first, forcing a fresh configure
REM   build.bat regen    - also re-run N64Recomp + RSPRecomp on the .toml files
REM                        (use when mnsg.toml / aspMain.toml change)
REM
REM Use the `clean` form if a CMakeLists change leaves the cache in a bad state
REM (symptom: regen picks the wrong lld-link and fails with an x86/x64 machine
REM type mismatch).
setlocal enabledelayedexpansion

REM Repo root = this script's directory (strip trailing backslash).
set "REPO=%~dp0"
if "%REPO:~-1%"=="\" set "REPO=%REPO:~0,-1%"
set "BLD=%REPO%\build-cmake"
set "PATCHES_LLVM=%REPO%\..\portable-llvm\LLVM-19.1.3-Windows-X64\bin"
set "SIBLING_BANJO=%REPO%\..\BanjoRecomp3D"

if /i "%~1"=="clean" (
    echo [build] Clean requested - removing CMake cache...
    if exist "%BLD%\CMakeCache.txt" del /q "%BLD%\CMakeCache.txt"
    if exist "%BLD%\CMakeFiles" rmdir /s /q "%BLD%\CMakeFiles"
)

REM Bring in the VS x64 developer environment. Prepend the x64 LLVM toolchain
REM (NOT the 32-bit ...\Llvm\bin) so any in-build CMake regen keeps using the
REM x64 lld-link and doesn't trip the machine-type mismatch.
set "VSINSTALLER=C:\Program Files (x86)\Microsoft Visual Studio\Installer"
set "PATH=%VSINSTALLER%;%PATH%"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set "PATH=C:\ProgramData\chocolatey\bin;C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin;%PATH%"

REM Sanity-check the patches toolchain. PATCHES_LLVM points at portable-llvm
REM checked out as a sibling of this repo (see Banjo's setup).
if not exist "%PATCHES_LLVM%\clang.exe" (
    echo [build] ERROR: patches LLVM not found at "%PATCHES_LLVM%"
    echo [build]        Expected sibling layout: %REPO%\..\portable-llvm\LLVM-19.1.3-Windows-X64
    echo [build]        Extract portable LLVM 19.1.3 there or adjust PATCHES_LLVM in build.bat.
    exit /b 1
)

REM Ensure N64Recomp.exe + RSPRecomp.exe exist in the repo root. If absent,
REM copy from the sibling BanjoRecomp3D repo (they're build artifacts of the
REM same N64Recomp source under lib/N64ModernRuntime/N64Recomp, so the binaries
REM are interchangeable across N64Recomp ports).
if not exist "%REPO%\N64Recomp.exe" (
    if exist "%SIBLING_BANJO%\N64Recomp.exe" (
        echo [build] Copying N64Recomp.exe from %SIBLING_BANJO%...
        copy /Y "%SIBLING_BANJO%\N64Recomp.exe" "%REPO%\N64Recomp.exe" >nul
    ) else (
        echo [build] ERROR: N64Recomp.exe missing. Build it from lib\N64ModernRuntime\N64Recomp
        echo [build]        or copy from a sibling N64Recomp port.
        exit /b 1
    )
)
if not exist "%REPO%\RSPRecomp.exe" (
    if exist "%SIBLING_BANJO%\RSPRecomp.exe" (
        echo [build] Copying RSPRecomp.exe from %SIBLING_BANJO%...
        copy /Y "%SIBLING_BANJO%\RSPRecomp.exe" "%REPO%\RSPRecomp.exe" >nul
    ) else (
        echo [build] ERROR: RSPRecomp.exe missing.
        exit /b 1
    )
)

REM Verify the decompressed ROM is present (CMake will fail later without it
REM with a less obvious error message).
if not exist "%REPO%\mnsg.us.decompressed.z64" (
    echo [build] ERROR: mnsg.us.decompressed.z64 not found at repo root.
    echo [build]        See BUILDING.md step 3 for how to generate it from the mnsg decomp.
    exit /b 1
)

REM Run N64Recomp on mnsg.toml to generate RecompiledFuncs/. Skipped if the
REM output directory exists and is newer than the toml. Pass `regen` as the
REM first arg to force re-running.
set "RECOMP_FORCE=0"
if /i "%~1"=="regen" set "RECOMP_FORCE=1"

if "%RECOMP_FORCE%"=="1" (
    if exist "%REPO%\RecompiledFuncs" (
        echo [build] regen requested - removing RecompiledFuncs...
        rmdir /s /q "%REPO%\RecompiledFuncs"
    )
    if exist "%REPO%\rsp\aspMain.cpp" del /q "%REPO%\rsp\aspMain.cpp"
)

if not exist "%REPO%\RecompiledFuncs\funcs.h" (
    echo [build] Running N64Recomp on mnsg.toml...
    pushd "%REPO%"
    .\N64Recomp.exe mnsg.toml
    set "NRC=!ERRORLEVEL!"
    popd
    REM N64Recomp returns non-zero if any single function fails to recompile
    REM (e.g. unhandled MIPS instruction), but typically still emits valid C
    REM for the other 7000+ functions. We treat funcs.h's existence as the
    REM success signal instead of trusting the exit code, matching how the
    REM upstream N64Recomp ports validate this step.
    if not exist "%REPO%\RecompiledFuncs\funcs.h" (
        echo [build] ERROR: N64Recomp didn't generate funcs.h (exit code !NRC!^).
        exit /b 1
    )
    if not "!NRC!"=="0" (
        echo [build] WARN: N64Recomp exited !NRC! - some functions may have failed to recompile.
    )
)

if not exist "%REPO%\rsp" mkdir "%REPO%\rsp"
if not exist "%REPO%\rsp\aspMain.cpp" (
    echo [build] Running RSPRecomp on aspMain.toml...
    pushd "%REPO%"
    .\RSPRecomp.exe aspMain.toml
    set "RRC=!ERRORLEVEL!"
    popd
    if not exist "%REPO%\rsp\aspMain.cpp" (
        echo [build] ERROR: RSPRecomp didn't generate rsp\aspMain.cpp (exit code !RRC!^).
        exit /b 1
    )
)

if not exist "%BLD%\CMakeCache.txt" (
    echo [build] Configuring CMake...
    cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
        -DPATCHES_C_COMPILER="%PATCHES_LLVM%\clang.exe" ^
        -DPATCHES_LD="%PATCHES_LLVM%\ld.lld.exe" ^
        -S "%REPO%" -B "%BLD%"
    if errorlevel 1 exit /b %errorlevel%
)

echo [build] Building Goemon64Recompiled...
cmake --build "%BLD%" --target Goemon64Recompiled --config Release
exit /b %errorlevel%
