# Sonic on the Nokia 9210i, Part 7: The First ARM Assembly Blitter

**Development log — July 3, 2026**  
[Русская версия](../../ru/devlog/07-first-arm-assembly.md)  
[← Part 6: From Slideshow to Motion](06-from-slideshow-to-motion.md)

The previous round of optimisation ended at a clear boundary. Frameskip had raised game-logic throughput, paired 32-bit stores had reduced framebuffer traffic, and the remaining hot paths were already reasonably efficient C. The next meaningful step was no longer another compiler flag or a small loop rewrite. It was ARM assembly.

That sounds like the obvious moment to start writing a large blitter. It is also the obvious way to lose an evening to the toolchain. The first task was therefore much smaller: prove that SDK9210 could compile an assembly source file, link it into the emulator and call it correctly on the physical phone.

## First prove the path

There was a real reason for caution. The SNES9x UIQ port used as an architectural reference had needed a custom build route for its assembly sources. Its build setup lowercased the `.S` extension, interfering with preprocessing, so the author used a manual Makefile.

E32SMS did not need that workaround.

I added a tiny `asmtest.S` containing two deliberately boring functions:

- `asm_add(40, 2)`, expected to return 42;
- `asm_fill16(...)`, expected to fill a small array with known values.

The functions were declared with C linkage, added to the ordinary `.mmp` file and built through the standard `bldmake` / `abld` pipeline. The resulting ARMI UREL build ran on the Nokia 9210i and wrote the expected results to the log.

That test established four things at once:

1. SDK9210 accepts a preprocessed `.S` source through the normal build system;
2. the assembler output links with the C and C++ parts of E32SMS;
3. the calling convention works as expected;
4. no custom linker or hand-written build script is required.

Only after that did the real optimisation begin.

## Choosing the safest first assembly target

The first target was not the Z80 core or the SMS background renderer. Both contain complicated state and control flow. The safer target was the final indexed-colour-to-framebuffer conversion: a small, isolated loop specific to E32SMS and already covered by reliable profiling.

For every output pixel, the C path performed the same operations:

1. read an 8-bit palette index;
2. mask it to the active 32-colour range;
3. read the corresponding 16-bit RGB444 value;
4. pack adjacent pixels;
5. write the result to the Nokia framebuffer.

The existing C optimisation already packed two 16-bit pixels into one 32-bit store. The assembly version goes further. It prepares four pixels as two machine words and writes both words with one ARM multiple-store instruction:

```asm
stmia r0!, {r4, r7}
```

The public C interface remains simple:

```c
void asm_remap_line(
    unsigned short *dst,
    const unsigned char *src,
    const unsigned short *palette,
    int pixel_count
);
```

The assembly path is guarded by `E32_ASM_REMAP`. The previous paired-write C implementation remains available as an immediate fallback, which makes the experiment reversible instead of turning it into a one-way rewrite.

## Three traps in the twenty-year-old toolchain

The first builds failed for three unrelated reasons. All three are now documented.

### 1. The C preprocessor sees the assembly comments

A `.S` file is preprocessed before it reaches the assembler. An apostrophe in an English comment was interpreted as the beginning of a character constant and produced an “unterminated character constant” error.

The practical rule is simple: comments in SDK9210 assembly sources should avoid apostrophes and quotation marks.

### 2. ARMv4 halfword loads do not support a scaled register offset

The first palette lookup attempted the equivalent of:

```asm
ldrh rX, [rPalette, rIndex, lsl #1]
```

That addressing mode is not available for `ldrh` on this ARM architecture. The index must first be doubled with a separate data-processing instruction and then used as an ordinary register offset.

Normal word and byte loads are more flexible here; halfword loads are not.

### 3. `abld clean` does not remove every generated build file

After the test source had been removed from the project, the build system continued looking for it. The generated `Epoc32\BUILD\...\GROUP_EMU\` tree still contained a stale source list.

Deleting that generated build directory manually, then rerunning `bldmake` and `abld`, forced the project metadata to be regenerated correctly.

## Measured result

The assembly output is visually identical to the C version: no palette corruption, no shifted lines and no geometry changes. That visual equivalence matters because this loop touches every displayed game pixel.

The performance difference is substantial:

| Build | Remap and VRAM output | Logic rate | Rendered rate |
|---|---:|---:|---:|
| Paired-write C path | ~14.8 ms | ~24.5 fps | ~12.2 fps |
| ARM assembly path | ~8.2 ms | ~27.0 fps | ~13.5 fps |

The remap-and-output stage became approximately **1.8 times faster**, falling by about **44%**. Total game-logic throughput increased by roughly **10%**, while the displayed frame rate also rose by about **11%**.

The overall gain is smaller than the local 1.8× figure because the blitter is only one part of each emulated frame. Amdahl's law still applies even on a Nokia Communicator.

## The new performance map

With `frameskip = 1`, an actually rendered frame now spends approximately:

| Stage | Time |
|---|---:|
| Background rendering | ~11.5 ms |
| Sprite rendering | ~8.8 ms |
| Assembly remap and VRAM output | ~8.2 ms |
| Tile-cache work | ~2.6 ms |
| Remaining rendering overhead | a few milliseconds |
| **Total rendered-frame work** | **~34 ms** |

The framebuffer conversion is no longer the largest measured component. Background rendering has taken that position.

Across the whole profiling and optimisation arc, game-logic throughput has risen from roughly **12.5 fps to 27.0 fps**. Those figures are not the same as displayed frame rate because the current build uses frameskip, but they show that the game advances at more than twice the original measured rate and now reaches about **45% of the Master System's 60 Hz update pace**.

## What this milestone really proves

The important result is larger than one faster loop.

The complete assembly route is now proven on original hardware:

- `.S` sources build through the standard SDK9210 project;
- assembly functions link and can be called from C;
- the expected ARM calling convention is working;
- burst writes to the physical framebuffer produce correct pixels;
- the C fallback remains available for comparison and recovery;
- the improvement survives an actual Nokia 9210i hardware test.

That opens the door to more ambitious assembly work. The background and sprite renderers are the next obvious performance targets, but they are considerably harder: they contain tile attributes, scrolling, priority rules and far more branching than the simple final-output loop.

## The next useful feature is not another benchmark

There is still no keyboard input. Sonic is moving fast enough that the missing controls have become more frustrating than the remaining performance deficit. For now, the phone is running the game's attract mode rather than letting a person play.

The next high-value step is therefore to map the Nokia keyboard to the Master System controller ports. More assembly can follow after the emulator stops being only a demonstration and becomes something that can actually be controlled.

To be continued.

---

**Current result:** ~27.0 logic fps / ~13.5 rendered fps, `frameskip = 1`.  
**Assembly result:** remap and VRAM output reduced from ~14.8 ms to ~8.2 ms per rendered frame.  
**Hardware:** Nokia 9210i, Symbian OS 6.0, EKA1, ARM920T ~52 MHz, 640×200 Color4K display.  
**Build:** Symbian SDK9210, gcc 2.9-psion, ARMI UREL.  
**Core:** SMS Plus by Charles MacDonald.  
**Development method:** AI-assisted source work; builds, measurements and conclusions validated on original hardware.  
**Reference:** the assembly-output strategy was informed by study of the SNES9x UIQ port by notaz; E32SMS uses its own implementation.
