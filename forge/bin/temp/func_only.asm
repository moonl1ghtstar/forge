; Forge-generated x86-64 assembly (Windows x64 ABI)
; Assemble with: nasm -f win64 <file>.asm
; Link with:     ld <file>.o -o <file>.exe

section .data

section .text


global print
print:
    push rbp
    mov rbp, rsp
    sub rsp, 128
    mov eax, 0
    jmp .Lexit0
.Lexit0:
    mov rsp, rbp
    pop rbp
    ret

global main
main:
    push rbp
    mov rbp, rsp
    sub rsp, 128
    mov eax, 0
    jmp .Lexit1
.Lexit1:
    mov rsp, rbp
    pop rbp
    ret
