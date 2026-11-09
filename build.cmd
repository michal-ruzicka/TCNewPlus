@echo off
REM ===========================================================================
REM Build script for the TCNewPlus File System plugin.
REM
REM Reads sources from src\, builds both bitnesses with CMake, runs the test
REM executable for both bitnesses, stages the artifacts under build\stage\,
REM and packages them into a Total Commander auto-install ZIP whose name
REM carries the version read from src\pluginst.inf. After the build, dist\
REM contains nothing but the ZIP(s) — older versioned ZIPs are preserved.
REM
REM Layout after a successful build:
REM
REM   src\                              (all source files — committed)
REM   templates\                        (sample template set — committed)
REM   build\fingerprint-x64.txt         (toolchain, libraries, options and
REM   build\fingerprint-x86.txt          hashes of one build, for comparing
REM                                      two builds; see CONTRIBUTING.md)
REM   build\stage\                      (transient staging for ZIP contents)
REM     TCNewPlus.wfx                   (32-bit plugin DLL)
REM     TCNewPlus.wfx64                 (64-bit plugin DLL)
REM     Templates\                      (sample template set)
REM     sha256sums.sha256               (SHA-256 of every other staged file)
REM     pluginst.inf                    (TC auto-installer metadata)
REM     TCNewPlus.sample.ini            (sample config; never overwrites a
REM                                      user's TCNewPlus.ini on update)
REM     *.md                            (all repo-root Markdown docs)
REM   dist\
REM     TCNewPlus.v<version>.zip        (distributable bundle)
REM     ...                             (any older versioned ZIPs are kept)
REM
REM Prerequisites:
REM   - Visual Studio 2026 Build Tools with the components in .vsconfig
REM     (C++ toolset, Windows SDK, CMake)
REM   - PowerShell 5.1+ (ships with Windows; used for Compress-Archive)
REM ===========================================================================

REM ---------------------------------------------------------------------------
REM Pinned toolchain versions — update all four when upgrading (see
REM CONTRIBUTING.md, section "Reproducible Builds / Upgrading the pinned
REM toolchain"):
REM   EXPECTED_VS  — Visual Studio version (VsDevCmd banner); the CI workflow
REM                  installs exactly this one (VS_BUILDTOOLS_* in build.yml)
REM   EXPECTED_CL  — exact cl.exe version reported by "cl.exe /Bv"
REM   CMAKE_T      — -vcvars_ver= token passed to VsDevCmd.bat (toolset major.minor)
REM   CMAKE_SDK    — -DCMAKE_SYSTEM_VERSION= value (Windows SDK)
REM
REM With several Visual Studio installations, set TCNP_VSINSTALL to the folder
REM of the one to use (CI sets it to C:\BuildTools); otherwise vswhere picks
REM the newest.
REM ---------------------------------------------------------------------------
set EXPECTED_VS=18.11.2
set EXPECTED_CL=19.51.36260
set CMAKE_T=14.51
set CMAKE_SDK=10.0.26100.0

setlocal enabledelayedexpansion

REM %~dp0 expands with a trailing backslash; quoting "%ROOT%\" then escapes the
REM closing quote and breaks command-line parsing for cmake. Strip it.
set ROOT=%~dp0
if "%ROOT:~-1%"=="\" set ROOT=%ROOT:~0,-1%
set SRC=%ROOT%\src
set BUILD=%ROOT%\build
set STAGE=%BUILD%\stage
set OUT=%ROOT%\dist

if not exist "%BUILD%" mkdir "%BUILD%"
if not exist "%OUT%"   mkdir "%OUT%"

REM Locate Build Tools via vswhere and activate the x64 compiler environment.
REM cmake's VS generator instance detection fails for Build Tools installed
REM without VS IDE, so we use the NMake generator and configure the compiler
REM environment explicitly via VsDevCmd.bat for each target architecture.
set VSINSTALL=%TCNP_VSINSTALL%
if not defined VSINSTALL for /f "usebackq tokens=*" %%i in (`"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -products * -latest -property installationPath`) do set VSINSTALL=%%i
if not defined VSINSTALL (
    echo ERROR: Visual Studio Build Tools not found. Run setup-build-environment-example.cmd first.
    goto :error
)
set VSDEVCMD=%VSINSTALL%\Common7\Tools\VsDevCmd.bat
call "%VSDEVCMD%" -startdir=none -arch=x64 -host_arch=x64 -vcvars_ver=%CMAKE_T%
if errorlevel 1 goto :error

echo.
echo === Verify Visual Studio version ===
REM A Visual Studio update can service the toolset libraries and the Windows
REM SDK without changing the cl.exe version, so the Visual Studio version is
REM checked as well. Like the cl.exe check below, a mismatch only warns.
set VS_MISMATCH=0
if not "%VSCMD_VER%"=="%EXPECTED_VS%" (
    echo WARNING: Visual Studio version mismatch
    echo   expected : %EXPECTED_VS%
    echo   actual   : %VSCMD_VER% in !VSINSTALL!
    echo   Binaries from this build may differ from builds using Visual Studio %EXPECTED_VS%.
    set VS_MISMATCH=1
) else (
    echo Visual Studio %VSCMD_VER% OK
)

echo.
echo === Verify compiler version ===
REM Print the cl.exe version so it is visible in the build log. If a local
REM build and a CI build produce different binary hashes, compare this line
REM in both logs — a version mismatch is the first thing to check.
REM
REM A mismatch is a WARNING, not an error: cl.exe patch versions advance
REM independently on local machines and CI runner images and cannot be
REM synchronised without coordinated updates on both sides. EXPECTED_CL
REM documents the version at the time the pin was last set; update it
REM (along with a matching VS update on the other side) when you want to
REM re-establish bit-for-bit parity.
for /f "tokens=7 delims= " %%v in ('cl.exe 2^>^&1 ^| findstr /C:"Compiler Version"') do set ACTUAL_CL=%%v
set CL_MISMATCH=0
if not defined ACTUAL_CL (
    echo WARNING: could not determine cl.exe version
    set CL_MISMATCH=1
) else if not "%ACTUAL_CL%"=="%EXPECTED_CL%" (
    echo WARNING: cl.exe version mismatch
    echo   expected : %EXPECTED_CL%
    echo   actual   : %ACTUAL_CL%
    echo   Binaries from this build may differ from builds using %EXPECTED_CL%.
    set CL_MISMATCH=1
) else (
    echo cl.exe %ACTUAL_CL% OK
)

echo.
echo === Configure + build (x64) ===
cmake -S "%SRC%" -B "%BUILD%\x64" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_VERSION=%CMAKE_SDK%
if errorlevel 1 goto :error
cmake --build "%BUILD%\x64"
if errorlevel 1 goto :error

echo.
echo === Test (x64) ===
"%BUILD%\x64\TCNewPlusTest.exe" "%BUILD%\x64\TCNewPlus.wfx64"
if errorlevel 1 goto :error

echo.
echo === Fingerprint (x64) ===
call :fingerprint x64

echo.
echo === Configure + build (x86) ===
call "%VSDEVCMD%" -startdir=none -arch=x86 -host_arch=x64 -vcvars_ver=%CMAKE_T%
if errorlevel 1 goto :error
cmake -S "%SRC%" -B "%BUILD%\x86" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_VERSION=%CMAKE_SDK%
if errorlevel 1 goto :error
cmake --build "%BUILD%\x86"
if errorlevel 1 goto :error

echo.
echo === Test (x86) ===
REM The 32-bit test runs under WOW64 and loads the 32-bit plugin DLL.
"%BUILD%\x86\TCNewPlusTest.exe" "%BUILD%\x86\TCNewPlus.wfx"
if errorlevel 1 goto :error

echo.
echo === Fingerprint (x86) ===
call :fingerprint x86

echo.
echo === Stage ===
if exist "%STAGE%" rmdir /S /Q "%STAGE%"
mkdir "%STAGE%"

copy /Y "%BUILD%\x64\TCNewPlus.wfx64"  "%STAGE%\"  >nul
copy /Y "%BUILD%\x86\TCNewPlus.wfx"    "%STAGE%\"  >nul
copy /Y "%SRC%\pluginst.inf"           "%STAGE%\"  >nul
copy /Y "%SRC%\TCNewPlus.sample.ini"   "%STAGE%\"  >nul
copy /Y "%ROOT%\*.md"                  "%STAGE%\"  >nul
xcopy /E /I /Q "%ROOT%\templates" "%STAGE%\Templates" >nul
if errorlevel 1 goto :error

echo.
echo === Normalize line endings (staged text files) ===
REM Rewrite all line endings to CRLF in every text file in the stage,
REM including the sample templates. Must run before the checksum step so
REM hashes cover the final content. This is a Windows-only tool; CRLF is the
REM native line ending and ensures text files open correctly in any Windows
REM editor. Uses [char] casts to avoid batch quoting issues with escape
REM sequences.
powershell -NoLogo -NoProfile -Command ^
    "Get-ChildItem '%STAGE%' -Recurse -File | Where-Object { $_.Extension -in '.bat','.cmd','.inf','.ini','.md','.sha256','.txt','.url' } | ForEach-Object { $lf = [string][char]10; $cr = [string][char]13; $crlf = $cr + $lf; $t = [System.IO.File]::ReadAllText($_.FullName); [System.IO.File]::WriteAllText($_.FullName, $t.Replace($crlf, $lf).Replace($cr, $lf).Replace($lf, $crlf), [System.Text.UTF8Encoding]::new($false)) }"
if errorlevel 1 goto :error

echo.
echo === Checksums (all staged files) ===
REM SHA-256 of every file in the stage except sha256sums.sha256 itself, keyed
REM by the stage-relative path and sorted for a stable order. Written with
REM CRLF so it opens correctly in any Windows editor. Users can verify
REM extracted files against a known-good build; CI log shows the same hashes
REM for cross-build comparison.
powershell -NoLogo -NoProfile -Command ^
    "$stage = '%STAGE%'; $crlf = [string][char]13 + [string][char]10; $h = [System.Security.Cryptography.SHA256]::Create(); $d = [System.Collections.Generic.SortedDictionary[string,System.IO.FileInfo]]::new([System.StringComparer]::Ordinal); Get-ChildItem $stage -Recurse -File | Where-Object { $_.Name -ne 'sha256sums.sha256' } | ForEach-Object { $d[$_.FullName.Substring($stage.Length + 1)] = $_ }; $lines = $d.GetEnumerator() | ForEach-Object { [System.BitConverter]::ToString($h.ComputeHash([System.IO.File]::ReadAllBytes($_.Value.FullName))).Replace('-','').ToLower() + '  ' + $_.Key }; [System.IO.File]::WriteAllText((Join-Path $stage 'sha256sums.sha256'), ($lines -join $crlf) + $crlf, [System.Text.UTF8Encoding]::new($false)); $lines"
if errorlevel 1 goto :error

echo.
echo === Normalize file timestamps ===
REM Set all staged file and directory timestamps to the release date from
REM pluginst.inf so that ZIP entries have a deterministic mtime regardless of
REM when or where the build runs (no git dependency).
set RELEASE_DATE=
for /f "usebackq tokens=2 delims==" %%d in (`findstr /b /c:"release-date=" "%SRC%\pluginst.inf"`) do set RELEASE_DATE=%%d
if "%RELEASE_DATE%"=="" (
    echo Could not read release-date= from pluginst.inf
    goto :error
)
powershell -NoLogo -NoProfile -Command ^
    "$t = [datetime]::Parse('%RELEASE_DATE%'); Get-ChildItem -Path '%STAGE%' -Recurse | ForEach-Object { $_.LastWriteTime = $t; $_.CreationTime = $t }"
if errorlevel 1 goto :error

echo.
echo === Package ===

REM Pull the version out of pluginst.inf (single source of truth). The line
REM looks like   version=0.1.0   — split on '=' and grab the second token.
set VERSION=
for /f "usebackq tokens=2 delims==" %%v in (`findstr /b /c:"version=" "%SRC%\pluginst.inf"`) do set VERSION=%%v
if "%VERSION%"=="" (
    echo Could not read version= from pluginst.inf
    goto :error
)

set ZIP=%OUT%\TCNewPlus.v%VERSION%.zip
echo Building %ZIP%
powershell -NoLogo -NoProfile -Command ^
    "Compress-Archive -Force -DestinationPath '%ZIP%' -Path '%STAGE%\*'"
if errorlevel 1 goto :error

echo.
echo === Done ===
echo Output: %OUT%
dir /b "%OUT%"
echo.
echo SHA-256 (ZIP):
powershell -NoLogo -NoProfile -Command ^
    "$h = [System.Security.Cryptography.SHA256]::Create(); [System.BitConverter]::ToString($h.ComputeHash([System.IO.File]::ReadAllBytes('%ZIP%'))).Replace('-','').ToLower() + '  ' + (Split-Path '%ZIP%' -Leaf)"
if %VS_MISMATCH%==1 (
    echo.
    echo WARNING: Visual Studio version mismatch ^(expected %EXPECTED_VS%, used %VSCMD_VER%^); binaries from this build can differ from what was expected. Reproducible build not achieved.
)
if %CL_MISMATCH%==1 (
    echo.
    if not defined ACTUAL_CL (
        echo WARNING: cl.exe version could not be determined; reproducible build not achieved.
    ) else (
        echo WARNING: cl.exe version mismatch ^(expected %EXPECTED_CL%, used %ACTUAL_CL%^); binaries from this build can differ from what was expected. Reproducible build not achieved.
    )
)
goto :eof

:fingerprint
REM Writes build\fingerprint-%1.txt and prints it: everything that decides the
REM bytes of the shipped binaries, with machine-specific folders hidden, so that
REM the fingerprints of two builds can be compared line by line (fc). A failure
REM only warns: the fingerprint is a diagnostic, not part of the release.
REM -ExecutionPolicy Bypass applies to this one process, which runs a script
REM from this repository; the default policy on Windows clients refuses .ps1.
powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\build-fingerprint.ps1" -BuildDir "%BUILD%\%~1" -SourceDir "%SRC%" -OutFile "%BUILD%\fingerprint-%~1.txt"
if errorlevel 1 echo WARNING: could not write build\fingerprint-%~1.txt
exit /b 0

:error
echo BUILD FAILED
exit /b 1
