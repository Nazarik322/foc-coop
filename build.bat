@echo off
rem Builds the 32-bit dinput8.dll proxy. Run from anywhere.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
if not exist "%~dp0build" mkdir "%~dp0build"
pushd "%~dp0build"
cl /nologo /LD /O2 /MT /EHsc /W3 "%~dp0src\proxy_dinput8\dinput8.cpp" /Fe:dinput8.dll /link /DEF:"%~dp0src\proxy_dinput8\dinput8.def" ws2_32.lib user32.lib
set RC=%ERRORLEVEL%
popd
exit /b %RC%
