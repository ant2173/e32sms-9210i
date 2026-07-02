@ asmtest.S -- minimal ARM assembly sanity test for SDK9210 / gcc 2.9.
@ Purpose: PROVE the toolchain compiles+links a .S file and can call it from C,
@ BEFORE writing a real assembly blitter. Keep it dead simple and obviously
@ correct.
@
@   int  asm_add(int a, int b);                       -> a + b
@   void asm_fill16(unsigned short* dst, int count, unsigned short val);
@        -> writes 'count' copies of val as 16-bit halfwords at dst
@
@ APCS: args r0,r1,r2,r3; return r0; r0-r3,r12 scratch; r4-r11 preserved.

    .text

    .global asm_add
asm_add:
    add     r0, r0, r1
    mov     pc, lr

    .global asm_fill16
asm_fill16:
    @ r0 = dst (uint16*), r1 = count, r2 = val
    cmp     r1, #0
    movle   pc, lr            @ nothing to do if count <= 0
fill16_loop:
    strh    r2, [r0], #2      @ store halfword, post-increment dst by 2
    subs    r1, r1, #1
    bne     fill16_loop
    mov     pc, lr
