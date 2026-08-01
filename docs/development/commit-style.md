# Anvil Commit Style Guide

This document defines the commit message convention used in the Anvil repository.

The goal of this convention is to keep the project history readable,
searchable, and easy to understand.

---

## Commit Message Format

Anvil follows a simplified Conventional Commits style:

```
<type>(<scope>): <description>
```

Example:

```
feat(helix-parser): add struct field access parsing
fix(codegen): fix stack alignment before function calls
docs(commit): add commit message guidelines
```

---

# Commit Types

## feat

A new feature or capability.

Examples:

```
feat(assembler): add mov instruction encoding
feat(helix): add switch statement support
feat(cli): add -dump-ir option
```

Use when adding something users can use.

---

## fix

A bug fix.

Examples:

```
fix(parser): prevent crash on empty function body
fix(codegen): fix incorrect stack offset calculation
fix(linker): resolve object path handling on Windows
```

Use when correcting existing behavior.

---

## docs

Documentation changes only.

Examples:

```
docs(readme): update build instructions
docs(architecture): describe compiler pipeline
docs(commit): add commit style guide
```

---

## refactor

Code restructuring without changing behavior.

Examples:

```
refactor(ir): simplify instruction builder
refactor(parser): split expression parser functions
```

Use when improving internal design.

---

## perf

Performance improvements.

Examples:

```
perf(ir): optimize constant folding pass
perf(lexer): reduce token allocation
```

---

## test

Adding or modifying tests.

Examples:

```
test(helix): add loop parser tests
test(assembler): add mov encoding cases
```

---

## build

Build system or dependency changes.

Examples:

```
build(msys2): update gcc build script
build(cmake): add compiler configuration
```

---

## chore

General maintenance tasks.

Examples:

```
chore(repo): reorganize documentation folders
chore(tools): update development scripts
```

Use this for changes that do not affect the compiler itself.

---

## style

Formatting or code style changes.

Examples:

```
style(parser): format error handling code
style(codegen): normalize indentation
```

No behavior changes are allowed.

---

# Scope Rules

The scope describes the affected component.

Common Anvil scopes:

| Scope | Description |
|---|---|
| `helix` | Helix language frontend |
| `c` | C frontend |
| `lexer` | Lexers |
| `parser` | Parsers |
| `sema` | Semantic analysis |
| `ast` | AST system |
| `ir` | Intermediate representation |
| `codegen` | Assembly generation |
| `assembler` | Built-in assembler |
| `coff` | COFF object writer |
| `linker` | Linking system |
| `cli` | Command-line interface(main.c) |
| `module` | Built-in modules |
| `docs` | Documentation |
| `build` | Build system |
| `tools` | Development tools |

---

# Description Rules

## Use imperative mood

Good:

```
add struct declaration parser
fix incorrect register allocation
update installation guide
```

Bad:

```
added struct declaration parser
fixed register allocation
updated installation guide
```

The commit describes what the commit does,
not what happened in the past.

---

## Keep descriptions short

Recommended:

```
feat(parser): add while statement parsing
```

Avoid:

```
feat(parser): add a new while statement parsing system with improved recursive handling and better error recovery
```

If more explanation is needed, use the commit body.

---

# Commit Body

For larger changes, add a body after a blank line.

Example:

```
feat(ir): add basic optimizer pass

Adds constant folding for integer expressions.

Supported:
- arithmetic constants
- comparison constants
- dead branch removal
```

---

# Breaking Changes

If a change breaks existing behavior, add:

```
BREAKING CHANGE:
description of the incompatible change
```

Example:

```
feat(helix): change module import syntax

BREAKING CHANGE:
import statements now require explicit module names.
```

---

# Examples

## Adding a feature

```
feat(helix): add struct field access
```

## Fixing a compiler bug

```
fix(codegen): fix incorrect call stack alignment
```

## Adding documentation

```
docs(development): document backend architecture
```

## Internal cleanup

```
refactor(parser): split expression parsing logic
```

## Adding tests

```
test(assembler): add memory operand tests
```

---

# Recommended Commit Frequency

Prefer small focused commits.

Good:

```
feat(assembler): add register parser
feat(assembler): add memory operand support
test(assembler): add operand tests
```

Avoid:

```
feat: update compiler
```

Large commits make debugging and reviewing difficult.

---

# Anvil Commit Checklist

Before committing:

- [ ] Commit type is correct
- [ ] Scope identifies the affected component
- [ ] Description is written in imperative form
- [ ] Commit contains one logical change
- [ ] Code builds successfully
- [ ] Tests pass if available

---

Following this convention keeps Anvil's development history clean
as the compiler grows.
```