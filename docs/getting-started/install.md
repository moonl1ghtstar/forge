# Install Forge

This guide explains how to install Forge after building it.

Forge does not currently use a package manager. Installation means placing the compiler binaries and standard library modules in a location that can be accessed from your system.

---

# Installation Methods

There are two recommended ways to install Forge.

## Method 1: Use the Local Build (Recommended for Development)

If you built Forge from source, the compiler is already available here:

```text
forge/
└── forge/
    └── bin/
        ├── bin/
        │   ├── forge.exe
        │   ├── fg.exe
        │   └── 4g.exe
        │
        └── lib/
            └── modules
```

You can run Forge directly:

```powershell
forge\bin\bin\forge.exe program.hlx
```

This method is recommended when developing Forge itself.

---

# Add Forge to PATH

To use Forge from any directory, add the binary directory to your Windows PATH.

Add:

```text
forge\bin\bin
```

Example:

```text
C:\Tools\forge\bin\bin
```

After adding it, restart your terminal.

Verify:

```powershell
forge --help
```

or:

```powershell
4g --help
```

---

# Standard Library Modules

Forge modules are stored separately from the compiler executable.

The default module location is:

```text
forge/bin/lib/
```

Example:

```text
forge/bin/lib/
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

Forge searches this module directory automatically.

---

# Manual Installation

To install Forge manually:

## 1. Copy binaries

Copy:

```text
forge.exe
fg.exe
4g.exe
```

to an installation directory.

Example:

```text
C:\Forge\bin\
```

---

## 2. Copy modules

Copy the module directory:

```text
forge/bin/lib/
```

to:

```text
C:\Forge\lib\
```

The final layout should be:

```text
C:\Forge\
│
├── bin/
│   ├── forge.exe
│   ├── fg.exe
│   └── 4g.exe
│
└── lib/
    ├── console/
    └── win32/
```

---

# Updating Forge

To update Forge:

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

To remove Forge:

1. Delete the Forge installation directory.

Example:

```text
C:\Forge\
```

2. Remove the Forge path entry from your system PATH.

---

# Next Step

After installation, continue with:

- **quick-start.md** — Create your first Helix program.
- **build.md** — Learn how to build Forge from source.