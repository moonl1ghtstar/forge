// C library for cross-language linking demo.
// Compile to .obj with:
//   anv samples\mylib.c -obj -o samples\clib.obj

int multiply(int a, int b) {
    return a * b;
}

int square(int x) {
    return multiply(x, x);
}
