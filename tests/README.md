# Anvil Compiler Regression Test Suite

This directory contains the automated regression test suite for the Anvil compiler.
The tests verify core compiler functions, output flags, manual linking, directory space safety, temporary file cleanups, multi-process compilation isolation, and error handling.

## Requirements

1. **Python 3.x**: Used to execute the test suite script.
2. **Anvil Compiler**: The `anv.exe` executable must be built (typically at `anvil/bin/bin/anv.exe` or present in parent folders / PATH).
3. **GCC & Linker**: Ensure standard Windows x64 ABI GCC toolchain is in your system PATH (e.g. MinGW-w64).

## How to Run

From the project root directory:

```bash
python tests/run_tests.py
```

Or from the `tests` directory:

```bash
python run_tests.py
```

## Test Case Coverage

- **Basic compile**: Compiles a source file and executes the generated binary to match output.
- **Custom output**: Validates `-o` output naming configuration.
- **Object generation**: Compiles source code to standard COFF `.obj` via `-o <name>.obj`.
- **Manual linking**: Links `.obj` files to `.exe` using `-link`.
- **Space path**: Runs compilation with folder structures and paths containing spaces.
- **Cleanup check**: Audits workspace and `%TEMP%` to confirm no temporary compiler files (like `anv-build.asm`) or isolated PID folders are leaked.
- **Parallel build**: Compiles 10 binaries concurrently to guarantee PID directory isolation.
- **Error handling**: Asserts that compiler exits with a non-zero code and logs proper errors for nonexistent files.

## Example Output

```text
Using Anvil compiler executable: C:\Users\User\Desktop\anvil\anvil\bin\bin\anv.exe

[ RUN ] Basic compile
[ PASS ] Basic compile
[ RUN ] Custom output
[ PASS ] Custom output
[ RUN ] Object generation
[ PASS ] Object generation
[ RUN ] Manual linking
[ PASS ] Manual linking
[ RUN ] Space path
[ PASS ] Space path
[ RUN ] Cleanup check
[ PASS ] Cleanup check
[ RUN ] Parallel build
[ PASS ] Parallel build
[ RUN ] Error handling
[ PASS ] Error handling

================================
Passed: 8/8
Failed: 0/8
================================
```
