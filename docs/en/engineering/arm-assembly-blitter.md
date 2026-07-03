# ARM Assembly Remap-to-VRAM Blitter

[Русская версия](../../ru/engineering/arm-assembly-blitter.md)

## Scope

This note documents the first ARM assembly optimisation integrated into E32SMS:
replacement of the C indexed-colour-to-RGB444 framebuffer loop with
`asm_remap_line`.

The change was built as ARMI UREL with the standard SDK9210 `bldmake` / `abld`
pipeline and validated on an original Nokia 9210i.

## Baseline

Configuration:

- `frameskip = 1`;
- direct, focus-gated access to the physical Nokia framebuffer;
- 256×192 SMS image centred in a 640×200 Color4K display;
- C fallback packs two RGB444 pixels into each 32-bit store.

Measured C baseline:

| Metric | Value |
|---|---:|
| Remap and VRAM output | ~14.8 ms per rendered frame |
| Logic rate | ~24.5 fps |
| Rendered rate | ~12.2 fps |

## Build-path validation

Before integrating the blitter, `asmtest.S` verified that a `.S` source listed in
the `.mmp` can be:

1. preprocessed and assembled by the SDK9210 toolchain;
2. linked into the existing executable;
3. called through C linkage;
4. executed correctly on ARM hardware.

The hardware test covered a scalar return value and a small memory-writing
function.

## Interface

```c
void asm_remap_line(
    unsigned short *dst,
    const unsigned char *src,
    const unsigned short *palette,
    int pixel_count
);
```

The implementation is selected with `E32_ASM_REMAP`. The previous C loop remains
compiled as a fallback when that macro is disabled.

## Data path

For each pixel:

1. load an 8-bit source index;
2. mask it with `0x1F`;
3. scale the index to address a 16-bit palette entry;
4. load the RGB444 value with `ldrh`;
5. combine two 16-bit pixels into one 32-bit word;
6. prepare two words and store four pixels with:

```asm
stmia r0!, {r4, r7}
```

The destination alignment and 256-pixel line width allow the loop to process the
line in four-pixel groups.

## Measured result

| Metric | C path | Assembly path | Change |
|---|---:|---:|---:|
| Remap and VRAM output | ~14.8 ms | ~8.2 ms | -44%, ~1.8× faster |
| Logic rate | ~24.5 fps | ~27.0 fps | +10% |
| Rendered rate | ~12.2 fps | ~13.5 fps | +11% |

The output was visually identical on hardware: colours, line position and image
geometry remained correct.

## SDK9210 assembly issues encountered

### Preprocessing of `.S`

The C preprocessor runs before assembly. Apostrophes or quotation marks in
comments can be interpreted as unterminated tokens. Keep comments conservative.

### `ldrh` addressing on ARMv4

A scaled register offset is not available in the attempted halfword-load form:

```asm
ldrh rX, [rBase, rIndex, lsl #1]   /* invalid here */
```

Scale the index separately, then use a normal register offset.

### Stale generated source list

`abld clean` did not remove the generated `Epoc32\BUILD\...\GROUP_EMU\` metadata.
After removing `asmtest.S` from the project, the stale build tree still referred
to it. Deleting that generated directory and regenerating the build files fixed
the problem.

## Current profile

Approximate work per rendered frame:

| Component | Time |
|---|---:|
| Background rendering | ~11.5 ms |
| Sprite rendering | ~8.8 ms |
| Assembly remap to VRAM | ~8.2 ms |
| Tile cache | ~2.6 ms |
| Total rendering | ~34 ms |

The assembly blitter is no longer the largest measured component. Further major
performance work would target background and sprite rendering, but those paths
have substantially more complex control flow.

## Attribution

The general use of an ARM burst-write blitter for early Symbian framebuffer
output was informed by study of the SNES9x UIQ port by notaz. No third-party
source is intended to be included in this implementation; see the repository's
influences and third-party notices for the project's attribution policy.
