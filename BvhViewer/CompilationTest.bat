@echo off
setlocal EnableDelayedExpansion

rem ============================================================
rem  CompilationTest.bat
rem  Builds the BvhViewer project for x64, ARM64 and ARM64EC,
rem  with both the default MSVC toolset and the ClangCL toolset,
rem  in both Debug and Release configurations. Each combination
rem  is configured into its own build folder so the builds do
rem  not interfere with each other. The Visual Studio CMake
rem  generator is multi-config, so a single configure step
rem  covers both Debug and Release for each combination.
rem
rem  The ARM64EC + ClangCL combination is excluded from the test
rem  matrix and is reported as SKIPPED in the summary.
rem
rem  All build folders are placed under a single _CompilationTest
rem  parent directory (e.g. _CompilationTest\x64_MSVC). The parent
rem  directory is wiped at the start of the run to ensure a clean
rem  baseline.
rem
rem  After every configuration of an arch/toolset combination
rem  builds successfully, the corresponding build folder is
rem  removed automatically. Folders for combinations that failed
rem  to configure, or that had any failing build, are kept on
rem  disk so the failure can be investigated. If every combination
rem  passes the _CompilationTest folder is removed entirely.
rem
rem  Usage:
rem      CompilationTest.bat
rem ============================================================

pushd "%~dp0"

echo.
echo ============================================================
echo  BvhViewer compilation test
echo  Architectures:   x64, ARM64, ARM64EC
echo  Toolsets:        MSVC, ClangCL
echo  Configurations:  Debug, Release
echo  Excluded:        ARM64EC + ClangCL
echo ============================================================

set "ARCH_LIST=x64 ARM64 ARM64EC"
set "TOOLSET_LIST=MSVC ClangCL"
set "CONFIG_LIST=Debug Release"
set "ROOT_BUILD_DIR=_CompilationTest"

set "OVERALL_RESULT=0"
set "SUMMARY="

rem --- Pre-cleanup: wipe the parent build folder so we always
rem     start from a clean baseline.
echo.
echo ------------------------------------------------------------
echo  Pre-run cleanup of "%ROOT_BUILD_DIR%"
echo ------------------------------------------------------------
if exist "%ROOT_BUILD_DIR%" (
    echo   Removing "%ROOT_BUILD_DIR%"
    rmdir /s /q "%ROOT_BUILD_DIR%"
    if exist "%ROOT_BUILD_DIR%" (
        echo   WARNING: Could not fully remove "%ROOT_BUILD_DIR%".
    )
) else (
    echo   "%ROOT_BUILD_DIR%" does not exist, nothing to clean.
)

for %%A in (%ARCH_LIST%) do (
    for %%T in (%TOOLSET_LIST%) do (
        set "ARCH=%%A"
        set "TOOLSET=%%T"

        rem Combinations that are intentionally excluded from the test matrix.
        rem ClangCL on ARM64EC is not part of the supported configurations.
        set "SKIP_COMBO=0"
        if /i "!ARCH!"=="ARM64EC" if /i "!TOOLSET!"=="ClangCL" set "SKIP_COMBO=1"

        if "!SKIP_COMBO!"=="1" (
            echo.
            echo ------------------------------------------------------------
            echo  [!ARCH! / !TOOLSET!] Skipped ^(excluded combination^).
            echo ------------------------------------------------------------
            set "SUMMARY=!SUMMARY!  !ARCH! / !TOOLSET!: SKIPPED;"
        ) else (
            set "BUILD_DIR=!ROOT_BUILD_DIR!\!ARCH!_!TOOLSET!"
            set "ARCH_FAILED=0"

            rem MSVC is the default toolset and takes no -T flag.
            rem ClangCL (and any other non-default toolset) is selected via -T <name>.
            set "TOOLSET_ARG="
            if /i not "!TOOLSET!"=="MSVC" set "TOOLSET_ARG=-T !TOOLSET!"

            echo.
            echo ------------------------------------------------------------
            echo  [!ARCH! / !TOOLSET!] Configuring  ^( -A !ARCH! -B !BUILD_DIR! !TOOLSET_ARG! ^)
            echo ------------------------------------------------------------

            cmake -S . -B "!BUILD_DIR!" -A !ARCH! !TOOLSET_ARG!
            if errorlevel 1 (
                echo [!ARCH! / !TOOLSET!] CMake configuration FAILED.
                echo [!ARCH! / !TOOLSET!] Keeping build folder for investigation: !BUILD_DIR!
                set "OVERALL_RESULT=1"
                set "SUMMARY=!SUMMARY!  !ARCH! / !TOOLSET!: CONFIGURE FAILED;"
            ) else (
                for %%C in (%CONFIG_LIST%) do (
                    set "CFG=%%C"

                    echo.
                    echo ------------------------------------------------------------
                    echo  [!ARCH! / !TOOLSET! / !CFG!] Building
                    echo ------------------------------------------------------------

                    cmake --build "!BUILD_DIR!" --config !CFG!
                    if errorlevel 1 (
                        echo [!ARCH! / !TOOLSET! / !CFG!] Build FAILED.
                        set "OVERALL_RESULT=1"
                        set "ARCH_FAILED=1"
                        set "SUMMARY=!SUMMARY!  !ARCH! / !TOOLSET! [!CFG!]: BUILD FAILED;"
                    ) else (
                        echo [!ARCH! / !TOOLSET! / !CFG!] Build SUCCEEDED.
                        set "SUMMARY=!SUMMARY!  !ARCH! / !TOOLSET! [!CFG!]: OK;"
                    )
                )

                if "!ARCH_FAILED!"=="0" (
                    echo.
                    echo [!ARCH! / !TOOLSET!] All configurations built successfully.
                    echo [!ARCH! / !TOOLSET!] Cleaning up build folder: !BUILD_DIR!
                    rmdir /s /q "!BUILD_DIR!"
                    if exist "!BUILD_DIR!" (
                        echo [!ARCH! / !TOOLSET!] WARNING: Could not fully remove "!BUILD_DIR!".
                    )
                ) else (
                    echo.
                    echo [!ARCH! / !TOOLSET!] One or more configurations failed.
                    echo [!ARCH! / !TOOLSET!] Keeping build folder for investigation: !BUILD_DIR!
                )
            )
        )
    )
)

rem --- Post-cleanup: drop the parent folder when every combination
rem     has already cleaned up its own subfolder. Use rmdir without
rem     /s so a non-empty folder (kept failures) is preserved.
if exist "%ROOT_BUILD_DIR%" (
    rmdir "%ROOT_BUILD_DIR%" 2>nul
)

echo.
echo ============================================================
echo  Compilation test summary
echo ============================================================
for %%S in ("!SUMMARY:;=" "!") do (
    if not %%S=="" echo %%~S
)

echo.
if "%OVERALL_RESULT%"=="0" (
    echo Result: ALL COMBINATIONS COMPILED SUCCESSFULLY.
) else (
    echo Result: ONE OR MORE COMBINATIONS FAILED.
)
echo ============================================================

if not "%OVERALL_RESULT%"=="0" (
    echo.
    echo One or more builds failed. The console window will stay open
    echo so the output can be reviewed when this script is launched
    echo directly from Explorer.
    pause
)

popd
endlocal & exit /b %OVERALL_RESULT%
