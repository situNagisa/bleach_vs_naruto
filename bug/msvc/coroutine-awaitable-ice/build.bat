@echo off
setlocal

set "ROOT=%~dp0"
set "STDEXEC_INCLUDE=D:\project\stdexec\include"
set "NAGISA_INCLUDE=D:\project\nagisa\include"

where cl.exe >nul 2>nul
if errorlevel 1 (
  echo cl.exe was not found. Run this from a Visual Studio Developer Command Prompt,
  echo or call vcvars64.bat before running this script.
  exit /b 1
)

set "FAILED=0"

echo.
echo === standalone_run_this_ice.cpp: expected MSVC C1001 ===
cl /nologo /c /std:c++latest /permissive- /Zc:__cplusplus /Zc:preprocessor /EHsc /utf-8 /W4 ^
  "%ROOT%standalone_run_this_ice.cpp" ^
  /Fo"%TEMP%\standalone_run_this_ice.obj" /Fd"%TEMP%\standalone_run_this_ice.pdb"
if "%ERRORLEVEL%"=="0" set "FAILED=1"

echo.
echo === standalone_capture_stop_token_ice.cpp: expected MSVC C1001 ===
cl /nologo /c /std:c++latest /permissive- /Zc:__cplusplus /Zc:preprocessor /EHsc /utf-8 /W4 ^
  "%ROOT%standalone_capture_stop_token_ice.cpp" ^
  /Fo"%TEMP%\standalone_capture_stop_token_ice.obj" /Fd"%TEMP%\standalone_capture_stop_token_ice.pdb"
if "%ERRORLEVEL%"=="0" set "FAILED=1"

echo.
echo === run_this_sender_ice.cpp: Nagisa/stdexec source-shape repro ===
cl /nologo /c /std:c++latest /permissive- /Zc:__cplusplus /Zc:preprocessor /EHsc /utf-8 /W4 ^
  /I "%STDEXEC_INCLUDE%" /I "%NAGISA_INCLUDE%" ^
  "%ROOT%run_this_sender_ice.cpp" ^
  /Fo"%TEMP%\run_this_sender_ice.obj" /Fd"%TEMP%\run_this_sender_ice.pdb"
if "%ERRORLEVEL%"=="0" set "FAILED=1"

echo.
echo === capture_inplace_stop_token_sender_ice.cpp: expected MSVC C1001 at stop_token.h(159) ===
cl /nologo /c /std:c++latest /permissive- /Zc:__cplusplus /Zc:preprocessor /EHsc /utf-8 /W4 ^
  /I "%STDEXEC_INCLUDE%" /I "%NAGISA_INCLUDE%" ^
  "%ROOT%capture_inplace_stop_token_sender_ice.cpp" ^
  /Fo"%TEMP%\capture_inplace_stop_token_sender_ice.obj" /Fd"%TEMP%\capture_inplace_stop_token_sender_ice.pdb"
if "%ERRORLEVEL%"=="0" set "FAILED=1"

echo.
echo === capture_inplace_stop_token_min_ice.cpp: smaller investigation case ===
cl /nologo /c /std:c++latest /permissive- /Zc:__cplusplus /Zc:preprocessor /EHsc /utf-8 /W4 ^
  /I "%STDEXEC_INCLUDE%" /I "%NAGISA_INCLUDE%" ^
  "%ROOT%capture_inplace_stop_token_min_ice.cpp" ^
  /Fo"%TEMP%\capture_inplace_stop_token_min_ice.obj" /Fd"%TEMP%\capture_inplace_stop_token_min_ice.pdb"
if "%ERRORLEVEL%"=="0" set "FAILED=1"

if "%FAILED%"=="1" (
  echo.
  echo At least one expected ICE did not reproduce.
  endlocal
  exit /b 1
)

echo.
echo All expected compiler failures reproduced.
endlocal
exit /b 0
