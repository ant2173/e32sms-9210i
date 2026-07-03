@ asmblit.S -- ARM assembly blitter for E32SMS remap->VRAM (SDK9210/gcc 2.9).
@
@ Replaces the C paired-write loop in the remap_8_to_16 VRAM branch. For each
@ pixel: take an 8-bit palette index (mask 0x1F), look up a 16-bit RGB444 color
@ in pal[], pack two adjacent pixels into a 32-bit word, and burst-write words
@ to slow video memory using stm (store-multiple) for fewer VRAM transactions.
@
@ C prototype (call via extern C linkage):
@   void asm_remap_line(unsigned short* dst,      @ r0  VRAM, 4-byte aligned
@                       const unsigned char* src, @ r1  8-bit indices (+8 origin)
@                       const unsigned short* pal,@ r2  palette[32], 16-bit
@                       int npix);                @ r3  pixel count (>0)
@
@ Processes 4 pixels per iteration (=> 2 words written per stm). npix is 256
@ for SMS (multiple of 4), so the main loop covers the whole line; a tail loop
@ handles any remainder (1..3) one pixel at a time for safety.
@
@ NOTE: ARMv4 ldrh does NOT support a scaled index (no lsl in the address), so
@ we double the index into a byte offset with a separate add before each ldrh.
@
@ APCS: r0-r3 args/scratch, r12 scratch; r4-r11 preserved. PIXEL_MASK = 0x1F.

    .text
    .global asm_remap_line

asm_remap_line:
    stmfd   sp!, {r4-r7, lr}      @ preserve callee-saved regs we use

    movs    r12, r3, lsr #2       @ r12 = npix / 4  (4 pixels per iter)
    beq     remap_tail_check      @ fewer than 4 pixels -> skip main loop

remap_loop4:
    @ --- pixel 0 and 1 -> word A (r4) ---
    ldrb    r5, [r1], #1          @ r5 = src[0]
    and     r5, r5, #0x1F         @ mask to palette range
    add     r5, r5, r5            @ r5 = idx0 * 2  (byte offset into pal)
    ldrh    r5, [r2, r5]          @ r5 = pal[idx0]
    ldrb    r6, [r1], #1          @ r6 = src[1]
    and     r6, r6, #0x1F
    add     r6, r6, r6            @ idx1 * 2
    ldrh    r6, [r2, r6]          @ r6 = pal[idx1]
    orr     r4, r5, r6, lsl #16   @ word A = idx0 | (idx1 << 16)

    @ --- pixel 2 and 3 -> word B (r7) ---
    ldrb    r5, [r1], #1
    and     r5, r5, #0x1F
    add     r5, r5, r5
    ldrh    r5, [r2, r5]          @ pal[idx2]
    ldrb    r6, [r1], #1
    and     r6, r6, #0x1F
    add     r6, r6, r6
    ldrh    r6, [r2, r6]          @ pal[idx3]
    orr     r7, r5, r6, lsl #16   @ word B = idx2 | (idx3 << 16)

    stmia   r0!, {r4, r7}         @ burst-write 2 words (4 pixels) to VRAM

    subs    r12, r12, #1
    bne     remap_loop4

remap_tail_check:
    ands    r12, r3, #3           @ r12 = npix & 3  (leftover pixels)
    beq     remap_done

remap_tail:
    ldrb    r5, [r1], #1
    and     r5, r5, #0x1F
    add     r5, r5, r5
    ldrh    r5, [r2, r5]
    strh    r5, [r0], #2          @ single 16-bit pixel
    subs    r12, r12, #1
    bne     remap_tail

remap_done:
    ldmfd   sp!, {r4-r7, pc}      @ restore and return
