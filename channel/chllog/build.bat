setlocal
set MSBUILD="C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
%MSBUILD% /p:Configuration=Release /p:Platform=x64 /v:minimal 
