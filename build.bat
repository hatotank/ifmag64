@echo off
rem Change to workspace directory (handles spaces)
pushd "%~dp0"

rem Load Visual Studio environment (adjust the path if necessary)
call "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\vcvarsall.bat" amd64

rem Run build command
rem Use /wd4819 to suppress warning C4819 (sources are UTF-8)
cl.exe /LD /O2 /EHsc /wd4819 "src\ifmag64.cpp" /Fo"dist\ifmag64.obj" /Fe"dist\ifmag64.sph" user32.lib

rem Return to the original directory after the build completes
popd
