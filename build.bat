@echo off
REM Build with MinGW-w64 from this folder.
windres app.rc -O coff -o app.res
if errorlevel 1 exit /b %errorlevel%
g++ -O2 -std=c++17 -municode -mwindows -static -static-libgcc -static-libstdc++ "CPS by Rafu.cpp" app.res -o "CPS by Rafu.exe" -luser32 -lcomctl32 -luxtheme -ldwmapi -lwinmm -lgdi32
if errorlevel 1 exit /b %errorlevel%
echo Done: CPS by Rafu.exe
