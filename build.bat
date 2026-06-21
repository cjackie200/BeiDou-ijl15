@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
cd /d D:\Game\BeiDou\BeiDou-ijl15\ezorsia
msbuild ezorsia.vcxproj /p:Configuration=Release /p:Platform=Win32
