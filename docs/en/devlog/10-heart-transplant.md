# Sonic on the Nokia 9210i, Part 10: A Heart Transplant

**Development log — July 2026**  
[Русская версия](../../ru/devlog/10-heart-transplant.md)  
[← Part 9: The Last Cheap Win](09-last-cheap-win.md)

Part 9 ended at an honest wall. Every cheap and medium rendering optimisation
had been collected, and profiling had proved that the remaining large cost —
Z80 emulation — could not be reduced with a spot fix. The instruction dispatch
machinery itself was the floor. The only real lever left was to replace the C
interpreter with a Z80 core written entirely in ARM assembly, and that sounded
like months of work.

It was not, because someone had already done it twenty years earlier.

## A ready-made answer

DrZ80, by Reesy and FluBBa, is a Z80 emulator written in ARM assembly. It maps
the Z80's registers directly onto ARM registers and its flags onto ARM flags —
exactly the approach that makes fast emulators on this class of processor, and
exactly the work I had been dreading. The Nokia 9210i's ARM920T is squarely in
its target range.

Having a ready core is not the same as having it running inside our emulator,
though. The core has to be wired to our memory map, our timing, our interrupts
and our bank switching. That is a separate integration project with several
delicate joints.

## The recon jackpot

Before writing anything, I looked for a working reference. The emulator porter
whose techniques had guided this project throughout had already integrated this
assembly core into his own multi-system emulator — and, remarkably, for the Sega
Master System specifically.

That reference answered every hard question with working code: how to recompute
address bases on a bank switch, how to present the memory map to the core, how
to bridge cycle counting and line interrupts. The memory-map granularity even
matched ours: the reference deliberately used 1 KB pages for the SMS mapper,
which is exactly how our own read map is organised.

The lesson this project keeps teaching held again: dissect a working example
before writing. Half a day of reading saved weeks of guessing.

## Building foreign assembly on a 2001 toolchain

The first real risk was mundane: would eight thousand lines of someone else's
ARM assembly even assemble under our 2001-era toolchain? I brought the core in
on its own and cleared errors iteratively, the same way earlier assembly work
had gone. The ancient assembler has opinions. It does not honour C preprocessor
macros inside assembly files, so the position-independent-code macros had to be
expanded inline. It chokes on apostrophes in comments. It rejects an empty
literal pool. One class of error per build, each fixed precisely, until the core
assembled and linked cleanly.

## The adapter

With the core building, I wrote a thin adapter: a layer that exposes the exact
function names the emulator already calls, but drives the assembly core beneath
them. Memory access is forwarded to our existing map and port handlers.
Program-counter and stack-pointer rebasing is derived from the same map the rest
of the emulator already updates on a bank switch. The old C interpreter stayed
in place behind a build switch, so reverting was a one-line change.

Then the moment of truth: build, install, run. Sonic ran on the assembly core on
the first attempt. The most delicate joints — bank rebasing, timing, interrupts
— fell into place from the reference.

## The result

Z80 time dropped by roughly half. This is a win that nothing else could have
produced, because the CPU runs on every single frame — frame skipping never
touched it. Gameplay logic on a level rose by about a third, and it is felt
directly: the game is noticeably more comfortable to play. The CPU went from the
single largest remaining cost to a minor one, and rendering became the dominant
cost again.

## The twist: whose code is this

Here the story turns, and it is worth telling plainly.

This project is published under the GPL, which requires everything combined into
it to be free for any use, including commercial. The assembly core, in the
revision that builds and runs correctly for us, is offered under a different
term: free for non-commercial use only. That restriction is incompatible with
the GPL. You cannot simply drop this core into a GPL project and publish the
combination, however well it runs.

There is an earlier original release of the same core that is pure GPL. I tried
to switch to it, because that would remove the barrier entirely. It compiles for
us after the same class of toolchain fixes plus a couple more — but it faults on
the device inside the core's own execution. An isolation test settled it beyond
doubt: stubbing out the single call into the core made the fault disappear, so
the problem lives inside that revision's execution path, not in our adapter or
setup. Without an on-device debugger, and with the fault occurring too early for
file-based logging to survive, that revision could not be brought up. So the
core that actually works is the non-commercial one.

## The resolution

The fix is the one that serious projects use for licence-encumbered components:
do not ship the component. The assembly core is not included in this repository.
The default build uses the GPL C interpreter and works out of the box. Anyone
who wants the assembly speed can obtain the core themselves, apply the documented
toolchain edits, drop it into place and rebuild with a build switch. The adapter
that connects it — original work, part of this project — is included under the
GPL. No third-party core is redistributed here.

The lesson: third-party code raises two questions, not one. Does it work, and
may you take it. The second question has to be asked before the commit, not
after. It is satisfying to make the emulator run twice as fast; it is better to
do it without misrepresenting anyone's licence.

To be continued.

---

**Measured result:** Z80 time reduced from ~23 ms to ~11 ms per frame (~2.1×); level gameplay logic ~19 → ~25 FPS.  
**Method:** DrZ80 (ARM assembly Z80 core) behind a build switch, driven by an in-project adapter modeled on PicoDrive's interface layer.  
**Validation:** Sonic level 1 played on original hardware; picture and sprites correct.  
**Licensing:** the working DrZ80 revision is non-commercial and is NOT distributed here; see `DRZ80_OPTIONAL.md` and `THIRD_PARTY_NOTICES.md`. The default build uses the GPL C interpreter.  
**Hardware:** Nokia 9210i, Symbian OS 6.0, EKA1, ARM920T ~52 MHz, 640×200 Color4K display.  
**Build:** Symbian SDK9210, gcc 2.9-psion, ARMI UREL.  
**Core:** SMS Plus by Charles MacDonald; DrZ80 by Reesy & FluBBa (optional).  
**Development method:** AI-assisted source work; builds, measurements and conclusions validated on original hardware.  
**Licensing:** see the repository's `LICENSE.md` and `THIRD_PARTY_NOTICES.md`.
