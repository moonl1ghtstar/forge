# C++ Tools

Use this folder for helper programs only.

Suggested layout:

- `src/` - tool source files
- `include/` - local headers
- `tests/` - tool-specific tests
- `build/` - generated artifacts, ignored by git

Build:

```bash
cmake -S tools/cpp -B tools/cpp/build
cmake --build tools/cpp/build
```

Commands:

- `forge-tools token-dump <file>`
- `forge-tools ast-dump <file>`
- `forge-tools asm-preview <file>`
- `forge-tools smoke-test`
