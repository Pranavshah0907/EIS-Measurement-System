@echo off
setlocal
cd /d "%~dp0"

echo.
echo ============================================================
echo   EIS Measurement System - Setup
echo ============================================================
echo.

:: ── Check Python ─────────────────────────────────────────────
python --version >nul 2>&1
if errorlevel 1 goto :nopython
for /f "tokens=*" %%v in ('python --version') do echo   Found: %%v
goto :havepython

:nopython
echo ERROR: Python not found on PATH.
echo   Install Python 3.10+ from https://python.org
echo   Make sure to check "Add Python to PATH" during install.
pause
exit /b 1

:havepython

:: ── Create virtual environment ───────────────────────────────
if exist "pc\.venv\Scripts\python.exe" (
    echo   Virtual environment already exists.
) else (
    echo   Creating virtual environment ...
    python -m venv pc\.venv
    if errorlevel 1 goto :venvfail
    echo   Done.
)
goto :venvok

:venvfail
echo ERROR: venv creation failed.
pause
exit /b 1

:venvok

:: ── Install dependencies ──────────────────────────────────────
echo   Installing dependencies ...
pc\.venv\Scripts\pip install -q -r pc\requirements.txt
if errorlevel 1 goto :pipfail
echo   Done.
goto :pipok

:pipfail
echo ERROR: pip install failed.
pause
exit /b 1

:pipok

:: ── Kill stale server on port 5000 ───────────────────────────
echo   Checking for existing server on port 5000 ...
for /f "tokens=5" %%p in ('netstat -aon ^| findstr :5000 ^| findstr LISTENING') do (
    echo   Stopping previous server PID %%p ...
    taskkill /PID %%p /F >nul 2>&1
)

:: ── Start server ─────────────────────────────────────────────
echo   Starting server ...
start "EIS Server" /min pc\.venv\Scripts\python.exe pc\server.py

:: ── Wait and open browser ────────────────────────────────────
echo   Waiting for server to start ...
timeout /t 3 /nobreak >nul
start http://localhost:5000

echo.
echo ============================================================
echo   Server is running at http://localhost:5000
echo   Browser should open automatically.
echo.
echo   To stop: close the "EIS Server" window, or press Ctrl+C here.
echo.
echo   Prerequisites (install once per PC):
echo     - Python 3.10+       https://python.org
echo     - Vector XL Driver   (matches your VN1640A)
echo     - Vector HW Config:  app "EIS-Measurement-System", CAN1=CH1, 125 kbps
echo ============================================================
echo.
pause
