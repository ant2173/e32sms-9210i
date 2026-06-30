# Hardcore Vibe Coding: How I Got Sonic Running on a Nokia 9210i

> [Русская версия](../../ru/devlog/01-hardcore-vibe-coding.md)
>
> **Part 1 of 5.** [Series index](README.md) · [Next part →](02-booting-is-not-playing.md)

In 2026, I decided to test a fairly absurd hypothesis: could someone who had never written code use AI to port an emulator to a handheld computer from 2002?

My test machine was a Nokia 9210i. My test game was the original Sonic the Hedgehog for the Sega Master System. The first major result was impressive and humiliating in equal measure: Sonic really did appear on the screen of an actual Communicator, but each frame took about ten seconds to draw.

The program worked. It was completely unplayable.

<!-- Add photo: Nokia 9210i showing the Sonic title screen -->

## The machine

The Nokia 9210i belongs to a separate branch of Nokia Communicators. It is not an oversized Series 60 smartphone, but a Series 80 device with its own software platform and application library.

Inside is an ARM920T running at roughly 52 MHz, Symbian OS 6.0 and a wide 640×200 display capable of 4,096 colours. The resources look almost toy-like today, but the software architecture is serious: a multitasking OS, a window server, separate processes, a dedicated SDK and native machine code.

That combination of weak hardware and a sophisticated platform made the challenge interesting. Another calculator for an old phone would have been easy enough. A full emulator was a different proposition.

## My role in the project

I am a lawyer, not a developer. Before this project I had never written code, and I deliberately chose not to begin with `Hello, world`.

The workflow looks like this:

- I choose the direction and formulate the hypotheses;
- I build the project using an ancient toolchain;
- I install SIS packages on the real device;
- I collect logs, photograph the results and test the behaviour;
- AI helps analyse the source, prepare patches and document experiments;
- no change is accepted until it has been tested on the actual Nokia 9210i.

This is not autonomous software generation from a one-line prompt. It is closer to extremely slow pair programming: one participant writes and analyses the code, while the other directs the experiment and is responsible for validating it on the hardware.

## A laboratory spanning two eras

The first task was to assemble an early-2000s development environment:

- Microsoft Visual C++ 6.0;
- the Symbian SDK for the Nokia 9210;
- the EPOC compiler and build tools;
- the WINS device emulator;
- a physical Nokia 9210i for final testing.

The biggest surprise was that I did not need a Windows 98 virtual machine. The SDK and VC6 ran reliably on Windows 7. On one side of the screen were tools from 2002; in the next browser window was a modern language model receiving source files and crash logs.

<!-- Add screenshot: VC6, command prompt, SDK emulator and AI window -->

## What the port was built from

The emulator core is **SMS Plus 1.3** by Charles MacDonald, an open-source Sega Master System emulator written in C.

Writing an entire Symbian application from scratch would have taken too long, so I used an old Nokia 9210 port of the Commodore 64 emulator **Frodo** as the initial shell. It already provided:

- an application icon;
- a window;
- an event loop;
- a timer;
- basic keyboard and display scaffolding.

The SMS Plus core was embedded inside that framework. The project received the working name **E32SMS**.

## The main trap: it works in the emulator and crashes on the phone

The first recurring pattern of the project looked like this:

1. the program launched normally in the WINS emulator on the PC;
2. the same build installed successfully on the Nokia;
3. the phone immediately raised `KERN-EXEC 3`.

The cause was the EKA1 memory model. On this version of Symbian, an `.app` is loaded as a DLL, and writable static data inside that DLL caused a crash on the real device. The x86 emulator did not expose the same restriction.

SMS Plus originally kept its state in numerous global structures, tables and buffers. These had to be moved into dynamically allocated memory, while the remaining problem areas were tracked down one by one.

The culprits included:

- tables of pointers to Z80 opcode handlers;
- arrays of pointers to timing tables;
- tables that should have been constant but ended up in writable data sections;
- global rendering buffers.

The old toolchain did not produce a useful linker map. To locate the last writable sections, I had to inspect the COFF object files, find which `.o` files still contained a non-zero `.data` section, and then trace that back to individual symbols.

## The crash that waited several minutes

Once the static-data problem was almost solved, the program began to run for longer — and then crashed again with the same `KERN-EXEC 3`.

This time the cause was completely different. The failure only appeared when the game enabled fine horizontal scrolling. The renderer attempted an unaligned 32-bit write.

x86 tolerated it. The ARM920T responded with a Data Abort.

The source already contained a safe byte-by-byte path, but it was enabled only by a macro:

```c
#define ALIGN_DWORD 1
```

That one line switched the renderer to an ARM-safe path. I also had to enlarge the internal buffer and shift its starting point so that fine scrolling could not run past the array boundary.

## The first real run

After five separate crash causes had been removed, the physical Nokia displayed, in sequence:

- the SEGA logo;
- the Sonic the Hedgehog title screen;
- the `PRESS BUTTON` prompt.

The emulator was not showing a pre-rendered image. The core was executing Z80 code and generating actual game frames. But the cost of diagnostic logging, indirect state access and the slow display path was enormous: roughly **0.1 frames per second**.

<!-- Add short video or GIF: the first working build changing frames very slowly -->

Even so, this was an important milestone. The question “Can SMS Plus run on a Nokia 9210i at all?” now had an answer: yes.

The next question was much harder: how could a technically working emulator be turned into something that at least looked like a game?

In the next part, I examine a rare SNES9x port for the Sony Ericsson P800 and discover that we had been fighting not only poor optimisation, but the wrong application architecture.

---

**Project:** E32SMS · Nokia 9210i · Symbian OS 6.0 · EKA1 · ARM920T ~52 MHz  
**Core:** SMS Plus 1.3 · **initial shell:** Frodo for EPOC  
**Development method:** AI-assisted, with every stage built and tested on original hardware.
