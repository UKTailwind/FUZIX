@echo off
rem  build.bat - translate and build one MMBasic program on Windows
rem
rem    build tests\t1.bas          -> build\t1.exe
rem    build tests\t1.bas run      -> ... and run it
rem
rem  Works from a Visual Studio developer prompt (cl) or with gcc on PATH.

setlocal
if "%~1"=="" (
  echo usage: build ^<program.bas^> [run]
  exit /b 1
)
if not exist build mkdir build
for %%F in ("%~1") do set NAME=%%~nF

python mmb2c.py "%~1" -o "build\%NAME%.c" || exit /b 1

where cl >nul 2>nul
if %errorlevel%==0 (
  cl /nologo /W3 /I. /Fo:build\ /Fe:build\%NAME%.exe build\%NAME%.c mmb_runtime.c || exit /b 1
) else (
  where gcc >nul 2>nul || (echo Need cl or gcc on PATH & exit /b 1)
  gcc -std=c99 -Wall -I. -o build\%NAME%.exe build\%NAME%.c mmb_runtime.c -lm || exit /b 1
)

echo built build\%NAME%.exe
if /i "%~2"=="run" build\%NAME%.exe
endlocal
