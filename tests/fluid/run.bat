@echo off
rem fo4-ocbpc: the climax fluid's particles (FluidSim.h), tested outside the game (fluid_test.cpp says what).
rem Builds with MSVC (Build Tools 2022) into %TEMP%\fo4-ocbpc-fluid-test and runs; exit code 1 on a failure.
setlocal
set "PATH=%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\Installer"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "SRC=%~dp0..\..\CBPSSE-RD"
set "OUT=%TEMP%\fo4-ocbpc-fluid-test"
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /EHsc /std:c++20 /O2 /W3 /I "%SRC%" "%~dp0fluid_test.cpp" "%SRC%\FluidSim.cpp" /Fo"%OUT%\\" /Fe"%OUT%\fluid_test.exe" >"%OUT%\build.log" 2>&1 || (type "%OUT%\build.log" & exit /b 1)
"%OUT%\fluid_test.exe"
