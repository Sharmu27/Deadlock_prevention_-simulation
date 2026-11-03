@echo off
REM Opens just the live dashboard (it shows whichever program is running).
cd /d "%~dp0"
python visualizer\live_view.py
pause
