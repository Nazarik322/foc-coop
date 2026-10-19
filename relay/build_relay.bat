@echo off
rem Builds relay\build\FoCRelay.exe and relay\build\mock\steam_api.dll (32-bit). Run from anywhere.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
if not exist "%~dp0build\mock" mkdir "%~dp0build\mock"
pushd "%~dp0build"
cl /nologo /O2 /MT /W3 /D_CRT_SECURE_NO_WARNINGS "%~dp0focrelay.c" /Fe:FoCRelay.exe /link ws2_32.lib advapi32.lib
if errorlevel 1 (popd & exit /b 1)
cd mock
cl /nologo /LD /O2 /MT /W3 /D_CRT_SECURE_NO_WARNINGS "%~dp0mock\mock_steam_api.c" /Fe:steam_api.dll /link ws2_32.lib
set RC=%ERRORLEVEL%
popd
exit /b %RC%
