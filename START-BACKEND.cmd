@echo off
setlocal
cd /d "%~dp0"
set "PATH=%~dp0dependencies\msys2\ucrt64\bin;C:\msys64\ucrt64\bin;%PATH%"
if not exist "build\backend-windows\lms-backend.exe" (
  echo Run backend\build-windows.ps1 first.
  pause
  exit /b 1
)
echo Drogon framework checkpoint: http://127.0.0.1:18082/api/health
echo This entry does not serve the existing order UI yet.
"build\backend-windows\lms-backend.exe"
pause
