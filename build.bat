@echo off
rem Builds the 32-bit dinput8.dll proxy (with the in-game menu). Run from anywhere.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
if not exist "%~dp0build" mkdir "%~dp0build"
pushd "%~dp0build"
set IM=%~dp0third_party\imgui
cl /nologo /LD /O2 /MT /EHsc /W3 /I"%IM%" /I"%~dp0src\proxy_dinput8" "%~dp0src\proxy_dinput8\dinput8.cpp" "%IM%\imgui.cpp" "%IM%\imgui_draw.cpp" "%IM%\imgui_tables.cpp" "%IM%\imgui_widgets.cpp" "%IM%\backends\imgui_impl_dx9.cpp" /Fe:dinput8.dll /link /MAP /DEF:"%~dp0src\proxy_dinput8\dinput8.def" ws2_32.lib user32.lib d3d9.lib advapi32.lib shell32.lib
set RC=%ERRORLEVEL%
popd
exit /b %RC%
