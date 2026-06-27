/*
 * samples/hello_c.c
 *
 * Forge C frontend sample.
 * Demonstrates: int function, arithmetic, return.
 *
 * Build (obj only):
 *   forge samples\hello_c.c -obj -o samples\hello_c.obj
 *
 * Build (exe):
 *   forge samples\hello_c.c -o samples\hello_c.exe
 *
 * Cross-link with Helix obj:
 *   forge samples\greet.hlx -obj -o samples\greet.obj
 *   forge -link samples\hello_c.obj samples\greet.obj -o samples\mixed.exe
 */

/* Forge C frontend: only int type is supported currently */

int add(int a, int b) {
    return a + b;
}

int main() {
    int x;
    int y;
    int result;
    x = 10;
    y = 32;
    result = add(x, y);
    return result;
}
