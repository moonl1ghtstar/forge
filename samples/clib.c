/*
 * samples/clib.c
 *
 * C library for cross-link demo.
 * Provides add() and mul() — no main().
 * Compiled to .obj and linked with a Helix .obj that has main().
 *
 * Build as obj:
 *   forge samples\clib.c -obj -o samples\clib.obj
 *
 * Link with Helix main:
 *   forge samples\cross_main.hlx -obj -o samples\cross_main.obj
 *   forge -link samples\cross_main.obj samples\clib.obj -o samples\cross.exe
 */

int add(int a, int b) {
    return a + b;
}

int mul(int a, int b) {
    return a * b;
}
