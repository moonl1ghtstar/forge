@echo off
setlocal

set "ORIGROOT=%~dp0"
set "DRIVE=X:"

subst %DRIVE% "%ORIGROOT%" >nul 2>nul
if errorlevel 1 (
    echo Anvil error: failed to map %DRIVE% to project root.
    set "RC=1"
    goto fail
)

set "ROOT=%DRIVE%\"
set "SRC=%ROOT%anvil\helix\src"
set "IRSRC=%SRC%\ir"
set "CSRC=%ROOT%anvil\c\src"
set "BIN=%ROOT%anvil\bin\bin"
set "OUT=%BIN%\anv.exe"
set "WORKDIR=%DRIVE%\anv-build"
set "WORKOUT=%WORKDIR%\anv.exe"
set "TMPDIR=%WORKDIR%\temp"
set "TEMP=%TMPDIR%"
set "TMP=%TMPDIR%"

if not exist "%BIN%" mkdir "%BIN%" || goto fail
if not exist "%WORKDIR%" mkdir "%WORKDIR%" || goto fail
if not exist "%TMPDIR%" mkdir "%TMPDIR%" || goto fail

where gcc >nul 2>nul
if errorlevel 1 (
    echo Anvil error: gcc not found in PATH.
    set "RC=1"
    goto fail
)

where ar >nul 2>nul
if errorlevel 1 (
    echo Anvil error: ar not found in PATH.
    set "RC=1"
    goto fail
)

echo Building Anvil...
gcc -std=c11 -O2 -Wall -Wextra ^
    -I"%SRC%" ^
    -I"%SRC%\ast" ^
    -I"%SRC%\lexer" ^
    -I"%SRC%\parser" ^
    -I"%SRC%\sema" ^
    -I"%SRC%\ir" ^
    -I"%SRC%\codegen" ^
    -I"%SRC%\errors" ^
    -I"%ROOT%anvil\assembler" ^
    -I"%ROOT%anvil\assembler\src\lexer" ^
    -I"%ROOT%anvil\assembler\src\parser" ^
    -I"%ROOT%anvil\assembler\src\x86-encode" ^
    -I"%ROOT%anvil\assembler\src\coff-writer" ^
    -I"%ROOT%anvil\assembler\src\anv-asm" ^
    -I"%CSRC%" ^
    -I"%CSRC%\lexer" ^
    -I"%CSRC%\parser" ^
    -I"%CSRC%\frontend" ^
    "%ROOT%anvil\main.c" ^
    "%SRC%\lexer\helix-lexer.c" ^
    "%SRC%\parser\helix-parser.c" ^
    "%SRC%\ast\helix-ast.c" ^
    "%SRC%\sema\helix-sema.c" ^
    "%SRC%\ir\ir\ir.c" ^
    "%SRC%\ir\builder\ir-builder.c" ^
    "%SRC%\ir\opt\ir-opt.c" ^
    "%SRC%\codegen\helix-codegen.c" ^
    "%ROOT%anvil\assembler\src\lexer\asm-lexer.c" ^
    "%ROOT%anvil\assembler\src\parser\asm-parser.c" ^
    "%ROOT%anvil\assembler\src\x86-encode\x86-encode.c" ^
    "%ROOT%anvil\assembler\src\coff-writer\coff-writer.c" ^
    "%ROOT%anvil\assembler\src\anv-asm\anv-asm.c" ^
    "%SRC%\errors\helix-errors.c" ^
    "%CSRC%\lexer\c-lexer.c" ^
    "%CSRC%\parser\c-parser.c" ^
    "%CSRC%\frontend\c-frontend.c" ^
    -o "%WORKOUT%"

if errorlevel 1 (
    set "RC=1"
    goto fail
)

copy /y "%WORKOUT%" "%OUT%" >nul
if errorlevel 1 goto fail

echo Building standard libraries...
gcc -std=c11 -O2 -Wall -Wextra -c "%ROOT%module\helix\console\console.c" -o "%ROOT%module\helix\console\console.obj"
if errorlevel 1 (
    set "RC=1"
    goto fail
)

ar rcs "%ROOT%module\helix\console\console.lib" "%ROOT%module\helix\console\console.obj"
if errorlevel 1 (
    set "RC=1"
    goto fail
)

set "MODDIR=%ROOT%module\helix"
set "LIBDIR=%ROOT%anvil\bin\lib"

if not exist "%LIBDIR%" mkdir "%LIBDIR%"
echo Copying modules to %LIBDIR%...
xcopy /e /i /y "%MODDIR%" "%LIBDIR%" >nul
if errorlevel 1 (
    echo Anvil error: failed to copy modules.
    set "RC=1"
    goto fail
)

set "RC=0"
echo Built:
echo   %OUT%
echo   %LIBDIR% (modules)
echo.
goto cleanup

:fail
echo Build failed.

:cleanup
if exist "%TMPDIR%" rmdir /s /q "%TMPDIR%"
if exist "%WORKDIR%\anv.exe" del /q "%WORKDIR%\anv.exe" >nul 2>nul
if exist "%WORKDIR%" rmdir /s /q "%WORKDIR%"
subst %DRIVE% /d >nul 2>nul
echo.
pause
exit /b %RC%
