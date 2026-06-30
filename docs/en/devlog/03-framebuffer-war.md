# Sonic on the Nokia 9210i, Part 3: Four Lost Rounds with the Framebuffer

> [Русская версия](../../ru/devlog/03-framebuffer-war.md)
>
> [← Previous part](02-booting-is-not-playing.md) · [Series index](README.md) · [Next part →](04-open-heart-surgery.md)

In the previous part, I proved on real hardware that a separate EKA1 EXE could use writable global data without crashing. That opened the way to a new emulator architecture.

The second half of the idea still needed proof: could the EXE write frames directly into the Nokia 9210i screen memory, bypassing the slow standard rendering path?

The task looked almost trivial. Obtain the framebuffer address, write some colour bars, look at the screen.

In practice, it became four rounds of diagonal noise, lock-ups and repeated battery removal.

## Starting information

Through `UserSvr::ScreenInfo`, the program obtained the display parameters:

- resolution: 640×200;
- mode: `EColor4K`;
- 12-bit colour stored in a 16-bit word;
- framebuffer address on the test device: `0x5A000000`;
- assumed line stride: 640 × 2 = 1,280 bytes.

Following the SNES9x example, all that seemed necessary was to obtain a pointer and start writing pixels.

<!-- Add diagram: EXE → UserSvr::ScreenInfo → framebuffer -->

## Round 1: diagonal noise

The first test filled the screen with a colour pattern and produced diagonal interference.

That was almost encouraging: the memory clearly reacted, so the address was not completely wrong. The pattern looked like a stride error, with each new line beginning at the wrong location.

I went back to check the geometry.

## Round 2: three seconds of black

In the second build, the program wrote all diagnostics to a file, closed the log and only then performed the dangerous operation. On old hardware this is a useful technique: if the phone freezes, the final saved line still survives on disk.

The framebuffer was filled with zeroes. The screen really did turn black — for about three seconds. Then diagonal noise began crawling back over it.

My code was no longer drawing at that point. It was simply waiting.

The corruption therefore came from somewhere else: the window server continued repainting the shell, clock and cursor over the frame that had been written directly into memory.

Two independent renderers were fighting over one screen.

## Round 3: adding a window made it worse

The next idea seemed obvious: if the reference port created a full-screen window, I should do the same.

I created a 640×200 window, activated it, raised the priority and called `Flush()` on the window server after every frame.

The result was worse. The noise appeared immediately, followed by fragments of menus and visual garbage, and then the device froze.

The mistake was almost comic. `Flush()` forced the window server to process its own queue immediately. On every frame, my code was effectively inviting the system to draw something over the direct framebuffer write.

A full-screen window alone was not enough. The SNES9x UIQ port used `RDirectScreenAccess`, but that class did not exist in the Nokia 9210 SDK.

The reference platform was similar, not identical.

## I needed code written specifically for Series 80

I then searched for projects that rendered directly on the Nokia 9210 itself. I found a Doom port and, more importantly, the old **SDL backend for EPOC**.

`SDL_epocvideo.cpp` contained a separate path for the Crystal platform, the early Series 80 environment. It explained every previous failure.

## Three missing conditions

### 1. A service table sits at the start of the framebuffer

In `EColor4K`, the first 16 words of screen memory were occupied by a palette table. The actual pixel data began 32 bytes later.

SDL did exactly this:

```cpp
framebuffer += 16 * 2;
```

I had been writing from offset zero and partially corrupting the service area.

### 2. Direct drawing is safe only while the application owns focus

Before touching the framebuffer, SDL checked whether its window group was the active one. If another application held focus, it did not write to video memory at all.

My test wrote continuously, including while the system considered its own shell active. That created the race with the window server.

The rule became simple: before each frame, compare the active focus group with our own. No focus means no VRAM write.

### 3. The window server cannot be ignored completely

Even while drawing directly, SDL created a valid window group and graphics context, handled redraw events and kept the system informed.

That did not mean rendering through the window server. It meant cooperating with it instead of declaring war on it.

## Round 4: a clean image

The next test combined the SDL recipe:

- the framebuffer pointer was advanced by 32 bytes;
- a named window group was created;
- direct drawing was permitted only while the application held focus;
- window-server events were handled correctly;
- `Invalidate()` and `Flush()` were used during the proof-of-concept stage.

The screen displayed clean colour bars across the full 640×200 area. They moved smoothly, did not dissolve into noise, and the program returned to the menu normally after the test interval.

<!-- Add photo or GIF: moving test colour bars -->

That particular class of lock-up disappeared. Without focus, the program simply stopped drawing instead of competing for the framebuffer.

## What had now been proven

By this point, three independent experiments had passed on an actual Nokia 9210i:

1. an EXE could be launched from an `.app` using `RProcess::Create`;
2. the EXE could use writable global data without `KERN-EXEC 3`;
3. the EXE could animate directly in the framebuffer without fighting the window server.

The new architecture was no longer a theory. The remaining task was to place the SMS Plus core inside this tested framework.

The next part is open-heart surgery: removing the old Frodo scaffold, restoring global structures and running Sonic in a separate process for the first time.

---

**Primary display reference:** SDL EPOC, Crystal backend  
**Additional references:** CDoom for Nokia 9210; SNES9x/UIQ port by notaz.
