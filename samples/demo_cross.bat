@echo off
rem ============================================================
rem  demo_cross.bat  –  Forge cross-language linking demo
rem
rem  Shows how to combine a Helix .obj (main entry) with a
rem  Forge-compiled C .obj (library functions) under the same
rem  Windows x64 ABI linker.
rem
rem  Source files used:
rem    cross_main.hlx  – Helix: calls add() and mul() from clib.c
rem    clib.c          – C:     provides add(a,b) and mul(a,b)
rem
rem  Expected exit code: 42  (mul(6,7) = 42)
rem
rem  Prerequisites: forge.exe on PATH, nasm on PATH, ld on PATH
rem ============================================================
setlocal

set "FORGE=forge"
set "SAMPLES=%~dp0"

echo.
echo [1/4] Preview Helix assembly (-asm)
%FORGE% "%SAMPLES%cross_main.hlx" -asm -o "%SAMPLES%cross_main.asm"
if errorlevel 1 goto fail

echo.
echo [2/4] Compile Helix source to .obj
%FORGE% "%SAMPLES%cross_main.hlx" -obj -o "%SAMPLES%cross_main.obj"
if errorlevel 1 goto fail

echo.
echo [3/4] Compile C library to .obj  (library mode – no main injected)
%FORGE% "%SAMPLES%clib.c" -obj -o "%SAMPLES%clib.obj"
if errorlevel 1 goto fail

echo.
echo [4/4] Link both objects into cross.exe
%FORGE% -link "%SAMPLES%cross_main.obj" "%SAMPLES%clib.obj" -o "%SAMPLES%cross.exe"
if errorlevel 1 goto fail

echo.
echo Running cross.exe ...
"%SAMPLES%cross.exe"
set "EC=%errorlevel%"
echo Exit code: %EC%  (expected 42 = mul(6,7))
if "%EC%"=="42" (
    echo PASS
) else (
    echo FAIL – unexpected exit code
    set "EC=1"
)

endlocal
exit /b %EC%

:fail
echo demo_cross.bat: build step failed.
endlocal
exit /b 1
