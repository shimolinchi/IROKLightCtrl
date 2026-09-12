@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-ambient.ps1" -Configuration Release
if errorlevel 1 (
  echo Installation did not complete.
) else (
  echo Installation complete. You can close this window.
)
pause
