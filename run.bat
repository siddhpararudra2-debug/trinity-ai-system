@echo off
REM Double-clickable entry point for Trinity. All the real work is in
REM start.ps1; this only finds it and hands over, so there is one launcher
REM to keep correct rather than two.
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0start.ps1" %*
set EXITCODE=%ERRORLEVEL%
if not "%EXITCODE%"=="0" (
  echo.
  echo Trinity exited with code %EXITCODE%.
  pause
)
endlocal & exit /b %EXITCODE%