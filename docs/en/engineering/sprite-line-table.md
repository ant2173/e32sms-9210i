# Per-Scanline Sprite Visibility Table

[Русская версия](../../ru/engineering/sprite-line-table.md)

## Scope

This note documents the structural optimization of the SMS Plus sprite renderer
used by E32SMS on the Nokia 9210i.

The original scanline renderer inspected all 64 sprite records on every visible
line. The new path builds per-scanline visible-sprite lists once per frame.

## Baseline

The original `render_obj_sms` performed:

```text
64 sprite records × 192 visible lines = 12,288 Y-inclusion tests per frame
```

Most tests were negative because an individual sprite covers only the scanlines
inside its height.

Measured baseline in the tested gameplay scene:

| Component | Time |
|---|---:|
| Sprite rendering | ~8.8 ms per rendered frame |

## Implementation

At the beginning of the frame, `build_sprite_line_table()` walks the sprite
attribute table once.

For each visible sprite, the implementation determines the covered scanline
range and records the sprite index in the corresponding line lists.

The current tables are:

```c
spr_cnt[192]
spr_idx[192][8]
spr_ovf[192]
```

`render_obj_sms` then reads `spr_idx[line]` instead of rescanning all 64 sprite
records.

The implementation preserves:

- original front-to-back sprite order;
- the eight-sprites-per-line limit;
- overflow state;
- the `208` sprite-list end marker;
- the SMS Y-coordinate increment and wrap behaviour;
- normal and double-size sprite height.

The optimization removes 12,288 repeated full-list inclusion tests. It does not
reduce all work to exactly 64 operations: each visible sprite still has to be
inserted into every scanline list covered by its height.

## Integration point

The line table is built at the start of the frame, currently when
`render_line()` receives line zero and the active object renderer is
`render_obj_sms`.

## Result

| Component | Before | After | Change |
|---|---:|---:|---:|
| Sprite rendering | ~8.8 ms | ~3.8 ms | 2.3× faster, ~5 ms saved |

Hardware validation covered Sonic, rings and enemies. Sprite order and placement
remained correct, with no visible duplication or disappearance.

Approximate gameplay-scene rendering profile after the change:

| Component | Time per rendered frame |
|---|---:|
| Background rendering | ~16 ms |
| Assembly remap and VRAM output | ~11 ms |
| Sprite rendering | ~3.8 ms |
| Tile-cache work | ~3 ms |

## Measurement caveat

This profile was captured during a busy gameplay level.

The earlier approximately 27 emulated-fps result was measured on a title-screen
workload. Those scene-level results are not a valid direct overall-fps
comparison. The reliable before-and-after measurement is the component timing:
`render_obj` fell from approximately 8.8 to 3.8 ms.

## Compatibility caveat

The table assumes that the sprite attribute table remains stable during a
frame. That is valid for the current target games and tested workload.

A game that modifies the sprite attribute table mid-frame through raster effects
would require rebuilding or updating the relevant line lists.

## Background-renderer decision

The dominant background path was investigated but deliberately left unchanged.

Fine horizontal scrolling can produce unaligned output. Candidate solutions
included an aligned staging copy, shifted word assembly and folding the shift
into the final remap stage. The estimated gain was small relative to the code
complexity and layer-composition risk.

The sprite table was therefore selected as the better low-risk structural
optimization.
