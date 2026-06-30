@echo off
setlocal

set "ORIGROOT=%~dp0"
set "DRIVE=X:"

subst %DRIVE% "%ORIGROOT%" >nul 2>nul
if errorlevel 1 (
    echo Forge error: failed to map %DRIVE% to project root.
    set "RC=1"
    goto fail
)

set "ROOT=%~dp0"
set "SRC=%ROOT%forge\helix\src"
set "IRSRC=%SRC%\ir"
set "CSRC=%ROOT%forge\c\src"
set "BIN=%ROOT%forge\bin"
set "OUT=%BIN%\forge.exe"
set "WORKDIR=%DRIVE%\forge-build"
set "WORKOUT=%WORKDIR%\forge.exe"
set "TMPDIR=%WORKDIR%\tmp"
set "TEMP=%TMPDIR%"

if not exist "%BIN%" mkdir "%BIN%" || goto fail
if not exist "%WORKDIR%" mkdir "%WORKDIR%" || goto fail
if not exist "%TMPDIR%" mkdir "%TMPDIR%" || goto fail

where gcc >nul 2>nul
if errorlevel 1 (
    echo Forge error: gcc not found in PATH.
    set "RC=1"
    goto fail
)

echo Building Forge...
gcc -std=c11 -O2 -Wall -Wextra ^
    -I"%SRC%" ^
    -I"%SRC%\ast" ^
    -I"%SRC%\lexer" ^
    -I"%SRC%\parser" ^
    -I"%SRC%\sema" ^
    -I"%SRC%\ir" ^
    -I"%SRC%\codegen" ^
    -I"%CSRC%" ^
    -I"%CSRC%\lexer" ^
    -I"%CSRC%\parser" ^
    -I"%CSRC%\frontend" ^
    "%ROOT%forge\main.c" ^
    "%SRC%\lexer\helix-lexer.c" ^
    "%SRC%\parser\helix-parser.c" ^
    "%SRC%\ast\helix-ast.c" ^
    "%SRC%\sema\helix-sema.c" ^
    "%SRC%\ir\ir.c" ^
    "%SRC%\ir\builder\ir-builder.c" ^
    "%SRC%\ir\ir-opt.c" ^
    "%SRC%\codegen\helix-codegen.c" ^
    "%CSRC%\lexer\c-lexer.c" ^
    "%CSRC%\parser\c-parser.c" ^
    "%CSRC%\frontend\c-frontend.c" ^
    -o "%WORKOUT%"

if errorlevel 1 (
    set "RC=1"
    goto fail
)

copy /y "%WORKOUT%" "%OUT%" >nul
if errorlevel 1 (
    set "RC=1"
    goto fail
)

set "RC=0"
echo Built: %OUT%
echo.
goto cleanup

:fail
echo Build failed.

:cleanup
if exist "%TMPDIR%" rmdir /s /q "%TMPDIR%"
if exist "%WORKDIR%\forge.exe" del /q "%WORKDIR%\forge.exe" >nul 2>nul
if exist "%WORKDIR%" rmdir /s /q "%WORKDIR%"
subst %DRIVE% /d >nul 2>nul
echo.
pause
exit /b %RC%
