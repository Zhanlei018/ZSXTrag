@echo off
chcp 65001 >nul
cd /d "%~dp0"
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto :error
cmake --build build -j
if errorlevel 1 goto :error
echo.
echo 构建完成：build\hxrag.exe
exit /b 0
:error
echo 构建失败，请确认已安装 CMake 和 MinGW g++。
pause
exit /b 1
