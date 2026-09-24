@echo off
rem fo4-ocbpc: the face written after the merge, tested outside the game (face_test.cpp says what).
rem Builds with MSVC (Build Tools 2022) into %TEMP%\fo4-ocbpc-face-test and runs; exit code 1 on a failure.
setlocal
set "PATH=%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\Installer"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "SRC=%~dp0..\..\CBPSSE"
set "OUT=%TEMP%\fo4-ocbpc-face-test"
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /EHsc /std:c++17 /W3 /I "%SRC%" "%~dp0face_test.cpp" "%SRC%\FaceAuthority.cpp" "%SRC%\FaceCompose.cpp" /Fo"%OUT%\\" /Fe"%OUT%\face_test.exe" >"%OUT%\build.log" 2>&1 || (type "%OUT%\build.log" & exit /b 1)
"%OUT%\face_test.exe"
