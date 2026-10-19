@echo off
rem Builds dist\FoCCoopSetup.exe: the DLL, the Steam relay and the real 32-bit steam_api.dll embedded in one installer.
rem steam_api.dll: Steamworks.NET 2025.164.1 Windows-x86, SHA-256 fdafb7c65ec7d4182b42bf92cc8671c68e4dc547cc6e613899d2618a47eb65da
rem (path below, or pass another folder as the first argument).
rem ReShade extra: installer\reshade\* + ReShade32.dll from ReShade_Setup_6.8.0.exe (SHA-256 of the setup:
rem 207aea16205fbf952bc8fe1879966672454cf04002e7ad34237c7990a5b3c0b4) as d3d9.dll, zipped into build\reshade.zip.
setlocal
set ROOT=%~dp0..
set STEAMAPI=%~1
if "%STEAMAPI%"=="" set STEAMAPI=C:\GitRepo\coop-anything\local\tools\steamworksnet\x\Windows-x86\steam_api.dll
set RESHADEDLL=%~2
if "%RESHADEDLL%"=="" set RESHADEDLL=E:\FoC-coop\downloads\reshade\dll\ReShade32.dll
call "%ROOT%\build.bat" || exit /b 1
call "%ROOT%\relay\build_relay.bat" || exit /b 1
if not exist "%ROOT%\dist" mkdir "%ROOT%\dist"
set STAGE=%ROOT%\build\reshade_stage
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%"
xcopy /e /q /y "%~dp0reshade\*" "%STAGE%\" >nul || exit /b 1
copy /y "%RESHADEDLL%" "%STAGE%\d3d9.dll" >nul || exit /b 1
powershell -NoProfile -Command "Compress-Archive -Force -Path '%STAGE%\*' -DestinationPath '%ROOT%\build\reshade.zip'" || exit /b 1
"%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe" /nologo /target:winexe /optimize /out:"%ROOT%\dist\FoCCoopSetup.exe" ^
  /resource:"%ROOT%\build\dinput8.dll",dinput8.dll ^
  /resource:"%ROOT%\relay\build\FoCRelay.exe",FoCRelay.exe ^
  /resource:"%STEAMAPI%",steam_api.dll ^
  /resource:"%ROOT%\build\reshade.zip",reshade.zip ^
  /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll ^
  "%~dp0Setup.cs" "%~dp0Mods.cs"
exit /b %ERRORLEVEL%
