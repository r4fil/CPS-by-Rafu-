@echo off
setlocal
cd /d "%~dp0"
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" goto missing
call "%VCVARS%" x64 >nul
if errorlevel 1 exit /b 1
rc.exe /nologo /fo setup.res setup.rc
if errorlevel 1 exit /b 1
cl.exe /nologo /O2 /MT /std:c++17 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS setup.cpp setup.res /Fe:"CPS by Rafu Setup.exe" /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib ole32.lib advapi32.lib comctl32.lib uuid.lib
if errorlevel 1 exit /b 1
echo.
echo Created: %CD%\CPS by Rafu Setup.exe
exit /b 0

:missing
echo Visual Studio Build Tools were not found at:
echo %VCVARS%
exit /b 1
