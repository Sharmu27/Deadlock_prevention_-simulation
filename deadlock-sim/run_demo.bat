@echo off
REM ONE-CLICK DEMO: opens the live dashboard, then runs the whole Workload Lab
REM automatically (no menus, no typing).
cd /d "%~dp0"
if not exist workload.exe call build.bat
if not exist workload.exe goto end
del system_state.json 2>nul
start "Live Dashboard" cmd /k python visualizer\live_view.py
echo Waiting for the dashboard window to open...
timeout /t 4 /nobreak >nul
workload.exe %*
echo.
echo Finished. Results are in workload_results.csv
:end
pause
