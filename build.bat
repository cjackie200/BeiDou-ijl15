@echo off
call "C:\Program Files\Microsoft Visual Studio\2026\Community\VC\Auxiliary\Build\vcvars32.bat"
cd /d D:\Game\BeiDou\BeiDou-ijl15\ezorsia
msbuild ezorsia.vcxproj /p:Configuration=Release /p:Platform=Win32
