# Build Anvil

This guide explains how to build the Anvil compiler from source.

---

# Requirements

Before building Anvil, make sure the following tools are installed.

## Operating System

- Windows 10 or later (x64)

## Required Tools

- MSYS2
- MinGW-w64 (UCRT64 or MINGW64)
- GCC
- GNU LD

Example:

```powershell
gcc --version
ld --version
```

If both commands work, your environment is ready.

---

# Project Structure

The repository should look similar to this:

```text
anvil/
│
├── build.bat
├── module/
├── anvil/
├── docs/
└── samples/
```

---

# Build

From the project root:

```powershell
build.bat
```

The script will:

1. Compile all Anvil source files.
2. Build `anv.exe`.
3. Create the shortcut executables:
   - `fg.exe`
   - `4g.exe`
4. Copy all built-in modules into the runtime library directory.

---

# Build Output

After a successful build:

```text
anvil/
└── anvil/
    └── bin/
        ├── bin/
        │   ├── anv.exe
        │   ├── fg.exe
        │   └── 4g.exe
        │
        └── lib/
            ├── console/
            ├── http/
            └── ...
```

---

# Verify the Build

Compile one of the sample programs.

```powershell
anv samples\hello.hlx -run
```

Expected output:

```text
Hello from Helix!
```

---

# Add Anvil to PATH (Optional)

To use Anvil globally, add:

```text
anvil\bin\bin
```

to your Windows **PATH** environment variable.

After reopening the terminal:

```powershell
anv --help
```

or

```powershell
4g --help
```

should work from any directory.

---

# Common Problems

## gcc is not recognized

Cause:

```text
'gcc' is not recognized...
```

Solution:

- Install the MinGW toolchain.
- Add the MinGW `bin` directory to PATH.

---

## ld is not found

Anvil uses the GNU linker.

Make sure `ld.exe` is available from the installed MinGW toolchain.

---

## Modules are missing

If importing modules fails:

```hlx
import console
```

check that

```text
anvil/bin/lib/
```

contains the built-in modules.

If not, run:

```powershell
build.bat
```

again.

---

# Next Step

Once Anvil is successfully built, continue with:

- **quick-start.md** — Write and compile your first Helix program.
- **install.md** — Install Anvil for system-wide usage.