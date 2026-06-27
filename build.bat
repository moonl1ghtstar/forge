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

set "ROOT=%DRIVE%\"
set "SRC=%ROOT%forge\helix\src"
set "BIN=%ROOT%forge\bin"
set "OUT=%BIN%\forge.exe"
set "TMPDIR=%BIN%\.build-tmp"
set "TEMP=%TMPDIR%"
set "TMP=%TMPDIR%"

if not exist "%BIN%" mkdir "%BIN%" || goto fail
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
    -I"%SRC%\codegen" ^
    "%SRC%\main.c" ^
    "%SRC%\lexer\helix-lexer.c" ^
    "%SRC%\parser\helix-parser.c" ^
    "%SRC%\ast\helix-ast.c" ^
    "%SRC%\sema\helix-sema.c" ^
    "%SRC%\codegen\helix-codegen.c" ^
    -o "%OUT%"

if errorlevel 1 (
    set "RC=1"
    goto fail
)

set "RC=0"
echo Built: %OUT%
goto cleanup

:fail
echo Build failed.

:cleanup
if exist "%TMPDIR%" rmdir /s /q "%TMPDIR%"
subst %DRIVE% /d >nul 2>nul
echo.
pause
exit /b %RC%
