# Nokia 9210i Keyboard Input

[Русская версия](../../ru/engineering/keyboard-input.md)

## Scope

This note documents the first functional input path in E32SMS: physical Nokia 9210i keyboard events mapped to the Sega Master System controller state used by SMS Plus.

The implementation was built as ARMI UREL and verified on original hardware by completing the first level of Sonic the Hedgehog.

## Development strategy

Input was implemented in two layers:

1. a hard-coded mapping to prove the event path;
2. configurable remapping to be added later.

This avoided debugging keyboard events and a settings interface at the same time.

## Key-code discovery

The physical key codes were measured on the device rather than inferred from symbolic Symbian constants.

A diagnostic handler logged every key press and release. Keys were pressed in a known order, producing a hardware-derived mapping for Up, Down, Left, Right, Button 1, Button 2 and Pause.

The exact numeric table should be documented beside the implementation once the final layout is fixed.

## Controller-state integration

SMS Plus represents Master System controls as bits in the controller state:

- key-down event: set the corresponding controller bit;
- key-up event: clear the corresponding controller bit.

The first fixed layout uses the Nokia arrow keys for movement, two adjacent keys for the two action buttons and Enter for Pause.

## Hardware validation

Validation covered:

- every direction;
- both action buttons;
- Pause;
- correct release handling;
- absence of swapped directions;
- completion of the first level and loading of the second.

The result proves the complete path from the physical Series 80 keyboard through Symbian events to the emulated controller ports.

## Current limitation

The mapping is fixed in code. There is no configuration UI yet.

The emulator currently advances at approximately 27 emulated fps, so the whole game runs at roughly 45% of the intended 60 Hz pace. Input is functional, but the slow emulation rate remains noticeable during play.

## Attribution and licence check

The event-handling approach was adapted from the gnuboy port for Series 60.

If source code was copied or closely adapted rather than merely studied, the exact upstream files, copyright notice and licence must be added to `THIRD_PARTY_NOTICES.md`, and any required notices must remain in the affected source files.
