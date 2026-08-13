@echo off
rem ============================================================
rem  build.bat  --  convenience wrapper around msbuild.exe
rem
rem  Usage:
rem    build.bat                    Build Release x64 (default)
rem    build.bat Debug              Build Debug   x64
rem    build.bat Release "D:\IBM\MQ"  Override MQ install dir
rem
rem  Prerequisites:
rem    msbuild.exe must be in the stated directory.
rem    Visual C++ toolset must be available (run from a VS
rem    Developer Command Prompt, or call vcvarsall.bat first).
rem ============================================================

setlocal

set CONFIG=%~1
set MQDIR=%~2

if "%CONFIG%"=="" set CONFIG=Release
if "%MQDIR%"=="" set MQDIR=C:\Program Files\IBM\MQ

echo.
echo Building mqinootel  [Configuration=%CONFIG%  MQInstallDir=%MQDIR%]
echo.

set MSBUILD="C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"

%MSBUILD% mqinootel.vcxproj ^
    /nologo ^
    /p:Configuration=%CONFIG% ^
    /p:Platform=x64 ^
    /p:MQInstallDir="%MQDIR%" ^
    /m ^
    /v:minimal

if %ERRORLEVEL% neq 0 (
    echo.
    echo BUILD FAILED  ^(exit code %ERRORLEVEL%^)
    exit /b %ERRORLEVEL%
)

echo.
echo Build succeeded.  Output: bin\x64\%CONFIG%\mqinootel.dll
endlocal
