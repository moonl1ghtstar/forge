# Forge Roadmap

Forge is currently under active development.

This roadmap describes planned features and long-term goals.

---

# Current Status

## Completed

- [x] Helix lexer
- [x] Helix parser
- [x] Semantic analysis
- [x] Shared IR layer
- [x] x86-64 code generation
- [x] Built-in assembler
- [x] COFF object generation
- [x] Windows x64 linking
- [x] Module system

---

# Short Term

## Language

- [ ] More built-in types
- [ ] Better type inference
- [ ] Generic functions
- [ ] Improved error messages

## Compiler

- [ ] More IR optimizations
- [ ] Better diagnostics
- [ ] Incremental compilation

## Standard Library

- [ ] math module
- [ ] filesystem module
- [ ] networking module

---

# Long Term

## Multiple Backends

- [ ] LLVM backend
- [ ] Linux ELF backend
- [ ] ARM64 backend

## Language Support

- [ ] More language frontends
- [ ] Package manager

## Developer Experience

- [ ] Language server
- [ ] Debugger integration
- [ ] IDE support

---

# Philosophy

Forge aims to become a lightweight,
multi-language compiler infrastructure with:

- shared compiler core
- multiple frontends
- multiple backends
- rich diagnostics