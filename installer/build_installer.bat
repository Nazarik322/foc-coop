@echo off
rem Builds dist\FoCCoopSetup.exe: the DLL, the Steam relay and the real 32-bit steam_api.dll embedded in one installer.
rem steam_api.dll: Steamworks.NET 2025.164.1 Windows-x86, SHA-256 fdafb7c65ec7d4182b42bf92cc8671c68e4dc547cc6e613899d2618a47eb65da
rem (path below, or pass another folder as the first argument).
setlocal
set ROOT=%~dp0..
set STEAMAPI=%~1
if "%STEAMAPI%"=="" set STEAMAPI=C:\GitRepo\coop-anything\local\tools\steamworksnet\x\Windows-x86\steam_api.dll
call "%ROOT%\build.bat" || exit /b 1
call "%ROOT%\relay\build_relay.bat" || exit /b 1
if not exist "%ROOT%\dist" mkdir "%ROOT%\dist"
"%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe" /nologo /target:winexe /optimize /out:"%ROOT%\dist\FoCCoopSetup.exe" ^
  /resource:"%ROOT%\build\dinput8.dll",dinput8.dll ^
  /resource:"%ROOT%\relay\build\FoCRelay.exe",FoCRelay.exe ^
  /resource:"%STEAMAPI%",steam_api.dll ^
  "%~dp0Setup.cs"
exit /b %ERRORLEVEL%
