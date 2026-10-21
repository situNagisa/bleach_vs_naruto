@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 (
 echo Run from an x64 Visual Studio Developer Command Prompt.
 exit /b 1
)
set "repro_middle=stop_middle.cppm"
set "repro_variant=failing"
if /I "%~1"=="control" (
 set "repro_middle=stop_middle_control.cppm"
 set "repro_variant=control"
)
if not exist "%~dp0out\%repro_variant%" mkdir "%~dp0out\%repro_variant%"
pushd "%~dp0out\%repro_variant%"
cl /nologo /c /std:c++latest /EHsc /MDd /interface /TP "%~dp0stop_origin.cppm" /ifcOutput stop_origin.ifc /Fostop_origin.obj
if errorlevel 1 goto failed
cl /nologo /c /std:c++latest /EHsc /MDd /interface /TP "%~dp0%repro_middle%" /reference stop_origin=stop_origin.ifc /ifcOutput stop_middle.ifc /Fostop_middle.obj
if errorlevel 1 goto failed
cl /nologo /c /std:c++latest /EHsc /MDd "%~dp0use.cpp" /reference probe=stop_middle.ifc /reference stop_origin=stop_origin.ifc /Fouse.obj
if errorlevel 1 goto failed
popd
exit /b 0
:failed
set "repro_exit=%errorlevel%"
popd
exit /b %repro_exit%
