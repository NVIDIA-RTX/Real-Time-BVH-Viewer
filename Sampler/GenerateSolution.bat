@echo off
set "BUILD_DIR=_Build"
if exist "%BUILD_DIR%\CMakeCache.txt" del /f /q "%BUILD_DIR%\CMakeCache.txt"
@echo on

cmake -S . -B %BUILD_DIR% -A x64
