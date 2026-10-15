@echo off
setlocal
for %%I in ("%~dp0..") do set "PROJECT_DIR=%%~fI"
set "CTEST_EXE=ctest.exe"
where ctest.exe >nul 2>&1 || (echo CTest was not found in PATH.& exit /b 1)
if not exist "%PROJECT_DIR%\test-output" mkdir "%PROJECT_DIR%\test-output"
"%CTEST_EXE%" --test-dir "%PROJECT_DIR%\build" -C Release -R "^window_capture$" --output-on-failure --output-log "%PROJECT_DIR%\test-output\capture-desktop.log"
exit /b %errorlevel%
