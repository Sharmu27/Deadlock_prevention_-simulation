@echo off
REM Builds both programs on Windows with MinGW gcc (no make needed)
cd /d "%~dp0"
echo Building simulator.exe ...
gcc -Wall -Wextra -g -std=c99 -o simulator.exe src\main.c src\models.c src\detection.c src\bankers.c src\prevention.c src\recovery.c src\visualize.c src\export.c src\util.c
if %errorlevel% neq 0 goto fail
echo Building workload.exe ...
gcc -Wall -Wextra -g -std=c99 -o workload.exe src\workload.c src\rm.c src\threads.c src\models.c src\detection.c src\bankers.c src\prevention.c src\recovery.c src\visualize.c src\export.c src\util.c
if %errorlevel% neq 0 goto fail
echo.
echo Build OK.  Now run:  run_demo.bat
exit /b 0
:fail
echo.
echo BUILD FAILED - copy the error above and send it.
exit /b 1
