# Install Anvil

This guide explains how to install Anvil after building it.

Anvil does not currently use a package manager. Installation means placing the compiler binaries and standard library modules in a location that can be accessed from your system.

---

# Installation Methods

There are two recommended ways to install Anvil.

## Method 1: Use the Local Build (Recommended for Development)

If you built Anvil from source, the compiler is already available here:

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
            └── modules
```

You can run Anvil directly:

```powershell
anvil\bin\bin\anv.exe program.hlx
```

This method is recommended when developing Anvil itself.

---

# Add Anvil to PATH

To use Anvil from any directory, add the binary directory to your Windows PATH.

Add:

```text
anvil\bin\bin
```

Example:

```text
C:\Tools\anvil\bin\bin
```

After adding it, restart your terminal.

Verify:

```powershell
anv --help
```

or:

```powershell
4g --help
```

---

# Standard Library Modules

Anvil modules are stored separately from the compiler executable.

The default module location is:

```text
anvil/bin/lib/
```

Example:

```text
anvil/bin/lib/
├── console/
│   ├── config.json
│   └── src/
│
└── win32/
    ├── config.json
    └── src/
```

When compiling:

```hlx
import console
```

Anvil searches this module directory automatically.

---

# Manual Installation

To install Anvil manually:

## 1. Copy binaries

Copy:

```text
anv.exe
fg.exe
4g.exe
```

to an installation directory.

Example:

```text
C:\Anvil\bin\
```

---

## 2. Copy modules

Copy the module directory:

```text
anvil/bin/lib/
```

to:

```text
C:\Anvil\lib\
```

The final layout should be:

```text
C:\Anvil\
│
├── bin/
│   ├── anv.exe
│   ├── fg.exe
│   └── 4g.exe
│
└── lib/
    ├── console/
    └── win32/
```

---

# Updating Anvil

To update Anvil:

1. Pull the latest source code.

```powershell
git pull
```

2. Rebuild:

```powershell
build.bat
```

3. Replace the installed binaries and modules.

---

# Uninstall

To remove Anvil:

1. Delete the Anvil installation directory.

Example:

```text
C:\Anvil\
```

2. Remove the Anvil path entry from your system PATH.

---

# Next Step

After installation, continue with:

- **quick-start.md** — Create your first Helix program.
- **build.md** — Learn how to build Anvil from source.