# Sonic on the Nokia 9210i, Part 9: The Last Cheap Win

**Development log — July 2026**  
[Русская версия](../../ru/devlog/09-last-cheap-win.md)  
[← Part 8: The First Playable Build](08-first-playable-build.md)

The previous milestone made E32SMS genuinely playable. That also changed the
way performance could be judged: not from an attract-mode sequence, but with
the communicator in hand and Sonic responding to real key presses.

The next profiling pass exposed an interesting lesson. The largest measured
component was not necessarily the best component to optimize.

## The obvious target

In a busy gameplay scene, background rendering was the largest measured part of
a rendered frame. The natural reaction was to attack it first, possibly with
another ARM assembly routine.

Before writing code, however, I traced the full background path.

The main difficulty was horizontal fine scrolling. During scrolling, background
output can begin at an address that is not aligned to a machine-word boundary.
That prevents the straightforward use of the fast aligned word-write path that
worked so well in the final framebuffer blitter.

Several possible fixes were considered:

- render through an aligned staging buffer;
- assemble shifted words with a barrel-shift-style loop;
- postpone the fine-scroll shift until the final output stage.

None produced an attractive trade-off. The first risked spending the saved time
on another copy. The second would introduce delicate, bug-prone code for an
estimated gain of only about five percent. The third risked breaking the
alignment between background and sprites because the scroll offset is part of
the layer-composition rules.

So the background renderer was left alone.

That was not a failure to optimize it. It was a decision that the expected gain
did not justify the implementation and regression risk.

## The smaller target with the cleaner waste

The sprite renderer initially looked less important because it consumed roughly
half as much time as the background renderer. Its structure, however, contained
a much cleaner source of waste.

The Master System image is rendered one scanline at a time. The original
`render_obj_sms` path inspected all 64 possible sprite records for every one of
the 192 visible lines.

That means:

```text
64 sprites × 192 lines = 12,288 vertical-inclusion tests per frame
```

Most of those tests cannot produce a pixel. A sprite occupies only the handful
of scanlines covered by its current height.

## Building the scanline lists once

The new path builds a sprite-visibility table once at the beginning of each
frame.

It walks the 64 sprite records, determines which scanlines each visible sprite
covers, and adds the sprite index to those line-specific lists. When a scanline
is later rendered, `render_obj_sms` processes only the sprites already known to
intersect that line.

The implementation preserves the details that affect Master System behaviour:

- sprite order;
- the eight-sprites-per-line limit;
- overflow state;
- the sprite-list end marker;
- wrapped Y coordinates;
- normal and double-size sprites.

The optimization changes how candidates are selected, not how the sprites
themselves are drawn.

A more precise description than “12,288 operations became 64” is this: the
12,288 repeated full-list inclusion tests were removed. The 64 sprite records
are now inspected once per frame, after which each visible sprite is inserted
only into the scanline lists it actually covers.

## Result on the Nokia 9210i

The hardware result was clear:

| Component | Before | After | Change |
|---|---:|---:|---:|
| Sprite rendering | ~8.8 ms | ~3.8 ms | 2.3× faster, about 5 ms saved |

Sonic, rings and enemies remained correctly positioned. Nothing disappeared,
duplicated or changed order.

In the measured gameplay scene, the approximate rendering profile became:

| Component | Time per rendered frame |
|---|---:|
| Background rendering | ~16 ms |
| Assembly remap and VRAM output | ~11 ms |
| Sprite rendering | ~3.8 ms |
| Tile-cache work | ~3 ms |

Sprite processing went from the second-largest measured rendering component to
the smallest.

These numbers come from a busy level scene. The earlier figure of approximately
27 emulated fps was measured on a title-screen workload, so it would be
misleading to present the two scenes as a direct overall-fps before-and-after
comparison. The component-level reduction from 8.8 to 3.8 ms is the reliable
result.

## A useful double pivot

This experiment changed direction twice.

First:

> The largest component must offer the largest practical gain.

became:

> The largest component may also be the hardest and riskiest one to change.

Then:

> The smaller component is less interesting.

became:

> The smaller component contains a cleaner structural inefficiency and therefore
> offers the better engineering return.

Profiling tells us where time is spent. Code reconnaissance tells us whether
that time can be removed safely. Both are required.

## The practical ceiling of cheap optimizations

The project has not reached an absolute performance limit. It has reached
something more specific: the current limit of low-risk, high-return changes.

The remaining large targets are expensive:

- background rendering is dominant, but fine scrolling makes the fast path
  difficult;
- the Z80 core would require a far larger assembly project;
- increasing frameskip would trade motion quality for a crude speed gain.

There are still improvements to make, but the next percentage points will cost
more than the previous ones.

That is a reasonable place to pause. E32SMS has moved from a barely moving first
boot to a controllable game on an original Nokia 9210i. The communicator can now
be picked up, played and judged as a machine rather than watched as a demo.

For now, Sonic feels noticeably more responsive in an actual level — and the
last easy structural win has been collected.

To be continued.

---

**Measured result:** sprite rendering reduced from ~8.8 ms to ~3.8 ms per rendered frame.  
**Method:** per-scanline visible-sprite lists built once per frame in C.  
**Validation:** Sonic, rings and enemies checked during gameplay on original hardware.  
**Current gameplay-scene profile:** background ~16 ms, remap/VRAM ~11 ms, sprites ~3.8 ms, tile cache ~3 ms.  
**Hardware:** Nokia 9210i, Symbian OS 6.0, EKA1, ARM920T ~52 MHz, 640×200 Color4K display.  
**Build:** Symbian SDK9210, gcc 2.9-psion, ARMI UREL.  
**Core:** SMS Plus by Charles MacDonald.  
**Development method:** AI-assisted source work; builds, measurements and conclusions validated on original hardware.  
**Licensing:** see the repository's `LICENSE.md` and `THIRD_PARTY_NOTICES.md`.
