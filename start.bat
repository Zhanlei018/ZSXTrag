@echo off
chcp 65001 >nul
cd /d "%~dp0"
if not exist config.json copy /y config.example.json config.json >nul
if not exist build\hxrag.exe call build.bat
if not exist build\hxrag.exe exit /b 1
start "" cmd /c "timeout /t 2 /nobreak >nul & start http://127.0.0.1:8790"
build\hxrag.exe serve
pause
