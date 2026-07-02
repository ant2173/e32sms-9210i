/*****************************************************************************
 *
 *	 z80.c
 *	 Portable Z80 emulator V3.3
 *
 *	 Copyright (C) 1998,1999,2000 Juergen Buchmueller, all rights reserved.
 *
 *	 - This source code is released as freeware for non-commercial purposes.
 *	 - You are free to use and redistribute this code in modified or
 *	   unmodified form, provided you list me in the credits.
 *	 - If you modify this source code, you must add a notice to each modified
 *	   source file that it has been changed.  If you're a nice person, you
 *	   will clearly mark each change too.  :)
 *	 - If you wish to use this for commercial purposes, please contact me at
 *	   pullmoll@t-online.de
 *	 - The author of this copywritten work reserves the right to change the
 *	   terms of its usage and license at any time, including retroactively
 *	 - This entire notice must remain in the source code.
 *
 *	 Changes in 3,3
 *	 - Fixed undocumented flags XF & YF in the non-asm versions of CP,
 *	   and all the 16 bit arithmetic instructions. [Sean Young]
 *	 Changes in 3.2
 *	 - Fixed undocumented flags XF & YF of RRCA, and CF and HF of
 *	   INI/IND/OUTI/OUTD/INIR/INDR/OTIR/OTDR [Sean Young]
 *	 Changes in 3.1
 *	 - removed the REPEAT_AT_ONCE execution of LDIR/CPIR etc. opcodes
 *	   for readabilities sake and because the implementation was buggy
 *	   (and I was not able to find the difference)
 *	 Changes in 3.0
 *	 - 'finished' switch to dynamically overrideable cycle count tables
 *	 Changes in 2.9:
 *	 - added methods to access and override the cycle count tables
 *	 - fixed handling and timing of multiple DD/FD prefixed opcodes
 *	 Changes in 2.8:
 *	 - OUTI/OUTD/OTIR/OTDR also pre-decrement the B register now.
 *	   This was wrong because of a bug fix on the wrong side
 *	   (astrocade sound driver).
 *	 Changes in 2.7:
 *	  - removed z80_vm specific code, it's not needed (and never was).
 *	 Changes in 2.6:
 *	  - BUSY_LOOP_HACKS needed to call change_pc16() earlier, before
 *		checking the opcodes at the new address, because otherwise they
 *		might access the old (wrong or even NULL) banked memory region.
 *		Thanks to Sean Young for finding this nasty bug.
 *	 Changes in 2.5:
 *	  - Burning cycles always adjusts the ICount by a multiple of 4.
 *	  - In REPEAT_AT_ONCE cases the R register wasn't incremented twice
 *		per repetition as it should have been. Those repeated opcodes
 *		could also underflow the ICount.
 *	  - Simplified TIME_LOOP_HACKS for BC and added two more for DE + HL
 *		timing loops. I think those hacks weren't endian safe before too.
 *	 Changes in 2.4:
 *	  - z80_reset zaps the entire context, sets IX and IY to 0xffff(!) and
 *		sets the Z flag. With these changes the Tehkan World Cup driver
 *		_seems_ to work again.
 *	 Changes in 2.3:
 *	  - External termination of the execution loop calls z80_burn() and
 *		z80_vm_burn() to burn an amount of cycles (R adjustment)
 *	  - Shortcuts which burn CPU cycles (BUSY_LOOP_HACKS and TIME_LOOP_HACKS)
 *		now also adjust the R register depending on the skipped opcodes.
 *	 Changes in 2.2:
 *	  - Fixed bugs in CPL, SCF and CCF instructions flag handling.
 *	  - Changed variable EA and ARG16() function to UINT32; this
 *		produces slightly more efficient code.
 *	  - The DD/FD XY CB opcodes where XY is 40-7F and Y is not 6/E
 *		are changed to calls to the X6/XE opcodes to reduce object size.
 *		They're hardly ever used so this should not yield a speed penalty.
 *	 New in 2.0:
 *	  - Optional more exact Z80 emulation (#define Z80_EXACT 1) according
 *		to a detailed description by Sean Young which can be found at:
 *		http://www.msxnet.org/tech/Z80/z80undoc.txt
 *****************************************************************************/

#include "shared.h"
#include "z80.h"
#include "cpuintrf.h"

/* Z80-internal globals -> heap state struct. Must come after shared.h
   (which defines SMS_State and the struct) and before any function body
   that uses Z80/_PC/SZ/cpu_readmap/etc. */
#include "z80_state.h"

#define change_pc16(a)
#define MAME_INLINE static __inline

#ifdef Z80_MSX
// #include "z80_msx.h"
#endif

#define VERBOSE 0

#if VERBOSE
#define LOG(x)	logerror x
#else
#define LOG(x)
#endif

/* z80_exec, z80_cycle_count, z80_requested_cycles, cpu_writemem16,
   cpu_writeport16, cpu_readport16, cpu_readmap[], cpu_writemap[] are now
   members of the heap state struct (sms_state.h), reached via macros.
   shared.h (included above) pulls in sms_state.h and activates them. */

#define cpu_readmem16(a)        cpu_readmap[(a) >> 10][(a) & 0x03FF]
#define cpu_readop(a)           cpu_readmap[(a) >> 10][(a) & 0x03FF]
#define cpu_readop_arg(a)       cpu_readmap[(a) >> 10][(a) & 0x03FF]

/* execute main opcodes inside a big switch statement */
#ifndef BIG_SWITCH
#define BIG_SWITCH			1
#endif

/* big flags array for ADD/ADC/SUB/SBC/CP results */
#define BIG_FLAGS_ARRAY     0

/* Set to 1 for a more exact (but somewhat slower) Z80 emulation */
#define Z80_EXACT			1

/* on JP and JR opcodes check for tight loops */
#define BUSY_LOOP_HACKS 	1

/* check for delay loops counting down BC */
#define TIME_LOOP_HACKS     1

#ifdef X86_ASM
#undef	BIG_FLAGS_ARRAY
#define BIG_FLAGS_ARRAY 	0
#endif

#define CF	0x01
#define NF	0x02
#define PF	0x04
#define VF	PF
#define XF	0x08
#define HF	0x10
#define YF	0x20
#define ZF	0x40
#define SF	0x80

#define INT_IRQ 0x01
#define NMI_IRQ 0x02

#define _PPC	Z80.PREPC.d 	/* previous program counter */

#define _PCD	Z80.PC.d
#define _PC 	Z80.PC.w.l

#define _SPD	Z80.SP.d
#define _SP 	Z80.SP.w.l

#define _AFD	Z80.AF.d
#define _AF 	Z80.AF.w.l
#define _A		Z80.AF.b.h
#define _F		Z80.AF.b.l

#define _BCD	Z80.BC.d
#define _BC 	Z80.BC.w.l
#define _B		Z80.BC.b.h
#define _C		Z80.BC.b.l

#define _DED	Z80.DE.d
#define _DE 	Z80.DE.w.l
#define _D		Z80.DE.b.h
#define _E		Z80.DE.b.l

#define _HLD	Z80.HL.d
#define _HL 	Z80.HL.w.l
#define _H		Z80.HL.b.h
#define _L		Z80.HL.b.l

#define _IXD	Z80.IX.d
#define _IX 	Z80.IX.w.l
#define _HX 	Z80.IX.b.h
#define _LX 	Z80.IX.b.l

#define _IYD	Z80.IY.d
#define _IY 	Z80.IY.w.l
#define _HY 	Z80.IY.b.h
#define _LY 	Z80.IY.b.l

#define _I		Z80.I
#define _R		Z80.R
#define _R2 	Z80.R2
#define _IM 	Z80.IM
#define _IFF1	Z80.IFF1
#define _IFF2	Z80.IFF2
#define _HALT	Z80.HALT

#ifdef Z80_MSX
	#define Z80_ICOUNT	z80_msx_ICount
#else
	#define	Z80_ICOUNT	z80_ICount
#endif

/* Z80_ICOUNT (== z80_ICount), Z80, Z80_Context, EA, after_EI and the
   SZ/SZ_BIT/SZP/SZHV_inc/SZHV_dec flag tables are now members of the heap
   state struct (sms_state.h), reached via macros. No writable statics here. */

#include "z80daa.h"

#if BIG_FLAGS_ARRAY
#include <signal.h>
/* SZHVC_add/SZHVC_sub also live in the state struct (only used when
   BIG_FLAGS_ARRAY is set, which it is not on this target). */
#endif

#if Z80_EXACT
/* tmp1 value for ini/inir/outi/otir for [C.1-0][io.1-0] */
static const UINT8 irep_tmp1[4][4] = {
	{0,0,1,0},{0,1,0,1},{1,0,1,1},{0,1,1,0}
};

/* tmp1 value for ind/indr/outd/otdr for [C.1-0][io.1-0] */
static const UINT8 drep_tmp1[4][4] = {
	{0,1,0,0},{1,0,0,1},{0,0,1,0},{0,1,0,1}
};

/* tmp2 value for all in/out repeated opcodes for B.7-0 */
static const UINT8 breg_tmp2[256] = {
	0,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,
	0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,
	1,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,
	1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,
	0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,
	1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,
	0,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,
	0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,
	1,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,
	1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,
	0,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,
	0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,
	1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,
	0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,
	1,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,
	1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1
};
#endif

static const UINT8 cc_op[0x100] = {
 4,10, 7, 6, 4, 4, 7, 4, 4,11, 7, 6, 4, 4, 7, 4,
 8,10, 7, 6, 4, 4, 7, 4,12,11, 7, 6, 4, 4, 7, 4,
 7,10,16, 6, 4, 4, 7, 4, 7,11,16, 6, 4, 4, 7, 4,
 7,10,13, 6,11,11,10, 4, 7,11,13, 6, 4, 4, 7, 4,
 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
 7, 7, 7, 7, 7, 7, 4, 7, 4, 4, 4, 4, 4, 4, 7, 4,
 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
 5,10,10,10,10,11, 7,11, 5,10,10, 0,10,17, 7,11,
 5,10,10,11,10,11, 7,11, 5, 4,10,11,10, 0, 7,11,
 5,10,10,19,10,11, 7,11, 5, 4,10, 4,10, 0, 7,11,
 5,10,10, 4,10,11, 7,11, 5, 6,10, 4,10, 0, 7,11};

static const UINT8 cc_cb[0x100] = {
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8,
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8,
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8,
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8,
 8, 8, 8, 8, 8, 8,12, 8, 8, 8, 8, 8, 8, 8,12, 8,
 8, 8, 8, 8, 8, 8,12, 8, 8, 8, 8, 8, 8, 8,12, 8,
 8, 8, 8, 8, 8, 8,12, 8, 8, 8, 8, 8, 8, 8,12, 8,
 8, 8, 8, 8, 8, 8,12, 8, 8, 8, 8, 8, 8, 8,12, 8,
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8,
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8,
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8,
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8,
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8,
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8,
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8,
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8};

static const UINT8 cc_ed[0x100] = {
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
12,12,15,20, 8, 8, 8, 9,12,12,15,20, 8, 8, 8, 9,
12,12,15,20, 8, 8, 8, 9,12,12,15,20, 8, 8, 8, 9,
12,12,15,20, 8, 8, 8,18,12,12,15,20, 8, 8, 8,18,
12,12,15,20, 8, 8, 8, 8,12,12,15,20, 8, 8, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
16,16,16,16, 8, 8, 8, 8,16,16,16,16, 8, 8, 8, 8,
16,16,16,16, 8, 8, 8, 8,16,16,16,16, 8, 8, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8};

static const UINT8 cc_xy[0x100] = {
 4, 4, 4, 4, 4, 4, 4, 4, 4,15, 4, 4, 4, 4, 4, 4,
 4, 4, 4, 4, 4, 4, 4, 4, 4,15, 4, 4, 4, 4, 4, 4,
 4,14,20,10, 9, 9, 9, 4, 4,15,20,10, 9, 9, 9, 4,
 4, 4, 4, 4,23,23,19, 4, 4,15, 4, 4, 4, 4, 4, 4,
 4, 4, 4, 4, 9, 9,19, 4, 4, 4, 4, 4, 9, 9,19, 4,
 4, 4, 4, 4, 9, 9,19, 4, 4, 4, 4, 4, 9, 9,19, 4,
 9, 9, 9, 9, 9, 9,19, 9, 9, 9, 9, 9, 9, 9,19, 9,
19,19,19,19,19,19, 4,19, 4, 4, 4, 4, 9, 9,19, 4,
 4, 4, 4, 4, 9, 9,19, 4, 4, 4, 4, 4, 9, 9,19, 4,
 4, 4, 4, 4, 9, 9,19, 4, 4, 4, 4, 4, 9, 9,19, 4,
 4, 4, 4, 4, 9, 9,19, 4, 4, 4, 4, 4, 9, 9,19, 4,
 4, 4, 4, 4, 9, 9,19, 4, 4, 4, 4, 4, 9, 9,19, 4,
 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0, 4, 4, 4, 4,
 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
 4,14, 4,23, 4,15, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4,
 4, 4, 4, 4, 4, 4, 4, 4, 4,10, 4, 4, 4, 4, 4, 4};

static const UINT8 cc_xycb[0x100] = {
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,
20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,
20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,
20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23};

/* extra cycles if jr/jp/call taken and 'interrupt latency' on rst 0-7 */
static const UINT8 cc_ex[0x100] = {
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* DJNZ */
 5, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0,	/* JR NZ/JR Z */
 5, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0,	/* JR NC/JR C */
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 5, 5, 5, 5, 0, 0, 0, 0, 5, 5, 5, 5, 0, 0, 0, 0,	/* LDIR/CPIR/INIR/OTIR LDDR/CPDR/INDR/OTDR */
 6, 0, 0, 0, 7, 0, 0, 2, 6, 0, 0, 0, 7, 0, 0, 2,
 6, 0, 0, 0, 7, 0, 0, 2, 6, 0, 0, 0, 7, 0, 0, 2,
 6, 0, 0, 0, 7, 0, 0, 2, 6, 0, 0, 0, 7, 0, 0, 2,
 6, 0, 0, 0, 7, 0, 0, 2, 6, 0, 0, 0, 7, 0, 0, 2};

/* E32SMS / EKA1 fix: the original `static const UINT8 *cc[6] = { cc_op, ... }`
   is an array of pointers to the cycle-count tables. Its initialisers are the
   addresses of other objects, so it carries load-time relocations -- which
   petran rejects as "initialised data" in a .app DLL once EPOCALLOWDLLDATA is
   removed. (It is also technically writable: z80_set_cycle_table() assigns to
   it, though that function is never called in this build.)

   Replaced with a reloc-free inline selector. Each cc_* table is itself a
   `static const UINT8[...]` of plain numbers, which lives in .rodata with no
   relocations and is accepted by petran. cc(t) returns the right one by index.
   All former `cc(t)[op]` sites now read `cc(t)[op]` (see sed-applied edits). */
static __inline const UINT8 *cc(int t)
{
	switch(t)
	{
	case Z80_TABLE_op:   return cc_op;
	case Z80_TABLE_cb:   return cc_cb;
	case Z80_TABLE_ed:   return cc_ed;
	case Z80_TABLE_xy:   return cc_xy;
	case Z80_TABLE_xycb: return cc_xycb;
	case Z80_TABLE_ex:   return cc_ex;
	}
	return cc_op;
}
#define Z80_TABLE_dd	Z80_TABLE_xy
#define Z80_TABLE_fd	Z80_TABLE_xy

static void take_interrupt(void);

typedef void (*funcptr)(void);

#define PROTOTYPES(tablename,prefix) \
    MAME_INLINE void prefix##_00(void); MAME_INLINE void prefix##_01(void); MAME_INLINE void prefix##_02(void); MAME_INLINE void prefix##_03(void); \
    MAME_INLINE void prefix##_04(void); MAME_INLINE void prefix##_05(void); MAME_INLINE void prefix##_06(void); MAME_INLINE void prefix##_07(void); \
    MAME_INLINE void prefix##_08(void); MAME_INLINE void prefix##_09(void); MAME_INLINE void prefix##_0a(void); MAME_INLINE void prefix##_0b(void); \
    MAME_INLINE void prefix##_0c(void); MAME_INLINE void prefix##_0d(void); MAME_INLINE void prefix##_0e(void); MAME_INLINE void prefix##_0f(void); \
    MAME_INLINE void prefix##_10(void); MAME_INLINE void prefix##_11(void); MAME_INLINE void prefix##_12(void); MAME_INLINE void prefix##_13(void); \
    MAME_INLINE void prefix##_14(void); MAME_INLINE void prefix##_15(void); MAME_INLINE void prefix##_16(void); MAME_INLINE void prefix##_17(void); \
    MAME_INLINE void prefix##_18(void); MAME_INLINE void prefix##_19(void); MAME_INLINE void prefix##_1a(void); MAME_INLINE void prefix##_1b(void); \
    MAME_INLINE void prefix##_1c(void); MAME_INLINE void prefix##_1d(void); MAME_INLINE void prefix##_1e(void); MAME_INLINE void prefix##_1f(void); \
    MAME_INLINE void prefix##_20(void); MAME_INLINE void prefix##_21(void); MAME_INLINE void prefix##_22(void); MAME_INLINE void prefix##_23(void); \
    MAME_INLINE void prefix##_24(void); MAME_INLINE void prefix##_25(void); MAME_INLINE void prefix##_26(void); MAME_INLINE void prefix##_27(void); \
    MAME_INLINE void prefix##_28(void); MAME_INLINE void prefix##_29(void); MAME_INLINE void prefix##_2a(void); MAME_INLINE void prefix##_2b(void); \
    MAME_INLINE void prefix##_2c(void); MAME_INLINE void prefix##_2d(void); MAME_INLINE void prefix##_2e(void); MAME_INLINE void prefix##_2f(void); \
    MAME_INLINE void prefix##_30(void); MAME_INLINE void prefix##_31(void); MAME_INLINE void prefix##_32(void); MAME_INLINE void prefix##_33(void); \
    MAME_INLINE void prefix##_34(void); MAME_INLINE void prefix##_35(void); MAME_INLINE void prefix##_36(void); MAME_INLINE void prefix##_37(void); \
    MAME_INLINE void prefix##_38(void); MAME_INLINE void prefix##_39(void); MAME_INLINE void prefix##_3a(void); MAME_INLINE void prefix##_3b(void); \
    MAME_INLINE void prefix##_3c(void); MAME_INLINE void prefix##_3d(void); MAME_INLINE void prefix##_3e(void); MAME_INLINE void prefix##_3f(void); \
    MAME_INLINE void prefix##_40(void); MAME_INLINE void prefix##_41(void); MAME_INLINE void prefix##_42(void); MAME_INLINE void prefix##_43(void); \
    MAME_INLINE void prefix##_44(void); MAME_INLINE void prefix##_45(void); MAME_INLINE void prefix##_46(void); MAME_INLINE void prefix##_47(void); \
    MAME_INLINE void prefix##_48(void); MAME_INLINE void prefix##_49(void); MAME_INLINE void prefix##_4a(void); MAME_INLINE void prefix##_4b(void); \
    MAME_INLINE void prefix##_4c(void); MAME_INLINE void prefix##_4d(void); MAME_INLINE void prefix##_4e(void); MAME_INLINE void prefix##_4f(void); \
    MAME_INLINE void prefix##_50(void); MAME_INLINE void prefix##_51(void); MAME_INLINE void prefix##_52(void); MAME_INLINE void prefix##_53(void); \
    MAME_INLINE void prefix##_54(void); MAME_INLINE void prefix##_55(void); MAME_INLINE void prefix##_56(void); MAME_INLINE void prefix##_57(void); \
    MAME_INLINE void prefix##_58(void); MAME_INLINE void prefix##_59(void); MAME_INLINE void prefix##_5a(void); MAME_INLINE void prefix##_5b(void); \
    MAME_INLINE void prefix##_5c(void); MAME_INLINE void prefix##_5d(void); MAME_INLINE void prefix##_5e(void); MAME_INLINE void prefix##_5f(void); \
    MAME_INLINE void prefix##_60(void); MAME_INLINE void prefix##_61(void); MAME_INLINE void prefix##_62(void); MAME_INLINE void prefix##_63(void); \
    MAME_INLINE void prefix##_64(void); MAME_INLINE void prefix##_65(void); MAME_INLINE void prefix##_66(void); MAME_INLINE void prefix##_67(void); \
    MAME_INLINE void prefix##_68(void); MAME_INLINE void prefix##_69(void); MAME_INLINE void prefix##_6a(void); MAME_INLINE void prefix##_6b(void); \
    MAME_INLINE void prefix##_6c(void); MAME_INLINE void prefix##_6d(void); MAME_INLINE void prefix##_6e(void); MAME_INLINE void prefix##_6f(void); \
    MAME_INLINE void prefix##_70(void); MAME_INLINE void prefix##_71(void); MAME_INLINE void prefix##_72(void); MAME_INLINE void prefix##_73(void); \
    MAME_INLINE void prefix##_74(void); MAME_INLINE void prefix##_75(void); MAME_INLINE void prefix##_76(void); MAME_INLINE void prefix##_77(void); \
    MAME_INLINE void prefix##_78(void); MAME_INLINE void prefix##_79(void); MAME_INLINE void prefix##_7a(void); MAME_INLINE void prefix##_7b(void); \
    MAME_INLINE void prefix##_7c(void); MAME_INLINE void prefix##_7d(void); MAME_INLINE void prefix##_7e(void); MAME_INLINE void prefix##_7f(void); \
    MAME_INLINE void prefix##_80(void); MAME_INLINE void prefix##_81(void); MAME_INLINE void prefix##_82(void); MAME_INLINE void prefix##_83(void); \
    MAME_INLINE void prefix##_84(void); MAME_INLINE void prefix##_85(void); MAME_INLINE void prefix##_86(void); MAME_INLINE void prefix##_87(void); \
    MAME_INLINE void prefix##_88(void); MAME_INLINE void prefix##_89(void); MAME_INLINE void prefix##_8a(void); MAME_INLINE void prefix##_8b(void); \
    MAME_INLINE void prefix##_8c(void); MAME_INLINE void prefix##_8d(void); MAME_INLINE void prefix##_8e(void); MAME_INLINE void prefix##_8f(void); \
    MAME_INLINE void prefix##_90(void); MAME_INLINE void prefix##_91(void); MAME_INLINE void prefix##_92(void); MAME_INLINE void prefix##_93(void); \
    MAME_INLINE void prefix##_94(void); MAME_INLINE void prefix##_95(void); MAME_INLINE void prefix##_96(void); MAME_INLINE void prefix##_97(void); \
    MAME_INLINE void prefix##_98(void); MAME_INLINE void prefix##_99(void); MAME_INLINE void prefix##_9a(void); MAME_INLINE void prefix##_9b(void); \
    MAME_INLINE void prefix##_9c(void); MAME_INLINE void prefix##_9d(void); MAME_INLINE void prefix##_9e(void); MAME_INLINE void prefix##_9f(void); \
    MAME_INLINE void prefix##_a0(void); MAME_INLINE void prefix##_a1(void); MAME_INLINE void prefix##_a2(void); MAME_INLINE void prefix##_a3(void); \
    MAME_INLINE void prefix##_a4(void); MAME_INLINE void prefix##_a5(void); MAME_INLINE void prefix##_a6(void); MAME_INLINE void prefix##_a7(void); \
    MAME_INLINE void prefix##_a8(void); MAME_INLINE void prefix##_a9(void); MAME_INLINE void prefix##_aa(void); MAME_INLINE void prefix##_ab(void); \
    MAME_INLINE void prefix##_ac(void); MAME_INLINE void prefix##_ad(void); MAME_INLINE void prefix##_ae(void); MAME_INLINE void prefix##_af(void); \
    MAME_INLINE void prefix##_b0(void); MAME_INLINE void prefix##_b1(void); MAME_INLINE void prefix##_b2(void); MAME_INLINE void prefix##_b3(void); \
    MAME_INLINE void prefix##_b4(void); MAME_INLINE void prefix##_b5(void); MAME_INLINE void prefix##_b6(void); MAME_INLINE void prefix##_b7(void); \
    MAME_INLINE void prefix##_b8(void); MAME_INLINE void prefix##_b9(void); MAME_INLINE void prefix##_ba(void); MAME_INLINE void prefix##_bb(void); \
    MAME_INLINE void prefix##_bc(void); MAME_INLINE void prefix##_bd(void); MAME_INLINE void prefix##_be(void); MAME_INLINE void prefix##_bf(void); \
    MAME_INLINE void prefix##_c0(void); MAME_INLINE void prefix##_c1(void); MAME_INLINE void prefix##_c2(void); MAME_INLINE void prefix##_c3(void); \
    MAME_INLINE void prefix##_c4(void); MAME_INLINE void prefix##_c5(void); MAME_INLINE void prefix##_c6(void); MAME_INLINE void prefix##_c7(void); \
    MAME_INLINE void prefix##_c8(void); MAME_INLINE void prefix##_c9(void); MAME_INLINE void prefix##_ca(void); MAME_INLINE void prefix##_cb(void); \
    MAME_INLINE void prefix##_cc(void); MAME_INLINE void prefix##_cd(void); MAME_INLINE void prefix##_ce(void); MAME_INLINE void prefix##_cf(void); \
    MAME_INLINE void prefix##_d0(void); MAME_INLINE void prefix##_d1(void); MAME_INLINE void prefix##_d2(void); MAME_INLINE void prefix##_d3(void); \
    MAME_INLINE void prefix##_d4(void); MAME_INLINE void prefix##_d5(void); MAME_INLINE void prefix##_d6(void); MAME_INLINE void prefix##_d7(void); \
    MAME_INLINE void prefix##_d8(void); MAME_INLINE void prefix##_d9(void); MAME_INLINE void prefix##_da(void); MAME_INLINE void prefix##_db(void); \
    MAME_INLINE void prefix##_dc(void); MAME_INLINE void prefix##_dd(void); MAME_INLINE void prefix##_de(void); MAME_INLINE void prefix##_df(void); \
    MAME_INLINE void prefix##_e0(void); MAME_INLINE void prefix##_e1(void); MAME_INLINE void prefix##_e2(void); MAME_INLINE void prefix##_e3(void); \
    MAME_INLINE void prefix##_e4(void); MAME_INLINE void prefix##_e5(void); MAME_INLINE void prefix##_e6(void); MAME_INLINE void prefix##_e7(void); \
    MAME_INLINE void prefix##_e8(void); MAME_INLINE void prefix##_e9(void); MAME_INLINE void prefix##_ea(void); MAME_INLINE void prefix##_eb(void); \
    MAME_INLINE void prefix##_ec(void); MAME_INLINE void prefix##_ed(void); MAME_INLINE void prefix##_ee(void); MAME_INLINE void prefix##_ef(void); \
    MAME_INLINE void prefix##_f0(void); MAME_INLINE void prefix##_f1(void); MAME_INLINE void prefix##_f2(void); MAME_INLINE void prefix##_f3(void); \
    MAME_INLINE void prefix##_f4(void); MAME_INLINE void prefix##_f5(void); MAME_INLINE void prefix##_f6(void); MAME_INLINE void prefix##_f7(void); \
    MAME_INLINE void prefix##_f8(void); MAME_INLINE void prefix##_f9(void); MAME_INLINE void prefix##_fa(void); MAME_INLINE void prefix##_fb(void); \
    MAME_INLINE void prefix##_fc(void); MAME_INLINE void prefix##_fd(void); MAME_INLINE void prefix##_fe(void); MAME_INLINE void prefix##_ff(void);
/* NOTE (E32SMS / EKA1 fix): the original PROTOTYPES macro also emitted
   `static const funcptr tablename[0x100] = { ... }`.  On EKA1 a const array of
   function pointers carries load-time relocations, which petran rejects in a
   .app DLL ("has initialised data") once EPOCALLOWDLLDATA is removed.  The
   table is no longer generated; every opcode is now dispatched through the
   switch form of EXEC (see below), so the tables are unused.  The `tablename`
   parameter is retained only for call-site compatibility. */

PROTOTYPES(Z80op,op);
PROTOTYPES(Z80cb,cb);
PROTOTYPES(Z80dd,dd);
PROTOTYPES(Z80ed,ed);
PROTOTYPES(Z80fd,fd);
PROTOTYPES(Z80xycb,xycb);

/****************************************************************************/
/* Burn an odd amount of cycles, that is instructions taking something		*/
/* different from 4 T-states per opcode (and R increment)					*/
/****************************************************************************/
MAME_INLINE void BURNODD(int cycles, int opcodes, int cyclesum)
{
    SMS_Z_CACHE;
	if( cycles > 0 )
	{
		_R += (cycles / cyclesum) * opcodes;
		Z80_ICOUNT -= (cycles / cyclesum) * cyclesum;
	}
}

/***************************************************************
 * define an opcode function
 ***************************************************************/
#define OP(prefix,opcode)  MAME_INLINE void prefix##_##opcode(void)

/***************************************************************
 * adjust cycle count by n T-states
 ***************************************************************/
#define CC(prefix,opcode) Z80_ICOUNT -= cc(Z80_TABLE_##prefix)[opcode]

/***************************************************************
 * execute an opcode
 ***************************************************************/
/* E32SMS / EKA1 fix: EXEC formerly dispatched through the const funcptr table
   Z80##prefix[op], which is initialised data petran rejects in a .app DLL.
   The table has been removed; EXEC now uses the same switch(op) form as
   EXEC_INLINE, so every opcode -- base and prefixed (cb/dd/ed/fd/xycb) -- is a
   direct call.  EXEC_INLINE is defined as EXEC below. */
#define EXEC(prefix,opcode)										\
{																\
	unsigned op = opcode;										\
	CC(prefix,op);												\
	switch(op)													\
	{															\
	case 0x00:prefix##_##00();break; case 0x01:prefix##_##01();break; case 0x02:prefix##_##02();break; case 0x03:prefix##_##03();break; \
	case 0x04:prefix##_##04();break; case 0x05:prefix##_##05();break; case 0x06:prefix##_##06();break; case 0x07:prefix##_##07();break; \
	case 0x08:prefix##_##08();break; case 0x09:prefix##_##09();break; case 0x0a:prefix##_##0a();break; case 0x0b:prefix##_##0b();break; \
	case 0x0c:prefix##_##0c();break; case 0x0d:prefix##_##0d();break; case 0x0e:prefix##_##0e();break; case 0x0f:prefix##_##0f();break; \
	case 0x10:prefix##_##10();break; case 0x11:prefix##_##11();break; case 0x12:prefix##_##12();break; case 0x13:prefix##_##13();break; \
	case 0x14:prefix##_##14();break; case 0x15:prefix##_##15();break; case 0x16:prefix##_##16();break; case 0x17:prefix##_##17();break; \
	case 0x18:prefix##_##18();break; case 0x19:prefix##_##19();break; case 0x1a:prefix##_##1a();break; case 0x1b:prefix##_##1b();break; \
	case 0x1c:prefix##_##1c();break; case 0x1d:prefix##_##1d();break; case 0x1e:prefix##_##1e();break; case 0x1f:prefix##_##1f();break; \
	case 0x20:prefix##_##20();break; case 0x21:prefix##_##21();break; case 0x22:prefix##_##22();break; case 0x23:prefix##_##23();break; \
	case 0x24:prefix##_##24();break; case 0x25:prefix##_##25();break; case 0x26:prefix##_##26();break; case 0x27:prefix##_##27();break; \
	case 0x28:prefix##_##28();break; case 0x29:prefix##_##29();break; case 0x2a:prefix##_##2a();break; case 0x2b:prefix##_##2b();break; \
	case 0x2c:prefix##_##2c();break; case 0x2d:prefix##_##2d();break; case 0x2e:prefix##_##2e();break; case 0x2f:prefix##_##2f();break; \
	case 0x30:prefix##_##30();break; case 0x31:prefix##_##31();break; case 0x32:prefix##_##32();break; case 0x33:prefix##_##33();break; \
	case 0x34:prefix##_##34();break; case 0x35:prefix##_##35();break; case 0x36:prefix##_##36();break; case 0x37:prefix##_##37();break; \
	case 0x38:prefix##_##38();break; case 0x39:prefix##_##39();break; case 0x3a:prefix##_##3a();break; case 0x3b:prefix##_##3b();break; \
	case 0x3c:prefix##_##3c();break; case 0x3d:prefix##_##3d();break; case 0x3e:prefix##_##3e();break; case 0x3f:prefix##_##3f();break; \
	case 0x40:prefix##_##40();break; case 0x41:prefix##_##41();break; case 0x42:prefix##_##42();break; case 0x43:prefix##_##43();break; \
	case 0x44:prefix##_##44();break; case 0x45:prefix##_##45();break; case 0x46:prefix##_##46();break; case 0x47:prefix##_##47();break; \
	case 0x48:prefix##_##48();break; case 0x49:prefix##_##49();break; case 0x4a:prefix##_##4a();break; case 0x4b:prefix##_##4b();break; \
	case 0x4c:prefix##_##4c();break; case 0x4d:prefix##_##4d();break; case 0x4e:prefix##_##4e();break; case 0x4f:prefix##_##4f();break; \
	case 0x50:prefix##_##50();break; case 0x51:prefix##_##51();break; case 0x52:prefix##_##52();break; case 0x53:prefix##_##53();break; \
	case 0x54:prefix##_##54();break; case 0x55:prefix##_##55();break; case 0x56:prefix##_##56();break; case 0x57:prefix##_##57();break; \
	case 0x58:prefix##_##58();break; case 0x59:prefix##_##59();break; case 0x5a:prefix##_##5a();break; case 0x5b:prefix##_##5b();break; \
	case 0x5c:prefix##_##5c();break; case 0x5d:prefix##_##5d();break; case 0x5e:prefix##_##5e();break; case 0x5f:prefix##_##5f();break; \
	case 0x60:prefix##_##60();break; case 0x61:prefix##_##61();break; case 0x62:prefix##_##62();break; case 0x63:prefix##_##63();break; \
	case 0x64:prefix##_##64();break; case 0x65:prefix##_##65();break; case 0x66:prefix##_##66();break; case 0x67:prefix##_##67();break; \
	case 0x68:prefix##_##68();break; case 0x69:prefix##_##69();break; case 0x6a:prefix##_##6a();break; case 0x6b:prefix##_##6b();break; \
	case 0x6c:prefix##_##6c();break; case 0x6d:prefix##_##6d();break; case 0x6e:prefix##_##6e();break; case 0x6f:prefix##_##6f();break; \
	case 0x70:prefix##_##70();break; case 0x71:prefix##_##71();break; case 0x72:prefix##_##72();break; case 0x73:prefix##_##73();break; \
	case 0x74:prefix##_##74();break; case 0x75:prefix##_##75();break; case 0x76:prefix##_##76();break; case 0x77:prefix##_##77();break; \
	case 0x78:prefix##_##78();break; case 0x79:prefix##_##79();break; case 0x7a:prefix##_##7a();break; case 0x7b:prefix##_##7b();break; \
	case 0x7c:prefix##_##7c();break; case 0x7d:prefix##_##7d();break; case 0x7e:prefix##_##7e();break; case 0x7f:prefix##_##7f();break; \
	case 0x80:prefix##_##80();break; case 0x81:prefix##_##81();break; case 0x82:prefix##_##82();break; case 0x83:prefix##_##83();break; \
	case 0x84:prefix##_##84();break; case 0x85:prefix##_##85();break; case 0x86:prefix##_##86();break; case 0x87:prefix##_##87();break; \
	case 0x88:prefix##_##88();break; case 0x89:prefix##_##89();break; case 0x8a:prefix##_##8a();break; case 0x8b:prefix##_##8b();break; \
	case 0x8c:prefix##_##8c();break; case 0x8d:prefix##_##8d();break; case 0x8e:prefix##_##8e();break; case 0x8f:prefix##_##8f();break; \
	case 0x90:prefix##_##90();break; case 0x91:prefix##_##91();break; case 0x92:prefix##_##92();break; case 0x93:prefix##_##93();break; \
	case 0x94:prefix##_##94();break; case 0x95:prefix##_##95();break; case 0x96:prefix##_##96();break; case 0x97:prefix##_##97();break; \
	case 0x98:prefix##_##98();break; case 0x99:prefix##_##99();break; case 0x9a:prefix##_##9a();break; case 0x9b:prefix##_##9b();break; \
	case 0x9c:prefix##_##9c();break; case 0x9d:prefix##_##9d();break; case 0x9e:prefix##_##9e();break; case 0x9f:prefix##_##9f();break; \
	case 0xa0:prefix##_##a0();break; case 0xa1:prefix##_##a1();break; case 0xa2:prefix##_##a2();break; case 0xa3:prefix##_##a3();break; \
	case 0xa4:prefix##_##a4();break; case 0xa5:prefix##_##a5();break; case 0xa6:prefix##_##a6();break; case 0xa7:prefix##_##a7();break; \
	case 0xa8:prefix##_##a8();break; case 0xa9:prefix##_##a9();break; case 0xaa:prefix##_##aa();break; case 0xab:prefix##_##ab();break; \
	case 0xac:prefix##_##ac();break; case 0xad:prefix##_##ad();break; case 0xae:prefix##_##ae();break; case 0xaf:prefix##_##af();break; \
	case 0xb0:prefix##_##b0();break; case 0xb1:prefix##_##b1();break; case 0xb2:prefix##_##b2();break; case 0xb3:prefix##_##b3();break; \
	case 0xb4:prefix##_##b4();break; case 0xb5:prefix##_##b5();break; case 0xb6:prefix##_##b6();break; case 0xb7:prefix##_##b7();break; \
	case 0xb8:prefix##_##b8();break; case 0xb9:prefix##_##b9();break; case 0xba:prefix##_##ba();break; case 0xbb:prefix##_##bb();break; \
	case 0xbc:prefix##_##bc();break; case 0xbd:prefix##_##bd();break; case 0xbe:prefix##_##be();break; case 0xbf:prefix##_##bf();break; \
	case 0xc0:prefix##_##c0();break; case 0xc1:prefix##_##c1();break; case 0xc2:prefix##_##c2();break; case 0xc3:prefix##_##c3();break; \
	case 0xc4:prefix##_##c4();break; case 0xc5:prefix##_##c5();break; case 0xc6:prefix##_##c6();break; case 0xc7:prefix##_##c7();break; \
	case 0xc8:prefix##_##c8();break; case 0xc9:prefix##_##c9();break; case 0xca:prefix##_##ca();break; case 0xcb:prefix##_##cb();break; \
	case 0xcc:prefix##_##cc();break; case 0xcd:prefix##_##cd();break; case 0xce:prefix##_##ce();break; case 0xcf:prefix##_##cf();break; \
	case 0xd0:prefix##_##d0();break; case 0xd1:prefix##_##d1();break; case 0xd2:prefix##_##d2();break; case 0xd3:prefix##_##d3();break; \
	case 0xd4:prefix##_##d4();break; case 0xd5:prefix##_##d5();break; case 0xd6:prefix##_##d6();break; case 0xd7:prefix##_##d7();break; \
	case 0xd8:prefix##_##d8();break; case 0xd9:prefix##_##d9();break; case 0xda:prefix##_##da();break; case 0xdb:prefix##_##db();break; \
	case 0xdc:prefix##_##dc();break; case 0xdd:prefix##_##dd();break; case 0xde:prefix##_##de();break; case 0xdf:prefix##_##df();break; \
	case 0xe0:prefix##_##e0();break; case 0xe1:prefix##_##e1();break; case 0xe2:prefix##_##e2();break; case 0xe3:prefix##_##e3();break; \
	case 0xe4:prefix##_##e4();break; case 0xe5:prefix##_##e5();break; case 0xe6:prefix##_##e6();break; case 0xe7:prefix##_##e7();break; \
	case 0xe8:prefix##_##e8();break; case 0xe9:prefix##_##e9();break; case 0xea:prefix##_##ea();break; case 0xeb:prefix##_##eb();break; \
	case 0xec:prefix##_##ec();break; case 0xed:prefix##_##ed();break; case 0xee:prefix##_##ee();break; case 0xef:prefix##_##ef();break; \
	case 0xf0:prefix##_##f0();break; case 0xf1:prefix##_##f1();break; case 0xf2:prefix##_##f2();break; case 0xf3:prefix##_##f3();break; \
	case 0xf4:prefix##_##f4();break; case 0xf5:prefix##_##f5();break; case 0xf6:prefix##_##f6();break; case 0xf7:prefix##_##f7();break; \
	case 0xf8:prefix##_##f8();break; case 0xf9:prefix##_##f9();break; case 0xfa:prefix##_##fa();break; case 0xfb:prefix##_##fb();break; \
	case 0xfc:prefix##_##fc();break; case 0xfd:prefix##_##fd();break; case 0xfe:prefix##_##fe();break; case 0xff:prefix##_##ff();break; \
	}																																	\
}

/* EXEC_INLINE is now identical to EXEC (both switch-based, direct calls). */
#define EXEC_INLINE EXEC


/***************************************************************
 * Enter HALT state; write 1 to fake port on first execution
 ***************************************************************/
#ifdef Z80_MSX
#define ENTER_HALT {											\
	_PC--;														\
	_HALT = 1;													\
	if( !after_EI ) 											\
		z80_msx_burn( Z80_ICOUNT ); 							\
}
#else
#define ENTER_HALT {											\
	_PC--;														\
	_HALT = 1;													\
	if( !after_EI ) 											\
		z80_burn( Z80_ICOUNT ); 								\
}
#endif

/***************************************************************
 * Leave HALT state; write 0 to fake port
 ***************************************************************/
#define LEAVE_HALT {											\
	if( _HALT ) 												\
	{															\
		_HALT = 0;												\
		_PC++;													\
	}															\
}

/***************************************************************
 * Input a byte from given I/O port
 ***************************************************************/
#define IN(port)   ((UINT8)cpu_readport16(port))

/***************************************************************
 * Output a byte to given I/O port
 ***************************************************************/
#define OUT(port,value) cpu_writeport16(port,value)

/***************************************************************
 * Read a byte from given memory location
 ***************************************************************/
#define RM(addr) (UINT8)cpu_readmem16(addr)

/***************************************************************
 * Read a word from given memory location
 ***************************************************************/
MAME_INLINE void RM16( UINT32 addr, PAIR *r )
{
    SMS_Z_CACHE;
	r->b.l = RM(addr);
	r->b.h = RM((addr+1)&0xffff);
}

/***************************************************************
 * Write a byte to given memory location
 ***************************************************************/
#define WM(addr,value) cpu_writemem16(addr,value)

/***************************************************************
 * Write a word to given memory location
 ***************************************************************/
MAME_INLINE void WM16( UINT32 addr, PAIR *r )
{
    SMS_Z_CACHE;
	WM(addr,r->b.l);
	WM((addr+1)&0xffff,r->b.h);
}

/***************************************************************
 * ROP() is identical to RM() except it is used for
 * reading opcodes. In case of system with memory mapped I/O,
 * this function can be used to greatly speed up emulation
 ***************************************************************/
MAME_INLINE UINT8 ROP(void)
{
    SMS_Z_CACHE;
	unsigned pc = _PCD;
#ifdef Z80_MSX
	if ( !(pc & 0x1fff) ) change_pc16 (pc);
#endif
	_PC++;
	return cpu_readop(pc);
}

/****************************************************************
 * ARG() is identical to ROP() except it is used
 * for reading opcode arguments. This difference can be used to
 * support systems that use different encoding mechanisms for
 * opcodes and opcode arguments
 ***************************************************************/
MAME_INLINE UINT8 ARG(void)
{
    SMS_Z_CACHE;
	unsigned pc = _PCD;
#ifdef Z80_MSX
	if ( !(pc & 0x1fff) ) change_pc16 (pc);
#endif
	_PC++;
	return cpu_readop_arg(pc);
}

MAME_INLINE UINT32 ARG16(void)
{
    SMS_Z_CACHE;
#ifdef Z80_MSX
	unsigned ret = ARG();
	return ret | ((ARG()&0xffff) << 8);
#else
	unsigned pc = _PCD;
	_PC += 2;
	return cpu_readop_arg(pc) | (cpu_readop_arg((pc+1)&0xffff) << 8);
#endif
}

/***************************************************************
 * Calculate the effective address EA of an opcode using
 * IX+offset resp. IY+offset addressing.
 ***************************************************************/
#define EAX EA = (UINT32)(UINT16)(_IX+(INT8)ARG())
#define EAY EA = (UINT32)(UINT16)(_IY+(INT8)ARG())

/***************************************************************
 * POP
 ***************************************************************/
#define POP(DR) { RM16( _SPD, &Z80.DR ); _SP += 2; }

/***************************************************************
 * PUSH
 ***************************************************************/
#define PUSH(SR) { _SP -= 2; WM16( _SPD, &Z80.SR ); }

/***************************************************************
 * JP
 ***************************************************************/
#if BUSY_LOOP_HACKS
#define JP {													\
	unsigned oldpc = _PCD-1;									\
	_PCD = ARG16(); 											\
	change_pc16(_PCD);											\
	/* speed up busy loop */									\
	if( _PCD == oldpc ) 										\
	{															\
		if( !after_EI ) 										\
			BURNODD( Z80_ICOUNT, 1, cc(Z80_TABLE_op)[0xc3] );	\
	}															\
	else														\
	{															\
		UINT8 op = cpu_readop(_PCD);							\
		if( _PCD == oldpc-1 )									\
		{														\
			/* NOP - JP $-1 or EI - JP $-1 */					\
			if ( op == 0x00 || op == 0xfb ) 					\
			{													\
				if( !after_EI ) 								\
					BURNODD( Z80_ICOUNT-cc(Z80_TABLE_op)[0x00], \
						2, cc(Z80_TABLE_op)[0x00]+cc(Z80_TABLE_op)[0xc3]); \
			}													\
		}														\
		else													\
		/* LD SP,#xxxx - JP $-3 (Galaga) */ 					\
		if( _PCD == oldpc-3 && op == 0x31 ) 					\
		{														\
			if( !after_EI ) 									\
				BURNODD( Z80_ICOUNT-cc(Z80_TABLE_op)[0x31], 	\
					2, cc(Z80_TABLE_op)[0x31]+cc(Z80_TABLE_op)[0xc3]); \
		}														\
	}															\
}
#else
#define JP {													\
	_PCD = ARG16(); 											\
	change_pc16(_PCD);											\
}
#endif

/***************************************************************
 * JP_COND
 ***************************************************************/

#define JP_COND(cond)											\
	if( cond )													\
	{															\
		_PCD = ARG16(); 										\
		change_pc16(_PCD);										\
	}															\
	else														\
	{															\
		_PC += 2;												\
	}

/***************************************************************
 * JR
 ***************************************************************/
#define JR()													\
{																\
	unsigned oldpc = _PCD-1;									\
	INT8 arg = (INT8)ARG(); /* ARG() also increments _PC */ 	\
	_PC += arg; 			/* so don't do _PC += ARG() */      \
	change_pc16(_PCD);											\
	/* speed up busy loop */									\
	if( _PCD == oldpc ) 										\
	{															\
		if( !after_EI ) 										\
			BURNODD( Z80_ICOUNT, 1, cc(Z80_TABLE_op)[0x18] );	\
	}															\
	else														\
	{															\
		UINT8 op = cpu_readop(_PCD);							\
		if( _PCD == oldpc-1 )									\
		{														\
			/* NOP - JR $-1 or EI - JR $-1 */					\
			if ( op == 0x00 || op == 0xfb ) 					\
			{													\
				if( !after_EI ) 								\
				   BURNODD( Z80_ICOUNT-cc(Z80_TABLE_op)[0x00],	\
					   2, cc(Z80_TABLE_op)[0x00]+cc(Z80_TABLE_op)[0x18]); \
			}													\
		}														\
		else													\
		/* LD SP,#xxxx - JR $-3 */								\
		if( _PCD == oldpc-3 && op == 0x31 ) 					\
		{														\
			if( !after_EI ) 									\
			   BURNODD( Z80_ICOUNT-cc(Z80_TABLE_op)[0x31],		\
				   2, cc(Z80_TABLE_op)[0x31]+cc(Z80_TABLE_op)[0x18]); \
		}														\
	}															\
}

/***************************************************************
 * JR_COND
 ***************************************************************/
#define JR_COND(cond,opcode)									\
	if( cond )													\
	{															\
		INT8 arg = (INT8)ARG(); /* ARG() also increments _PC */ \
		_PC += arg; 			/* so don't do _PC += ARG() */  \
		CC(ex,opcode);											\
		change_pc16(_PCD);										\
	}															\
	else _PC++; 												\

/***************************************************************
 * CALL
 ***************************************************************/
#define CALL()													\
	EA = ARG16();												\
	PUSH( PC ); 												\
	_PCD = EA;													\
	change_pc16(_PCD)

/***************************************************************
 * CALL_COND
 ***************************************************************/
#define CALL_COND(cond,opcode)									\
	if( cond )													\
	{															\
		EA = ARG16();											\
		PUSH( PC ); 											\
		_PCD = EA;												\
		CC(ex,opcode);											\
		change_pc16(_PCD);										\
	}															\
	else														\
	{															\
		_PC+=2; 												\
	}

/***************************************************************
 * RET_COND
 ***************************************************************/
#define RET_COND(cond,opcode)									\
	if( cond )													\
	{															\
		POP(PC);												\
		change_pc16(_PCD);										\
		CC(ex,opcode);											\
	}

/***************************************************************
 * RETN
 ***************************************************************/
#define RETN	{												\
    LOG(("Z80 #%d RETN IFF1:%d IFF2:%d\n", 0, _IFF1, _IFF2)); \
	POP(PC);													\
	change_pc16(_PCD);											\
	if( _IFF1 == 0 && _IFF2 == 1 )								\
	{															\
		_IFF1 = 1;												\
		if( Z80.irq_state != CLEAR_LINE ||						\
			Z80.request_irq >= 0 )								\
		{														\
			LOG(("Z80 #%d RETN takes IRQ\n",                    \
                0));                           \
			take_interrupt();									\
		}														\
	}															\
	else _IFF1 = _IFF2; 										\
}

/***************************************************************
 * RETI
 ***************************************************************/
#define RETI	{												\
	int device = Z80.service_irq;								\
	POP(PC);													\
	change_pc16(_PCD);											\
/* according to http://www.msxnet.org/tech/Z80/z80undoc.txt */	\
/*	_IFF1 = _IFF2;	*/											\
	if( device >= 0 )											\
	{															\
		LOG(("Z80 #%d RETI device %d: $%02x\n",                 \
            0, device, Z80.irq[device].irq_param)); \
		Z80.irq[device].interrupt_reti(Z80.irq[device].irq_param); \
	}															\
}

/***************************************************************
 * LD	R,A
 ***************************************************************/
#define LD_R_A {												\
	_R = _A;													\
	_R2 = _A & 0x80;				/* keep bit 7 of R */		\
}

/***************************************************************
 * LD	A,R
 ***************************************************************/
#define LD_A_R {												\
	_A = (_R & 0x7f) | _R2; 									\
	_F = (_F & CF) | SZ[_A] | ( _IFF2 << 2 );					\
}

/***************************************************************
 * LD	I,A
 ***************************************************************/
#define LD_I_A {												\
	_I = _A;													\
}

/***************************************************************
 * LD	A,I
 ***************************************************************/
#define LD_A_I {												\
	_A = _I;													\
	_F = (_F & CF) | SZ[_A] | ( _IFF2 << 2 );					\
}

/***************************************************************
 * RST
 ***************************************************************/
#define RST(addr)												\
	PUSH( PC ); 												\
	_PCD = addr;												\
	change_pc16(_PCD)

/***************************************************************
 * INC	r8
 ***************************************************************/
MAME_INLINE UINT8 INC(UINT8 value)
{
    SMS_Z_CACHE;
	UINT8 res = value + 1;
	_F = (_F & CF) | SZHV_inc[res];
	return (UINT8)res;
}

/***************************************************************
 * DEC	r8
 ***************************************************************/
MAME_INLINE UINT8 DEC(UINT8 value)
{
    SMS_Z_CACHE;
	UINT8 res = value - 1;
	_F = (_F & CF) | SZHV_dec[res];
	return res;
}

/***************************************************************
 * RLCA
 ***************************************************************/
#if Z80_EXACT
#define RLCA													\
	_A = (_A << 1) | (_A >> 7); 								\
	_F = (_F & (SF | ZF | PF)) | (_A & (YF | XF | CF))
#else
#define RLCA													\
	_A = (_A << 1) | (_A >> 7); 								\
	_F = (_F & (SF | ZF | YF | XF | PF)) | (_A & CF)
#endif

/***************************************************************
 * RRCA
 ***************************************************************/
#if Z80_EXACT
#define RRCA													\
	_F = (_F & (SF | ZF | PF)) | (_A & CF); 					\
	_A = (_A >> 1) | (_A << 7); 								\
	_F |= (_A & (YF | XF) )
#else
#define RRCA													\
	_F = (_F & (SF | ZF | YF | XF | PF)) | (_A & CF);			\
	_A = (_A >> 1) | (_A << 7)
#endif

/***************************************************************
 * RLA
 ***************************************************************/
#if Z80_EXACT
#define RLA {													\
	UINT8 res = (_A << 1) | (_F & CF);							\
	UINT8 c = (_A & 0x80) ? CF : 0; 							\
	_F = (_F & (SF | ZF | PF)) | c | (res & (YF | XF)); 		\
	_A = res;													\
}
#else
#define RLA {													\
	UINT8 res = (_A << 1) | (_F & CF);							\
	UINT8 c = (_A & 0x80) ? CF : 0; 							\
	_F = (_F & (SF | ZF | YF | XF | PF)) | c;					\
	_A = res;													\
}
#endif

/***************************************************************
 * RRA
 ***************************************************************/
#if Z80_EXACT
#define RRA {													\
	UINT8 res = (_A >> 1) | (_F << 7);							\
	UINT8 c = (_A & 0x01) ? CF : 0; 							\
	_F = (_F & (SF | ZF | PF)) | c | (res & (YF | XF)); 		\
	_A = res;													\
}
#else
#define RRA {													\
	UINT8 res = (_A >> 1) | (_F << 7);							\
	UINT8 c = (_A & 0x01) ? CF : 0; 							\
	_F = (_F & (SF | ZF | YF | XF | PF)) | c;					\
	_A = res;													\
}
#endif

/***************************************************************
 * RRD
 ***************************************************************/
#define RRD {													\
	UINT8 n = RM(_HL);											\
	WM( _HL, (n >> 4) | (_A << 4) );							\
	_A = (_A & 0xf0) | (n & 0x0f);								\
	_F = (_F & CF) | SZP[_A];									\
}

/***************************************************************
 * RLD
 ***************************************************************/
#define RLD {													\
	UINT8 n = RM(_HL);											\
	WM( _HL, (n << 4) | (_A & 0x0f) );							\
	_A = (_A & 0xf0) | (n >> 4);								\
	_F = (_F & CF) | SZP[_A];									\
}

/***************************************************************
 * ADD	A,n
 ***************************************************************/
#ifdef X86_ASM
#if Z80_EXACT
#define ADD(value)												\
 asm (															\
 " addb %2,%0           \n"                                     \
 " lahf                 \n"                                     \
 " setob %1             \n" /* al = 1 if overflow */            \
 " addb %1,%1           \n"                                     \
 " addb %1,%1           \n" /* shift to P/V bit position */     \
 " andb $0xd1,%%ah      \n" /* sign, zero, half carry, carry */ \
 " orb %%ah,%1          \n"                                     \
 " movb %0,%%ah         \n" /* get result */                    \
 " andb $0x28,%%ah      \n" /* maks flags 5+3 */                \
 " orb %%ah,%1          \n" /* put them into flags */           \
 :"=q" (_A), "=q" (_F)                                          \
 :"q" (value), "1" (_F), "0" (_A)                               \
 )
#else
#define ADD(value)												\
 asm (															\
 " addb %2,%0           \n"                                     \
 " lahf                 \n"                                     \
 " setob %1             \n" /* al = 1 if overflow */            \
 " addb %1,%1           \n"                                     \
 " addb %1,%1           \n" /* shift to P/V bit position */     \
 " andb $0xd1,%%ah      \n" /* sign, zero, half carry, carry */ \
 " orb %%ah,%1          \n"                                     \
 :"=q" (_A), "=q" (_F)                                          \
 :"q" (value), "1" (_F), "0" (_A)                               \
 )
#endif
#else
#if BIG_FLAGS_ARRAY
#define ADD(value)												\
{																\
	UINT32 ah = _AFD & 0xff00;									\
	UINT32 res = (UINT8)((ah >> 8) + value);					\
	_F = SZHVC_add[ah | res];									\
	_A = res;													\
}
#else
#define ADD(value)												\
{																\
	unsigned val = value;										\
	unsigned res = _A + val;									\
	_F = SZ[(UINT8)res] | ((res >> 8) & CF) |					\
		((_A ^ res ^ val) & HF) |								\
		(((val ^ _A ^ 0x80) & (val ^ res) & 0x80) >> 5);		\
	_A = (UINT8)res;											\
}
#endif
#endif

/***************************************************************
 * ADC	A,n
 ***************************************************************/
#ifdef X86_ASM
#if Z80_EXACT
#define ADC(value)												\
 asm (															\
 " shrb $1,%1           \n"                                     \
 " adcb %2,%0           \n"                                     \
 " lahf                 \n"                                     \
 " setob %1             \n" /* al = 1 if overflow */            \
 " addb %1,%1           \n" /* shift to P/V bit position */     \
 " addb %1,%1           \n"                                     \
 " andb $0xd1,%%ah      \n" /* sign, zero, half carry, carry */ \
 " orb %%ah,%1          \n" /* combine with P/V */              \
 " movb %0,%%ah         \n" /* get result */                    \
 " andb $0x28,%%ah      \n" /* maks flags 5+3 */                \
 " orb %%ah,%1          \n" /* put them into flags */           \
 :"=q" (_A), "=q" (_F)                                          \
 :"q" (value), "1" (_F), "0" (_A)                               \
 )
#else
#define ADC(value)												\
 asm (															\
 " shrb $1,%1           \n"                                     \
 " adcb %2,%0           \n"                                     \
 " lahf                 \n"                                     \
 " setob %1             \n" /* al = 1 if overflow */            \
 " addb %1,%1           \n" /* shift to P/V bit position */     \
 " addb %1,%1           \n"                                     \
 " andb $0xd1,%%ah      \n" /* sign, zero, half carry, carry */ \
 " orb %%ah,%1          \n" /* combine with P/V */              \
 :"=q" (_A), "=q" (_F)                                          \
 :"q" (value), "1" (_F), "0" (_A)                               \
 )
#endif
#else
#if BIG_FLAGS_ARRAY
#define ADC(value)												\
{																\
	UINT32 ah = _AFD & 0xff00, c = _AFD & 1;					\
	UINT32 res = (UINT8)((ah >> 8) + value + c);				\
	_F = SZHVC_add[(c << 16) | ah | res];						\
	_A = res;													\
}
#else
#define ADC(value)												\
{																\
	unsigned val = value;										\
	unsigned res = _A + val + (_F & CF);						\
	_F = SZ[res & 0xff] | ((res >> 8) & CF) |					\
		((_A ^ res ^ val) & HF) |								\
		(((val ^ _A ^ 0x80) & (val ^ res) & 0x80) >> 5);		\
	_A = res;													\
}
#endif
#endif

/***************************************************************
 * SUB	n
 ***************************************************************/
#ifdef X86_ASM
#if Z80_EXACT
#define SUB(value)												\
 asm (															\
 " subb %2,%0           \n"                                     \
 " lahf                 \n"                                     \
 " setob %1             \n" /* al = 1 if overflow */            \
 " stc                  \n" /* prepare to set N flag */         \
 " adcb %1,%1           \n" /* shift to P/V bit position */     \
 " addb %1,%1           \n"                                     \
 " andb $0xd1,%%ah      \n" /* sign, zero, half carry, carry */ \
 " orb %%ah,%1          \n" /* combine with P/V */              \
 " movb %0,%%ah         \n" /* get result */                    \
 " andb $0x28,%%ah      \n" /* maks flags 5+3 */                \
 " orb %%ah,%1          \n" /* put them into flags */           \
 :"=q" (_A), "=q" (_F)                                          \
 :"q" (value), "1" (_F), "0" (_A)                               \
 )
#else
#define SUB(value)												\
 asm (															\
 " subb %2,%0           \n"                                     \
 " lahf                 \n"                                     \
 " setob %1             \n" /* al = 1 if overflow */            \
 " stc                  \n" /* prepare to set N flag */         \
 " adcb %1,%1           \n" /* shift to P/V bit position */     \
 " addb %1,%1           \n"                                     \
 " andb $0xd1,%%ah      \n" /* sign, zero, half carry, carry */ \
 " orb %%ah,%1          \n" /* combine with P/V */              \
 :"=q" (_A), "=q" (_F)                                          \
 :"q" (value), "1" (_F), "0" (_A)                               \
 )
#endif
#else
#if BIG_FLAGS_ARRAY
#define SUB(value)												\
{																\
	UINT32 ah = _AFD & 0xff00;									\
	UINT32 res = (UINT8)((ah >> 8) - value);					\
	_F = SZHVC_sub[ah | res];									\
	_A = res;													\
}
#else
#define SUB(value)												\
{																\
	unsigned val = value;										\
	unsigned res = _A - val;									\
	_F = SZ[res & 0xff] | ((res >> 8) & CF) | NF |				\
		((_A ^ res ^ val) & HF) |								\
		(((val ^ _A) & (_A ^ res) & 0x80) >> 5);				\
	_A = res;													\
}
#endif
#endif

/***************************************************************
 * SBC	A,n
 ***************************************************************/
#ifdef X86_ASM
#if Z80_EXACT
#define SBC(value)												\
 asm (															\
 " shrb $1,%1           \n"                                     \
 " sbbb %2,%0           \n"                                     \
 " lahf                 \n"                                     \
 " setob %1             \n" /* al = 1 if overflow */            \
 " stc                  \n" /* prepare to set N flag */         \
 " adcb %1,%1           \n" /* shift to P/V bit position */     \
 " addb %1,%1           \n"                                     \
 " andb $0xd1,%%ah      \n" /* sign, zero, half carry, carry */ \
 " orb %%ah,%1          \n" /* combine with P/V */              \
 " movb %0,%%ah         \n" /* get result */                    \
 " andb $0x28,%%ah      \n" /* maks flags 5+3 */                \
 " orb %%ah,%1          \n" /* put them into flags */           \
 :"=q" (_A), "=q" (_F)                                          \
 :"q" (value), "1" (_F), "0" (_A)                               \
 )
#else
#define SBC(value)												\
 asm (															\
 " shrb $1,%1           \n"                                     \
 " sbbb %2,%0           \n"                                     \
 " lahf                 \n"                                     \
 " setob %1             \n" /* al = 1 if overflow */            \
 " stc                  \n" /* prepare to set N flag */         \
 " adcb %1,%1           \n" /* shift to P/V bit position */     \
 " addb %1,%1           \n"                                     \
 " andb $0xd1,%%ah      \n" /* sign, zero, half carry, carry */ \
 " orb %%ah,%1          \n" /* combine with P/V */              \
 :"=q" (_A), "=q" (_F)                                          \
 :"q" (value), "1" (_F), "0" (_A)                               \
 )
#endif
#else
#if BIG_FLAGS_ARRAY
#define SBC(value)												\
{																\
	UINT32 ah = _AFD & 0xff00, c = _AFD & 1;					\
	UINT32 res = (UINT8)((ah >> 8) - value - c);				\
	_F = SZHVC_sub[(c<<16) | ah | res]; 						\
	_A = res;													\
}
#else
#define SBC(value)												\
{																\
	unsigned val = value;										\
	unsigned res = _A - val - (_F & CF);						\
	_F = SZ[res & 0xff] | ((res >> 8) & CF) | NF |				\
		((_A ^ res ^ val) & HF) |								\
		(((val ^ _A) & (_A ^ res) & 0x80) >> 5);				\
	_A = res;													\
}
#endif
#endif

/***************************************************************
 * NEG
 ***************************************************************/
#define NEG {													\
	UINT8 value = _A;											\
	_A = 0; 													\
	SUB(value); 												\
}

/***************************************************************
 * DAA
 ***************************************************************/
#define DAA {													\
	int idx = _A;												\
	if( _F & CF ) idx |= 0x100; 								\
	if( _F & HF ) idx |= 0x200; 								\
	if( _F & NF ) idx |= 0x400; 								\
	_AF = DAATable[idx];										\
}

/***************************************************************
 * AND	n
 ***************************************************************/
#define AND(value)												\
	_A &= value;												\
	_F = SZP[_A] | HF

/***************************************************************
 * OR	n
 ***************************************************************/
#define OR(value)												\
	_A |= value;												\
	_F = SZP[_A]

/***************************************************************
 * XOR	n
 ***************************************************************/
#define XOR(value)												\
	_A ^= value;												\
	_F = SZP[_A]

/***************************************************************
 * CP	n
 ***************************************************************/
#ifdef X86_ASM
#if Z80_EXACT
#define CP(value)												\
 asm (															\
 " cmpb %2,%0           \n"                                     \
 " lahf                 \n"                                     \
 " setob %1             \n" /* al = 1 if overflow */            \
 " stc                  \n" /* prepare to set N flag */         \
 " adcb %1,%1           \n" /* shift to P/V bit position */     \
 " addb %1,%1           \n"                                     \
 " andb $0xd1,%%ah      \n" /* sign, zero, half carry, carry */ \
 " orb %%ah,%1          \n" /* combine with P/V */              \
 " movb %2,%%ah         \n" /* get result */                    \
 " andb $0x28,%%ah      \n" /* maks flags 5+3 */                \
 " orb %%ah,%1          \n" /* put them into flags */           \
 :"=q" (_A), "=q" (_F)                                          \
 :"q" (value), "1" (_F), "0" (_A)                               \
 )
#else
#define CP(value)												\
 asm (															\
 " cmpb %2,%0           \n"                                     \
 " lahf                 \n"                                     \
 " setob %1             \n" /* al = 1 if overflow */            \
 " stc                  \n" /* prepare to set N flag */         \
 " adcb %1,%1           \n" /* shift to P/V bit position */     \
 " addb %1,%1           \n"                                     \
 " andb $0xd1,%%ah      \n" /* sign, zero, half carry, carry */ \
 " orb %%ah,%1          \n" /* combine with P/V */              \
 :"=q" (_A), "=q" (_F)                                          \
 :"q" (value), "1" (_F), "0" (_A)                               \
 )
#endif
#else
#if BIG_FLAGS_ARRAY
#define CP(value)												\
{																\
	unsigned val = value;										\
	UINT32 ah = _AFD & 0xff00;									\
	UINT32 res = (UINT8)((ah >> 8) - val);						\
	_F = (SZHVC_sub[ah | res] & ~(YF | XF)) |					\
		(val & (YF | XF));										\
}
#else
#define CP(value)												\
{																\
	unsigned val = value;										\
	unsigned res = _A - val;									\
	_F = (SZ[res & 0xff] & (SF | ZF)) |							\
		(val & (YF | XF)) | ((res >> 8) & CF) | NF |			\
		((_A ^ res ^ val) & HF) |								\
		((((val ^ _A) & (_A ^ res)) >> 5) & VF);				\
}
#endif
#endif

/***************************************************************
 * EX	AF,AF'
 ***************************************************************/
#define EX_AF { 												\
	PAIR tmp;													\
	tmp = Z80.AF; Z80.AF = Z80.AF2; Z80.AF2 = tmp;				\
}

/***************************************************************
 * EX	DE,HL
 ***************************************************************/
#define EX_DE_HL {												\
	PAIR tmp;													\
	tmp = Z80.DE; Z80.DE = Z80.HL; Z80.HL = tmp;				\
}

/***************************************************************
 * EXX
 ***************************************************************/
#define EXX {													\
	PAIR tmp;													\
	tmp = Z80.BC; Z80.BC = Z80.BC2; Z80.BC2 = tmp;				\
	tmp = Z80.DE; Z80.DE = Z80.DE2; Z80.DE2 = tmp;				\
	tmp = Z80.HL; Z80.HL = Z80.HL2; Z80.HL2 = tmp;				\
}

/***************************************************************
 * EX	(SP),r16
 ***************************************************************/
#define EXSP(DR)												\
{																\
	PAIR tmp = { { 0, 0, 0, 0 } };								\
	RM16( _SPD, &tmp ); 										\
	WM16( _SPD, &Z80.DR );										\
	Z80.DR = tmp;												\
}


/***************************************************************
 * ADD16
 ***************************************************************/
#ifdef	X86_ASM
#if Z80_EXACT
#define ADD16(DR,SR)											\
 asm (															\
 " andb $0xc4,%1        \n"                                     \
 " addb %%dl,%%cl       \n"                                     \
 " adcb %%dh,%%ch       \n"                                     \
 " lahf                 \n"                                     \
 " andb $0x11,%%ah      \n"                                     \
 " orb %%ah,%1          \n"                                     \
 " movb %%ch,%%ah       \n" /* get result MSB */                \
 " andb $0x28,%%ah      \n" /* maks flags 5+3 */                \
 " orb %%ah,%1          \n" /* put them into flags */           \
 :"=c" (Z80.DR.d), "=q" (_F)                                    \
 :"0" (Z80.DR.d), "1" (_F), "d" (Z80.SR.d)                      \
 )
#else
#define ADD16(DR,SR)											\
 asm (															\
 " andb $0xc4,%1        \n"                                     \
 " addb %%dl,%%cl       \n"                                     \
 " adcb %%dh,%%ch       \n"                                     \
 " lahf                 \n"                                     \
 " andb $0x11,%%ah      \n"                                     \
 " orb %%ah,%1          \n"                                     \
 :"=c" (Z80.DR.d), "=q" (_F)                                    \
 :"0" (Z80.DR.d), "1" (_F), "d" (Z80.SR.d)                      \
 )
#endif
#else
#define ADD16(DR,SR)											\
{																\
	UINT32 res = Z80.DR.d + Z80.SR.d;							\
	_F = (_F & (SF | ZF | VF)) |								\
		(((Z80.DR.d ^ res ^ Z80.SR.d) >> 8) & HF) | 			\
		((res >> 16) & CF) | ((res >> 8) & (YF | XF)); 			\
	Z80.DR.w.l = (UINT16)res;									\
}
#endif

/***************************************************************
 * ADC	r16,r16
 ***************************************************************/
#ifdef	X86_ASM
#if Z80_EXACT
#define ADC16(Reg)												\
 asm (															\
 " shrb $1,%1           \n"                                     \
 " adcb %%dl,%%cl       \n"                                     \
 " lahf                 \n"                                     \
 " movb %%ah,%%dl       \n"                                     \
 " adcb %%dh,%%ch       \n"                                     \
 " lahf                 \n"                                     \
 " setob %1             \n"                                     \
 " orb $0xbf,%%dl       \n" /* set all but zero */              \
 " addb %1,%1           \n"                                     \
 " andb $0xd1,%%ah      \n" /* sign,zero,half carry and carry */\
 " addb %1,%1           \n"                                     \
 " orb %%ah,%1          \n" /* overflow into P/V */             \
 " andb %%dl,%1         \n" /* mask zero */                     \
 " movb %%ch,%%ah       \n" /* get result MSB */                \
 " andb $0x28,%%ah      \n" /* maks flags 5+3 */                \
 " orb %%ah,%1          \n" /* put them into flags */           \
 :"=c" (_HLD), "=q" (_F)                                        \
 :"0" (_HLD), "1" (_F), "d" (Z80.Reg.d)                         \
 )
#else
#define ADC16(Reg)												\
 asm (															\
 " shrb $1,%1           \n"                                     \
 " adcb %%dl,%%cl       \n"                                     \
 " lahf                 \n"                                     \
 " movb %%ah,%%dl       \n"                                     \
 " adcb %%dh,%%ch       \n"                                     \
 " lahf                 \n"                                     \
 " setob %1             \n"                                     \
 " orb $0xbf,%%dl       \n" /* set all but zero */              \
 " addb %1,%1           \n"                                     \
 " andb $0xd1,%%ah      \n" /* sign,zero,half carry and carry */\
 " addb %1,%1           \n"                                     \
 " orb %%ah,%1          \n" /* overflow into P/V */             \
 " andb %%dl,%1         \n" /* mask zero */                     \
 :"=c" (_HLD), "=q" (_F)                                        \
 :"0" (_HLD), "1" (_F), "d" (Z80.Reg.d)                         \
 )
#endif
#else
#define ADC16(Reg)												\
{																\
	UINT32 res = _HLD + Z80.Reg.d + (_F & CF);					\
	_F = (((_HLD ^ res ^ Z80.Reg.d) >> 8) & HF) |				\
		((res >> 16) & CF) |									\
		((res >> 8) & (SF | YF | XF)) |							\
		((res & 0xffff) ? 0 : ZF) | 							\
		(((Z80.Reg.d ^ _HLD ^ 0x8000) & (Z80.Reg.d ^ res) & 0x8000) >> 13); \
	_HL = (UINT16)res;											\
}
#endif

/***************************************************************
 * SBC	r16,r16
 ***************************************************************/
#ifdef	X86_ASM
#if Z80_EXACT
#define SBC16(Reg)												\
asm (															\
 " shrb $1,%1           \n"                                     \
 " sbbb %%dl,%%cl       \n"                                     \
 " lahf                 \n"                                     \
 " movb %%ah,%%dl       \n"                                     \
 " sbbb %%dh,%%ch       \n"                                     \
 " lahf                 \n"                                     \
 " setob %1             \n"                                     \
 " orb $0xbf,%%dl       \n" /* set all but zero */              \
 " stc                  \n"                                     \
 " adcb %1,%1           \n"                                     \
 " andb $0xd1,%%ah      \n" /* sign,zero,half carry and carry */\
 " addb %1,%1           \n"                                     \
 " orb %%ah,%1          \n" /* overflow into P/V */             \
 " andb %%dl,%1         \n" /* mask zero */                     \
 " movb %%ch,%%ah       \n" /* get result MSB */                \
 " andb $0x28,%%ah      \n" /* maks flags 5+3 */                \
 " orb %%ah,%1          \n" /* put them into flags */           \
 :"=c" (_HLD), "=q" (_F)                                        \
 :"0" (_HLD), "1" (_F), "d" (Z80.Reg.d)                         \
 )
#else
#define SBC16(Reg)												\
asm (															\
 " shrb $1,%1           \n"                                     \
 " sbbb %%dl,%%cl       \n"                                     \
 " lahf                 \n"                                     \
 " movb %%ah,%%dl       \n"                                     \
 " sbbb %%dh,%%ch       \n"                                     \
 " lahf                 \n"                                     \
 " setob %1             \n"                                     \
 " orb $0xbf,%%dl       \n" /* set all but zero */              \
 " stc                  \n"                                     \
 " adcb %1,%1           \n"                                     \
 " andb $0xd1,%%ah      \n" /* sign,zero,half carry and carry */\
 " addb %1,%1           \n"                                     \
 " orb %%ah,%1          \n" /* overflow into P/V */             \
 " andb %%dl,%1         \n" /* mask zero */                     \
 :"=c" (_HLD), "=q" (_F)                                        \
 :"0" (_HLD), "1" (_F), "d" (Z80.Reg.d)                         \
 )
#endif
#else
#define SBC16(Reg)												\
{																\
	UINT32 res = _HLD - Z80.Reg.d - (_F & CF);					\
	_F = (((_HLD ^ res ^ Z80.Reg.d) >> 8) & HF) | NF |			\
		((res >> 16) & CF) |									\
		((res >> 8) & (SF | YF | XF)) | 						\
		((res & 0xffff) ? 0 : ZF) | 							\
		(((Z80.Reg.d ^ _HLD) & (_HLD ^ res) &0x8000) >> 13);	\
	_HL = (UINT16)res;											\
}
#endif

/***************************************************************
 * RLC	r8
 ***************************************************************/
MAME_INLINE UINT8 RLC(UINT8 value)
{
    SMS_Z_CACHE;
	unsigned res = value;
	unsigned c = (res & 0x80) ? CF : 0;
	res = ((res << 1) | (res >> 7)) & 0xff;
	_F = SZP[res] | c;
	return res;
}

/***************************************************************
 * RRC	r8
 ***************************************************************/
MAME_INLINE UINT8 RRC(UINT8 value)
{
    SMS_Z_CACHE;
	unsigned res = value;
	unsigned c = (res & 0x01) ? CF : 0;
	res = ((res >> 1) | (res << 7)) & 0xff;
	_F = SZP[res] | c;
	return res;
}

/***************************************************************
 * RL	r8
 ***************************************************************/
MAME_INLINE UINT8 RL(UINT8 value)
{
    SMS_Z_CACHE;
	unsigned res = value;
	unsigned c = (res & 0x80) ? CF : 0;
	res = ((res << 1) | (_F & CF)) & 0xff;
	_F = SZP[res] | c;
	return res;
}

/***************************************************************
 * RR	r8
 ***************************************************************/
MAME_INLINE UINT8 RR(UINT8 value)
{
    SMS_Z_CACHE;
	unsigned res = value;
	unsigned c = (res & 0x01) ? CF : 0;
	res = ((res >> 1) | (_F << 7)) & 0xff;
	_F = SZP[res] | c;
	return res;
}

/***************************************************************
 * SLA	r8
 ***************************************************************/
MAME_INLINE UINT8 SLA(UINT8 value)
{
    SMS_Z_CACHE;
	unsigned res = value;
	unsigned c = (res & 0x80) ? CF : 0;
	res = (res << 1) & 0xff;
	_F = SZP[res] | c;
	return res;
}

/***************************************************************
 * SRA	r8
 ***************************************************************/
MAME_INLINE UINT8 SRA(UINT8 value)
{
    SMS_Z_CACHE;
	unsigned res = value;
	unsigned c = (res & 0x01) ? CF : 0;
	res = ((res >> 1) | (res & 0x80)) & 0xff;
	_F = SZP[res] | c;
	return res;
}

/***************************************************************
 * SLL	r8
 ***************************************************************/
MAME_INLINE UINT8 SLL(UINT8 value)
{
    SMS_Z_CACHE;
	unsigned res = value;
	unsigned c = (res & 0x80) ? CF : 0;
	res = ((res << 1) | 0x01) & 0xff;
	_F = SZP[res] | c;
	return res;
}

/***************************************************************
 * SRL	r8
 ***************************************************************/
MAME_INLINE UINT8 SRL(UINT8 value)
{
    SMS_Z_CACHE;
	unsigned res = value;
	unsigned c = (res & 0x01) ? CF : 0;
	res = (res >> 1) & 0xff;
	_F = SZP[res] | c;
	return res;
}

/***************************************************************
 * BIT	bit,r8
 ***************************************************************/
#undef BIT
#define BIT(bit,reg)											\
	_F = (_F & CF) | HF | SZ_BIT[reg & (1<<bit)]

/***************************************************************
 * BIT	bit,(IX/Y+o)
 ***************************************************************/
#if Z80_EXACT
#define BIT_XY(bit,reg) 										\
	_F = (_F & CF) | HF | (SZ_BIT[reg & (1<<bit)] & ~(YF|XF)) | ((EA>>8) & (YF|XF))
#else
#define BIT_XY	BIT
#endif

/***************************************************************
 * RES	bit,r8
 ***************************************************************/
MAME_INLINE UINT8 RES(UINT8 bit, UINT8 value)
{
	return value & ~(1<<bit);
}

/***************************************************************
 * SET	bit,r8
 ***************************************************************/
MAME_INLINE UINT8 SET(UINT8 bit, UINT8 value)
{
	return value | (1<<bit);
}

/***************************************************************
 * LDI
 ***************************************************************/
#if Z80_EXACT
#define LDI {													\
	UINT8 io = RM(_HL); 										\
	WM( _DE, io );												\
	_F &= SF | ZF | CF; 										\
	if( (_A + io) & 0x02 ) _F |= YF; /* bit 1 -> flag 5 */		\
	if( (_A + io) & 0x08 ) _F |= XF; /* bit 3 -> flag 3 */		\
	_HL++; _DE++; _BC--;										\
	if( _BC ) _F |= VF; 										\
}
#else
#define LDI {													\
	WM( _DE, RM(_HL) ); 										\
	_F &= SF | ZF | YF | XF | CF;								\
	_HL++; _DE++; _BC--;										\
	if( _BC ) _F |= VF; 										\
}
#endif

/***************************************************************
 * CPI
 ***************************************************************/
#if Z80_EXACT
#define CPI {													\
	UINT8 val = RM(_HL);										\
	UINT8 res = _A - val;										\
	_HL++; _BC--;												\
	_F = (_F & CF) | (SZ[res] & ~(YF|XF)) | ((_A ^ val ^ res) & HF) | NF;  \
	if( _F & HF ) res -= 1; 									\
	if( res & 0x02 ) _F |= YF; /* bit 1 -> flag 5 */			\
	if( res & 0x08 ) _F |= XF; /* bit 3 -> flag 3 */			\
	if( _BC ) _F |= VF; 										\
}
#else
#define CPI {													\
	UINT8 val = RM(_HL);										\
	UINT8 res = _A - val;										\
	_HL++; _BC--;												\
	_F = (_F & CF) | SZ[res] | ((_A ^ val ^ res) & HF) | NF;	\
	if( _BC ) _F |= VF; 										\
}
#endif

/***************************************************************
 * INI
 ***************************************************************/
#if Z80_EXACT
#define INI {													\
	UINT8 io = IN(_BC); 										\
	_B--;														\
	WM( _HL, io );												\
	_HL++;														\
	_F = SZ[_B];												\
	if( io & SF ) _F |= NF; 									\
	if( ( ( (_C + 1) & 0xff) + io) & 0x100 ) _F |= HF | CF; 	\
	if( (irep_tmp1[_C & 3][io & 3] ^							\
		 breg_tmp2[_B] ^										\
		 (_C >> 2) ^											\
		 (io >> 2)) & 1 )										\
		_F |= PF;												\
}
#else
#define INI {													\
	_B--;														\
	WM( _HL, IN(_BC) ); 										\
	_HL++;														\
	_F = (_B) ? NF : NF | ZF;									\
}
#endif

/***************************************************************
 * OUTI
 ***************************************************************/
#if Z80_EXACT
#define OUTI {													\
	UINT8 io = RM(_HL); 										\
	_B--;														\
	OUT( _BC, io ); 											\
	_HL++;														\
	_F = SZ[_B];												\
	if( io & SF ) _F |= NF; 									\
	if( ( ( (_C + 1) & 0xff) + io) & 0x100 ) _F |= HF | CF; 	\
	if( (irep_tmp1[_C & 3][io & 3] ^							\
		 breg_tmp2[_B] ^										\
		 (_C >> 2) ^											\
		 (io >> 2)) & 1 )										\
		_F |= PF;												\
}
#else
#define OUTI {													\
	_B--;														\
	OUT( _BC, RM(_HL) );										\
	_HL++;														\
	_F = (_B) ? NF : NF | ZF;									\
}
#endif

/***************************************************************
 * LDD
 ***************************************************************/
#if Z80_EXACT
#define LDD {													\
	UINT8 io = RM(_HL); 										\
	WM( _DE, io );												\
	_F &= SF | ZF | CF; 										\
	if( (_A + io) & 0x02 ) _F |= YF; /* bit 1 -> flag 5 */		\
	if( (_A + io) & 0x08 ) _F |= XF; /* bit 3 -> flag 3 */		\
	_HL--; _DE--; _BC--;										\
	if( _BC ) _F |= VF; 										\
}
#else
#define LDD {													\
	WM( _DE, RM(_HL) ); 										\
	_F &= SF | ZF | YF | XF | CF;								\
	_HL--; _DE--; _BC--;										\
	if( _BC ) _F |= VF; 										\
}
#endif

/***************************************************************
 * CPD
 ***************************************************************/
#if Z80_EXACT
#define CPD {													\
	UINT8 val = RM(_HL);										\
	UINT8 res = _A - val;										\
	_HL--; _BC--;												\
	_F = (_F & CF) | (SZ[res] & ~(YF|XF)) | ((_A ^ val ^ res) & HF) | NF;  \
	if( _F & HF ) res -= 1; 									\
	if( res & 0x02 ) _F |= YF; /* bit 1 -> flag 5 */			\
	if( res & 0x08 ) _F |= XF; /* bit 3 -> flag 3 */			\
	if( _BC ) _F |= VF; 										\
}
#else
#define CPD {													\
	UINT8 val = RM(_HL);										\
	UINT8 res = _A - val;										\
	_HL--; _BC--;												\
	_F = (_F & CF) | SZ[res] | ((_A ^ val ^ res) & HF) | NF;	\
	if( _BC ) _F |= VF; 										\
}
#endif

/***************************************************************
 * IND
 ***************************************************************/
#if Z80_EXACT
#define IND {													\
	UINT8 io = IN(_BC); 										\
	_B--;														\
	WM( _HL, io );												\
	_HL--;														\
	_F = SZ[_B];												\
	if( io & SF ) _F |= NF; 									\
	if( ( ( (_C - 1) & 0xff) + io) & 0x100 ) _F |= HF | CF; 	\
	if( (drep_tmp1[_C & 3][io & 3] ^							\
		 breg_tmp2[_B] ^										\
		 (_C >> 2) ^											\
		 (io >> 2)) & 1 )										\
		_F |= PF;												\
}
#else
#define IND {													\
	_B--;														\
	WM( _HL, IN(_BC) ); 										\
	_HL--;														\
	_F = (_B) ? NF : NF | ZF;									\
}
#endif

/***************************************************************
 * OUTD
 ***************************************************************/
#if Z80_EXACT
#define OUTD {													\
	UINT8 io = RM(_HL); 										\
	_B--;														\
	OUT( _BC, io ); 											\
	_HL--;														\
	_F = SZ[_B];												\
	if( io & SF ) _F |= NF; 									\
	if( ( ( (_C - 1) & 0xff) + io) & 0x100 ) _F |= HF | CF; 	\
	if( (drep_tmp1[_C & 3][io & 3] ^							\
		 breg_tmp2[_B] ^										\
		 (_C >> 2) ^											\
		 (io >> 2)) & 1 )										\
		_F |= PF;												\
}
#else
#define OUTD {													\
	_B--;														\
	OUT( _BC, RM(_HL) );										\
	_HL--;														\
	_F = (_B) ? NF : NF | ZF;									\
}
#endif

/***************************************************************
 * LDIR
 ***************************************************************/
#define LDIR													\
	LDI;														\
	if( _BC )													\
	{															\
		_PC -= 2;												\
		CC(ex,0xb0);											\
	}

/***************************************************************
 * CPIR
 ***************************************************************/
#define CPIR													\
	CPI;														\
	if( _BC && !(_F & ZF) ) 									\
	{															\
		_PC -= 2;												\
		CC(ex,0xb1);											\
	}

/***************************************************************
 * INIR
 ***************************************************************/
#define INIR													\
	INI;														\
	if( _B )													\
	{															\
		_PC -= 2;												\
		CC(ex,0xb2);											\
	}

/***************************************************************
 * OTIR
 ***************************************************************/
#define OTIR													\
	OUTI;														\
	if( _B )													\
	{															\
		_PC -= 2;												\
		CC(ex,0xb3);											\
	}

/***************************************************************
 * LDDR
 ***************************************************************/
#define LDDR													\
	LDD;														\
	if( _BC )													\
	{															\
		_PC -= 2;												\
		CC(ex,0xb8);											\
	}

/***************************************************************
 * CPDR
 ***************************************************************/
#define CPDR													\
	CPD;														\
	if( _BC && !(_F & ZF) ) 									\
	{															\
		_PC -= 2;												\
		CC(ex,0xb9);											\
	}

/***************************************************************
 * INDR
 ***************************************************************/
#define INDR													\
	IND;														\
	if( _B )													\
	{															\
		_PC -= 2;												\
		CC(ex,0xba);											\
	}

/***************************************************************
 * OTDR
 ***************************************************************/
#define OTDR													\
	OUTD;														\
	if( _B )													\
	{															\
		_PC -= 2;												\
		CC(ex,0xbb);											\
	}

/***************************************************************
 * EI
 ***************************************************************/
#define EI {													\
	/* If interrupts were disabled, execute one more			\
	 * instruction and check the IRQ line.						\
	 * If not, simply set interrupt flip-flop 2 				\
	 */ 														\
	if( _IFF1 == 0 )											\
	{															\
		_IFF1 = _IFF2 = 1;										\
		_PPC = _PCD;											\
		_R++;													\
		while( cpu_readop(_PCD) == 0xfb ) /* more EIs? */		\
		{														\
			LOG(("Z80 #%d multiple EI opcodes at %04X\n",       \
                0, _PC));                      \
			CC(op,0xfb);										\
			_PPC =_PCD; 										\
			_PC++;												\
			_R++;												\
		}														\
		if( Z80.irq_state != CLEAR_LINE ||						\
			Z80.request_irq >= 0 )								\
		{														\
			after_EI = 1;	/* avoid cycle skip hacks */		\
			EXEC(op,ROP()); 									\
			after_EI = 0;										\
            LOG(("Z80 #%d EI takes irq\n", 0)); \
			take_interrupt();									\
		} else EXEC(op,ROP());									\
	} else _IFF2 = 1;											\
}

/**********************************************************
 * opcodes with CB prefix
 * rotate, shift and bit operations
 **********************************************************/
OP(cb,00) { SMS_Z_CACHE; _B = RLC(_B);											} /* RLC  B 		  */
OP(cb,01) { SMS_Z_CACHE; _C = RLC(_C);											} /* RLC  C 		  */
OP(cb,02) { SMS_Z_CACHE; _D = RLC(_D);											} /* RLC  D 		  */
OP(cb,03) { SMS_Z_CACHE; _E = RLC(_E);											} /* RLC  E 		  */
OP(cb,04) { SMS_Z_CACHE; _H = RLC(_H);											} /* RLC  H 		  */
OP(cb,05) { SMS_Z_CACHE; _L = RLC(_L);											} /* RLC  L 		  */
OP(cb,06) { SMS_Z_CACHE; WM( _HL, RLC(RM(_HL)) );								} /* RLC  (HL)		  */
OP(cb,07) { SMS_Z_CACHE; _A = RLC(_A);											} /* RLC  A 		  */

OP(cb,08) { SMS_Z_CACHE; _B = RRC(_B);											} /* RRC  B 		  */
OP(cb,09) { SMS_Z_CACHE; _C = RRC(_C);											} /* RRC  C 		  */
OP(cb,0a) { SMS_Z_CACHE; _D = RRC(_D);											} /* RRC  D 		  */
OP(cb,0b) { SMS_Z_CACHE; _E = RRC(_E);											} /* RRC  E 		  */
OP(cb,0c) { SMS_Z_CACHE; _H = RRC(_H);											} /* RRC  H 		  */
OP(cb,0d) { SMS_Z_CACHE; _L = RRC(_L);											} /* RRC  L 		  */
OP(cb,0e) { SMS_Z_CACHE; WM( _HL, RRC(RM(_HL)) );								} /* RRC  (HL)		  */
OP(cb,0f) { SMS_Z_CACHE; _A = RRC(_A);											} /* RRC  A 		  */

OP(cb,10) { SMS_Z_CACHE; _B = RL(_B);											} /* RL   B 		  */
OP(cb,11) { SMS_Z_CACHE; _C = RL(_C);											} /* RL   C 		  */
OP(cb,12) { SMS_Z_CACHE; _D = RL(_D);											} /* RL   D 		  */
OP(cb,13) { SMS_Z_CACHE; _E = RL(_E);											} /* RL   E 		  */
OP(cb,14) { SMS_Z_CACHE; _H = RL(_H);											} /* RL   H 		  */
OP(cb,15) { SMS_Z_CACHE; _L = RL(_L);											} /* RL   L 		  */
OP(cb,16) { SMS_Z_CACHE; WM( _HL, RL(RM(_HL)) ); 								} /* RL   (HL)		  */
OP(cb,17) { SMS_Z_CACHE; _A = RL(_A);											} /* RL   A 		  */

OP(cb,18) { SMS_Z_CACHE; _B = RR(_B);											} /* RR   B 		  */
OP(cb,19) { SMS_Z_CACHE; _C = RR(_C);											} /* RR   C 		  */
OP(cb,1a) { SMS_Z_CACHE; _D = RR(_D);											} /* RR   D 		  */
OP(cb,1b) { SMS_Z_CACHE; _E = RR(_E);											} /* RR   E 		  */
OP(cb,1c) { SMS_Z_CACHE; _H = RR(_H);											} /* RR   H 		  */
OP(cb,1d) { SMS_Z_CACHE; _L = RR(_L);											} /* RR   L 		  */
OP(cb,1e) { SMS_Z_CACHE; WM( _HL, RR(RM(_HL)) ); 								} /* RR   (HL)		  */
OP(cb,1f) { SMS_Z_CACHE; _A = RR(_A);											} /* RR   A 		  */

OP(cb,20) { SMS_Z_CACHE; _B = SLA(_B);											} /* SLA  B 		  */
OP(cb,21) { SMS_Z_CACHE; _C = SLA(_C);											} /* SLA  C 		  */
OP(cb,22) { SMS_Z_CACHE; _D = SLA(_D);											} /* SLA  D 		  */
OP(cb,23) { SMS_Z_CACHE; _E = SLA(_E);											} /* SLA  E 		  */
OP(cb,24) { SMS_Z_CACHE; _H = SLA(_H);											} /* SLA  H 		  */
OP(cb,25) { SMS_Z_CACHE; _L = SLA(_L);											} /* SLA  L 		  */
OP(cb,26) { SMS_Z_CACHE; WM( _HL, SLA(RM(_HL)) );								} /* SLA  (HL)		  */
OP(cb,27) { SMS_Z_CACHE; _A = SLA(_A);											} /* SLA  A 		  */

OP(cb,28) { SMS_Z_CACHE; _B = SRA(_B);											} /* SRA  B 		  */
OP(cb,29) { SMS_Z_CACHE; _C = SRA(_C);											} /* SRA  C 		  */
OP(cb,2a) { SMS_Z_CACHE; _D = SRA(_D);											} /* SRA  D 		  */
OP(cb,2b) { SMS_Z_CACHE; _E = SRA(_E);											} /* SRA  E 		  */
OP(cb,2c) { SMS_Z_CACHE; _H = SRA(_H);											} /* SRA  H 		  */
OP(cb,2d) { SMS_Z_CACHE; _L = SRA(_L);											} /* SRA  L 		  */
OP(cb,2e) { SMS_Z_CACHE; WM( _HL, SRA(RM(_HL)) );								} /* SRA  (HL)		  */
OP(cb,2f) { SMS_Z_CACHE; _A = SRA(_A);											} /* SRA  A 		  */

OP(cb,30) { SMS_Z_CACHE; _B = SLL(_B);											} /* SLL  B 		  */
OP(cb,31) { SMS_Z_CACHE; _C = SLL(_C);											} /* SLL  C 		  */
OP(cb,32) { SMS_Z_CACHE; _D = SLL(_D);											} /* SLL  D 		  */
OP(cb,33) { SMS_Z_CACHE; _E = SLL(_E);											} /* SLL  E 		  */
OP(cb,34) { SMS_Z_CACHE; _H = SLL(_H);											} /* SLL  H 		  */
OP(cb,35) { SMS_Z_CACHE; _L = SLL(_L);											} /* SLL  L 		  */
OP(cb,36) { SMS_Z_CACHE; WM( _HL, SLL(RM(_HL)) );								} /* SLL  (HL)		  */
OP(cb,37) { SMS_Z_CACHE; _A = SLL(_A);											} /* SLL  A 		  */

OP(cb,38) { SMS_Z_CACHE; _B = SRL(_B);											} /* SRL  B 		  */
OP(cb,39) { SMS_Z_CACHE; _C = SRL(_C);											} /* SRL  C 		  */
OP(cb,3a) { SMS_Z_CACHE; _D = SRL(_D);											} /* SRL  D 		  */
OP(cb,3b) { SMS_Z_CACHE; _E = SRL(_E);											} /* SRL  E 		  */
OP(cb,3c) { SMS_Z_CACHE; _H = SRL(_H);											} /* SRL  H 		  */
OP(cb,3d) { SMS_Z_CACHE; _L = SRL(_L);											} /* SRL  L 		  */
OP(cb,3e) { SMS_Z_CACHE; WM( _HL, SRL(RM(_HL)) );								} /* SRL  (HL)		  */
OP(cb,3f) { SMS_Z_CACHE; _A = SRL(_A);											} /* SRL  A 		  */

OP(cb,40) { SMS_Z_CACHE; BIT(0,_B);												} /* BIT  0,B		  */
OP(cb,41) { SMS_Z_CACHE; BIT(0,_C);												} /* BIT  0,C		  */
OP(cb,42) { SMS_Z_CACHE; BIT(0,_D);												} /* BIT  0,D		  */
OP(cb,43) { SMS_Z_CACHE; BIT(0,_E);												} /* BIT  0,E		  */
OP(cb,44) { SMS_Z_CACHE; BIT(0,_H);												} /* BIT  0,H		  */
OP(cb,45) { SMS_Z_CACHE; BIT(0,_L);												} /* BIT  0,L		  */
OP(cb,46) { SMS_Z_CACHE; BIT(0,RM(_HL)); 										} /* BIT  0,(HL)	  */
OP(cb,47) { SMS_Z_CACHE; BIT(0,_A);												} /* BIT  0,A		  */

OP(cb,48) { SMS_Z_CACHE; BIT(1,_B);												} /* BIT  1,B		  */
OP(cb,49) { SMS_Z_CACHE; BIT(1,_C);												} /* BIT  1,C		  */
OP(cb,4a) { SMS_Z_CACHE; BIT(1,_D);												} /* BIT  1,D		  */
OP(cb,4b) { SMS_Z_CACHE; BIT(1,_E);												} /* BIT  1,E		  */
OP(cb,4c) { SMS_Z_CACHE; BIT(1,_H);												} /* BIT  1,H		  */
OP(cb,4d) { SMS_Z_CACHE; BIT(1,_L);												} /* BIT  1,L		  */
OP(cb,4e) { SMS_Z_CACHE; BIT(1,RM(_HL)); 										} /* BIT  1,(HL)	  */
OP(cb,4f) { SMS_Z_CACHE; BIT(1,_A);												} /* BIT  1,A		  */

OP(cb,50) { SMS_Z_CACHE; BIT(2,_B);												} /* BIT  2,B		  */
OP(cb,51) { SMS_Z_CACHE; BIT(2,_C);												} /* BIT  2,C		  */
OP(cb,52) { SMS_Z_CACHE; BIT(2,_D);												} /* BIT  2,D		  */
OP(cb,53) { SMS_Z_CACHE; BIT(2,_E);												} /* BIT  2,E		  */
OP(cb,54) { SMS_Z_CACHE; BIT(2,_H);												} /* BIT  2,H		  */
OP(cb,55) { SMS_Z_CACHE; BIT(2,_L);												} /* BIT  2,L		  */
OP(cb,56) { SMS_Z_CACHE; BIT(2,RM(_HL)); 										} /* BIT  2,(HL)	  */
OP(cb,57) { SMS_Z_CACHE; BIT(2,_A);												} /* BIT  2,A		  */

OP(cb,58) { SMS_Z_CACHE; BIT(3,_B);												} /* BIT  3,B		  */
OP(cb,59) { SMS_Z_CACHE; BIT(3,_C);												} /* BIT  3,C		  */
OP(cb,5a) { SMS_Z_CACHE; BIT(3,_D);												} /* BIT  3,D		  */
OP(cb,5b) { SMS_Z_CACHE; BIT(3,_E);												} /* BIT  3,E		  */
OP(cb,5c) { SMS_Z_CACHE; BIT(3,_H);												} /* BIT  3,H		  */
OP(cb,5d) { SMS_Z_CACHE; BIT(3,_L);												} /* BIT  3,L		  */
OP(cb,5e) { SMS_Z_CACHE; BIT(3,RM(_HL)); 										} /* BIT  3,(HL)	  */
OP(cb,5f) { SMS_Z_CACHE; BIT(3,_A);												} /* BIT  3,A		  */

OP(cb,60) { SMS_Z_CACHE; BIT(4,_B);												} /* BIT  4,B		  */
OP(cb,61) { SMS_Z_CACHE; BIT(4,_C);												} /* BIT  4,C		  */
OP(cb,62) { SMS_Z_CACHE; BIT(4,_D);												} /* BIT  4,D		  */
OP(cb,63) { SMS_Z_CACHE; BIT(4,_E);												} /* BIT  4,E		  */
OP(cb,64) { SMS_Z_CACHE; BIT(4,_H);												} /* BIT  4,H		  */
OP(cb,65) { SMS_Z_CACHE; BIT(4,_L);												} /* BIT  4,L		  */
OP(cb,66) { SMS_Z_CACHE; BIT(4,RM(_HL)); 										} /* BIT  4,(HL)	  */
OP(cb,67) { SMS_Z_CACHE; BIT(4,_A);												} /* BIT  4,A		  */

OP(cb,68) { SMS_Z_CACHE; BIT(5,_B);												} /* BIT  5,B		  */
OP(cb,69) { SMS_Z_CACHE; BIT(5,_C);												} /* BIT  5,C		  */
OP(cb,6a) { SMS_Z_CACHE; BIT(5,_D);												} /* BIT  5,D		  */
OP(cb,6b) { SMS_Z_CACHE; BIT(5,_E);												} /* BIT  5,E		  */
OP(cb,6c) { SMS_Z_CACHE; BIT(5,_H);												} /* BIT  5,H		  */
OP(cb,6d) { SMS_Z_CACHE; BIT(5,_L);												} /* BIT  5,L		  */
OP(cb,6e) { SMS_Z_CACHE; BIT(5,RM(_HL)); 										} /* BIT  5,(HL)	  */
OP(cb,6f) { SMS_Z_CACHE; BIT(5,_A);												} /* BIT  5,A		  */

OP(cb,70) { SMS_Z_CACHE; BIT(6,_B);												} /* BIT  6,B		  */
OP(cb,71) { SMS_Z_CACHE; BIT(6,_C);												} /* BIT  6,C		  */
OP(cb,72) { SMS_Z_CACHE; BIT(6,_D);												} /* BIT  6,D		  */
OP(cb,73) { SMS_Z_CACHE; BIT(6,_E);												} /* BIT  6,E		  */
OP(cb,74) { SMS_Z_CACHE; BIT(6,_H);												} /* BIT  6,H		  */
OP(cb,75) { SMS_Z_CACHE; BIT(6,_L);												} /* BIT  6,L		  */
OP(cb,76) { SMS_Z_CACHE; BIT(6,RM(_HL)); 										} /* BIT  6,(HL)	  */
OP(cb,77) { SMS_Z_CACHE; BIT(6,_A);												} /* BIT  6,A		  */

OP(cb,78) { SMS_Z_CACHE; BIT(7,_B);												} /* BIT  7,B		  */
OP(cb,79) { SMS_Z_CACHE; BIT(7,_C);												} /* BIT  7,C		  */
OP(cb,7a) { SMS_Z_CACHE; BIT(7,_D);												} /* BIT  7,D		  */
OP(cb,7b) { SMS_Z_CACHE; BIT(7,_E);												} /* BIT  7,E		  */
OP(cb,7c) { SMS_Z_CACHE; BIT(7,_H);												} /* BIT  7,H		  */
OP(cb,7d) { SMS_Z_CACHE; BIT(7,_L);												} /* BIT  7,L		  */
OP(cb,7e) { SMS_Z_CACHE; BIT(7,RM(_HL)); 										} /* BIT  7,(HL)	  */
OP(cb,7f) { SMS_Z_CACHE; BIT(7,_A);												} /* BIT  7,A		  */

OP(cb,80) { SMS_Z_CACHE; _B = RES(0,_B); 										} /* RES  0,B		  */
OP(cb,81) { SMS_Z_CACHE; _C = RES(0,_C); 										} /* RES  0,C		  */
OP(cb,82) { SMS_Z_CACHE; _D = RES(0,_D); 										} /* RES  0,D		  */
OP(cb,83) { SMS_Z_CACHE; _E = RES(0,_E); 										} /* RES  0,E		  */
OP(cb,84) { SMS_Z_CACHE; _H = RES(0,_H); 										} /* RES  0,H		  */
OP(cb,85) { SMS_Z_CACHE; _L = RES(0,_L); 										} /* RES  0,L		  */
OP(cb,86) { SMS_Z_CACHE; WM( _HL, RES(0,RM(_HL)) );								} /* RES  0,(HL)	  */
OP(cb,87) { SMS_Z_CACHE; _A = RES(0,_A); 										} /* RES  0,A		  */

OP(cb,88) { SMS_Z_CACHE; _B = RES(1,_B); 										} /* RES  1,B		  */
OP(cb,89) { SMS_Z_CACHE; _C = RES(1,_C); 										} /* RES  1,C		  */
OP(cb,8a) { SMS_Z_CACHE; _D = RES(1,_D); 										} /* RES  1,D		  */
OP(cb,8b) { SMS_Z_CACHE; _E = RES(1,_E); 										} /* RES  1,E		  */
OP(cb,8c) { SMS_Z_CACHE; _H = RES(1,_H); 										} /* RES  1,H		  */
OP(cb,8d) { SMS_Z_CACHE; _L = RES(1,_L); 										} /* RES  1,L		  */
OP(cb,8e) { SMS_Z_CACHE; WM( _HL, RES(1,RM(_HL)) );								} /* RES  1,(HL)	  */
OP(cb,8f) { SMS_Z_CACHE; _A = RES(1,_A); 										} /* RES  1,A		  */

OP(cb,90) { SMS_Z_CACHE; _B = RES(2,_B); 										} /* RES  2,B		  */
OP(cb,91) { SMS_Z_CACHE; _C = RES(2,_C); 										} /* RES  2,C		  */
OP(cb,92) { SMS_Z_CACHE; _D = RES(2,_D); 										} /* RES  2,D		  */
OP(cb,93) { SMS_Z_CACHE; _E = RES(2,_E); 										} /* RES  2,E		  */
OP(cb,94) { SMS_Z_CACHE; _H = RES(2,_H); 										} /* RES  2,H		  */
OP(cb,95) { SMS_Z_CACHE; _L = RES(2,_L); 										} /* RES  2,L		  */
OP(cb,96) { SMS_Z_CACHE; WM( _HL, RES(2,RM(_HL)) );								} /* RES  2,(HL)	  */
OP(cb,97) { SMS_Z_CACHE; _A = RES(2,_A); 										} /* RES  2,A		  */

OP(cb,98) { SMS_Z_CACHE; _B = RES(3,_B); 										} /* RES  3,B		  */
OP(cb,99) { SMS_Z_CACHE; _C = RES(3,_C); 										} /* RES  3,C		  */
OP(cb,9a) { SMS_Z_CACHE; _D = RES(3,_D); 										} /* RES  3,D		  */
OP(cb,9b) { SMS_Z_CACHE; _E = RES(3,_E); 										} /* RES  3,E		  */
OP(cb,9c) { SMS_Z_CACHE; _H = RES(3,_H); 										} /* RES  3,H		  */
OP(cb,9d) { SMS_Z_CACHE; _L = RES(3,_L); 										} /* RES  3,L		  */
OP(cb,9e) { SMS_Z_CACHE; WM( _HL, RES(3,RM(_HL)) );								} /* RES  3,(HL)	  */
OP(cb,9f) { SMS_Z_CACHE; _A = RES(3,_A); 										} /* RES  3,A		  */

OP(cb,a0) { SMS_Z_CACHE; _B = RES(4,_B); 										} /* RES  4,B		  */
OP(cb,a1) { SMS_Z_CACHE; _C = RES(4,_C); 										} /* RES  4,C		  */
OP(cb,a2) { SMS_Z_CACHE; _D = RES(4,_D); 										} /* RES  4,D		  */
OP(cb,a3) { SMS_Z_CACHE; _E = RES(4,_E); 										} /* RES  4,E		  */
OP(cb,a4) { SMS_Z_CACHE; _H = RES(4,_H); 										} /* RES  4,H		  */
OP(cb,a5) { SMS_Z_CACHE; _L = RES(4,_L); 										} /* RES  4,L		  */
OP(cb,a6) { SMS_Z_CACHE; WM( _HL, RES(4,RM(_HL)) );								} /* RES  4,(HL)	  */
OP(cb,a7) { SMS_Z_CACHE; _A = RES(4,_A); 										} /* RES  4,A		  */

OP(cb,a8) { SMS_Z_CACHE; _B = RES(5,_B); 										} /* RES  5,B		  */
OP(cb,a9) { SMS_Z_CACHE; _C = RES(5,_C); 										} /* RES  5,C		  */
OP(cb,aa) { SMS_Z_CACHE; _D = RES(5,_D); 										} /* RES  5,D		  */
OP(cb,ab) { SMS_Z_CACHE; _E = RES(5,_E); 										} /* RES  5,E		  */
OP(cb,ac) { SMS_Z_CACHE; _H = RES(5,_H); 										} /* RES  5,H		  */
OP(cb,ad) { SMS_Z_CACHE; _L = RES(5,_L); 										} /* RES  5,L		  */
OP(cb,ae) { SMS_Z_CACHE; WM( _HL, RES(5,RM(_HL)) );								} /* RES  5,(HL)	  */
OP(cb,af) { SMS_Z_CACHE; _A = RES(5,_A); 										} /* RES  5,A		  */

OP(cb,b0) { SMS_Z_CACHE; _B = RES(6,_B); 										} /* RES  6,B		  */
OP(cb,b1) { SMS_Z_CACHE; _C = RES(6,_C); 										} /* RES  6,C		  */
OP(cb,b2) { SMS_Z_CACHE; _D = RES(6,_D); 										} /* RES  6,D		  */
OP(cb,b3) { SMS_Z_CACHE; _E = RES(6,_E); 										} /* RES  6,E		  */
OP(cb,b4) { SMS_Z_CACHE; _H = RES(6,_H); 										} /* RES  6,H		  */
OP(cb,b5) { SMS_Z_CACHE; _L = RES(6,_L); 										} /* RES  6,L		  */
OP(cb,b6) { SMS_Z_CACHE; WM( _HL, RES(6,RM(_HL)) );								} /* RES  6,(HL)	  */
OP(cb,b7) { SMS_Z_CACHE; _A = RES(6,_A); 										} /* RES  6,A		  */

OP(cb,b8) { SMS_Z_CACHE; _B = RES(7,_B); 										} /* RES  7,B		  */
OP(cb,b9) { SMS_Z_CACHE; _C = RES(7,_C); 										} /* RES  7,C		  */
OP(cb,ba) { SMS_Z_CACHE; _D = RES(7,_D); 										} /* RES  7,D		  */
OP(cb,bb) { SMS_Z_CACHE; _E = RES(7,_E); 										} /* RES  7,E		  */
OP(cb,bc) { SMS_Z_CACHE; _H = RES(7,_H); 										} /* RES  7,H		  */
OP(cb,bd) { SMS_Z_CACHE; _L = RES(7,_L); 										} /* RES  7,L		  */
OP(cb,be) { SMS_Z_CACHE; WM( _HL, RES(7,RM(_HL)) );								} /* RES  7,(HL)	  */
OP(cb,bf) { SMS_Z_CACHE; _A = RES(7,_A); 										} /* RES  7,A		  */

OP(cb,c0) { SMS_Z_CACHE; _B = SET(0,_B); 										} /* SET  0,B		  */
OP(cb,c1) { SMS_Z_CACHE; _C = SET(0,_C); 										} /* SET  0,C		  */
OP(cb,c2) { SMS_Z_CACHE; _D = SET(0,_D); 										} /* SET  0,D		  */
OP(cb,c3) { SMS_Z_CACHE; _E = SET(0,_E); 										} /* SET  0,E		  */
OP(cb,c4) { SMS_Z_CACHE; _H = SET(0,_H); 										} /* SET  0,H		  */
OP(cb,c5) { SMS_Z_CACHE; _L = SET(0,_L); 										} /* SET  0,L		  */
OP(cb,c6) { SMS_Z_CACHE; WM( _HL, SET(0,RM(_HL)) );								} /* SET  0,(HL)	  */
OP(cb,c7) { SMS_Z_CACHE; _A = SET(0,_A); 										} /* SET  0,A		  */

OP(cb,c8) { SMS_Z_CACHE; _B = SET(1,_B); 										} /* SET  1,B		  */
OP(cb,c9) { SMS_Z_CACHE; _C = SET(1,_C); 										} /* SET  1,C		  */
OP(cb,ca) { SMS_Z_CACHE; _D = SET(1,_D); 										} /* SET  1,D		  */
OP(cb,cb) { SMS_Z_CACHE; _E = SET(1,_E); 										} /* SET  1,E		  */
OP(cb,cc) { SMS_Z_CACHE; _H = SET(1,_H); 										} /* SET  1,H		  */
OP(cb,cd) { SMS_Z_CACHE; _L = SET(1,_L); 										} /* SET  1,L		  */
OP(cb,ce) { SMS_Z_CACHE; WM( _HL, SET(1,RM(_HL)) );								} /* SET  1,(HL)	  */
OP(cb,cf) { SMS_Z_CACHE; _A = SET(1,_A); 										} /* SET  1,A		  */

OP(cb,d0) { SMS_Z_CACHE; _B = SET(2,_B); 										} /* SET  2,B		  */
OP(cb,d1) { SMS_Z_CACHE; _C = SET(2,_C); 										} /* SET  2,C		  */
OP(cb,d2) { SMS_Z_CACHE; _D = SET(2,_D); 										} /* SET  2,D		  */
OP(cb,d3) { SMS_Z_CACHE; _E = SET(2,_E); 										} /* SET  2,E		  */
OP(cb,d4) { SMS_Z_CACHE; _H = SET(2,_H); 										} /* SET  2,H		  */
OP(cb,d5) { SMS_Z_CACHE; _L = SET(2,_L); 										} /* SET  2,L		  */
OP(cb,d6) { SMS_Z_CACHE; WM( _HL, SET(2,RM(_HL)) );								}/* SET  2,(HL) 	 */
OP(cb,d7) { SMS_Z_CACHE; _A = SET(2,_A); 										} /* SET  2,A		  */

OP(cb,d8) { SMS_Z_CACHE; _B = SET(3,_B); 										} /* SET  3,B		  */
OP(cb,d9) { SMS_Z_CACHE; _C = SET(3,_C); 										} /* SET  3,C		  */
OP(cb,da) { SMS_Z_CACHE; _D = SET(3,_D); 										} /* SET  3,D		  */
OP(cb,db) { SMS_Z_CACHE; _E = SET(3,_E); 										} /* SET  3,E		  */
OP(cb,dc) { SMS_Z_CACHE; _H = SET(3,_H); 										} /* SET  3,H		  */
OP(cb,dd) { SMS_Z_CACHE; _L = SET(3,_L); 										} /* SET  3,L		  */
OP(cb,de) { SMS_Z_CACHE; WM( _HL, SET(3,RM(_HL)) );								} /* SET  3,(HL)	  */
OP(cb,df) { SMS_Z_CACHE; _A = SET(3,_A); 										} /* SET  3,A		  */

OP(cb,e0) { SMS_Z_CACHE; _B = SET(4,_B); 										} /* SET  4,B		  */
OP(cb,e1) { SMS_Z_CACHE; _C = SET(4,_C); 										} /* SET  4,C		  */
OP(cb,e2) { SMS_Z_CACHE; _D = SET(4,_D); 										} /* SET  4,D		  */
OP(cb,e3) { SMS_Z_CACHE; _E = SET(4,_E); 										} /* SET  4,E		  */
OP(cb,e4) { SMS_Z_CACHE; _H = SET(4,_H); 										} /* SET  4,H		  */
OP(cb,e5) { SMS_Z_CACHE; _L = SET(4,_L); 										} /* SET  4,L		  */
OP(cb,e6) { SMS_Z_CACHE; WM( _HL, SET(4,RM(_HL)) );								} /* SET  4,(HL)	  */
OP(cb,e7) { SMS_Z_CACHE; _A = SET(4,_A); 										} /* SET  4,A		  */

OP(cb,e8) { SMS_Z_CACHE; _B = SET(5,_B); 										} /* SET  5,B		  */
OP(cb,e9) { SMS_Z_CACHE; _C = SET(5,_C); 										} /* SET  5,C		  */
OP(cb,ea) { SMS_Z_CACHE; _D = SET(5,_D); 										} /* SET  5,D		  */
OP(cb,eb) { SMS_Z_CACHE; _E = SET(5,_E); 										} /* SET  5,E		  */
OP(cb,ec) { SMS_Z_CACHE; _H = SET(5,_H); 										} /* SET  5,H		  */
OP(cb,ed) { SMS_Z_CACHE; _L = SET(5,_L); 										} /* SET  5,L		  */
OP(cb,ee) { SMS_Z_CACHE; WM( _HL, SET(5,RM(_HL)) );								} /* SET  5,(HL)	  */
OP(cb,ef) { SMS_Z_CACHE; _A = SET(5,_A); 										} /* SET  5,A		  */

OP(cb,f0) { SMS_Z_CACHE; _B = SET(6,_B); 										} /* SET  6,B		  */
OP(cb,f1) { SMS_Z_CACHE; _C = SET(6,_C); 										} /* SET  6,C		  */
OP(cb,f2) { SMS_Z_CACHE; _D = SET(6,_D); 										} /* SET  6,D		  */
OP(cb,f3) { SMS_Z_CACHE; _E = SET(6,_E); 										} /* SET  6,E		  */
OP(cb,f4) { SMS_Z_CACHE; _H = SET(6,_H); 										} /* SET  6,H		  */
OP(cb,f5) { SMS_Z_CACHE; _L = SET(6,_L); 										} /* SET  6,L		  */
OP(cb,f6) { SMS_Z_CACHE; WM( _HL, SET(6,RM(_HL)) );								} /* SET  6,(HL)	  */
OP(cb,f7) { SMS_Z_CACHE; _A = SET(6,_A); 										} /* SET  6,A		  */

OP(cb,f8) { SMS_Z_CACHE; _B = SET(7,_B); 										} /* SET  7,B		  */
OP(cb,f9) { SMS_Z_CACHE; _C = SET(7,_C); 										} /* SET  7,C		  */
OP(cb,fa) { SMS_Z_CACHE; _D = SET(7,_D); 										} /* SET  7,D		  */
OP(cb,fb) { SMS_Z_CACHE; _E = SET(7,_E); 										} /* SET  7,E		  */
OP(cb,fc) { SMS_Z_CACHE; _H = SET(7,_H); 										} /* SET  7,H		  */
OP(cb,fd) { SMS_Z_CACHE; _L = SET(7,_L); 										} /* SET  7,L		  */
OP(cb,fe) { SMS_Z_CACHE; WM( _HL, SET(7,RM(_HL)) );								} /* SET  7,(HL)	  */
OP(cb,ff) { SMS_Z_CACHE; _A = SET(7,_A); 										} /* SET  7,A		  */


/**********************************************************
* opcodes with DD/FD CB prefix
* rotate, shift and bit operations with (IX+o)
**********************************************************/
OP(xycb,00) { SMS_Z_CACHE; _B = RLC( RM(EA) ); WM( EA,_B );						} /* RLC  B=(XY+o)	  */
OP(xycb,01) { SMS_Z_CACHE; _C = RLC( RM(EA) ); WM( EA,_C );						} /* RLC  C=(XY+o)	  */
OP(xycb,02) { SMS_Z_CACHE; _D = RLC( RM(EA) ); WM( EA,_D );						} /* RLC  D=(XY+o)	  */
OP(xycb,03) { SMS_Z_CACHE; _E = RLC( RM(EA) ); WM( EA,_E );						} /* RLC  E=(XY+o)	  */
OP(xycb,04) { SMS_Z_CACHE; _H = RLC( RM(EA) ); WM( EA,_H );						} /* RLC  H=(XY+o)	  */
OP(xycb,05) { SMS_Z_CACHE; _L = RLC( RM(EA) ); WM( EA,_L );						} /* RLC  L=(XY+o)	  */
OP(xycb,06) { SMS_Z_CACHE; WM( EA, RLC( RM(EA) ) );								} /* RLC  (XY+o)	  */
OP(xycb,07) { SMS_Z_CACHE; _A = RLC( RM(EA) ); WM( EA,_A );						} /* RLC  A=(XY+o)	  */

OP(xycb,08) { SMS_Z_CACHE; _B = RRC( RM(EA) ); WM( EA,_B );						} /* RRC  B=(XY+o)	  */
OP(xycb,09) { SMS_Z_CACHE; _C = RRC( RM(EA) ); WM( EA,_C );						} /* RRC  C=(XY+o)	  */
OP(xycb,0a) { SMS_Z_CACHE; _D = RRC( RM(EA) ); WM( EA,_D );						} /* RRC  D=(XY+o)	  */
OP(xycb,0b) { SMS_Z_CACHE; _E = RRC( RM(EA) ); WM( EA,_E );						} /* RRC  E=(XY+o)	  */
OP(xycb,0c) { SMS_Z_CACHE; _H = RRC( RM(EA) ); WM( EA,_H );						} /* RRC  H=(XY+o)	  */
OP(xycb,0d) { SMS_Z_CACHE; _L = RRC( RM(EA) ); WM( EA,_L );						} /* RRC  L=(XY+o)	  */
OP(xycb,0e) { SMS_Z_CACHE; WM( EA,RRC( RM(EA) ) );								} /* RRC  (XY+o)	  */
OP(xycb,0f) { SMS_Z_CACHE; _A = RRC( RM(EA) ); WM( EA,_A );						} /* RRC  A=(XY+o)	  */

OP(xycb,10) { SMS_Z_CACHE; _B = RL( RM(EA) ); WM( EA,_B );						} /* RL   B=(XY+o)	  */
OP(xycb,11) { SMS_Z_CACHE; _C = RL( RM(EA) ); WM( EA,_C );						} /* RL   C=(XY+o)	  */
OP(xycb,12) { SMS_Z_CACHE; _D = RL( RM(EA) ); WM( EA,_D );						} /* RL   D=(XY+o)	  */
OP(xycb,13) { SMS_Z_CACHE; _E = RL( RM(EA) ); WM( EA,_E );						} /* RL   E=(XY+o)	  */
OP(xycb,14) { SMS_Z_CACHE; _H = RL( RM(EA) ); WM( EA,_H );						} /* RL   H=(XY+o)	  */
OP(xycb,15) { SMS_Z_CACHE; _L = RL( RM(EA) ); WM( EA,_L );						} /* RL   L=(XY+o)	  */
OP(xycb,16) { SMS_Z_CACHE; WM( EA,RL( RM(EA) ) );								} /* RL   (XY+o)	  */
OP(xycb,17) { SMS_Z_CACHE; _A = RL( RM(EA) ); WM( EA,_A );						} /* RL   A=(XY+o)	  */

OP(xycb,18) { SMS_Z_CACHE; _B = RR( RM(EA) ); WM( EA,_B );						} /* RR   B=(XY+o)	  */
OP(xycb,19) { SMS_Z_CACHE; _C = RR( RM(EA) ); WM( EA,_C );						} /* RR   C=(XY+o)	  */
OP(xycb,1a) { SMS_Z_CACHE; _D = RR( RM(EA) ); WM( EA,_D );						} /* RR   D=(XY+o)	  */
OP(xycb,1b) { SMS_Z_CACHE; _E = RR( RM(EA) ); WM( EA,_E );						} /* RR   E=(XY+o)	  */
OP(xycb,1c) { SMS_Z_CACHE; _H = RR( RM(EA) ); WM( EA,_H );						} /* RR   H=(XY+o)	  */
OP(xycb,1d) { SMS_Z_CACHE; _L = RR( RM(EA) ); WM( EA,_L );						} /* RR   L=(XY+o)	  */
OP(xycb,1e) { SMS_Z_CACHE; WM( EA,RR( RM(EA) ) );								} /* RR   (XY+o)	  */
OP(xycb,1f) { SMS_Z_CACHE; _A = RR( RM(EA) ); WM( EA,_A );						} /* RR   A=(XY+o)	  */

OP(xycb,20) { SMS_Z_CACHE; _B = SLA( RM(EA) ); WM( EA,_B );						} /* SLA  B=(XY+o)	  */
OP(xycb,21) { SMS_Z_CACHE; _C = SLA( RM(EA) ); WM( EA,_C );						} /* SLA  C=(XY+o)	  */
OP(xycb,22) { SMS_Z_CACHE; _D = SLA( RM(EA) ); WM( EA,_D );						} /* SLA  D=(XY+o)	  */
OP(xycb,23) { SMS_Z_CACHE; _E = SLA( RM(EA) ); WM( EA,_E );						} /* SLA  E=(XY+o)	  */
OP(xycb,24) { SMS_Z_CACHE; _H = SLA( RM(EA) ); WM( EA,_H );						} /* SLA  H=(XY+o)	  */
OP(xycb,25) { SMS_Z_CACHE; _L = SLA( RM(EA) ); WM( EA,_L );						} /* SLA  L=(XY+o)	  */
OP(xycb,26) { SMS_Z_CACHE; WM( EA,SLA( RM(EA) ) );								} /* SLA  (XY+o)	  */
OP(xycb,27) { SMS_Z_CACHE; _A = SLA( RM(EA) ); WM( EA,_A );						} /* SLA  A=(XY+o)	  */

OP(xycb,28) { SMS_Z_CACHE; _B = SRA( RM(EA) ); WM( EA,_B );						} /* SRA  B=(XY+o)	  */
OP(xycb,29) { SMS_Z_CACHE; _C = SRA( RM(EA) ); WM( EA,_C );						} /* SRA  C=(XY+o)	  */
OP(xycb,2a) { SMS_Z_CACHE; _D = SRA( RM(EA) ); WM( EA,_D );						} /* SRA  D=(XY+o)	  */
OP(xycb,2b) { SMS_Z_CACHE; _E = SRA( RM(EA) ); WM( EA,_E );						} /* SRA  E=(XY+o)	  */
OP(xycb,2c) { SMS_Z_CACHE; _H = SRA( RM(EA) ); WM( EA,_H );						} /* SRA  H=(XY+o)	  */
OP(xycb,2d) { SMS_Z_CACHE; _L = SRA( RM(EA) ); WM( EA,_L );						} /* SRA  L=(XY+o)	  */
OP(xycb,2e) { SMS_Z_CACHE; WM( EA,SRA( RM(EA) ) );								} /* SRA  (XY+o)	  */
OP(xycb,2f) { SMS_Z_CACHE; _A = SRA( RM(EA) ); WM( EA,_A );						} /* SRA  A=(XY+o)	  */

OP(xycb,30) { SMS_Z_CACHE; _B = SLL( RM(EA) ); WM( EA,_B );						} /* SLL  B=(XY+o)	  */
OP(xycb,31) { SMS_Z_CACHE; _C = SLL( RM(EA) ); WM( EA,_C );						} /* SLL  C=(XY+o)	  */
OP(xycb,32) { SMS_Z_CACHE; _D = SLL( RM(EA) ); WM( EA,_D );						} /* SLL  D=(XY+o)	  */
OP(xycb,33) { SMS_Z_CACHE; _E = SLL( RM(EA) ); WM( EA,_E );						} /* SLL  E=(XY+o)	  */
OP(xycb,34) { SMS_Z_CACHE; _H = SLL( RM(EA) ); WM( EA,_H );						} /* SLL  H=(XY+o)	  */
OP(xycb,35) { SMS_Z_CACHE; _L = SLL( RM(EA) ); WM( EA,_L );						} /* SLL  L=(XY+o)	  */
OP(xycb,36) { SMS_Z_CACHE; WM( EA,SLL( RM(EA) ) );								} /* SLL  (XY+o)	  */
OP(xycb,37) { SMS_Z_CACHE; _A = SLL( RM(EA) ); WM( EA,_A );						} /* SLL  A=(XY+o)	  */

OP(xycb,38) { SMS_Z_CACHE; _B = SRL( RM(EA) ); WM( EA,_B );						} /* SRL  B=(XY+o)	  */
OP(xycb,39) { SMS_Z_CACHE; _C = SRL( RM(EA) ); WM( EA,_C );						} /* SRL  C=(XY+o)	  */
OP(xycb,3a) { SMS_Z_CACHE; _D = SRL( RM(EA) ); WM( EA,_D );						} /* SRL  D=(XY+o)	  */
OP(xycb,3b) { SMS_Z_CACHE; _E = SRL( RM(EA) ); WM( EA,_E );						} /* SRL  E=(XY+o)	  */
OP(xycb,3c) { SMS_Z_CACHE; _H = SRL( RM(EA) ); WM( EA,_H );						} /* SRL  H=(XY+o)	  */
OP(xycb,3d) { SMS_Z_CACHE; _L = SRL( RM(EA) ); WM( EA,_L );						} /* SRL  L=(XY+o)	  */
OP(xycb,3e) { SMS_Z_CACHE; WM( EA,SRL( RM(EA) ) );								} /* SRL  (XY+o)	  */
OP(xycb,3f) { SMS_Z_CACHE; _A = SRL( RM(EA) ); WM( EA,_A );						} /* SRL  A=(XY+o)	  */

OP(xycb,40) { SMS_Z_CACHE; xycb_46();											} /* BIT  0,B=(XY+o)  */
OP(xycb,41) { SMS_Z_CACHE; xycb_46();													  } /* BIT	0,C=(XY+o)	*/
OP(xycb,42) { SMS_Z_CACHE; xycb_46();											} /* BIT  0,D=(XY+o)  */
OP(xycb,43) { SMS_Z_CACHE; xycb_46();											} /* BIT  0,E=(XY+o)  */
OP(xycb,44) { SMS_Z_CACHE; xycb_46();											} /* BIT  0,H=(XY+o)  */
OP(xycb,45) { SMS_Z_CACHE; xycb_46();											} /* BIT  0,L=(XY+o)  */
OP(xycb,46) { SMS_Z_CACHE; BIT_XY(0,RM(EA)); 									} /* BIT  0,(XY+o)	  */
OP(xycb,47) { SMS_Z_CACHE; xycb_46();											} /* BIT  0,A=(XY+o)  */

OP(xycb,48) { SMS_Z_CACHE; xycb_4e();											} /* BIT  1,B=(XY+o)  */
OP(xycb,49) { SMS_Z_CACHE; xycb_4e();													  } /* BIT	1,C=(XY+o)	*/
OP(xycb,4a) { SMS_Z_CACHE; xycb_4e();											} /* BIT  1,D=(XY+o)  */
OP(xycb,4b) { SMS_Z_CACHE; xycb_4e();											} /* BIT  1,E=(XY+o)  */
OP(xycb,4c) { SMS_Z_CACHE; xycb_4e();											} /* BIT  1,H=(XY+o)  */
OP(xycb,4d) { SMS_Z_CACHE; xycb_4e();											} /* BIT  1,L=(XY+o)  */
OP(xycb,4e) { SMS_Z_CACHE; BIT_XY(1,RM(EA)); 									} /* BIT  1,(XY+o)	  */
OP(xycb,4f) { SMS_Z_CACHE; xycb_4e();											} /* BIT  1,A=(XY+o)  */

OP(xycb,50) { SMS_Z_CACHE; xycb_56();											} /* BIT  2,B=(XY+o)  */
OP(xycb,51) { SMS_Z_CACHE; xycb_56();													  } /* BIT	2,C=(XY+o)	*/
OP(xycb,52) { SMS_Z_CACHE; xycb_56();											} /* BIT  2,D=(XY+o)  */
OP(xycb,53) { SMS_Z_CACHE; xycb_56();											} /* BIT  2,E=(XY+o)  */
OP(xycb,54) { SMS_Z_CACHE; xycb_56();											} /* BIT  2,H=(XY+o)  */
OP(xycb,55) { SMS_Z_CACHE; xycb_56();											} /* BIT  2,L=(XY+o)  */
OP(xycb,56) { SMS_Z_CACHE; BIT_XY(2,RM(EA)); 									} /* BIT  2,(XY+o)	  */
OP(xycb,57) { SMS_Z_CACHE; xycb_56();											} /* BIT  2,A=(XY+o)  */

OP(xycb,58) { SMS_Z_CACHE; xycb_5e();											} /* BIT  3,B=(XY+o)  */
OP(xycb,59) { SMS_Z_CACHE; xycb_5e();													  } /* BIT	3,C=(XY+o)	*/
OP(xycb,5a) { SMS_Z_CACHE; xycb_5e();											} /* BIT  3,D=(XY+o)  */
OP(xycb,5b) { SMS_Z_CACHE; xycb_5e();											} /* BIT  3,E=(XY+o)  */
OP(xycb,5c) { SMS_Z_CACHE; xycb_5e();											} /* BIT  3,H=(XY+o)  */
OP(xycb,5d) { SMS_Z_CACHE; xycb_5e();											} /* BIT  3,L=(XY+o)  */
OP(xycb,5e) { SMS_Z_CACHE; BIT_XY(3,RM(EA)); 									} /* BIT  3,(XY+o)	  */
OP(xycb,5f) { SMS_Z_CACHE; xycb_5e();											} /* BIT  3,A=(XY+o)  */

OP(xycb,60) { SMS_Z_CACHE; xycb_66();											} /* BIT  4,B=(XY+o)  */
OP(xycb,61) { SMS_Z_CACHE; xycb_66();													  } /* BIT	4,C=(XY+o)	*/
OP(xycb,62) { SMS_Z_CACHE; xycb_66();											} /* BIT  4,D=(XY+o)  */
OP(xycb,63) { SMS_Z_CACHE; xycb_66();											} /* BIT  4,E=(XY+o)  */
OP(xycb,64) { SMS_Z_CACHE; xycb_66();											} /* BIT  4,H=(XY+o)  */
OP(xycb,65) { SMS_Z_CACHE; xycb_66();											} /* BIT  4,L=(XY+o)  */
OP(xycb,66) { SMS_Z_CACHE; BIT_XY(4,RM(EA)); 									} /* BIT  4,(XY+o)	  */
OP(xycb,67) { SMS_Z_CACHE; xycb_66();											} /* BIT  4,A=(XY+o)  */

OP(xycb,68) { SMS_Z_CACHE; xycb_6e();											} /* BIT  5,B=(XY+o)  */
OP(xycb,69) { SMS_Z_CACHE; xycb_6e();													  } /* BIT	5,C=(XY+o)	*/
OP(xycb,6a) { SMS_Z_CACHE; xycb_6e();											} /* BIT  5,D=(XY+o)  */
OP(xycb,6b) { SMS_Z_CACHE; xycb_6e();											} /* BIT  5,E=(XY+o)  */
OP(xycb,6c) { SMS_Z_CACHE; xycb_6e();											} /* BIT  5,H=(XY+o)  */
OP(xycb,6d) { SMS_Z_CACHE; xycb_6e();											} /* BIT  5,L=(XY+o)  */
OP(xycb,6e) { SMS_Z_CACHE; BIT_XY(5,RM(EA)); 									} /* BIT  5,(XY+o)	  */
OP(xycb,6f) { SMS_Z_CACHE; xycb_6e();											} /* BIT  5,A=(XY+o)  */

OP(xycb,70) { SMS_Z_CACHE; xycb_76();											} /* BIT  6,B=(XY+o)  */
OP(xycb,71) { SMS_Z_CACHE; xycb_76();													  } /* BIT	6,C=(XY+o)	*/
OP(xycb,72) { SMS_Z_CACHE; xycb_76();											} /* BIT  6,D=(XY+o)  */
OP(xycb,73) { SMS_Z_CACHE; xycb_76();											} /* BIT  6,E=(XY+o)  */
OP(xycb,74) { SMS_Z_CACHE; xycb_76();											} /* BIT  6,H=(XY+o)  */
OP(xycb,75) { SMS_Z_CACHE; xycb_76();											} /* BIT  6,L=(XY+o)  */
OP(xycb,76) { SMS_Z_CACHE; BIT_XY(6,RM(EA)); 									} /* BIT  6,(XY+o)	  */
OP(xycb,77) { SMS_Z_CACHE; xycb_76();											} /* BIT  6,A=(XY+o)  */

OP(xycb,78) { SMS_Z_CACHE; xycb_7e();											} /* BIT  7,B=(XY+o)  */
OP(xycb,79) { SMS_Z_CACHE; xycb_7e();													  } /* BIT	7,C=(XY+o)	*/
OP(xycb,7a) { SMS_Z_CACHE; xycb_7e();											} /* BIT  7,D=(XY+o)  */
OP(xycb,7b) { SMS_Z_CACHE; xycb_7e();											} /* BIT  7,E=(XY+o)  */
OP(xycb,7c) { SMS_Z_CACHE; xycb_7e();											} /* BIT  7,H=(XY+o)  */
OP(xycb,7d) { SMS_Z_CACHE; xycb_7e();											} /* BIT  7,L=(XY+o)  */
OP(xycb,7e) { SMS_Z_CACHE; BIT_XY(7,RM(EA)); 									} /* BIT  7,(XY+o)	  */
OP(xycb,7f) { SMS_Z_CACHE; xycb_7e();											} /* BIT  7,A=(XY+o)  */

OP(xycb,80) { SMS_Z_CACHE; _B = RES(0, RM(EA) ); WM( EA,_B );					} /* RES  0,B=(XY+o)  */
OP(xycb,81) { SMS_Z_CACHE; _C = RES(0, RM(EA) ); WM( EA,_C );					} /* RES  0,C=(XY+o)  */
OP(xycb,82) { SMS_Z_CACHE; _D = RES(0, RM(EA) ); WM( EA,_D );					} /* RES  0,D=(XY+o)  */
OP(xycb,83) { SMS_Z_CACHE; _E = RES(0, RM(EA) ); WM( EA,_E );					} /* RES  0,E=(XY+o)  */
OP(xycb,84) { SMS_Z_CACHE; _H = RES(0, RM(EA) ); WM( EA,_H );					} /* RES  0,H=(XY+o)  */
OP(xycb,85) { SMS_Z_CACHE; _L = RES(0, RM(EA) ); WM( EA,_L );					} /* RES  0,L=(XY+o)  */
OP(xycb,86) { SMS_Z_CACHE; WM( EA, RES(0,RM(EA)) );								} /* RES  0,(XY+o)	  */
OP(xycb,87) { SMS_Z_CACHE; _A = RES(0, RM(EA) ); WM( EA,_A );					} /* RES  0,A=(XY+o)  */

OP(xycb,88) { SMS_Z_CACHE; _B = RES(1, RM(EA) ); WM( EA,_B );					} /* RES  1,B=(XY+o)  */
OP(xycb,89) { SMS_Z_CACHE; _C = RES(1, RM(EA) ); WM( EA,_C );					} /* RES  1,C=(XY+o)  */
OP(xycb,8a) { SMS_Z_CACHE; _D = RES(1, RM(EA) ); WM( EA,_D );					} /* RES  1,D=(XY+o)  */
OP(xycb,8b) { SMS_Z_CACHE; _E = RES(1, RM(EA) ); WM( EA,_E );					} /* RES  1,E=(XY+o)  */
OP(xycb,8c) { SMS_Z_CACHE; _H = RES(1, RM(EA) ); WM( EA,_H );					} /* RES  1,H=(XY+o)  */
OP(xycb,8d) { SMS_Z_CACHE; _L = RES(1, RM(EA) ); WM( EA,_L );					} /* RES  1,L=(XY+o)  */
OP(xycb,8e) { SMS_Z_CACHE; WM( EA, RES(1,RM(EA)) );								} /* RES  1,(XY+o)	  */
OP(xycb,8f) { SMS_Z_CACHE; _A = RES(1, RM(EA) ); WM( EA,_A );					} /* RES  1,A=(XY+o)  */

OP(xycb,90) { SMS_Z_CACHE; _B = RES(2, RM(EA) ); WM( EA,_B );					} /* RES  2,B=(XY+o)  */
OP(xycb,91) { SMS_Z_CACHE; _C = RES(2, RM(EA) ); WM( EA,_C );					} /* RES  2,C=(XY+o)  */
OP(xycb,92) { SMS_Z_CACHE; _D = RES(2, RM(EA) ); WM( EA,_D );					} /* RES  2,D=(XY+o)  */
OP(xycb,93) { SMS_Z_CACHE; _E = RES(2, RM(EA) ); WM( EA,_E );					} /* RES  2,E=(XY+o)  */
OP(xycb,94) { SMS_Z_CACHE; _H = RES(2, RM(EA) ); WM( EA,_H );					} /* RES  2,H=(XY+o)  */
OP(xycb,95) { SMS_Z_CACHE; _L = RES(2, RM(EA) ); WM( EA,_L );					} /* RES  2,L=(XY+o)  */
OP(xycb,96) { SMS_Z_CACHE; WM( EA, RES(2,RM(EA)) );								} /* RES  2,(XY+o)	  */
OP(xycb,97) { SMS_Z_CACHE; _A = RES(2, RM(EA) ); WM( EA,_A );					} /* RES  2,A=(XY+o)  */

OP(xycb,98) { SMS_Z_CACHE; _B = RES(3, RM(EA) ); WM( EA,_B );					} /* RES  3,B=(XY+o)  */
OP(xycb,99) { SMS_Z_CACHE; _C = RES(3, RM(EA) ); WM( EA,_C );					} /* RES  3,C=(XY+o)  */
OP(xycb,9a) { SMS_Z_CACHE; _D = RES(3, RM(EA) ); WM( EA,_D );					} /* RES  3,D=(XY+o)  */
OP(xycb,9b) { SMS_Z_CACHE; _E = RES(3, RM(EA) ); WM( EA,_E );					} /* RES  3,E=(XY+o)  */
OP(xycb,9c) { SMS_Z_CACHE; _H = RES(3, RM(EA) ); WM( EA,_H );					} /* RES  3,H=(XY+o)  */
OP(xycb,9d) { SMS_Z_CACHE; _L = RES(3, RM(EA) ); WM( EA,_L );					} /* RES  3,L=(XY+o)  */
OP(xycb,9e) { SMS_Z_CACHE; WM( EA, RES(3,RM(EA)) );								} /* RES  3,(XY+o)	  */
OP(xycb,9f) { SMS_Z_CACHE; _A = RES(3, RM(EA) ); WM( EA,_A );					} /* RES  3,A=(XY+o)  */

OP(xycb,a0) { SMS_Z_CACHE; _B = RES(4, RM(EA) ); WM( EA,_B );					} /* RES  4,B=(XY+o)  */
OP(xycb,a1) { SMS_Z_CACHE; _C = RES(4, RM(EA) ); WM( EA,_C );					} /* RES  4,C=(XY+o)  */
OP(xycb,a2) { SMS_Z_CACHE; _D = RES(4, RM(EA) ); WM( EA,_D );					} /* RES  4,D=(XY+o)  */
OP(xycb,a3) { SMS_Z_CACHE; _E = RES(4, RM(EA) ); WM( EA,_E );					} /* RES  4,E=(XY+o)  */
OP(xycb,a4) { SMS_Z_CACHE; _H = RES(4, RM(EA) ); WM( EA,_H );					} /* RES  4,H=(XY+o)  */
OP(xycb,a5) { SMS_Z_CACHE; _L = RES(4, RM(EA) ); WM( EA,_L );					} /* RES  4,L=(XY+o)  */
OP(xycb,a6) { SMS_Z_CACHE; WM( EA, RES(4,RM(EA)) );								} /* RES  4,(XY+o)	  */
OP(xycb,a7) { SMS_Z_CACHE; _A = RES(4, RM(EA) ); WM( EA,_A );					} /* RES  4,A=(XY+o)  */

OP(xycb,a8) { SMS_Z_CACHE; _B = RES(5, RM(EA) ); WM( EA,_B );					} /* RES  5,B=(XY+o)  */
OP(xycb,a9) { SMS_Z_CACHE; _C = RES(5, RM(EA) ); WM( EA,_C );					} /* RES  5,C=(XY+o)  */
OP(xycb,aa) { SMS_Z_CACHE; _D = RES(5, RM(EA) ); WM( EA,_D );					} /* RES  5,D=(XY+o)  */
OP(xycb,ab) { SMS_Z_CACHE; _E = RES(5, RM(EA) ); WM( EA,_E );					} /* RES  5,E=(XY+o)  */
OP(xycb,ac) { SMS_Z_CACHE; _H = RES(5, RM(EA) ); WM( EA,_H );					} /* RES  5,H=(XY+o)  */
OP(xycb,ad) { SMS_Z_CACHE; _L = RES(5, RM(EA) ); WM( EA,_L );					} /* RES  5,L=(XY+o)  */
OP(xycb,ae) { SMS_Z_CACHE; WM( EA, RES(5,RM(EA)) );								} /* RES  5,(XY+o)	  */
OP(xycb,af) { SMS_Z_CACHE; _A = RES(5, RM(EA) ); WM( EA,_A );					} /* RES  5,A=(XY+o)  */

OP(xycb,b0) { SMS_Z_CACHE; _B = RES(6, RM(EA) ); WM( EA,_B );					} /* RES  6,B=(XY+o)  */
OP(xycb,b1) { SMS_Z_CACHE; _C = RES(6, RM(EA) ); WM( EA,_C );					} /* RES  6,C=(XY+o)  */
OP(xycb,b2) { SMS_Z_CACHE; _D = RES(6, RM(EA) ); WM( EA,_D );					} /* RES  6,D=(XY+o)  */
OP(xycb,b3) { SMS_Z_CACHE; _E = RES(6, RM(EA) ); WM( EA,_E );					} /* RES  6,E=(XY+o)  */
OP(xycb,b4) { SMS_Z_CACHE; _H = RES(6, RM(EA) ); WM( EA,_H );					} /* RES  6,H=(XY+o)  */
OP(xycb,b5) { SMS_Z_CACHE; _L = RES(6, RM(EA) ); WM( EA,_L );					} /* RES  6,L=(XY+o)  */
OP(xycb,b6) { SMS_Z_CACHE; WM( EA, RES(6,RM(EA)) );								} /* RES  6,(XY+o)	  */
OP(xycb,b7) { SMS_Z_CACHE; _A = RES(6, RM(EA) ); WM( EA,_A );					} /* RES  6,A=(XY+o)  */

OP(xycb,b8) { SMS_Z_CACHE; _B = RES(7, RM(EA) ); WM( EA,_B );					} /* RES  7,B=(XY+o)  */
OP(xycb,b9) { SMS_Z_CACHE; _C = RES(7, RM(EA) ); WM( EA,_C );					} /* RES  7,C=(XY+o)  */
OP(xycb,ba) { SMS_Z_CACHE; _D = RES(7, RM(EA) ); WM( EA,_D );					} /* RES  7,D=(XY+o)  */
OP(xycb,bb) { SMS_Z_CACHE; _E = RES(7, RM(EA) ); WM( EA,_E );					} /* RES  7,E=(XY+o)  */
OP(xycb,bc) { SMS_Z_CACHE; _H = RES(7, RM(EA) ); WM( EA,_H );					} /* RES  7,H=(XY+o)  */
OP(xycb,bd) { SMS_Z_CACHE; _L = RES(7, RM(EA) ); WM( EA,_L );					} /* RES  7,L=(XY+o)  */
OP(xycb,be) { SMS_Z_CACHE; WM( EA, RES(7,RM(EA)) );								} /* RES  7,(XY+o)	  */
OP(xycb,bf) { SMS_Z_CACHE; _A = RES(7, RM(EA) ); WM( EA,_A );					} /* RES  7,A=(XY+o)  */

OP(xycb,c0) { SMS_Z_CACHE; _B = SET(0, RM(EA) ); WM( EA,_B );					} /* SET  0,B=(XY+o)  */
OP(xycb,c1) { SMS_Z_CACHE; _C = SET(0, RM(EA) ); WM( EA,_C );					} /* SET  0,C=(XY+o)  */
OP(xycb,c2) { SMS_Z_CACHE; _D = SET(0, RM(EA) ); WM( EA,_D );					} /* SET  0,D=(XY+o)  */
OP(xycb,c3) { SMS_Z_CACHE; _E = SET(0, RM(EA) ); WM( EA,_E );					} /* SET  0,E=(XY+o)  */
OP(xycb,c4) { SMS_Z_CACHE; _H = SET(0, RM(EA) ); WM( EA,_H );					} /* SET  0,H=(XY+o)  */
OP(xycb,c5) { SMS_Z_CACHE; _L = SET(0, RM(EA) ); WM( EA,_L );					} /* SET  0,L=(XY+o)  */
OP(xycb,c6) { SMS_Z_CACHE; WM( EA, SET(0,RM(EA)) );								} /* SET  0,(XY+o)	  */
OP(xycb,c7) { SMS_Z_CACHE; _A = SET(0, RM(EA) ); WM( EA,_A );					} /* SET  0,A=(XY+o)  */

OP(xycb,c8) { SMS_Z_CACHE; _B = SET(1, RM(EA) ); WM( EA,_B );					} /* SET  1,B=(XY+o)  */
OP(xycb,c9) { SMS_Z_CACHE; _C = SET(1, RM(EA) ); WM( EA,_C );					} /* SET  1,C=(XY+o)  */
OP(xycb,ca) { SMS_Z_CACHE; _D = SET(1, RM(EA) ); WM( EA,_D );					} /* SET  1,D=(XY+o)  */
OP(xycb,cb) { SMS_Z_CACHE; _E = SET(1, RM(EA) ); WM( EA,_E );					} /* SET  1,E=(XY+o)  */
OP(xycb,cc) { SMS_Z_CACHE; _H = SET(1, RM(EA) ); WM( EA,_H );					} /* SET  1,H=(XY+o)  */
OP(xycb,cd) { SMS_Z_CACHE; _L = SET(1, RM(EA) ); WM( EA,_L );					} /* SET  1,L=(XY+o)  */
OP(xycb,ce) { SMS_Z_CACHE; WM( EA, SET(1,RM(EA)) );								} /* SET  1,(XY+o)	  */
OP(xycb,cf) { SMS_Z_CACHE; _A = SET(1, RM(EA) ); WM( EA,_A );					} /* SET  1,A=(XY+o)  */

OP(xycb,d0) { SMS_Z_CACHE; _B = SET(2, RM(EA) ); WM( EA,_B );					} /* SET  2,B=(XY+o)  */
OP(xycb,d1) { SMS_Z_CACHE; _C = SET(2, RM(EA) ); WM( EA,_C );					} /* SET  2,C=(XY+o)  */
OP(xycb,d2) { SMS_Z_CACHE; _D = SET(2, RM(EA) ); WM( EA,_D );					} /* SET  2,D=(XY+o)  */
OP(xycb,d3) { SMS_Z_CACHE; _E = SET(2, RM(EA) ); WM( EA,_E );					} /* SET  2,E=(XY+o)  */
OP(xycb,d4) { SMS_Z_CACHE; _H = SET(2, RM(EA) ); WM( EA,_H );					} /* SET  2,H=(XY+o)  */
OP(xycb,d5) { SMS_Z_CACHE; _L = SET(2, RM(EA) ); WM( EA,_L );					} /* SET  2,L=(XY+o)  */
OP(xycb,d6) { SMS_Z_CACHE; WM( EA, SET(2,RM(EA)) );								} /* SET  2,(XY+o)	  */
OP(xycb,d7) { SMS_Z_CACHE; _A = SET(2, RM(EA) ); WM( EA,_A );					} /* SET  2,A=(XY+o)  */

OP(xycb,d8) { SMS_Z_CACHE; _B = SET(3, RM(EA) ); WM( EA,_B );					} /* SET  3,B=(XY+o)  */
OP(xycb,d9) { SMS_Z_CACHE; _C = SET(3, RM(EA) ); WM( EA,_C );					} /* SET  3,C=(XY+o)  */
OP(xycb,da) { SMS_Z_CACHE; _D = SET(3, RM(EA) ); WM( EA,_D );					} /* SET  3,D=(XY+o)  */
OP(xycb,db) { SMS_Z_CACHE; _E = SET(3, RM(EA) ); WM( EA,_E );					} /* SET  3,E=(XY+o)  */
OP(xycb,dc) { SMS_Z_CACHE; _H = SET(3, RM(EA) ); WM( EA,_H );					} /* SET  3,H=(XY+o)  */
OP(xycb,dd) { SMS_Z_CACHE; _L = SET(3, RM(EA) ); WM( EA,_L );					} /* SET  3,L=(XY+o)  */
OP(xycb,de) { SMS_Z_CACHE; WM( EA, SET(3,RM(EA)) );								} /* SET  3,(XY+o)	  */
OP(xycb,df) { SMS_Z_CACHE; _A = SET(3, RM(EA) ); WM( EA,_A );					} /* SET  3,A=(XY+o)  */

OP(xycb,e0) { SMS_Z_CACHE; _B = SET(4, RM(EA) ); WM( EA,_B );					} /* SET  4,B=(XY+o)  */
OP(xycb,e1) { SMS_Z_CACHE; _C = SET(4, RM(EA) ); WM( EA,_C );					} /* SET  4,C=(XY+o)  */
OP(xycb,e2) { SMS_Z_CACHE; _D = SET(4, RM(EA) ); WM( EA,_D );					} /* SET  4,D=(XY+o)  */
OP(xycb,e3) { SMS_Z_CACHE; _E = SET(4, RM(EA) ); WM( EA,_E );					} /* SET  4,E=(XY+o)  */
OP(xycb,e4) { SMS_Z_CACHE; _H = SET(4, RM(EA) ); WM( EA,_H );					} /* SET  4,H=(XY+o)  */
OP(xycb,e5) { SMS_Z_CACHE; _L = SET(4, RM(EA) ); WM( EA,_L );					} /* SET  4,L=(XY+o)  */
OP(xycb,e6) { SMS_Z_CACHE; WM( EA, SET(4,RM(EA)) );								} /* SET  4,(XY+o)	  */
OP(xycb,e7) { SMS_Z_CACHE; _A = SET(4, RM(EA) ); WM( EA,_A );					} /* SET  4,A=(XY+o)  */

OP(xycb,e8) { SMS_Z_CACHE; _B = SET(5, RM(EA) ); WM( EA,_B );					} /* SET  5,B=(XY+o)  */
OP(xycb,e9) { SMS_Z_CACHE; _C = SET(5, RM(EA) ); WM( EA,_C );					} /* SET  5,C=(XY+o)  */
OP(xycb,ea) { SMS_Z_CACHE; _D = SET(5, RM(EA) ); WM( EA,_D );					} /* SET  5,D=(XY+o)  */
OP(xycb,eb) { SMS_Z_CACHE; _E = SET(5, RM(EA) ); WM( EA,_E );					} /* SET  5,E=(XY+o)  */
OP(xycb,ec) { SMS_Z_CACHE; _H = SET(5, RM(EA) ); WM( EA,_H );					} /* SET  5,H=(XY+o)  */
OP(xycb,ed) { SMS_Z_CACHE; _L = SET(5, RM(EA) ); WM( EA,_L );					} /* SET  5,L=(XY+o)  */
OP(xycb,ee) { SMS_Z_CACHE; WM( EA, SET(5,RM(EA)) );								} /* SET  5,(XY+o)	  */
OP(xycb,ef) { SMS_Z_CACHE; _A = SET(5, RM(EA) ); WM( EA,_A );					} /* SET  5,A=(XY+o)  */

OP(xycb,f0) { SMS_Z_CACHE; _B = SET(6, RM(EA) ); WM( EA,_B );					} /* SET  6,B=(XY+o)  */
OP(xycb,f1) { SMS_Z_CACHE; _C = SET(6, RM(EA) ); WM( EA,_C );					} /* SET  6,C=(XY+o)  */
OP(xycb,f2) { SMS_Z_CACHE; _D = SET(6, RM(EA) ); WM( EA,_D );					} /* SET  6,D=(XY+o)  */
OP(xycb,f3) { SMS_Z_CACHE; _E = SET(6, RM(EA) ); WM( EA,_E );					} /* SET  6,E=(XY+o)  */
OP(xycb,f4) { SMS_Z_CACHE; _H = SET(6, RM(EA) ); WM( EA,_H );					} /* SET  6,H=(XY+o)  */
OP(xycb,f5) { SMS_Z_CACHE; _L = SET(6, RM(EA) ); WM( EA,_L );					} /* SET  6,L=(XY+o)  */
OP(xycb,f6) { SMS_Z_CACHE; WM( EA, SET(6,RM(EA)) );								} /* SET  6,(XY+o)	  */
OP(xycb,f7) { SMS_Z_CACHE; _A = SET(6, RM(EA) ); WM( EA,_A );					} /* SET  6,A=(XY+o)  */

OP(xycb,f8) { SMS_Z_CACHE; _B = SET(7, RM(EA) ); WM( EA,_B );					} /* SET  7,B=(XY+o)  */
OP(xycb,f9) { SMS_Z_CACHE; _C = SET(7, RM(EA) ); WM( EA,_C );					} /* SET  7,C=(XY+o)  */
OP(xycb,fa) { SMS_Z_CACHE; _D = SET(7, RM(EA) ); WM( EA,_D );					} /* SET  7,D=(XY+o)  */
OP(xycb,fb) { SMS_Z_CACHE; _E = SET(7, RM(EA) ); WM( EA,_E );					} /* SET  7,E=(XY+o)  */
OP(xycb,fc) { SMS_Z_CACHE; _H = SET(7, RM(EA) ); WM( EA,_H );					} /* SET  7,H=(XY+o)  */
OP(xycb,fd) { SMS_Z_CACHE; _L = SET(7, RM(EA) ); WM( EA,_L );					} /* SET  7,L=(XY+o)  */
OP(xycb,fe) { SMS_Z_CACHE; WM( EA, SET(7,RM(EA)) );								} /* SET  7,(XY+o)	  */
OP(xycb,ff) { SMS_Z_CACHE; _A = SET(7, RM(EA) ); WM( EA,_A );					} /* SET  7,A=(XY+o)  */

OP(illegal,1) {
}

/**********************************************************
 * IX register related opcodes (DD prefix)
 **********************************************************/
OP(dd,00) { SMS_Z_CACHE; illegal_1(); op_00();									} /* DB   DD		  */
OP(dd,01) { SMS_Z_CACHE; illegal_1(); op_01();									} /* DB   DD		  */
OP(dd,02) { SMS_Z_CACHE; illegal_1(); op_02();									} /* DB   DD		  */
OP(dd,03) { SMS_Z_CACHE; illegal_1(); op_03();									} /* DB   DD		  */
OP(dd,04) { SMS_Z_CACHE; illegal_1(); op_04();									} /* DB   DD		  */
OP(dd,05) { SMS_Z_CACHE; illegal_1(); op_05();									} /* DB   DD		  */
OP(dd,06) { SMS_Z_CACHE; illegal_1(); op_06();									} /* DB   DD		  */
OP(dd,07) { SMS_Z_CACHE; illegal_1(); op_07();									} /* DB   DD		  */

OP(dd,08) { SMS_Z_CACHE; illegal_1(); op_08();									} /* DB   DD		  */
OP(dd,09) { SMS_Z_CACHE; _R++; ADD16(IX,BC); 									} /* ADD  IX,BC 	  */
OP(dd,0a) { SMS_Z_CACHE; illegal_1(); op_0a();									} /* DB   DD		  */
OP(dd,0b) { SMS_Z_CACHE; illegal_1(); op_0b();									} /* DB   DD		  */
OP(dd,0c) { SMS_Z_CACHE; illegal_1(); op_0c();									} /* DB   DD		  */
OP(dd,0d) { SMS_Z_CACHE; illegal_1(); op_0d();									} /* DB   DD		  */
OP(dd,0e) { SMS_Z_CACHE; illegal_1(); op_0e();									} /* DB   DD		  */
OP(dd,0f) { SMS_Z_CACHE; illegal_1(); op_0f();									} /* DB   DD		  */

OP(dd,10) { SMS_Z_CACHE; illegal_1(); op_10();									} /* DB   DD		  */
OP(dd,11) { SMS_Z_CACHE; illegal_1(); op_11();									} /* DB   DD		  */
OP(dd,12) { SMS_Z_CACHE; illegal_1(); op_12();									} /* DB   DD		  */
OP(dd,13) { SMS_Z_CACHE; illegal_1(); op_13();									} /* DB   DD		  */
OP(dd,14) { SMS_Z_CACHE; illegal_1(); op_14();									} /* DB   DD		  */
OP(dd,15) { SMS_Z_CACHE; illegal_1(); op_15();									} /* DB   DD		  */
OP(dd,16) { SMS_Z_CACHE; illegal_1(); op_16();									} /* DB   DD		  */
OP(dd,17) { SMS_Z_CACHE; illegal_1(); op_17();									} /* DB   DD		  */

OP(dd,18) { SMS_Z_CACHE; illegal_1(); op_18();									} /* DB   DD		  */
OP(dd,19) { SMS_Z_CACHE; _R++; ADD16(IX,DE); 									} /* ADD  IX,DE 	  */
OP(dd,1a) { SMS_Z_CACHE; illegal_1(); op_1a();									} /* DB   DD		  */
OP(dd,1b) { SMS_Z_CACHE; illegal_1(); op_1b();									} /* DB   DD		  */
OP(dd,1c) { SMS_Z_CACHE; illegal_1(); op_1c();									} /* DB   DD		  */
OP(dd,1d) { SMS_Z_CACHE; illegal_1(); op_1d();									} /* DB   DD		  */
OP(dd,1e) { SMS_Z_CACHE; illegal_1(); op_1e();									} /* DB   DD		  */
OP(dd,1f) { SMS_Z_CACHE; illegal_1(); op_1f();									} /* DB   DD		  */

OP(dd,20) { SMS_Z_CACHE; illegal_1(); op_20();									} /* DB   DD		  */
OP(dd,21) { SMS_Z_CACHE; _R++; _IX = ARG16();									} /* LD   IX,w		  */
OP(dd,22) { SMS_Z_CACHE; _R++; EA = ARG16(); WM16( EA, &Z80.IX );				} /* LD   (w),IX	  */
OP(dd,23) { SMS_Z_CACHE; _R++; _IX++;											} /* INC  IX		  */
OP(dd,24) { SMS_Z_CACHE; _R++; _HX = INC(_HX);									} /* INC  HX		  */
OP(dd,25) { SMS_Z_CACHE; _R++; _HX = DEC(_HX);									} /* DEC  HX		  */
OP(dd,26) { SMS_Z_CACHE; _R++; _HX = ARG();										} /* LD   HX,n		  */
OP(dd,27) { SMS_Z_CACHE; illegal_1(); op_27();									} /* DB   DD		  */

OP(dd,28) { SMS_Z_CACHE; illegal_1(); op_28();									} /* DB   DD		  */
OP(dd,29) { SMS_Z_CACHE; _R++; ADD16(IX,IX); 									} /* ADD  IX,IX 	  */
OP(dd,2a) { SMS_Z_CACHE; _R++; EA = ARG16(); RM16( EA, &Z80.IX );				} /* LD   IX,(w)	  */
OP(dd,2b) { SMS_Z_CACHE; _R++; _IX--;											} /* DEC  IX		  */
OP(dd,2c) { SMS_Z_CACHE; _R++; _LX = INC(_LX);									} /* INC  LX		  */
OP(dd,2d) { SMS_Z_CACHE; _R++; _LX = DEC(_LX);									} /* DEC  LX		  */
OP(dd,2e) { SMS_Z_CACHE; _R++; _LX = ARG();										} /* LD   LX,n		  */
OP(dd,2f) { SMS_Z_CACHE; illegal_1(); op_2f();									} /* DB   DD		  */

OP(dd,30) { SMS_Z_CACHE; illegal_1(); op_30();									} /* DB   DD		  */
OP(dd,31) { SMS_Z_CACHE; illegal_1(); op_31();									} /* DB   DD		  */
OP(dd,32) { SMS_Z_CACHE; illegal_1(); op_32();									} /* DB   DD		  */
OP(dd,33) { SMS_Z_CACHE; illegal_1(); op_33();									} /* DB   DD		  */
OP(dd,34) { SMS_Z_CACHE; _R++; EAX; WM( EA, INC(RM(EA)) );						} /* INC  (IX+o)	  */
OP(dd,35) { SMS_Z_CACHE; _R++; EAX; WM( EA, DEC(RM(EA)) );						} /* DEC  (IX+o)	  */
OP(dd,36) { SMS_Z_CACHE; _R++; EAX; WM( EA, ARG() ); 							} /* LD   (IX+o),n	  */
OP(dd,37) { SMS_Z_CACHE; illegal_1(); op_37();									} /* DB   DD		  */

OP(dd,38) { SMS_Z_CACHE; illegal_1(); op_38();									} /* DB   DD		  */
OP(dd,39) { SMS_Z_CACHE; _R++; ADD16(IX,SP); 									} /* ADD  IX,SP 	  */
OP(dd,3a) { SMS_Z_CACHE; illegal_1(); op_3a();									} /* DB   DD		  */
OP(dd,3b) { SMS_Z_CACHE; illegal_1(); op_3b();									} /* DB   DD		  */
OP(dd,3c) { SMS_Z_CACHE; illegal_1(); op_3c();									} /* DB   DD		  */
OP(dd,3d) { SMS_Z_CACHE; illegal_1(); op_3d();									} /* DB   DD		  */
OP(dd,3e) { SMS_Z_CACHE; illegal_1(); op_3e();									} /* DB   DD		  */
OP(dd,3f) { SMS_Z_CACHE; illegal_1(); op_3f();									} /* DB   DD		  */

OP(dd,40) { SMS_Z_CACHE; illegal_1(); op_40();									} /* DB   DD		  */
OP(dd,41) { SMS_Z_CACHE; illegal_1(); op_41();									} /* DB   DD		  */
OP(dd,42) { SMS_Z_CACHE; illegal_1(); op_42();									} /* DB   DD		  */
OP(dd,43) { SMS_Z_CACHE; illegal_1(); op_43();									} /* DB   DD		  */
OP(dd,44) { SMS_Z_CACHE; _R++; _B = _HX; 										} /* LD   B,HX		  */
OP(dd,45) { SMS_Z_CACHE; _R++; _B = _LX; 										} /* LD   B,LX		  */
OP(dd,46) { SMS_Z_CACHE; _R++; EAX; _B = RM(EA); 								} /* LD   B,(IX+o)	  */
OP(dd,47) { SMS_Z_CACHE; illegal_1(); op_47();									} /* DB   DD		  */

OP(dd,48) { SMS_Z_CACHE; illegal_1(); op_48();									} /* DB   DD		  */
OP(dd,49) { SMS_Z_CACHE; illegal_1(); op_49();									} /* DB   DD		  */
OP(dd,4a) { SMS_Z_CACHE; illegal_1(); op_4a();									} /* DB   DD		  */
OP(dd,4b) { SMS_Z_CACHE; illegal_1(); op_4b();									} /* DB   DD		  */
OP(dd,4c) { SMS_Z_CACHE; _R++; _C = _HX; 										} /* LD   C,HX		  */
OP(dd,4d) { SMS_Z_CACHE; _R++; _C = _LX; 										} /* LD   C,LX		  */
OP(dd,4e) { SMS_Z_CACHE; _R++; EAX; _C = RM(EA); 								} /* LD   C,(IX+o)	  */
OP(dd,4f) { SMS_Z_CACHE; illegal_1(); op_4f();									} /* DB   DD		  */

OP(dd,50) { SMS_Z_CACHE; illegal_1(); op_50();									} /* DB   DD		  */
OP(dd,51) { SMS_Z_CACHE; illegal_1(); op_51();									} /* DB   DD		  */
OP(dd,52) { SMS_Z_CACHE; illegal_1(); op_52();									} /* DB   DD		  */
OP(dd,53) { SMS_Z_CACHE; illegal_1(); op_53();									} /* DB   DD		  */
OP(dd,54) { SMS_Z_CACHE; _R++; _D = _HX; 										} /* LD   D,HX		  */
OP(dd,55) { SMS_Z_CACHE; _R++; _D = _LX; 										} /* LD   D,LX		  */
OP(dd,56) { SMS_Z_CACHE; _R++; EAX; _D = RM(EA); 								} /* LD   D,(IX+o)	  */
OP(dd,57) { SMS_Z_CACHE; illegal_1(); op_57();									} /* DB   DD		  */

OP(dd,58) { SMS_Z_CACHE; illegal_1(); op_58();									} /* DB   DD		  */
OP(dd,59) { SMS_Z_CACHE; illegal_1(); op_59();									} /* DB   DD		  */
OP(dd,5a) { SMS_Z_CACHE; illegal_1(); op_5a();									} /* DB   DD		  */
OP(dd,5b) { SMS_Z_CACHE; illegal_1(); op_5b();									} /* DB   DD		  */
OP(dd,5c) { SMS_Z_CACHE; _R++; _E = _HX; 										} /* LD   E,HX		  */
OP(dd,5d) { SMS_Z_CACHE; _R++; _E = _LX; 										} /* LD   E,LX		  */
OP(dd,5e) { SMS_Z_CACHE; _R++; EAX; _E = RM(EA); 								} /* LD   E,(IX+o)	  */
OP(dd,5f) { SMS_Z_CACHE; illegal_1(); op_5f();									} /* DB   DD		  */

OP(dd,60) { SMS_Z_CACHE; _R++; _HX = _B; 										} /* LD   HX,B		  */
OP(dd,61) { SMS_Z_CACHE; _R++; _HX = _C; 										} /* LD   HX,C		  */
OP(dd,62) { SMS_Z_CACHE; _R++; _HX = _D; 										} /* LD   HX,D		  */
OP(dd,63) { SMS_Z_CACHE; _R++; _HX = _E; 										} /* LD   HX,E		  */
OP(dd,64) { 														} /* LD   HX,HX 	  */
OP(dd,65) { SMS_Z_CACHE; _R++; _HX = _LX;										} /* LD   HX,LX 	  */
OP(dd,66) { SMS_Z_CACHE; _R++; EAX; _H = RM(EA); 								} /* LD   H,(IX+o)	  */
OP(dd,67) { SMS_Z_CACHE; _R++; _HX = _A; 										} /* LD   HX,A		  */

OP(dd,68) { SMS_Z_CACHE; _R++; _LX = _B; 										} /* LD   LX,B		  */
OP(dd,69) { SMS_Z_CACHE; _R++; _LX = _C; 										} /* LD   LX,C		  */
OP(dd,6a) { SMS_Z_CACHE; _R++; _LX = _D; 										} /* LD   LX,D		  */
OP(dd,6b) { SMS_Z_CACHE; _R++; _LX = _E; 										} /* LD   LX,E		  */
OP(dd,6c) { SMS_Z_CACHE; _R++; _LX = _HX;										} /* LD   LX,HX 	  */
OP(dd,6d) { 														} /* LD   LX,LX 	  */
OP(dd,6e) { SMS_Z_CACHE; _R++; EAX; _L = RM(EA); 								} /* LD   L,(IX+o)	  */
OP(dd,6f) { SMS_Z_CACHE; _R++; _LX = _A; 										} /* LD   LX,A		  */

OP(dd,70) { SMS_Z_CACHE; _R++; EAX; WM( EA, _B );								} /* LD   (IX+o),B	  */
OP(dd,71) { SMS_Z_CACHE; _R++; EAX; WM( EA, _C );								} /* LD   (IX+o),C	  */
OP(dd,72) { SMS_Z_CACHE; _R++; EAX; WM( EA, _D );								} /* LD   (IX+o),D	  */
OP(dd,73) { SMS_Z_CACHE; _R++; EAX; WM( EA, _E );								} /* LD   (IX+o),E	  */
OP(dd,74) { SMS_Z_CACHE; _R++; EAX; WM( EA, _H );								} /* LD   (IX+o),H	  */
OP(dd,75) { SMS_Z_CACHE; _R++; EAX; WM( EA, _L );								} /* LD   (IX+o),L	  */
OP(dd,76) { SMS_Z_CACHE; illegal_1(); op_76();									}		  /* DB   DD		  */
OP(dd,77) { SMS_Z_CACHE; _R++; EAX; WM( EA, _A );								} /* LD   (IX+o),A	  */

OP(dd,78) { SMS_Z_CACHE; illegal_1(); op_78();									} /* DB   DD		  */
OP(dd,79) { SMS_Z_CACHE; illegal_1(); op_79();									} /* DB   DD		  */
OP(dd,7a) { SMS_Z_CACHE; illegal_1(); op_7a();									} /* DB   DD		  */
OP(dd,7b) { SMS_Z_CACHE; illegal_1(); op_7b();									} /* DB   DD		  */
OP(dd,7c) { SMS_Z_CACHE; _R++; _A = _HX; 										} /* LD   A,HX		  */
OP(dd,7d) { SMS_Z_CACHE; _R++; _A = _LX; 										} /* LD   A,LX		  */
OP(dd,7e) { SMS_Z_CACHE; _R++; EAX; _A = RM(EA); 								} /* LD   A,(IX+o)	  */
OP(dd,7f) { SMS_Z_CACHE; illegal_1(); op_7f();									} /* DB   DD		  */

OP(dd,80) { SMS_Z_CACHE; illegal_1(); op_80();									} /* DB   DD		  */
OP(dd,81) { SMS_Z_CACHE; illegal_1(); op_81();									} /* DB   DD		  */
OP(dd,82) { SMS_Z_CACHE; illegal_1(); op_82();									} /* DB   DD		  */
OP(dd,83) { SMS_Z_CACHE; illegal_1(); op_83();									} /* DB   DD		  */
OP(dd,84) { SMS_Z_CACHE; _R++; ADD(_HX); 										} /* ADD  A,HX		  */
OP(dd,85) { SMS_Z_CACHE; _R++; ADD(_LX); 										} /* ADD  A,LX		  */
OP(dd,86) { SMS_Z_CACHE; _R++; EAX; ADD(RM(EA)); 								} /* ADD  A,(IX+o)	  */
OP(dd,87) { SMS_Z_CACHE; illegal_1(); op_87();									} /* DB   DD		  */

OP(dd,88) { SMS_Z_CACHE; illegal_1(); op_88();									} /* DB   DD		  */
OP(dd,89) { SMS_Z_CACHE; illegal_1(); op_89();									} /* DB   DD		  */
OP(dd,8a) { SMS_Z_CACHE; illegal_1(); op_8a();									} /* DB   DD		  */
OP(dd,8b) { SMS_Z_CACHE; illegal_1(); op_8b();									} /* DB   DD		  */
OP(dd,8c) { SMS_Z_CACHE; _R++; ADC(_HX); 										} /* ADC  A,HX		  */
OP(dd,8d) { SMS_Z_CACHE; _R++; ADC(_LX); 										} /* ADC  A,LX		  */
OP(dd,8e) { SMS_Z_CACHE; _R++; EAX; ADC(RM(EA)); 								} /* ADC  A,(IX+o)	  */
OP(dd,8f) { SMS_Z_CACHE; illegal_1(); op_8f();									} /* DB   DD		  */

OP(dd,90) { SMS_Z_CACHE; illegal_1(); op_90();									} /* DB   DD		  */
OP(dd,91) { SMS_Z_CACHE; illegal_1(); op_91();									} /* DB   DD		  */
OP(dd,92) { SMS_Z_CACHE; illegal_1(); op_92();									} /* DB   DD		  */
OP(dd,93) { SMS_Z_CACHE; illegal_1(); op_93();									} /* DB   DD		  */
OP(dd,94) { SMS_Z_CACHE; _R++; SUB(_HX); 										} /* SUB  HX		  */
OP(dd,95) { SMS_Z_CACHE; _R++; SUB(_LX); 										} /* SUB  LX		  */
OP(dd,96) { SMS_Z_CACHE; _R++; EAX; SUB(RM(EA)); 								} /* SUB  (IX+o)	  */
OP(dd,97) { SMS_Z_CACHE; illegal_1(); op_97();									} /* DB   DD		  */

OP(dd,98) { SMS_Z_CACHE; illegal_1(); op_98();									} /* DB   DD		  */
OP(dd,99) { SMS_Z_CACHE; illegal_1(); op_99();									} /* DB   DD		  */
OP(dd,9a) { SMS_Z_CACHE; illegal_1(); op_9a();									} /* DB   DD		  */
OP(dd,9b) { SMS_Z_CACHE; illegal_1(); op_9b();									} /* DB   DD		  */
OP(dd,9c) { SMS_Z_CACHE; _R++; SBC(_HX); 										} /* SBC  A,HX		  */
OP(dd,9d) { SMS_Z_CACHE; _R++; SBC(_LX); 										} /* SBC  A,LX		  */
OP(dd,9e) { SMS_Z_CACHE; _R++; EAX; SBC(RM(EA)); 								} /* SBC  A,(IX+o)	  */
OP(dd,9f) { SMS_Z_CACHE; illegal_1(); op_9f();									} /* DB   DD		  */

OP(dd,a0) { SMS_Z_CACHE; illegal_1(); op_a0();									} /* DB   DD		  */
OP(dd,a1) { SMS_Z_CACHE; illegal_1(); op_a1();									} /* DB   DD		  */
OP(dd,a2) { SMS_Z_CACHE; illegal_1(); op_a2();									} /* DB   DD		  */
OP(dd,a3) { SMS_Z_CACHE; illegal_1(); op_a3();									} /* DB   DD		  */
OP(dd,a4) { SMS_Z_CACHE; _R++; AND(_HX); 										} /* AND  HX		  */
OP(dd,a5) { SMS_Z_CACHE; _R++; AND(_LX); 										} /* AND  LX		  */
OP(dd,a6) { SMS_Z_CACHE; _R++; EAX; AND(RM(EA)); 								} /* AND  (IX+o)	  */
OP(dd,a7) { SMS_Z_CACHE; illegal_1(); op_a7();									} /* DB   DD		  */

OP(dd,a8) { SMS_Z_CACHE; illegal_1(); op_a8();									} /* DB   DD		  */
OP(dd,a9) { SMS_Z_CACHE; illegal_1(); op_a9();									} /* DB   DD		  */
OP(dd,aa) { SMS_Z_CACHE; illegal_1(); op_aa();									} /* DB   DD		  */
OP(dd,ab) { SMS_Z_CACHE; illegal_1(); op_ab();									} /* DB   DD		  */
OP(dd,ac) { SMS_Z_CACHE; _R++; XOR(_HX); 										} /* XOR  HX		  */
OP(dd,ad) { SMS_Z_CACHE; _R++; XOR(_LX); 										} /* XOR  LX		  */
OP(dd,ae) { SMS_Z_CACHE; _R++; EAX; XOR(RM(EA)); 								} /* XOR  (IX+o)	  */
OP(dd,af) { SMS_Z_CACHE; illegal_1(); op_af();									} /* DB   DD		  */

OP(dd,b0) { SMS_Z_CACHE; illegal_1(); op_b0();									} /* DB   DD		  */
OP(dd,b1) { SMS_Z_CACHE; illegal_1(); op_b1();									} /* DB   DD		  */
OP(dd,b2) { SMS_Z_CACHE; illegal_1(); op_b2();									} /* DB   DD		  */
OP(dd,b3) { SMS_Z_CACHE; illegal_1(); op_b3();									} /* DB   DD		  */
OP(dd,b4) { SMS_Z_CACHE; _R++; OR(_HX);											} /* OR   HX		  */
OP(dd,b5) { SMS_Z_CACHE; _R++; OR(_LX);											} /* OR   LX		  */
OP(dd,b6) { SMS_Z_CACHE; _R++; EAX; OR(RM(EA));									} /* OR   (IX+o)	  */
OP(dd,b7) { SMS_Z_CACHE; illegal_1(); op_b7();									} /* DB   DD		  */

OP(dd,b8) { SMS_Z_CACHE; illegal_1(); op_b8();									} /* DB   DD		  */
OP(dd,b9) { SMS_Z_CACHE; illegal_1(); op_b9();									} /* DB   DD		  */
OP(dd,ba) { SMS_Z_CACHE; illegal_1(); op_ba();									} /* DB   DD		  */
OP(dd,bb) { SMS_Z_CACHE; illegal_1(); op_bb();									} /* DB   DD		  */
OP(dd,bc) { SMS_Z_CACHE; _R++; CP(_HX);											} /* CP   HX		  */
OP(dd,bd) { SMS_Z_CACHE; _R++; CP(_LX);											} /* CP   LX		  */
OP(dd,be) { SMS_Z_CACHE; _R++; EAX; CP(RM(EA));									} /* CP   (IX+o)	  */
OP(dd,bf) { SMS_Z_CACHE; illegal_1(); op_bf();									} /* DB   DD		  */

OP(dd,c0) { SMS_Z_CACHE; illegal_1(); op_c0();									} /* DB   DD		  */
OP(dd,c1) { SMS_Z_CACHE; illegal_1(); op_c1();									} /* DB   DD		  */
OP(dd,c2) { SMS_Z_CACHE; illegal_1(); op_c2();									} /* DB   DD		  */
OP(dd,c3) { SMS_Z_CACHE; illegal_1(); op_c3();									} /* DB   DD		  */
OP(dd,c4) { SMS_Z_CACHE; illegal_1(); op_c4();									} /* DB   DD		  */
OP(dd,c5) { SMS_Z_CACHE; illegal_1(); op_c5();									} /* DB   DD		  */
OP(dd,c6) { SMS_Z_CACHE; illegal_1(); op_c6();									} /* DB   DD		  */
OP(dd,c7) { SMS_Z_CACHE; illegal_1(); op_c7();									}		  /* DB   DD		  */

OP(dd,c8) { SMS_Z_CACHE; illegal_1(); op_c8();									} /* DB   DD		  */
OP(dd,c9) { SMS_Z_CACHE; illegal_1(); op_c9();									} /* DB   DD		  */
OP(dd,ca) { SMS_Z_CACHE; illegal_1(); op_ca();									} /* DB   DD		  */
OP(dd,cb) { SMS_Z_CACHE; _R++; EAX; EXEC(xycb,ARG());							} /* **   DD CB xx	  */
OP(dd,cc) { SMS_Z_CACHE; illegal_1(); op_cc();									} /* DB   DD		  */
OP(dd,cd) { SMS_Z_CACHE; illegal_1(); op_cd();									} /* DB   DD		  */
OP(dd,ce) { SMS_Z_CACHE; illegal_1(); op_ce();									} /* DB   DD		  */
OP(dd,cf) { SMS_Z_CACHE; illegal_1(); op_cf();									} /* DB   DD		  */

OP(dd,d0) { SMS_Z_CACHE; illegal_1(); op_d0();									} /* DB   DD		  */
OP(dd,d1) { SMS_Z_CACHE; illegal_1(); op_d1();									} /* DB   DD		  */
OP(dd,d2) { SMS_Z_CACHE; illegal_1(); op_d2();									} /* DB   DD		  */
OP(dd,d3) { SMS_Z_CACHE; illegal_1(); op_d3();									} /* DB   DD		  */
OP(dd,d4) { SMS_Z_CACHE; illegal_1(); op_d4();									} /* DB   DD		  */
OP(dd,d5) { SMS_Z_CACHE; illegal_1(); op_d5();									} /* DB   DD		  */
OP(dd,d6) { SMS_Z_CACHE; illegal_1(); op_d6();									} /* DB   DD		  */
OP(dd,d7) { SMS_Z_CACHE; illegal_1(); op_d7();									} /* DB   DD		  */

OP(dd,d8) { SMS_Z_CACHE; illegal_1(); op_d8();									} /* DB   DD		  */
OP(dd,d9) { SMS_Z_CACHE; illegal_1(); op_d9();									} /* DB   DD		  */
OP(dd,da) { SMS_Z_CACHE; illegal_1(); op_da();									} /* DB   DD		  */
OP(dd,db) { SMS_Z_CACHE; illegal_1(); op_db();									} /* DB   DD		  */
OP(dd,dc) { SMS_Z_CACHE; illegal_1(); op_dc();									} /* DB   DD		  */
OP(dd,dd) { SMS_Z_CACHE; illegal_1(); op_dd();									} /* DB   DD		  */
OP(dd,de) { SMS_Z_CACHE; illegal_1(); op_de();									} /* DB   DD		  */
OP(dd,df) { SMS_Z_CACHE; illegal_1(); op_df();									} /* DB   DD		  */

OP(dd,e0) { SMS_Z_CACHE; illegal_1(); op_e0();									} /* DB   DD		  */
OP(dd,e1) { SMS_Z_CACHE; _R++; POP(IX);											} /* POP  IX		  */
OP(dd,e2) { SMS_Z_CACHE; illegal_1(); op_e2();									} /* DB   DD		  */
OP(dd,e3) { SMS_Z_CACHE; _R++; EXSP(IX); 										} /* EX   (SP),IX	  */
OP(dd,e4) { SMS_Z_CACHE; illegal_1(); op_e4();									} /* DB   DD		  */
OP(dd,e5) { SMS_Z_CACHE; _R++; PUSH( IX );										} /* PUSH IX		  */
OP(dd,e6) { SMS_Z_CACHE; illegal_1(); op_e6();									} /* DB   DD		  */
OP(dd,e7) { SMS_Z_CACHE; illegal_1(); op_e7();									} /* DB   DD		  */

OP(dd,e8) { SMS_Z_CACHE; illegal_1(); op_e8();									} /* DB   DD		  */
OP(dd,e9) { SMS_Z_CACHE; _R++; _PC = _IX; change_pc16(_PCD); 					} /* JP   (IX)		  */
OP(dd,ea) { SMS_Z_CACHE; illegal_1(); op_ea();									} /* DB   DD		  */
OP(dd,eb) { SMS_Z_CACHE; illegal_1(); op_eb();									} /* DB   DD		  */
OP(dd,ec) { SMS_Z_CACHE; illegal_1(); op_ec();									} /* DB   DD		  */
OP(dd,ed) { SMS_Z_CACHE; illegal_1(); op_ed();									} /* DB   DD		  */
OP(dd,ee) { SMS_Z_CACHE; illegal_1(); op_ee();									} /* DB   DD		  */
OP(dd,ef) { SMS_Z_CACHE; illegal_1(); op_ef();									} /* DB   DD		  */

OP(dd,f0) { SMS_Z_CACHE; illegal_1(); op_f0();									} /* DB   DD		  */
OP(dd,f1) { SMS_Z_CACHE; illegal_1(); op_f1();									} /* DB   DD		  */
OP(dd,f2) { SMS_Z_CACHE; illegal_1(); op_f2();									} /* DB   DD		  */
OP(dd,f3) { SMS_Z_CACHE; illegal_1(); op_f3();									} /* DB   DD		  */
OP(dd,f4) { SMS_Z_CACHE; illegal_1(); op_f4();									} /* DB   DD		  */
OP(dd,f5) { SMS_Z_CACHE; illegal_1(); op_f5();									} /* DB   DD		  */
OP(dd,f6) { SMS_Z_CACHE; illegal_1(); op_f6();									} /* DB   DD		  */
OP(dd,f7) { SMS_Z_CACHE; illegal_1(); op_f7();									} /* DB   DD		  */

OP(dd,f8) { SMS_Z_CACHE; illegal_1(); op_f8();									} /* DB   DD		  */
OP(dd,f9) { SMS_Z_CACHE; _R++; _SP = _IX;										} /* LD   SP,IX 	  */
OP(dd,fa) { SMS_Z_CACHE; illegal_1(); op_fa();									} /* DB   DD		  */
OP(dd,fb) { SMS_Z_CACHE; illegal_1(); op_fb();									} /* DB   DD		  */
OP(dd,fc) { SMS_Z_CACHE; illegal_1(); op_fc();									} /* DB   DD		  */
OP(dd,fd) { SMS_Z_CACHE; illegal_1(); op_fd();									} /* DB   DD		  */
OP(dd,fe) { SMS_Z_CACHE; illegal_1(); op_fe();									} /* DB   DD		  */
OP(dd,ff) { SMS_Z_CACHE; illegal_1(); op_ff();									} /* DB   DD		  */

/**********************************************************
 * IY register related opcodes (FD prefix)
 **********************************************************/
OP(fd,00) { SMS_Z_CACHE; illegal_1(); op_00();									} /* DB   FD		  */
OP(fd,01) { SMS_Z_CACHE; illegal_1(); op_01();									} /* DB   FD		  */
OP(fd,02) { SMS_Z_CACHE; illegal_1(); op_02();									} /* DB   FD		  */
OP(fd,03) { SMS_Z_CACHE; illegal_1(); op_03();									} /* DB   FD		  */
OP(fd,04) { SMS_Z_CACHE; illegal_1(); op_04();									} /* DB   FD		  */
OP(fd,05) { SMS_Z_CACHE; illegal_1(); op_05();									} /* DB   FD		  */
OP(fd,06) { SMS_Z_CACHE; illegal_1(); op_06();									} /* DB   FD		  */
OP(fd,07) { SMS_Z_CACHE; illegal_1(); op_07();									} /* DB   FD		  */

OP(fd,08) { SMS_Z_CACHE; illegal_1(); op_08();									} /* DB   FD		  */
OP(fd,09) { SMS_Z_CACHE; _R++; ADD16(IY,BC); 									} /* ADD  IY,BC 	  */
OP(fd,0a) { SMS_Z_CACHE; illegal_1(); op_0a();									} /* DB   FD		  */
OP(fd,0b) { SMS_Z_CACHE; illegal_1(); op_0b();									} /* DB   FD		  */
OP(fd,0c) { SMS_Z_CACHE; illegal_1(); op_0c();									} /* DB   FD		  */
OP(fd,0d) { SMS_Z_CACHE; illegal_1(); op_0d();									} /* DB   FD		  */
OP(fd,0e) { SMS_Z_CACHE; illegal_1(); op_0e();									} /* DB   FD		  */
OP(fd,0f) { SMS_Z_CACHE; illegal_1(); op_0f();									} /* DB   FD		  */

OP(fd,10) { SMS_Z_CACHE; illegal_1(); op_10();									} /* DB   FD		  */
OP(fd,11) { SMS_Z_CACHE; illegal_1(); op_11();									} /* DB   FD		  */
OP(fd,12) { SMS_Z_CACHE; illegal_1(); op_12();									} /* DB   FD		  */
OP(fd,13) { SMS_Z_CACHE; illegal_1(); op_13();									} /* DB   FD		  */
OP(fd,14) { SMS_Z_CACHE; illegal_1(); op_14();									} /* DB   FD		  */
OP(fd,15) { SMS_Z_CACHE; illegal_1(); op_15();									} /* DB   FD		  */
OP(fd,16) { SMS_Z_CACHE; illegal_1(); op_16();									} /* DB   FD		  */
OP(fd,17) { SMS_Z_CACHE; illegal_1(); op_17();									} /* DB   FD		  */

OP(fd,18) { SMS_Z_CACHE; illegal_1(); op_18();									} /* DB   FD		  */
OP(fd,19) { SMS_Z_CACHE; _R++; ADD16(IY,DE); 									} /* ADD  IY,DE 	  */
OP(fd,1a) { SMS_Z_CACHE; illegal_1(); op_1a();									} /* DB   FD		  */
OP(fd,1b) { SMS_Z_CACHE; illegal_1(); op_1b();									} /* DB   FD		  */
OP(fd,1c) { SMS_Z_CACHE; illegal_1(); op_1c();									} /* DB   FD		  */
OP(fd,1d) { SMS_Z_CACHE; illegal_1(); op_1d();									} /* DB   FD		  */
OP(fd,1e) { SMS_Z_CACHE; illegal_1(); op_1e();									} /* DB   FD		  */
OP(fd,1f) { SMS_Z_CACHE; illegal_1(); op_1f();									} /* DB   FD		  */

OP(fd,20) { SMS_Z_CACHE; illegal_1(); op_20();									} /* DB   FD		  */
OP(fd,21) { SMS_Z_CACHE; _R++; _IY = ARG16();									} /* LD   IY,w		  */
OP(fd,22) { SMS_Z_CACHE; _R++; EA = ARG16(); WM16( EA, &Z80.IY );				} /* LD   (w),IY	  */
OP(fd,23) { SMS_Z_CACHE; _R++; _IY++;											} /* INC  IY		  */
OP(fd,24) { SMS_Z_CACHE; _R++; _HY = INC(_HY);									} /* INC  HY		  */
OP(fd,25) { SMS_Z_CACHE; _R++; _HY = DEC(_HY);									} /* DEC  HY		  */
OP(fd,26) { SMS_Z_CACHE; _R++; _HY = ARG();										} /* LD   HY,n		  */
OP(fd,27) { SMS_Z_CACHE; illegal_1(); op_27();									} /* DB   FD		  */

OP(fd,28) { SMS_Z_CACHE; illegal_1(); op_28();									} /* DB   FD		  */
OP(fd,29) { SMS_Z_CACHE; _R++; ADD16(IY,IY); 									} /* ADD  IY,IY 	  */
OP(fd,2a) { SMS_Z_CACHE; _R++; EA = ARG16(); RM16( EA, &Z80.IY );				} /* LD   IY,(w)	  */
OP(fd,2b) { SMS_Z_CACHE; _R++; _IY--;											} /* DEC  IY		  */
OP(fd,2c) { SMS_Z_CACHE; _R++; _LY = INC(_LY);									} /* INC  LY		  */
OP(fd,2d) { SMS_Z_CACHE; _R++; _LY = DEC(_LY);									} /* DEC  LY		  */
OP(fd,2e) { SMS_Z_CACHE; _R++; _LY = ARG();										} /* LD   LY,n		  */
OP(fd,2f) { SMS_Z_CACHE; illegal_1(); op_2f();									} /* DB   FD		  */

OP(fd,30) { SMS_Z_CACHE; illegal_1(); op_30();									} /* DB   FD		  */
OP(fd,31) { SMS_Z_CACHE; illegal_1(); op_31();									} /* DB   FD		  */
OP(fd,32) { SMS_Z_CACHE; illegal_1(); op_32();									} /* DB   FD		  */
OP(fd,33) { SMS_Z_CACHE; illegal_1(); op_33();									} /* DB   FD		  */
OP(fd,34) { SMS_Z_CACHE; _R++; EAY; WM( EA, INC(RM(EA)) );						} /* INC  (IY+o)	  */
OP(fd,35) { SMS_Z_CACHE; _R++; EAY; WM( EA, DEC(RM(EA)) );						} /* DEC  (IY+o)	  */
OP(fd,36) { SMS_Z_CACHE; _R++; EAY; WM( EA, ARG() ); 							} /* LD   (IY+o),n	  */
OP(fd,37) { SMS_Z_CACHE; illegal_1(); op_37();									} /* DB   FD		  */

OP(fd,38) { SMS_Z_CACHE; illegal_1(); op_38();									} /* DB   FD		  */
OP(fd,39) { SMS_Z_CACHE; _R++; ADD16(IY,SP); 									} /* ADD  IY,SP 	  */
OP(fd,3a) { SMS_Z_CACHE; illegal_1(); op_3a();									} /* DB   FD		  */
OP(fd,3b) { SMS_Z_CACHE; illegal_1(); op_3b();									} /* DB   FD		  */
OP(fd,3c) { SMS_Z_CACHE; illegal_1(); op_3c();									} /* DB   FD		  */
OP(fd,3d) { SMS_Z_CACHE; illegal_1(); op_3d();									} /* DB   FD		  */
OP(fd,3e) { SMS_Z_CACHE; illegal_1(); op_3e();									} /* DB   FD		  */
OP(fd,3f) { SMS_Z_CACHE; illegal_1(); op_3f();									} /* DB   FD		  */

OP(fd,40) { SMS_Z_CACHE; illegal_1(); op_40();									} /* DB   FD		  */
OP(fd,41) { SMS_Z_CACHE; illegal_1(); op_41();									} /* DB   FD		  */
OP(fd,42) { SMS_Z_CACHE; illegal_1(); op_42();									} /* DB   FD		  */
OP(fd,43) { SMS_Z_CACHE; illegal_1(); op_43();									} /* DB   FD		  */
OP(fd,44) { SMS_Z_CACHE; _R++; _B = _HY; 										} /* LD   B,HY		  */
OP(fd,45) { SMS_Z_CACHE; _R++; _B = _LY; 										} /* LD   B,LY		  */
OP(fd,46) { SMS_Z_CACHE; _R++; EAY; _B = RM(EA); 								} /* LD   B,(IY+o)	  */
OP(fd,47) { SMS_Z_CACHE; illegal_1(); op_47();									} /* DB   FD		  */

OP(fd,48) { SMS_Z_CACHE; illegal_1(); op_48();									} /* DB   FD		  */
OP(fd,49) { SMS_Z_CACHE; illegal_1(); op_49();									} /* DB   FD		  */
OP(fd,4a) { SMS_Z_CACHE; illegal_1(); op_4a();									} /* DB   FD		  */
OP(fd,4b) { SMS_Z_CACHE; illegal_1(); op_4b();									} /* DB   FD		  */
OP(fd,4c) { SMS_Z_CACHE; _R++; _C = _HY; 										} /* LD   C,HY		  */
OP(fd,4d) { SMS_Z_CACHE; _R++; _C = _LY; 										} /* LD   C,LY		  */
OP(fd,4e) { SMS_Z_CACHE; _R++; EAY; _C = RM(EA); 								} /* LD   C,(IY+o)	  */
OP(fd,4f) { SMS_Z_CACHE; illegal_1(); op_4f();									} /* DB   FD		  */

OP(fd,50) { SMS_Z_CACHE; illegal_1(); op_50();									} /* DB   FD		  */
OP(fd,51) { SMS_Z_CACHE; illegal_1(); op_51();									} /* DB   FD		  */
OP(fd,52) { SMS_Z_CACHE; illegal_1(); op_52();									} /* DB   FD		  */
OP(fd,53) { SMS_Z_CACHE; illegal_1(); op_53();									} /* DB   FD		  */
OP(fd,54) { SMS_Z_CACHE; _R++; _D = _HY; 										} /* LD   D,HY		  */
OP(fd,55) { SMS_Z_CACHE; _R++; _D = _LY; 										} /* LD   D,LY		  */
OP(fd,56) { SMS_Z_CACHE; _R++; EAY; _D = RM(EA); 								} /* LD   D,(IY+o)	  */
OP(fd,57) { SMS_Z_CACHE; illegal_1(); op_57();									} /* DB   FD		  */

OP(fd,58) { SMS_Z_CACHE; illegal_1(); op_58();									} /* DB   FD		  */
OP(fd,59) { SMS_Z_CACHE; illegal_1(); op_59();									} /* DB   FD		  */
OP(fd,5a) { SMS_Z_CACHE; illegal_1(); op_5a();									} /* DB   FD		  */
OP(fd,5b) { SMS_Z_CACHE; illegal_1(); op_5b();									} /* DB   FD		  */
OP(fd,5c) { SMS_Z_CACHE; _R++; _E = _HY; 										} /* LD   E,HY		  */
OP(fd,5d) { SMS_Z_CACHE; _R++; _E = _LY; 										} /* LD   E,LY		  */
OP(fd,5e) { SMS_Z_CACHE; _R++; EAY; _E = RM(EA); 								} /* LD   E,(IY+o)	  */
OP(fd,5f) { SMS_Z_CACHE; illegal_1(); op_5f();									} /* DB   FD		  */

OP(fd,60) { SMS_Z_CACHE; _R++; _HY = _B; 										} /* LD   HY,B		  */
OP(fd,61) { SMS_Z_CACHE; _R++; _HY = _C; 										} /* LD   HY,C		  */
OP(fd,62) { SMS_Z_CACHE; _R++; _HY = _D; 										} /* LD   HY,D		  */
OP(fd,63) { SMS_Z_CACHE; _R++; _HY = _E; 										} /* LD   HY,E		  */
OP(fd,64) { SMS_Z_CACHE; _R++;													} /* LD   HY,HY 	  */
OP(fd,65) { SMS_Z_CACHE; _R++; _HY = _LY;										} /* LD   HY,LY 	  */
OP(fd,66) { SMS_Z_CACHE; _R++; EAY; _H = RM(EA); 								} /* LD   H,(IY+o)	  */
OP(fd,67) { SMS_Z_CACHE; _R++; _HY = _A; 										} /* LD   HY,A		  */

OP(fd,68) { SMS_Z_CACHE; _R++; _LY = _B; 										} /* LD   LY,B		  */
OP(fd,69) { SMS_Z_CACHE; _R++; _LY = _C; 										} /* LD   LY,C		  */
OP(fd,6a) { SMS_Z_CACHE; _R++; _LY = _D; 										} /* LD   LY,D		  */
OP(fd,6b) { SMS_Z_CACHE; _R++; _LY = _E; 										} /* LD   LY,E		  */
OP(fd,6c) { SMS_Z_CACHE; _R++; _LY = _HY;										} /* LD   LY,HY 	  */
OP(fd,6d) { SMS_Z_CACHE; _R++;													} /* LD   LY,LY 	  */
OP(fd,6e) { SMS_Z_CACHE; _R++; EAY; _L = RM(EA); 								} /* LD   L,(IY+o)	  */
OP(fd,6f) { SMS_Z_CACHE; _R++; _LY = _A; 										} /* LD   LY,A		  */

OP(fd,70) { SMS_Z_CACHE; _R++; EAY; WM( EA, _B );								} /* LD   (IY+o),B	  */
OP(fd,71) { SMS_Z_CACHE; _R++; EAY; WM( EA, _C );								} /* LD   (IY+o),C	  */
OP(fd,72) { SMS_Z_CACHE; _R++; EAY; WM( EA, _D );								} /* LD   (IY+o),D	  */
OP(fd,73) { SMS_Z_CACHE; _R++; EAY; WM( EA, _E );								} /* LD   (IY+o),E	  */
OP(fd,74) { SMS_Z_CACHE; _R++; EAY; WM( EA, _H );								} /* LD   (IY+o),H	  */
OP(fd,75) { SMS_Z_CACHE; _R++; EAY; WM( EA, _L );								} /* LD   (IY+o),L	  */
OP(fd,76) { SMS_Z_CACHE; illegal_1(); op_76();									}		  /* DB   FD		  */
OP(fd,77) { SMS_Z_CACHE; _R++; EAY; WM( EA, _A );								} /* LD   (IY+o),A	  */

OP(fd,78) { SMS_Z_CACHE; illegal_1(); op_78();									} /* DB   FD		  */
OP(fd,79) { SMS_Z_CACHE; illegal_1(); op_79();									} /* DB   FD		  */
OP(fd,7a) { SMS_Z_CACHE; illegal_1(); op_7a();									} /* DB   FD		  */
OP(fd,7b) { SMS_Z_CACHE; illegal_1(); op_7b();									} /* DB   FD		  */
OP(fd,7c) { SMS_Z_CACHE; _R++; _A = _HY; 										} /* LD   A,HY		  */
OP(fd,7d) { SMS_Z_CACHE; _R++; _A = _LY; 										} /* LD   A,LY		  */
OP(fd,7e) { SMS_Z_CACHE; _R++; EAY; _A = RM(EA); 								} /* LD   A,(IY+o)	  */
OP(fd,7f) { SMS_Z_CACHE; illegal_1(); op_7f();									} /* DB   FD		  */

OP(fd,80) { SMS_Z_CACHE; illegal_1(); op_80();									} /* DB   FD		  */
OP(fd,81) { SMS_Z_CACHE; illegal_1(); op_81();									} /* DB   FD		  */
OP(fd,82) { SMS_Z_CACHE; illegal_1(); op_82();									} /* DB   FD		  */
OP(fd,83) { SMS_Z_CACHE; illegal_1(); op_83();									} /* DB   FD		  */
OP(fd,84) { SMS_Z_CACHE; _R++; ADD(_HY); 										} /* ADD  A,HY		  */
OP(fd,85) { SMS_Z_CACHE; _R++; ADD(_LY); 										} /* ADD  A,LY		  */
OP(fd,86) { SMS_Z_CACHE; _R++; EAY; ADD(RM(EA)); 								} /* ADD  A,(IY+o)	  */
OP(fd,87) { SMS_Z_CACHE; illegal_1(); op_87();									} /* DB   FD		  */

OP(fd,88) { SMS_Z_CACHE; illegal_1(); op_88();									} /* DB   FD		  */
OP(fd,89) { SMS_Z_CACHE; illegal_1(); op_89();									} /* DB   FD		  */
OP(fd,8a) { SMS_Z_CACHE; illegal_1(); op_8a();									} /* DB   FD		  */
OP(fd,8b) { SMS_Z_CACHE; illegal_1(); op_8b();									} /* DB   FD		  */
OP(fd,8c) { SMS_Z_CACHE; _R++; ADC(_HY); 										} /* ADC  A,HY		  */
OP(fd,8d) { SMS_Z_CACHE; _R++; ADC(_LY); 										} /* ADC  A,LY		  */
OP(fd,8e) { SMS_Z_CACHE; _R++; EAY; ADC(RM(EA)); 								} /* ADC  A,(IY+o)	  */
OP(fd,8f) { SMS_Z_CACHE; illegal_1(); op_8f();									} /* DB   FD		  */

OP(fd,90) { SMS_Z_CACHE; illegal_1(); op_90();									} /* DB   FD		  */
OP(fd,91) { SMS_Z_CACHE; illegal_1(); op_91();									} /* DB   FD		  */
OP(fd,92) { SMS_Z_CACHE; illegal_1(); op_92();									} /* DB   FD		  */
OP(fd,93) { SMS_Z_CACHE; illegal_1(); op_93();									} /* DB   FD		  */
OP(fd,94) { SMS_Z_CACHE; _R++; SUB(_HY); 										} /* SUB  HY		  */
OP(fd,95) { SMS_Z_CACHE; _R++; SUB(_LY); 										} /* SUB  LY		  */
OP(fd,96) { SMS_Z_CACHE; _R++; EAY; SUB(RM(EA)); 								} /* SUB  (IY+o)	  */
OP(fd,97) { SMS_Z_CACHE; illegal_1(); op_97();									} /* DB   FD		  */

OP(fd,98) { SMS_Z_CACHE; illegal_1(); op_98();									} /* DB   FD		  */
OP(fd,99) { SMS_Z_CACHE; illegal_1(); op_99();									} /* DB   FD		  */
OP(fd,9a) { SMS_Z_CACHE; illegal_1(); op_9a();									} /* DB   FD		  */
OP(fd,9b) { SMS_Z_CACHE; illegal_1(); op_9b();									} /* DB   FD		  */
OP(fd,9c) { SMS_Z_CACHE; _R++; SBC(_HY); 										} /* SBC  A,HY		  */
OP(fd,9d) { SMS_Z_CACHE; _R++; SBC(_LY); 										} /* SBC  A,LY		  */
OP(fd,9e) { SMS_Z_CACHE; _R++; EAY; SBC(RM(EA)); 								} /* SBC  A,(IY+o)	  */
OP(fd,9f) { SMS_Z_CACHE; illegal_1(); op_9f();									} /* DB   FD		  */

OP(fd,a0) { SMS_Z_CACHE; illegal_1(); op_a0();									} /* DB   FD		  */
OP(fd,a1) { SMS_Z_CACHE; illegal_1(); op_a1();									} /* DB   FD		  */
OP(fd,a2) { SMS_Z_CACHE; illegal_1(); op_a2();									} /* DB   FD		  */
OP(fd,a3) { SMS_Z_CACHE; illegal_1(); op_a3();									} /* DB   FD		  */
OP(fd,a4) { SMS_Z_CACHE; _R++; AND(_HY); 										} /* AND  HY		  */
OP(fd,a5) { SMS_Z_CACHE; _R++; AND(_LY); 										} /* AND  LY		  */
OP(fd,a6) { SMS_Z_CACHE; _R++; EAY; AND(RM(EA)); 								} /* AND  (IY+o)	  */
OP(fd,a7) { SMS_Z_CACHE; illegal_1(); op_a7();									} /* DB   FD		  */

OP(fd,a8) { SMS_Z_CACHE; illegal_1(); op_a8();									} /* DB   FD		  */
OP(fd,a9) { SMS_Z_CACHE; illegal_1(); op_a9();									} /* DB   FD		  */
OP(fd,aa) { SMS_Z_CACHE; illegal_1(); op_aa();									} /* DB   FD		  */
OP(fd,ab) { SMS_Z_CACHE; illegal_1(); op_ab();									} /* DB   FD		  */
OP(fd,ac) { SMS_Z_CACHE; _R++; XOR(_HY); 										} /* XOR  HY		  */
OP(fd,ad) { SMS_Z_CACHE; _R++; XOR(_LY); 										} /* XOR  LY		  */
OP(fd,ae) { SMS_Z_CACHE; _R++; EAY; XOR(RM(EA)); 								} /* XOR  (IY+o)	  */
OP(fd,af) { SMS_Z_CACHE; illegal_1(); op_af();									} /* DB   FD		  */

OP(fd,b0) { SMS_Z_CACHE; illegal_1(); op_b0();									} /* DB   FD		  */
OP(fd,b1) { SMS_Z_CACHE; illegal_1(); op_b1();									} /* DB   FD		  */
OP(fd,b2) { SMS_Z_CACHE; illegal_1(); op_b2();									} /* DB   FD		  */
OP(fd,b3) { SMS_Z_CACHE; illegal_1(); op_b3();									} /* DB   FD		  */
OP(fd,b4) { SMS_Z_CACHE; _R++; OR(_HY);											} /* OR   HY		  */
OP(fd,b5) { SMS_Z_CACHE; _R++; OR(_LY);											} /* OR   LY		  */
OP(fd,b6) { SMS_Z_CACHE; _R++; EAY; OR(RM(EA));									} /* OR   (IY+o)	  */
OP(fd,b7) { SMS_Z_CACHE; illegal_1(); op_b7();									} /* DB   FD		  */

OP(fd,b8) { SMS_Z_CACHE; illegal_1(); op_b8();									} /* DB   FD		  */
OP(fd,b9) { SMS_Z_CACHE; illegal_1(); op_b9();									} /* DB   FD		  */
OP(fd,ba) { SMS_Z_CACHE; illegal_1(); op_ba();									} /* DB   FD		  */
OP(fd,bb) { SMS_Z_CACHE; illegal_1(); op_bb();									} /* DB   FD		  */
OP(fd,bc) { SMS_Z_CACHE; _R++; CP(_HY);											} /* CP   HY		  */
OP(fd,bd) { SMS_Z_CACHE; _R++; CP(_LY);											} /* CP   LY		  */
OP(fd,be) { SMS_Z_CACHE; _R++; EAY; CP(RM(EA));									} /* CP   (IY+o)	  */
OP(fd,bf) { SMS_Z_CACHE; illegal_1(); op_bf();									} /* DB   FD		  */

OP(fd,c0) { SMS_Z_CACHE; illegal_1(); op_c0();									} /* DB   FD		  */
OP(fd,c1) { SMS_Z_CACHE; illegal_1(); op_c1();									} /* DB   FD		  */
OP(fd,c2) { SMS_Z_CACHE; illegal_1(); op_c2();									} /* DB   FD		  */
OP(fd,c3) { SMS_Z_CACHE; illegal_1(); op_c3();									} /* DB   FD		  */
OP(fd,c4) { SMS_Z_CACHE; illegal_1(); op_c4();									} /* DB   FD		  */
OP(fd,c5) { SMS_Z_CACHE; illegal_1(); op_c5();									} /* DB   FD		  */
OP(fd,c6) { SMS_Z_CACHE; illegal_1(); op_c6();									} /* DB   FD		  */
OP(fd,c7) { SMS_Z_CACHE; illegal_1(); op_c7();									} /* DB   FD		  */

OP(fd,c8) { SMS_Z_CACHE; illegal_1(); op_c8();									} /* DB   FD		  */
OP(fd,c9) { SMS_Z_CACHE; illegal_1(); op_c9();									} /* DB   FD		  */
OP(fd,ca) { SMS_Z_CACHE; illegal_1(); op_ca();									} /* DB   FD		  */
OP(fd,cb) { SMS_Z_CACHE; _R++; EAY; EXEC(xycb,ARG());							} /* **   FD CB xx	  */
OP(fd,cc) { SMS_Z_CACHE; illegal_1(); op_cc();									} /* DB   FD		  */
OP(fd,cd) { SMS_Z_CACHE; illegal_1(); op_cd();									} /* DB   FD		  */
OP(fd,ce) { SMS_Z_CACHE; illegal_1(); op_ce();									} /* DB   FD		  */
OP(fd,cf) { SMS_Z_CACHE; illegal_1(); op_cf();									} /* DB   FD		  */

OP(fd,d0) { SMS_Z_CACHE; illegal_1(); op_d0();									} /* DB   FD		  */
OP(fd,d1) { SMS_Z_CACHE; illegal_1(); op_d1();									} /* DB   FD		  */
OP(fd,d2) { SMS_Z_CACHE; illegal_1(); op_d2();									} /* DB   FD		  */
OP(fd,d3) { SMS_Z_CACHE; illegal_1(); op_d3();									} /* DB   FD		  */
OP(fd,d4) { SMS_Z_CACHE; illegal_1(); op_d4();									} /* DB   FD		  */
OP(fd,d5) { SMS_Z_CACHE; illegal_1(); op_d5();									} /* DB   FD		  */
OP(fd,d6) { SMS_Z_CACHE; illegal_1(); op_d6();									} /* DB   FD		  */
OP(fd,d7) { SMS_Z_CACHE; illegal_1(); op_d7();									} /* DB   FD		  */

OP(fd,d8) { SMS_Z_CACHE; illegal_1(); op_d8();									} /* DB   FD		  */
OP(fd,d9) { SMS_Z_CACHE; illegal_1(); op_d9();									} /* DB   FD		  */
OP(fd,da) { SMS_Z_CACHE; illegal_1(); op_da();									} /* DB   FD		  */
OP(fd,db) { SMS_Z_CACHE; illegal_1(); op_db();									} /* DB   FD		  */
OP(fd,dc) { SMS_Z_CACHE; illegal_1(); op_dc();									} /* DB   FD		  */
OP(fd,dd) { SMS_Z_CACHE; illegal_1(); op_dd();									} /* DB   FD		  */
OP(fd,de) { SMS_Z_CACHE; illegal_1(); op_de();									} /* DB   FD		  */
OP(fd,df) { SMS_Z_CACHE; illegal_1(); op_df();									} /* DB   FD		  */

OP(fd,e0) { SMS_Z_CACHE; illegal_1(); op_e0();									} /* DB   FD		  */
OP(fd,e1) { SMS_Z_CACHE; _R++; POP(IY);											} /* POP  IY		  */
OP(fd,e2) { SMS_Z_CACHE; illegal_1(); op_e2();									} /* DB   FD		  */
OP(fd,e3) { SMS_Z_CACHE; _R++; EXSP(IY); 										} /* EX   (SP),IY	  */
OP(fd,e4) { SMS_Z_CACHE; illegal_1(); op_e4();									} /* DB   FD		  */
OP(fd,e5) { SMS_Z_CACHE; _R++; PUSH( IY );										} /* PUSH IY		  */
OP(fd,e6) { SMS_Z_CACHE; illegal_1(); op_e6();									} /* DB   FD		  */
OP(fd,e7) { SMS_Z_CACHE; illegal_1(); op_e7();									} /* DB   FD		  */

OP(fd,e8) { SMS_Z_CACHE; illegal_1(); op_e8();									} /* DB   FD		  */
OP(fd,e9) { SMS_Z_CACHE; _R++; _PC = _IY; change_pc16(_PCD); 					} /* JP   (IY)		  */
OP(fd,ea) { SMS_Z_CACHE; illegal_1(); op_ea();									} /* DB   FD		  */
OP(fd,eb) { SMS_Z_CACHE; illegal_1(); op_eb();									} /* DB   FD		  */
OP(fd,ec) { SMS_Z_CACHE; illegal_1(); op_ec();									} /* DB   FD		  */
OP(fd,ed) { SMS_Z_CACHE; illegal_1(); op_ed();									} /* DB   FD		  */
OP(fd,ee) { SMS_Z_CACHE; illegal_1(); op_ee();									} /* DB   FD		  */
OP(fd,ef) { SMS_Z_CACHE; illegal_1(); op_ef();									} /* DB   FD		  */

OP(fd,f0) { SMS_Z_CACHE; illegal_1(); op_f0();									} /* DB   FD		  */
OP(fd,f1) { SMS_Z_CACHE; illegal_1(); op_f1();									} /* DB   FD		  */
OP(fd,f2) { SMS_Z_CACHE; illegal_1(); op_f2();									} /* DB   FD		  */
OP(fd,f3) { SMS_Z_CACHE; illegal_1(); op_f3();									} /* DB   FD		  */
OP(fd,f4) { SMS_Z_CACHE; illegal_1(); op_f4();									} /* DB   FD		  */
OP(fd,f5) { SMS_Z_CACHE; illegal_1(); op_f5();									} /* DB   FD		  */
OP(fd,f6) { SMS_Z_CACHE; illegal_1(); op_f6();									} /* DB   FD		  */
OP(fd,f7) { SMS_Z_CACHE; illegal_1(); op_f7();									} /* DB   FD		  */

OP(fd,f8) { SMS_Z_CACHE; illegal_1(); op_f8();									} /* DB   FD		  */
OP(fd,f9) { SMS_Z_CACHE; _R++; _SP = _IY;										} /* LD   SP,IY 	  */
OP(fd,fa) { SMS_Z_CACHE; illegal_1(); op_fa();									} /* DB   FD		  */
OP(fd,fb) { SMS_Z_CACHE; illegal_1(); op_fb();									} /* DB   FD		  */
OP(fd,fc) { SMS_Z_CACHE; illegal_1(); op_fc();									} /* DB   FD		  */
OP(fd,fd) { SMS_Z_CACHE; illegal_1(); op_fd();									} /* DB   FD		  */
OP(fd,fe) { SMS_Z_CACHE; illegal_1(); op_fe();									} /* DB   FD		  */
OP(fd,ff) { SMS_Z_CACHE; illegal_1(); op_ff();									} /* DB   FD		  */

OP(illegal,2)
{
}

/**********************************************************
 * special opcodes (ED prefix)
 **********************************************************/
OP(ed,00) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,01) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,02) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,03) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,04) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,05) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,06) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,07) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

OP(ed,08) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,09) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,0a) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,0b) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,0c) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,0d) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,0e) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,0f) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

OP(ed,10) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,11) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,12) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,13) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,14) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,15) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,16) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,17) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

OP(ed,18) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,19) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,1a) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,1b) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,1c) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,1d) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,1e) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,1f) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

OP(ed,20) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,21) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,22) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,23) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,24) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,25) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,26) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,27) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

OP(ed,28) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,29) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,2a) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,2b) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,2c) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,2d) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,2e) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,2f) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

OP(ed,30) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,31) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,32) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,33) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,34) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,35) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,36) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,37) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

OP(ed,38) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,39) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,3a) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,3b) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,3c) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,3d) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,3e) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,3f) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

OP(ed,40) { SMS_Z_CACHE; _B = IN(_BC); _F = (_F & CF) | SZP[_B]; 				} /* IN   B,(C) 	  */
OP(ed,41) { SMS_Z_CACHE; OUT(_BC,_B);											} /* OUT  (C),B 	  */
OP(ed,42) { SMS_Z_CACHE; SBC16( BC );											} /* SBC  HL,BC 	  */
OP(ed,43) { SMS_Z_CACHE; EA = ARG16(); WM16( EA, &Z80.BC );						} /* LD   (w),BC	  */
OP(ed,44) { SMS_Z_CACHE; NEG;													} /* NEG			  */
OP(ed,45) { SMS_Z_CACHE; RETN;													} /* RETN;			  */
OP(ed,46) { SMS_Z_CACHE; _IM = 0;												} /* IM   0 		  */
OP(ed,47) { SMS_Z_CACHE; LD_I_A; 												} /* LD   I,A		  */

OP(ed,48) { SMS_Z_CACHE; _C = IN(_BC); _F = (_F & CF) | SZP[_C]; 				} /* IN   C,(C) 	  */
OP(ed,49) { SMS_Z_CACHE; OUT(_BC,_C);											} /* OUT  (C),C 	  */
OP(ed,4a) { SMS_Z_CACHE; ADC16( BC );											} /* ADC  HL,BC 	  */
OP(ed,4b) { SMS_Z_CACHE; EA = ARG16(); RM16( EA, &Z80.BC );						} /* LD   BC,(w)	  */
OP(ed,4c) { SMS_Z_CACHE; NEG;													} /* NEG			  */
OP(ed,4d) { SMS_Z_CACHE; RETI;													} /* RETI			  */
OP(ed,4e) { SMS_Z_CACHE; _IM = 0;												} /* IM   0 		  */
OP(ed,4f) { SMS_Z_CACHE; LD_R_A; 												} /* LD   R,A		  */

OP(ed,50) { SMS_Z_CACHE; _D = IN(_BC); _F = (_F & CF) | SZP[_D]; 				} /* IN   D,(C) 	  */
OP(ed,51) { SMS_Z_CACHE; OUT(_BC,_D);											} /* OUT  (C),D 	  */
OP(ed,52) { SMS_Z_CACHE; SBC16( DE );											} /* SBC  HL,DE 	  */
OP(ed,53) { SMS_Z_CACHE; EA = ARG16(); WM16( EA, &Z80.DE );						} /* LD   (w),DE	  */
OP(ed,54) { SMS_Z_CACHE; NEG;													} /* NEG			  */
OP(ed,55) { SMS_Z_CACHE; RETN;													} /* RETN;			  */
OP(ed,56) { SMS_Z_CACHE; _IM = 1;												} /* IM   1 		  */
OP(ed,57) { SMS_Z_CACHE; LD_A_I; 												} /* LD   A,I		  */

OP(ed,58) { SMS_Z_CACHE; _E = IN(_BC); _F = (_F & CF) | SZP[_E]; 				} /* IN   E,(C) 	  */
OP(ed,59) { SMS_Z_CACHE; OUT(_BC,_E);											} /* OUT  (C),E 	  */
OP(ed,5a) { SMS_Z_CACHE; ADC16( DE );											} /* ADC  HL,DE 	  */
OP(ed,5b) { SMS_Z_CACHE; EA = ARG16(); RM16( EA, &Z80.DE );						} /* LD   DE,(w)	  */
OP(ed,5c) { SMS_Z_CACHE; NEG;													} /* NEG			  */
OP(ed,5d) { SMS_Z_CACHE; RETI;													} /* RETI			  */
OP(ed,5e) { SMS_Z_CACHE; _IM = 2;												} /* IM   2 		  */
OP(ed,5f) { SMS_Z_CACHE; LD_A_R; 												} /* LD   A,R		  */

OP(ed,60) { SMS_Z_CACHE; _H = IN(_BC); _F = (_F & CF) | SZP[_H]; 				} /* IN   H,(C) 	  */
OP(ed,61) { SMS_Z_CACHE; OUT(_BC,_H);											} /* OUT  (C),H 	  */
OP(ed,62) { SMS_Z_CACHE; SBC16( HL );											} /* SBC  HL,HL 	  */
OP(ed,63) { SMS_Z_CACHE; EA = ARG16(); WM16( EA, &Z80.HL );						} /* LD   (w),HL	  */
OP(ed,64) { SMS_Z_CACHE; NEG;													} /* NEG			  */
OP(ed,65) { SMS_Z_CACHE; RETN;													} /* RETN;			  */
OP(ed,66) { SMS_Z_CACHE; _IM = 0;												} /* IM   0 		  */
OP(ed,67) { SMS_Z_CACHE; RRD;													} /* RRD  (HL)		  */

OP(ed,68) { SMS_Z_CACHE; _L = IN(_BC); _F = (_F & CF) | SZP[_L]; 				} /* IN   L,(C) 	  */
OP(ed,69) { SMS_Z_CACHE; OUT(_BC,_L);											} /* OUT  (C),L 	  */
OP(ed,6a) { SMS_Z_CACHE; ADC16( HL );											} /* ADC  HL,HL 	  */
OP(ed,6b) { SMS_Z_CACHE; EA = ARG16(); RM16( EA, &Z80.HL );						} /* LD   HL,(w)	  */
OP(ed,6c) { SMS_Z_CACHE; NEG;													} /* NEG			  */
OP(ed,6d) { SMS_Z_CACHE; RETI;													} /* RETI			  */
OP(ed,6e) { SMS_Z_CACHE; _IM = 0;												} /* IM   0 		  */
OP(ed,6f) { SMS_Z_CACHE; RLD;													} /* RLD  (HL)		  */

OP(ed,70) { SMS_Z_CACHE; UINT8 res = IN(_BC); _F = (_F & CF) | SZP[res]; 		} /* IN   0,(C) 	  */
OP(ed,71) { SMS_Z_CACHE; OUT(_BC,0); 											} /* OUT  (C),0 	  */
OP(ed,72) { SMS_Z_CACHE; SBC16( SP );											} /* SBC  HL,SP 	  */
OP(ed,73) { SMS_Z_CACHE; EA = ARG16(); WM16( EA, &Z80.SP );						} /* LD   (w),SP	  */
OP(ed,74) { SMS_Z_CACHE; NEG;													} /* NEG			  */
OP(ed,75) { SMS_Z_CACHE; RETN;													} /* RETN;			  */
OP(ed,76) { SMS_Z_CACHE; _IM = 1;												} /* IM   1 		  */
OP(ed,77) { SMS_Z_CACHE; illegal_2();											} /* DB   ED,77 	  */

OP(ed,78) { SMS_Z_CACHE; _A = IN(_BC); _F = (_F & CF) | SZP[_A]; 				} /* IN   E,(C) 	  */
OP(ed,79) { SMS_Z_CACHE; OUT(_BC,_A);											} /* OUT  (C),E 	  */
OP(ed,7a) { SMS_Z_CACHE; ADC16( SP );											} /* ADC  HL,SP 	  */
OP(ed,7b) { SMS_Z_CACHE; EA = ARG16(); RM16( EA, &Z80.SP );						} /* LD   SP,(w)	  */
OP(ed,7c) { SMS_Z_CACHE; NEG;													} /* NEG			  */
OP(ed,7d) { SMS_Z_CACHE; RETI;													} /* RETI			  */
OP(ed,7e) { SMS_Z_CACHE; _IM = 2;												} /* IM   2 		  */
OP(ed,7f) { SMS_Z_CACHE; illegal_2();											} /* DB   ED,7F 	  */

OP(ed,80) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,81) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,82) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,83) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,84) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,85) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,86) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,87) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

OP(ed,88) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,89) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,8a) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,8b) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,8c) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,8d) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,8e) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,8f) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

OP(ed,90) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,91) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,92) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,93) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,94) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,95) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,96) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,97) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

OP(ed,98) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,99) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,9a) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,9b) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,9c) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,9d) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,9e) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,9f) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

OP(ed,a0) { SMS_Z_CACHE; LDI;													} /* LDI			  */
OP(ed,a1) { SMS_Z_CACHE; CPI;													} /* CPI			  */
OP(ed,a2) { SMS_Z_CACHE; INI;													} /* INI			  */
OP(ed,a3) { SMS_Z_CACHE; OUTI;													} /* OUTI			  */
OP(ed,a4) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,a5) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,a6) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,a7) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

OP(ed,a8) { SMS_Z_CACHE; LDD;													} /* LDD			  */
OP(ed,a9) { SMS_Z_CACHE; CPD;													} /* CPD			  */
OP(ed,aa) { SMS_Z_CACHE; IND;													} /* IND			  */
OP(ed,ab) { SMS_Z_CACHE; OUTD;													} /* OUTD			  */
OP(ed,ac) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,ad) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,ae) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,af) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

OP(ed,b0) { SMS_Z_CACHE; LDIR;													} /* LDIR			  */
OP(ed,b1) { SMS_Z_CACHE; CPIR;													} /* CPIR			  */
OP(ed,b2) { SMS_Z_CACHE; INIR;													} /* INIR			  */
OP(ed,b3) { SMS_Z_CACHE; OTIR;													} /* OTIR			  */
OP(ed,b4) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,b5) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,b6) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,b7) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

OP(ed,b8) { SMS_Z_CACHE; LDDR;													} /* LDDR			  */
OP(ed,b9) { SMS_Z_CACHE; CPDR;													} /* CPDR			  */
OP(ed,ba) { SMS_Z_CACHE; INDR;													} /* INDR			  */
OP(ed,bb) { SMS_Z_CACHE; OTDR;													} /* OTDR			  */
OP(ed,bc) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,bd) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,be) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,bf) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

OP(ed,c0) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,c1) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,c2) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,c3) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,c4) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,c5) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,c6) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,c7) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

OP(ed,c8) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,c9) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,ca) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,cb) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,cc) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,cd) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,ce) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,cf) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

OP(ed,d0) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,d1) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,d2) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,d3) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,d4) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,d5) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,d6) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,d7) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

OP(ed,d8) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,d9) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,da) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,db) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,dc) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,dd) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,de) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,df) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

OP(ed,e0) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,e1) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,e2) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,e3) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,e4) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,e5) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,e6) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,e7) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

OP(ed,e8) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,e9) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,ea) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,eb) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,ec) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,ed) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,ee) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,ef) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

OP(ed,f0) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,f1) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,f2) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,f3) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,f4) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,f5) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,f6) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,f7) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

OP(ed,f8) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,f9) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,fa) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,fb) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,fc) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,fd) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,fe) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */
OP(ed,ff) { SMS_Z_CACHE; illegal_2();											} /* DB   ED		  */

#if TIME_LOOP_HACKS

#define CHECK_BC_LOOP												\
if( _BC > 1 && _PCD < 0xfffc ) {									\
	UINT8 op1 = cpu_readop(_PCD);									\
	UINT8 op2 = cpu_readop(_PCD+1); 								\
	if( (op1==0x78 && op2==0xb1) || (op1==0x79 && op2==0xb0) )		\
	{																\
		UINT8 op3 = cpu_readop(_PCD+2); 							\
		UINT8 op4 = cpu_readop(_PCD+3); 							\
		if( op3==0x20 && op4==0xfb )								\
		{															\
			int cnt =												\
				cc(Z80_TABLE_op)[0x78] +							\
				cc(Z80_TABLE_op)[0xb1] +							\
				cc(Z80_TABLE_op)[0x20] +							\
				cc(Z80_TABLE_ex)[0x20]; 							\
			while( _BC > 0 && Z80_ICOUNT > cnt )					\
			{														\
				BURNODD( cnt, 4, cnt ); 							\
				_BC--;												\
			}														\
		}															\
		else														\
		if( op3 == 0xc2 )											\
		{															\
			UINT8 ad1 = cpu_readop_arg(_PCD+3); 					\
			UINT8 ad2 = cpu_readop_arg(_PCD+4); 					\
			if( (ad1 + 256 * ad2) == (_PCD - 1) )					\
			{														\
				int cnt =											\
					cc(Z80_TABLE_op)[0x78] +						\
					cc(Z80_TABLE_op)[0xb1] +						\
					cc(Z80_TABLE_op)[0xc2] +						\
					cc(Z80_TABLE_ex)[0xc2]; 						\
				while( _BC > 0 && Z80_ICOUNT > cnt )				\
				{													\
					BURNODD( cnt, 4, cnt ); 						\
					_BC--;											\
				}													\
			}														\
		}															\
	}																\
}

#define CHECK_DE_LOOP												\
if( _DE > 1 && _PCD < 0xfffc ) {									\
	UINT8 op1 = cpu_readop(_PCD);									\
	UINT8 op2 = cpu_readop(_PCD+1); 								\
	if( (op1==0x7a && op2==0xb3) || (op1==0x7b && op2==0xb2) )		\
	{																\
		UINT8 op3 = cpu_readop(_PCD+2); 							\
		UINT8 op4 = cpu_readop(_PCD+3); 							\
		if( op3==0x20 && op4==0xfb )								\
		{															\
			int cnt =												\
				cc(Z80_TABLE_op)[0x7a] +							\
				cc(Z80_TABLE_op)[0xb3] +							\
				cc(Z80_TABLE_op)[0x20] +							\
				cc(Z80_TABLE_ex)[0x20]; 							\
			while( _DE > 0 && Z80_ICOUNT > cnt )					\
			{														\
				BURNODD( cnt, 4, cnt ); 							\
				_DE--;												\
			}														\
		}															\
		else														\
		if( op3==0xc2 ) 											\
		{															\
			UINT8 ad1 = cpu_readop_arg(_PCD+3); 					\
			UINT8 ad2 = cpu_readop_arg(_PCD+4); 					\
			if( (ad1 + 256 * ad2) == (_PCD - 1) )					\
			{														\
				int cnt =											\
					cc(Z80_TABLE_op)[0x7a] +						\
					cc(Z80_TABLE_op)[0xb3] +						\
					cc(Z80_TABLE_op)[0xc2] +						\
					cc(Z80_TABLE_ex)[0xc2]; 						\
				while( _DE > 0 && Z80_ICOUNT > cnt )				\
				{													\
					BURNODD( cnt, 4, cnt ); 						\
					_DE--;											\
				}													\
			}														\
		}															\
	}																\
}

#define CHECK_HL_LOOP												\
if( _HL > 1 && _PCD < 0xfffc ) {									\
	UINT8 op1 = cpu_readop(_PCD);									\
	UINT8 op2 = cpu_readop(_PCD+1); 								\
	if( (op1==0x7c && op2==0xb5) || (op1==0x7d && op2==0xb4) )		\
	{																\
		UINT8 op3 = cpu_readop(_PCD+2); 							\
		UINT8 op4 = cpu_readop(_PCD+3); 							\
		if( op3==0x20 && op4==0xfb )								\
		{															\
			int cnt =												\
				cc(Z80_TABLE_op)[0x7c] +							\
				cc(Z80_TABLE_op)[0xb5] +							\
				cc(Z80_TABLE_op)[0x20] +							\
				cc(Z80_TABLE_ex)[0x20]; 							\
			while( _HL > 0 && Z80_ICOUNT > cnt )					\
			{														\
				BURNODD( cnt, 4, cnt ); 							\
				_HL--;												\
			}														\
		}															\
		else														\
		if( op3==0xc2 ) 											\
		{															\
			UINT8 ad1 = cpu_readop_arg(_PCD+3); 					\
			UINT8 ad2 = cpu_readop_arg(_PCD+4); 					\
			if( (ad1 + 256 * ad2) == (_PCD - 1) )					\
			{														\
				int cnt =											\
					cc(Z80_TABLE_op)[0x7c] +						\
					cc(Z80_TABLE_op)[0xb5] +						\
					cc(Z80_TABLE_op)[0xc2] +						\
					cc(Z80_TABLE_ex)[0xc2]; 						\
				while( _HL > 0 && Z80_ICOUNT > cnt )				\
				{													\
					BURNODD( cnt, 4, cnt ); 						\
					_HL--;											\
				}													\
			}														\
		}															\
	}																\
}

#else

#define CHECK_BC_LOOP
#define CHECK_DE_LOOP
#define CHECK_HL_LOOP

#endif

/**********************************************************
 * main opcodes
 **********************************************************/
OP(op,00) { 														} /* NOP			  */
OP(op,01) { SMS_Z_CACHE; _BC = ARG16();											} /* LD   BC,w		  */
OP(op,02) { SMS_Z_CACHE; WM( _BC, _A );											} /* LD   (BC),A	  */
OP(op,03) { SMS_Z_CACHE; _BC++;													} /* INC  BC		  */
OP(op,04) { SMS_Z_CACHE; _B = INC(_B);											} /* INC  B 		  */
OP(op,05) { SMS_Z_CACHE; _B = DEC(_B);											} /* DEC  B 		  */
OP(op,06) { SMS_Z_CACHE; _B = ARG(); 											} /* LD   B,n		  */
OP(op,07) { SMS_Z_CACHE; RLCA;													} /* RLCA			  */

OP(op,08) { SMS_Z_CACHE; EX_AF;													} /* EX   AF,AF'      */
OP(op,09) { SMS_Z_CACHE; ADD16(HL,BC);											} /* ADD  HL,BC 	  */
OP(op,0a) { SMS_Z_CACHE; _A = RM(_BC);											} /* LD   A,(BC)	  */
OP(op,0b) { SMS_Z_CACHE; _BC--; CHECK_BC_LOOP;									} /* DEC  BC		  */
OP(op,0c) { SMS_Z_CACHE; _C = INC(_C);											} /* INC  C 		  */
OP(op,0d) { SMS_Z_CACHE; _C = DEC(_C);											} /* DEC  C 		  */
OP(op,0e) { SMS_Z_CACHE; _C = ARG(); 											} /* LD   C,n		  */
OP(op,0f) { SMS_Z_CACHE; RRCA;													} /* RRCA			  */

OP(op,10) { SMS_Z_CACHE; _B--; JR_COND( _B, 0x10 );								} /* DJNZ o 		  */
OP(op,11) { SMS_Z_CACHE; _DE = ARG16();											} /* LD   DE,w		  */
OP(op,12) { SMS_Z_CACHE; WM( _DE, _A );											} /* LD   (DE),A	  */
OP(op,13) { SMS_Z_CACHE; _DE++;													} /* INC  DE		  */
OP(op,14) { SMS_Z_CACHE; _D = INC(_D);											} /* INC  D 		  */
OP(op,15) { SMS_Z_CACHE; _D = DEC(_D);											} /* DEC  D 		  */
OP(op,16) { SMS_Z_CACHE; _D = ARG(); 											} /* LD   D,n		  */
OP(op,17) { SMS_Z_CACHE; RLA;													} /* RLA			  */

OP(op,18) { SMS_Z_CACHE; JR();													} /* JR   o 		  */
OP(op,19) { SMS_Z_CACHE; ADD16(HL,DE);											} /* ADD  HL,DE 	  */
OP(op,1a) { SMS_Z_CACHE; _A = RM(_DE);											} /* LD   A,(DE)	  */
OP(op,1b) { SMS_Z_CACHE; _DE--; CHECK_DE_LOOP;									} /* DEC  DE		  */
OP(op,1c) { SMS_Z_CACHE; _E = INC(_E);											} /* INC  E 		  */
OP(op,1d) { SMS_Z_CACHE; _E = DEC(_E);											} /* DEC  E 		  */
OP(op,1e) { SMS_Z_CACHE; _E = ARG(); 											} /* LD   E,n		  */
OP(op,1f) { SMS_Z_CACHE; RRA;													} /* RRA			  */

OP(op,20) { SMS_Z_CACHE; JR_COND( !(_F & ZF), 0x20 );							} /* JR   NZ,o		  */
OP(op,21) { SMS_Z_CACHE; _HL = ARG16();											} /* LD   HL,w		  */
OP(op,22) { SMS_Z_CACHE; EA = ARG16(); WM16( EA, &Z80.HL );						} /* LD   (w),HL	  */
OP(op,23) { SMS_Z_CACHE; _HL++;													} /* INC  HL		  */
OP(op,24) { SMS_Z_CACHE; _H = INC(_H);											} /* INC  H 		  */
OP(op,25) { SMS_Z_CACHE; _H = DEC(_H);											} /* DEC  H 		  */
OP(op,26) { SMS_Z_CACHE; _H = ARG(); 											} /* LD   H,n		  */
OP(op,27) { SMS_Z_CACHE; DAA;													} /* DAA			  */

OP(op,28) { SMS_Z_CACHE; JR_COND( _F & ZF, 0x28 );								} /* JR   Z,o		  */
OP(op,29) { SMS_Z_CACHE; ADD16(HL,HL);											} /* ADD  HL,HL 	  */
OP(op,2a) { SMS_Z_CACHE; EA = ARG16(); RM16( EA, &Z80.HL );						} /* LD   HL,(w)	  */
OP(op,2b) { SMS_Z_CACHE; _HL--; CHECK_HL_LOOP;									} /* DEC  HL		  */
OP(op,2c) { SMS_Z_CACHE; _L = INC(_L);											} /* INC  L 		  */
OP(op,2d) { SMS_Z_CACHE; _L = DEC(_L);											} /* DEC  L 		  */
OP(op,2e) { SMS_Z_CACHE; _L = ARG(); 											} /* LD   L,n		  */
OP(op,2f) { SMS_Z_CACHE; _A ^= 0xff; _F = (_F&(SF|ZF|PF|CF))|HF|NF|(_A&(YF|XF)); } /* CPL			  */

OP(op,30) { SMS_Z_CACHE; JR_COND( !(_F & CF), 0x30 );							} /* JR   NC,o		  */
OP(op,31) { SMS_Z_CACHE; _SP = ARG16();											} /* LD   SP,w		  */
OP(op,32) { SMS_Z_CACHE; EA = ARG16(); WM( EA, _A ); 							} /* LD   (w),A 	  */
OP(op,33) { SMS_Z_CACHE; _SP++;													} /* INC  SP		  */
OP(op,34) { SMS_Z_CACHE; WM( _HL, INC(RM(_HL)) );								} /* INC  (HL)		  */
OP(op,35) { SMS_Z_CACHE; WM( _HL, DEC(RM(_HL)) );								} /* DEC  (HL)		  */
OP(op,36) { SMS_Z_CACHE; WM( _HL, ARG() );										} /* LD   (HL),n	  */
OP(op,37) { SMS_Z_CACHE; _F = (_F & (SF|ZF|PF)) | CF | (_A & (YF|XF));			} /* SCF			  */

OP(op,38) { SMS_Z_CACHE; JR_COND( _F & CF, 0x38 );								} /* JR   C,o		  */
OP(op,39) { SMS_Z_CACHE; ADD16(HL,SP);											} /* ADD  HL,SP 	  */
OP(op,3a) { SMS_Z_CACHE; EA = ARG16(); _A = RM( EA );							} /* LD   A,(w) 	  */
OP(op,3b) { SMS_Z_CACHE; _SP--;													} /* DEC  SP		  */
OP(op,3c) { SMS_Z_CACHE; _A = INC(_A);											} /* INC  A 		  */
OP(op,3d) { SMS_Z_CACHE; _A = DEC(_A);											} /* DEC  A 		  */
OP(op,3e) { SMS_Z_CACHE; _A = ARG(); 											} /* LD   A,n		  */
OP(op,3f) { SMS_Z_CACHE; _F = ((_F&(SF|ZF|PF|CF))|((_F&CF)<<4)|(_A&(YF|XF)))^CF; } /* CCF			  */
//OP(op,3f) { _F = ((_F & ~(HF|NF)) | ((_F & CF)<<4)) ^ CF; 		  } /* CCF				*/

OP(op,40) { 														} /* LD   B,B		  */
OP(op,41) { SMS_Z_CACHE; _B = _C;												} /* LD   B,C		  */
OP(op,42) { SMS_Z_CACHE; _B = _D;												} /* LD   B,D		  */
OP(op,43) { SMS_Z_CACHE; _B = _E;												} /* LD   B,E		  */
OP(op,44) { SMS_Z_CACHE; _B = _H;												} /* LD   B,H		  */
OP(op,45) { SMS_Z_CACHE; _B = _L;												} /* LD   B,L		  */
OP(op,46) { SMS_Z_CACHE; _B = RM(_HL);											} /* LD   B,(HL)	  */
OP(op,47) { SMS_Z_CACHE; _B = _A;												} /* LD   B,A		  */

OP(op,48) { SMS_Z_CACHE; _C = _B;												} /* LD   C,B		  */
OP(op,49) { 														} /* LD   C,C		  */
OP(op,4a) { SMS_Z_CACHE; _C = _D;												} /* LD   C,D		  */
OP(op,4b) { SMS_Z_CACHE; _C = _E;												} /* LD   C,E		  */
OP(op,4c) { SMS_Z_CACHE; _C = _H;												} /* LD   C,H		  */
OP(op,4d) { SMS_Z_CACHE; _C = _L;												} /* LD   C,L		  */
OP(op,4e) { SMS_Z_CACHE; _C = RM(_HL);											} /* LD   C,(HL)	  */
OP(op,4f) { SMS_Z_CACHE; _C = _A;												} /* LD   C,A		  */

OP(op,50) { SMS_Z_CACHE; _D = _B;												} /* LD   D,B		  */
OP(op,51) { SMS_Z_CACHE; _D = _C;												} /* LD   D,C		  */
OP(op,52) { 														} /* LD   D,D		  */
OP(op,53) { SMS_Z_CACHE; _D = _E;												} /* LD   D,E		  */
OP(op,54) { SMS_Z_CACHE; _D = _H;												} /* LD   D,H		  */
OP(op,55) { SMS_Z_CACHE; _D = _L;												} /* LD   D,L		  */
OP(op,56) { SMS_Z_CACHE; _D = RM(_HL);											} /* LD   D,(HL)	  */
OP(op,57) { SMS_Z_CACHE; _D = _A;												} /* LD   D,A		  */

OP(op,58) { SMS_Z_CACHE; _E = _B;												} /* LD   E,B		  */
OP(op,59) { SMS_Z_CACHE; _E = _C;												} /* LD   E,C		  */
OP(op,5a) { SMS_Z_CACHE; _E = _D;												} /* LD   E,D		  */
OP(op,5b) { 														} /* LD   E,E		  */
OP(op,5c) { SMS_Z_CACHE; _E = _H;												} /* LD   E,H		  */
OP(op,5d) { SMS_Z_CACHE; _E = _L;												} /* LD   E,L		  */
OP(op,5e) { SMS_Z_CACHE; _E = RM(_HL);											} /* LD   E,(HL)	  */
OP(op,5f) { SMS_Z_CACHE; _E = _A;												} /* LD   E,A		  */

OP(op,60) { SMS_Z_CACHE; _H = _B;												} /* LD   H,B		  */
OP(op,61) { SMS_Z_CACHE; _H = _C;												} /* LD   H,C		  */
OP(op,62) { SMS_Z_CACHE; _H = _D;												} /* LD   H,D		  */
OP(op,63) { SMS_Z_CACHE; _H = _E;												} /* LD   H,E		  */
OP(op,64) { 														} /* LD   H,H		  */
OP(op,65) { SMS_Z_CACHE; _H = _L;												} /* LD   H,L		  */
OP(op,66) { SMS_Z_CACHE; _H = RM(_HL);											} /* LD   H,(HL)	  */
OP(op,67) { SMS_Z_CACHE; _H = _A;												} /* LD   H,A		  */

OP(op,68) { SMS_Z_CACHE; _L = _B;												} /* LD   L,B		  */
OP(op,69) { SMS_Z_CACHE; _L = _C;												} /* LD   L,C		  */
OP(op,6a) { SMS_Z_CACHE; _L = _D;												} /* LD   L,D		  */
OP(op,6b) { SMS_Z_CACHE; _L = _E;												} /* LD   L,E		  */
OP(op,6c) { SMS_Z_CACHE; _L = _H;												} /* LD   L,H		  */
OP(op,6d) { 														} /* LD   L,L		  */
OP(op,6e) { SMS_Z_CACHE; _L = RM(_HL);											} /* LD   L,(HL)	  */
OP(op,6f) { SMS_Z_CACHE; _L = _A;												} /* LD   L,A		  */

OP(op,70) { SMS_Z_CACHE; WM( _HL, _B );											} /* LD   (HL),B	  */
OP(op,71) { SMS_Z_CACHE; WM( _HL, _C );											} /* LD   (HL),C	  */
OP(op,72) { SMS_Z_CACHE; WM( _HL, _D );											} /* LD   (HL),D	  */
OP(op,73) { SMS_Z_CACHE; WM( _HL, _E );											} /* LD   (HL),E	  */
OP(op,74) { SMS_Z_CACHE; WM( _HL, _H );											} /* LD   (HL),H	  */
OP(op,75) { SMS_Z_CACHE; WM( _HL, _L );											} /* LD   (HL),L	  */
OP(op,76) { SMS_Z_CACHE; ENTER_HALT; 											} /* HALT			  */
OP(op,77) { SMS_Z_CACHE; WM( _HL, _A );											} /* LD   (HL),A	  */

OP(op,78) { SMS_Z_CACHE; _A = _B;												} /* LD   A,B		  */
OP(op,79) { SMS_Z_CACHE; _A = _C;												} /* LD   A,C		  */
OP(op,7a) { SMS_Z_CACHE; _A = _D;												} /* LD   A,D		  */
OP(op,7b) { SMS_Z_CACHE; _A = _E;												} /* LD   A,E		  */
OP(op,7c) { SMS_Z_CACHE; _A = _H;												} /* LD   A,H		  */
OP(op,7d) { SMS_Z_CACHE; _A = _L;												} /* LD   A,L		  */
OP(op,7e) { SMS_Z_CACHE; _A = RM(_HL);											} /* LD   A,(HL)	  */
OP(op,7f) { 														} /* LD   A,A		  */

OP(op,80) { SMS_Z_CACHE; ADD(_B);												} /* ADD  A,B		  */
OP(op,81) { SMS_Z_CACHE; ADD(_C);												} /* ADD  A,C		  */
OP(op,82) { SMS_Z_CACHE; ADD(_D);												} /* ADD  A,D		  */
OP(op,83) { SMS_Z_CACHE; ADD(_E);												} /* ADD  A,E		  */
OP(op,84) { SMS_Z_CACHE; ADD(_H);												} /* ADD  A,H		  */
OP(op,85) { SMS_Z_CACHE; ADD(_L);												} /* ADD  A,L		  */
OP(op,86) { SMS_Z_CACHE; ADD(RM(_HL));											} /* ADD  A,(HL)	  */
OP(op,87) { SMS_Z_CACHE; ADD(_A);												} /* ADD  A,A		  */

OP(op,88) { SMS_Z_CACHE; ADC(_B);												} /* ADC  A,B		  */
OP(op,89) { SMS_Z_CACHE; ADC(_C);												} /* ADC  A,C		  */
OP(op,8a) { SMS_Z_CACHE; ADC(_D);												} /* ADC  A,D		  */
OP(op,8b) { SMS_Z_CACHE; ADC(_E);												} /* ADC  A,E		  */
OP(op,8c) { SMS_Z_CACHE; ADC(_H);												} /* ADC  A,H		  */
OP(op,8d) { SMS_Z_CACHE; ADC(_L);												} /* ADC  A,L		  */
OP(op,8e) { SMS_Z_CACHE; ADC(RM(_HL));											} /* ADC  A,(HL)	  */
OP(op,8f) { SMS_Z_CACHE; ADC(_A);												} /* ADC  A,A		  */

OP(op,90) { SMS_Z_CACHE; SUB(_B);												} /* SUB  B 		  */
OP(op,91) { SMS_Z_CACHE; SUB(_C);												} /* SUB  C 		  */
OP(op,92) { SMS_Z_CACHE; SUB(_D);												} /* SUB  D 		  */
OP(op,93) { SMS_Z_CACHE; SUB(_E);												} /* SUB  E 		  */
OP(op,94) { SMS_Z_CACHE; SUB(_H);												} /* SUB  H 		  */
OP(op,95) { SMS_Z_CACHE; SUB(_L);												} /* SUB  L 		  */
OP(op,96) { SMS_Z_CACHE; SUB(RM(_HL));											} /* SUB  (HL)		  */
OP(op,97) { SMS_Z_CACHE; SUB(_A);												} /* SUB  A 		  */

OP(op,98) { SMS_Z_CACHE; SBC(_B);												} /* SBC  A,B		  */
OP(op,99) { SMS_Z_CACHE; SBC(_C);												} /* SBC  A,C		  */
OP(op,9a) { SMS_Z_CACHE; SBC(_D);												} /* SBC  A,D		  */
OP(op,9b) { SMS_Z_CACHE; SBC(_E);												} /* SBC  A,E		  */
OP(op,9c) { SMS_Z_CACHE; SBC(_H);												} /* SBC  A,H		  */
OP(op,9d) { SMS_Z_CACHE; SBC(_L);												} /* SBC  A,L		  */
OP(op,9e) { SMS_Z_CACHE; SBC(RM(_HL));											} /* SBC  A,(HL)	  */
OP(op,9f) { SMS_Z_CACHE; SBC(_A);												} /* SBC  A,A		  */

OP(op,a0) { SMS_Z_CACHE; AND(_B);												} /* AND  B 		  */
OP(op,a1) { SMS_Z_CACHE; AND(_C);												} /* AND  C 		  */
OP(op,a2) { SMS_Z_CACHE; AND(_D);												} /* AND  D 		  */
OP(op,a3) { SMS_Z_CACHE; AND(_E);												} /* AND  E 		  */
OP(op,a4) { SMS_Z_CACHE; AND(_H);												} /* AND  H 		  */
OP(op,a5) { SMS_Z_CACHE; AND(_L);												} /* AND  L 		  */
OP(op,a6) { SMS_Z_CACHE; AND(RM(_HL));											} /* AND  (HL)		  */
OP(op,a7) { SMS_Z_CACHE; AND(_A);												} /* AND  A 		  */

OP(op,a8) { SMS_Z_CACHE; XOR(_B);												} /* XOR  B 		  */
OP(op,a9) { SMS_Z_CACHE; XOR(_C);												} /* XOR  C 		  */
OP(op,aa) { SMS_Z_CACHE; XOR(_D);												} /* XOR  D 		  */
OP(op,ab) { SMS_Z_CACHE; XOR(_E);												} /* XOR  E 		  */
OP(op,ac) { SMS_Z_CACHE; XOR(_H);												} /* XOR  H 		  */
OP(op,ad) { SMS_Z_CACHE; XOR(_L);												} /* XOR  L 		  */
OP(op,ae) { SMS_Z_CACHE; XOR(RM(_HL));											} /* XOR  (HL)		  */
OP(op,af) { SMS_Z_CACHE; XOR(_A);												} /* XOR  A 		  */

OP(op,b0) { SMS_Z_CACHE; OR(_B); 												} /* OR   B 		  */
OP(op,b1) { SMS_Z_CACHE; OR(_C); 												} /* OR   C 		  */
OP(op,b2) { SMS_Z_CACHE; OR(_D); 												} /* OR   D 		  */
OP(op,b3) { SMS_Z_CACHE; OR(_E); 												} /* OR   E 		  */
OP(op,b4) { SMS_Z_CACHE; OR(_H); 												} /* OR   H 		  */
OP(op,b5) { SMS_Z_CACHE; OR(_L); 												} /* OR   L 		  */
OP(op,b6) { SMS_Z_CACHE; OR(RM(_HL));											} /* OR   (HL)		  */
OP(op,b7) { SMS_Z_CACHE; OR(_A); 												} /* OR   A 		  */

OP(op,b8) { SMS_Z_CACHE; CP(_B); 												} /* CP   B 		  */
OP(op,b9) { SMS_Z_CACHE; CP(_C); 												} /* CP   C 		  */
OP(op,ba) { SMS_Z_CACHE; CP(_D); 												} /* CP   D 		  */
OP(op,bb) { SMS_Z_CACHE; CP(_E); 												} /* CP   E 		  */
OP(op,bc) { SMS_Z_CACHE; CP(_H); 												} /* CP   H 		  */
OP(op,bd) { SMS_Z_CACHE; CP(_L); 												} /* CP   L 		  */
OP(op,be) { SMS_Z_CACHE; CP(RM(_HL));											} /* CP   (HL)		  */
OP(op,bf) { SMS_Z_CACHE; CP(_A); 												} /* CP   A 		  */

OP(op,c0) { SMS_Z_CACHE; RET_COND( !(_F & ZF), 0xc0 );							} /* RET  NZ		  */
OP(op,c1) { SMS_Z_CACHE; POP(BC);												} /* POP  BC		  */
OP(op,c2) { SMS_Z_CACHE; JP_COND( !(_F & ZF) );									} /* JP   NZ,a		  */
OP(op,c3) { SMS_Z_CACHE; JP; 													} /* JP   a 		  */
OP(op,c4) { SMS_Z_CACHE; CALL_COND( !(_F & ZF), 0xc4 );							} /* CALL NZ,a		  */
OP(op,c5) { SMS_Z_CACHE; PUSH( BC ); 											} /* PUSH BC		  */
OP(op,c6) { SMS_Z_CACHE; ADD(ARG()); 											} /* ADD  A,n		  */
OP(op,c7) { SMS_Z_CACHE; RST(0x00);												} /* RST  0 		  */

OP(op,c8) { SMS_Z_CACHE; RET_COND( _F & ZF, 0xc8 );								} /* RET  Z 		  */
OP(op,c9) { SMS_Z_CACHE; POP(PC); change_pc16(_PCD); 							} /* RET			  */
OP(op,ca) { SMS_Z_CACHE; JP_COND( _F & ZF ); 									} /* JP   Z,a		  */
OP(op,cb) { SMS_Z_CACHE; _R++; EXEC(cb,ROP());									} /* **** CB xx 	  */
OP(op,cc) { SMS_Z_CACHE; CALL_COND( _F & ZF, 0xcc ); 							} /* CALL Z,a		  */
OP(op,cd) { SMS_Z_CACHE; CALL(); 												} /* CALL a 		  */
OP(op,ce) { SMS_Z_CACHE; ADC(ARG()); 											} /* ADC  A,n		  */
OP(op,cf) { SMS_Z_CACHE; RST(0x08);												} /* RST  1 		  */

OP(op,d0) { SMS_Z_CACHE; RET_COND( !(_F & CF), 0xd0 );							} /* RET  NC		  */
OP(op,d1) { SMS_Z_CACHE; POP(DE);												} /* POP  DE		  */
OP(op,d2) { SMS_Z_CACHE; JP_COND( !(_F & CF) );									} /* JP   NC,a		  */
OP(op,d3) { SMS_Z_CACHE; unsigned n = ARG() | (_A << 8); OUT( n, _A );			} /* OUT  (n),A 	  */
OP(op,d4) { SMS_Z_CACHE; CALL_COND( !(_F & CF), 0xd4 );							} /* CALL NC,a		  */
OP(op,d5) { SMS_Z_CACHE; PUSH( DE ); 											} /* PUSH DE		  */
OP(op,d6) { SMS_Z_CACHE; SUB(ARG()); 											} /* SUB  n 		  */
OP(op,d7) { SMS_Z_CACHE; RST(0x10);												} /* RST  2 		  */

OP(op,d8) { SMS_Z_CACHE; RET_COND( _F & CF, 0xd8 );								} /* RET  C 		  */
OP(op,d9) { SMS_Z_CACHE; EXX;													} /* EXX			  */
OP(op,da) { SMS_Z_CACHE; JP_COND( _F & CF ); 									} /* JP   C,a		  */
OP(op,db) { SMS_Z_CACHE; unsigned n = ARG() | (_A << 8); _A = IN( n );			} /* IN   A,(n) 	  */
OP(op,dc) { SMS_Z_CACHE; CALL_COND( _F & CF, 0xdc ); 							} /* CALL C,a		  */
OP(op,dd) { SMS_Z_CACHE; _R++; EXEC(dd,ROP());									} /* **** DD xx 	  */
OP(op,de) { SMS_Z_CACHE; SBC(ARG()); 											} /* SBC  A,n		  */
OP(op,df) { SMS_Z_CACHE; RST(0x18);												} /* RST  3 		  */

OP(op,e0) { SMS_Z_CACHE; RET_COND( !(_F & PF), 0xe0 );							} /* RET  PO		  */
OP(op,e1) { SMS_Z_CACHE; POP(HL);												} /* POP  HL		  */
OP(op,e2) { SMS_Z_CACHE; JP_COND( !(_F & PF) );									} /* JP   PO,a		  */
OP(op,e3) { SMS_Z_CACHE; EXSP(HL);												} /* EX   HL,(SP)	  */
OP(op,e4) { SMS_Z_CACHE; CALL_COND( !(_F & PF), 0xe4 );							} /* CALL PO,a		  */
OP(op,e5) { SMS_Z_CACHE; PUSH( HL ); 											} /* PUSH HL		  */
OP(op,e6) { SMS_Z_CACHE; AND(ARG()); 											} /* AND  n 		  */
OP(op,e7) { SMS_Z_CACHE; RST(0x20);												} /* RST  4 		  */

OP(op,e8) { SMS_Z_CACHE; RET_COND( _F & PF, 0xe8 );								} /* RET  PE		  */
OP(op,e9) { SMS_Z_CACHE; _PC = _HL; change_pc16(_PCD);							} /* JP   (HL)		  */
OP(op,ea) { SMS_Z_CACHE; JP_COND( _F & PF ); 									} /* JP   PE,a		  */
OP(op,eb) { SMS_Z_CACHE; EX_DE_HL;												} /* EX   DE,HL 	  */
OP(op,ec) { SMS_Z_CACHE; CALL_COND( _F & PF, 0xec ); 							} /* CALL PE,a		  */
OP(op,ed) { SMS_Z_CACHE; _R++; EXEC(ed,ROP());									} /* **** ED xx 	  */
OP(op,ee) { SMS_Z_CACHE; XOR(ARG()); 											} /* XOR  n 		  */
OP(op,ef) { SMS_Z_CACHE; RST(0x28);												} /* RST  5 		  */

OP(op,f0) { SMS_Z_CACHE; RET_COND( !(_F & SF), 0xf0 );							} /* RET  P 		  */
OP(op,f1) { SMS_Z_CACHE; POP(AF);												} /* POP  AF		  */
OP(op,f2) { SMS_Z_CACHE; JP_COND( !(_F & SF) );									} /* JP   P,a		  */
OP(op,f3) { SMS_Z_CACHE; _IFF1 = _IFF2 = 0;												} /* DI			  */
OP(op,f4) { SMS_Z_CACHE; CALL_COND( !(_F & SF), 0xf4 );							} /* CALL P,a		  */
OP(op,f5) { SMS_Z_CACHE; PUSH( AF ); 											} /* PUSH AF		  */
OP(op,f6) { SMS_Z_CACHE; OR(ARG());												} /* OR   n 		  */
OP(op,f7) { SMS_Z_CACHE; RST(0x30);												} /* RST  6 		  */

OP(op,f8) { SMS_Z_CACHE; RET_COND( _F & SF, 0xf8 );								} /* RET  M 		  */
OP(op,f9) { SMS_Z_CACHE; _SP = _HL;												} /* LD   SP,HL 	  */
OP(op,fa) { SMS_Z_CACHE; JP_COND(_F & SF);										} /* JP   M,a		  */
OP(op,fb) { SMS_Z_CACHE; EI; 													} /* EI 			  */
OP(op,fc) { SMS_Z_CACHE; CALL_COND( _F & SF, 0xfc ); 							} /* CALL M,a		  */
OP(op,fd) { SMS_Z_CACHE; _R++; EXEC(fd,ROP());									} /* **** FD xx 	  */
OP(op,fe) { SMS_Z_CACHE; CP(ARG());												} /* CP   n 		  */
OP(op,ff) { SMS_Z_CACHE; RST(0x38);												} /* RST  7 		  */


static void take_interrupt(void)
{
    SMS_Z_CACHE;
	if( _IFF1 )
	{
		int irq_vector;

		/* there isn't a valid previous program counter */
		_PPC = -1;

		/* Check if processor was halted */
		LEAVE_HALT;

		if( Z80.irq_max )			/* daisy chain mode */
		{
			if( Z80.request_irq >= 0 )
			{
				/* Clear both interrupt flip flops */
				_IFF1 = _IFF2 = 0;
				irq_vector = Z80.irq[Z80.request_irq].interrupt_entry(Z80.irq[Z80.request_irq].irq_param);
                LOG(("Z80 #%d daisy chain irq_vector $%02x\n", 0, irq_vector));
				Z80.request_irq = -1;
			} else return;
		}
		else
		{
			/* Clear both interrupt flip flops */
			_IFF1 = _IFF2 = 0;
			/* call back the cpu interface to retrieve the vector */
			irq_vector = (*Z80.irq_callback)(0);
            LOG(("Z80 #%d single int. irq_vector $%02x\n", 0, irq_vector));
		}

		/* Interrupt mode 2. Call [Z80.I:databyte] */
		if( _IM == 2 )
		{
			irq_vector = (irq_vector & 0xff) | (_I << 8);
			PUSH( PC );
			RM16( irq_vector, &Z80.PC );
            LOG(("Z80 #%d IM2 [$%04x] = $%04x\n",0 , irq_vector, _PCD));
			/* CALL opcode timing */
			Z80.extra_cycles += cc(Z80_TABLE_op)[0xcd];
		}
		else
		/* Interrupt mode 1. RST 38h */
		if( _IM == 1 )
		{
            LOG(("Z80 #%d IM1 $0038\n",0 ));
			PUSH( PC );
			_PCD = 0x0038;
			/* RST $38 + 'interrupt latency' cycles */
			Z80.extra_cycles += cc(Z80_TABLE_op)[0xff] + cc(Z80_TABLE_ex)[0xff];
		}
		else
		{
			/* Interrupt mode 0. We check for CALL and JP instructions, */
			/* if neither of these were found we assume a 1 byte opcode */
			/* was placed on the databus								*/
            LOG(("Z80 #%d IM0 $%04x\n",0 , irq_vector));
			switch (irq_vector & 0xff0000)
			{
				case 0xcd0000:	/* call */
					PUSH( PC );
					_PCD = irq_vector & 0xffff;
					 /* CALL $xxxx + 'interrupt latency' cycles */
					Z80.extra_cycles += cc(Z80_TABLE_op)[0xcd] + cc(Z80_TABLE_ex)[0xff];
					break;
				case 0xc30000:	/* jump */
					_PCD = irq_vector & 0xffff;
					/* JP $xxxx + 2 cycles */
					Z80.extra_cycles += cc(Z80_TABLE_op)[0xc3] + cc(Z80_TABLE_ex)[0xff];
					break;
				default:		/* rst (or other opcodes?) */
					PUSH( PC );
					_PCD = irq_vector & 0x0038;
					/* RST $xx + 2 cycles */
					Z80.extra_cycles += cc(Z80_TABLE_op)[_PCD] + cc(Z80_TABLE_ex)[_PCD];
					break;
			}
		}
		change_pc16(_PCD);
	}
}

/****************************************************************************
 * Processor initialization
 ****************************************************************************/
#ifdef Z80_MSX
void z80_msx_init(void)
#else
void z80_init(void)
#endif
{
    SMS_Z_CACHE;
	int i, p;
	Z80_Context = &Z80;
#if BIG_FLAGS_ARRAY
	if( !SZHVC_add || !SZHVC_sub )
	{
    SMS_Z_CACHE;
		int oldval, newval, val;
		UINT8 *padd, *padc, *psub, *psbc;
		/* allocate big flag arrays once */
		SZHVC_add = (UINT8 *)malloc(2*256*256);
		SZHVC_sub = (UINT8 *)malloc(2*256*256);
		if( !SZHVC_add || !SZHVC_sub )
		{
			LOG(("Z80: failed to allocate 2 * 128K flags arrays!!!\n"));
			raise(SIGABRT);
		}
		padd = &SZHVC_add[	0*256];
		padc = &SZHVC_add[256*256];
		psub = &SZHVC_sub[	0*256];
		psbc = &SZHVC_sub[256*256];
		for (oldval = 0; oldval < 256; oldval++)
		{
			for (newval = 0; newval < 256; newval++)
			{
				/* add or adc w/o carry set */
				val = newval - oldval;
				*padd = (newval) ? ((newval & 0x80) ? SF : 0) : ZF;
#if Z80_EXACT
				*padd |= (newval & (YF | XF));	/* undocumented flag bits 5+3 */
#endif
				if( (newval & 0x0f) < (oldval & 0x0f) ) *padd |= HF;
				if( newval < oldval ) *padd |= CF;
				if( (val^oldval^0x80) & (val^newval) & 0x80 ) *padd |= VF;
				padd++;

				/* adc with carry set */
				val = newval - oldval - 1;
				*padc = (newval) ? ((newval & 0x80) ? SF : 0) : ZF;
#if Z80_EXACT
				*padc |= (newval & (YF | XF));	/* undocumented flag bits 5+3 */
#endif
				if( (newval & 0x0f) <= (oldval & 0x0f) ) *padc |= HF;
				if( newval <= oldval ) *padc |= CF;
				if( (val^oldval^0x80) & (val^newval) & 0x80 ) *padc |= VF;
				padc++;

				/* cp, sub or sbc w/o carry set */
				val = oldval - newval;
				*psub = NF | ((newval) ? ((newval & 0x80) ? SF : 0) : ZF);
#if Z80_EXACT
				*psub |= (newval & (YF | XF));	/* undocumented flag bits 5+3 */
#endif
				if( (newval & 0x0f) > (oldval & 0x0f) ) *psub |= HF;
				if( newval > oldval ) *psub |= CF;
				if( (val^oldval) & (oldval^newval) & 0x80 ) *psub |= VF;
				psub++;

				/* sbc with carry set */
				val = oldval - newval - 1;
				*psbc = NF | ((newval) ? ((newval & 0x80) ? SF : 0) : ZF);
#if Z80_EXACT
				*psbc |= (newval & (YF | XF));	/* undocumented flag bits 5+3 */
#endif
				if( (newval & 0x0f) >= (oldval & 0x0f) ) *psbc |= HF;
				if( newval >= oldval ) *psbc |= CF;
				if( (val^oldval) & (oldval^newval) & 0x80 ) *psbc |= VF;
				psbc++;
			}
		}
	}
#endif
	for (i = 0; i < 256; i++)
	{
		p = 0;
		if( i&0x01 ) ++p;
		if( i&0x02 ) ++p;
		if( i&0x04 ) ++p;
		if( i&0x08 ) ++p;
		if( i&0x10 ) ++p;
		if( i&0x20 ) ++p;
		if( i&0x40 ) ++p;
		if( i&0x80 ) ++p;
		SZ[i] = i ? i & SF : ZF;
#if Z80_EXACT
		SZ[i] |= (i & (YF | XF));		/* undocumented flag bits 5+3 */
#endif
		SZ_BIT[i] = i ? i & SF : ZF | PF;
#if Z80_EXACT
		SZ_BIT[i] |= (i & (YF | XF));	/* undocumented flag bits 5+3 */
#endif
		SZP[i] = SZ[i] | ((p & 1) ? 0 : PF);
		SZHV_inc[i] = SZ[i];
		if( i == 0x80 ) SZHV_inc[i] |= VF;
		if( (i & 0x0f) == 0x00 ) SZHV_inc[i] |= HF;
		SZHV_dec[i] = SZ[i] | NF;
		if( i == 0x7f ) SZHV_dec[i] |= VF;
		if( (i & 0x0f) == 0x0f ) SZHV_dec[i] |= HF;
	}

	/* daisy chain needs to be saved by z80ctc.c somehow */
}

/****************************************************************************
 * Reset registers to their initial values
 ****************************************************************************/
#ifdef Z80_MSX
void z80_msx_reset(void *param)
#else
void z80_reset(void *param)
#endif
{
    SMS_Z_CACHE;
	Z80_DaisyChain *daisy_chain = (Z80_DaisyChain *)param;
	memset(&Z80, 0, sizeof(Z80));
	_IX = _IY = 0xffff; /* IX and IY are FFFF after a reset! */
	_F = ZF;			/* Zero flag is set */
	Z80.request_irq = -1;
	Z80.service_irq = -1;
	Z80.nmi_state = CLEAR_LINE;
	Z80.irq_state = CLEAR_LINE;

	if( daisy_chain )
	{
		while( daisy_chain->irq_param != -1 && Z80.irq_max < Z80_MAXDAISY )
		{
			/* set callbackhandler after reti */
			Z80.irq[Z80.irq_max] = *daisy_chain;
			/* device reset */
			if( Z80.irq[Z80.irq_max].reset )
				Z80.irq[Z80.irq_max].reset(Z80.irq[Z80.irq_max].irq_param);
			Z80.irq_max++;
			daisy_chain++;
		}
	}

	change_pc16(_PCD);
}

#ifdef Z80_MSX
void z80_msx_exit(void)
#else
void z80_exit(void)
#endif
{
    SMS_Z_CACHE;
#if BIG_FLAGS_ARRAY
	if (SZHVC_add) free(SZHVC_add);
	SZHVC_add = NULL;
	if (SZHVC_sub) free(SZHVC_sub);
	SZHVC_sub = NULL;
#endif
}

/****************************************************************************
 * Execute 'cycles' T-states. Return number of T-states really executed
 ****************************************************************************/
#ifdef Z80_MSX
int z80_msx_execute(int cycles)
#else
int z80_execute(int cycles)
#endif
{
    SMS_Z_CACHE;
    UINT8 op;

    Z80_ICOUNT = cycles - Z80.extra_cycles;
    Z80.extra_cycles = 0;
    z80_requested_cycles = Z80_ICOUNT;
    z80_exec = 1;
    change_pc16(Z80.PC.d);

    do
    {
        _PPC = _PCD;
        _R++;
        EXEC_INLINE(op, ROP());
    } while( Z80_ICOUNT > 0 );

    z80_exec = 0;
    Z80_ICOUNT -= Z80.extra_cycles;
    Z80.extra_cycles = 0;
    z80_cycle_count += (cycles - Z80_ICOUNT);

    return cycles - Z80_ICOUNT;
}

/****************************************************************************
 * Burn 'cycles' T-states. Adjust R register for the lost time
 ****************************************************************************/
#ifdef Z80_MSX
void z80_msx_burn(int cycles)
#else
void z80_burn(int cycles)
#endif
{
    SMS_Z_CACHE;
	if( cycles > 0 )
	{
    SMS_Z_CACHE;
		/* NOP takes 4 cycles per instruction */
		int n = (cycles + 3) / 4;
		_R += n;
		Z80_ICOUNT -= 4 * n;
	}
}

/****************************************************************************
 * Get all registers in given buffer
 ****************************************************************************/
#ifdef Z80_MSX
unsigned z80_msx_get_context (void *dst)
#else
unsigned z80_get_context (void *dst)
#endif
{
    SMS_Z_CACHE;
	if( dst )
		*(Z80_Regs*)dst = Z80;
	return sizeof(Z80_Regs);
}

/****************************************************************************
 * Set all registers to given values
 ****************************************************************************/
#ifdef Z80_MSX
void z80_msx_set_context (void *src)
#else
void z80_set_context (void *src)
#endif
{
    SMS_Z_CACHE;
	if( src )
		Z80 = *(Z80_Regs*)src;
	change_pc16(_PCD);
}

/****************************************************************************
 * Get a pointer to a cycle count table
 ****************************************************************************/
#ifdef Z80_MSX
const void *z80_msx_get_cycle_table (int which)
#else
const void *z80_get_cycle_table (int which)
#endif
{
	if (which >= 0 && which <= Z80_TABLE_xycb)
		return cc(which);
	return NULL;
}

/****************************************************************************
 * Set a new cycle count table
 ****************************************************************************/
#ifdef Z80_MSX
void z80_msx_set_cycle_table (int which, void *new_table)
#else
void z80_set_cycle_table (int which, void *new_table)
#endif
{
	/* E32SMS: cc is now an inline selector over fixed const tables; the
	   cycle tables are immutable in this build, so this setter is a no-op.
	   (void) casts silence unused-parameter warnings.) */
	(void)which; (void)new_table;
}

/****************************************************************************
 * Return a specific register
 ****************************************************************************/
#ifdef Z80_MSX
unsigned z80_msx_get_reg (int regnum)
#else
unsigned z80_get_reg (int regnum)
#endif
{
    SMS_Z_CACHE;
	switch( regnum )
	{
    SMS_Z_CACHE;
		case REG_PC: return _PCD;
		case Z80_PC: return Z80.PC.w.l;
		case REG_SP: return _SPD;
		case Z80_SP: return Z80.SP.w.l;
		case Z80_AF: return Z80.AF.w.l;
		case Z80_BC: return Z80.BC.w.l;
		case Z80_DE: return Z80.DE.w.l;
		case Z80_HL: return Z80.HL.w.l;
		case Z80_IX: return Z80.IX.w.l;
		case Z80_IY: return Z80.IY.w.l;
		case Z80_R: return (Z80.R & 0x7f) | (Z80.R2 & 0x80);
		case Z80_I: return Z80.I;
		case Z80_AF2: return Z80.AF2.w.l;
		case Z80_BC2: return Z80.BC2.w.l;
		case Z80_DE2: return Z80.DE2.w.l;
		case Z80_HL2: return Z80.HL2.w.l;
		case Z80_IM: return Z80.IM;
		case Z80_IFF1: return Z80.IFF1;
		case Z80_IFF2: return Z80.IFF2;
		case Z80_HALT: return Z80.HALT;
		case Z80_NMI_STATE: return Z80.nmi_state;
		case Z80_IRQ_STATE: return Z80.irq_state;
		case Z80_DC0: return Z80.int_state[0];
		case Z80_DC1: return Z80.int_state[1];
		case Z80_DC2: return Z80.int_state[2];
		case Z80_DC3: return Z80.int_state[3];
		case REG_PREVIOUSPC: return Z80.PREPC.w.l;
		default:
			if( regnum <= REG_SP_CONTENTS )
			{
				unsigned offset = _SPD + 2 * (REG_SP_CONTENTS - regnum);
				if( offset < 0xffff )
					return RM( offset ) | ( RM( offset + 1) << 8 );
			}
	}
	return 0;
}

/****************************************************************************
 * Set a specific register
 ****************************************************************************/
#ifdef Z80_MSX
void z80_msx_set_reg (int regnum, unsigned val)
#else
void z80_set_reg (int regnum, unsigned val)
#endif
{
    SMS_Z_CACHE;
	switch( regnum )
	{
		case REG_PC: _PC = val; change_pc16(_PCD); break;
		case Z80_PC: Z80.PC.w.l = val; break;
		case REG_SP: _SP = val; break;
		case Z80_SP: Z80.SP.w.l = val; break;
		case Z80_AF: Z80.AF.w.l = val; break;
		case Z80_BC: Z80.BC.w.l = val; break;
		case Z80_DE: Z80.DE.w.l = val; break;
		case Z80_HL: Z80.HL.w.l = val; break;
		case Z80_IX: Z80.IX.w.l = val; break;
		case Z80_IY: Z80.IY.w.l = val; break;
		case Z80_R: Z80.R = val; Z80.R2 = val & 0x80; break;
		case Z80_I: Z80.I = val; break;
		case Z80_AF2: Z80.AF2.w.l = val; break;
		case Z80_BC2: Z80.BC2.w.l = val; break;
		case Z80_DE2: Z80.DE2.w.l = val; break;
		case Z80_HL2: Z80.HL2.w.l = val; break;
		case Z80_IM: Z80.IM = val; break;
		case Z80_IFF1: Z80.IFF1 = val; break;
		case Z80_IFF2: Z80.IFF2 = val; break;
		case Z80_HALT: Z80.HALT = val; break;
#ifdef Z80_MSX
		case Z80_NMI_STATE: z80_msx_set_irq_line(IRQ_LINE_NMI,val); break;
		case Z80_IRQ_STATE: z80_msx_set_irq_line(0,val); break;
#else
		case Z80_NMI_STATE: z80_set_irq_line(IRQ_LINE_NMI,val); break;
		case Z80_IRQ_STATE: z80_set_irq_line(0,val); break;
#endif
		case Z80_DC0: Z80.int_state[0] = val; break;
		case Z80_DC1: Z80.int_state[1] = val; break;
		case Z80_DC2: Z80.int_state[2] = val; break;
		case Z80_DC3: Z80.int_state[3] = val; break;
		default:
			if( regnum <= REG_SP_CONTENTS )
			{
				unsigned offset = _SPD + 2 * (REG_SP_CONTENTS - regnum);
				if( offset < 0xffff )
				{
					WM( offset, val & 0xff );
					WM( offset+1, (val >> 8) & 0xff );
				}
			}
	}
}

/****************************************************************************
 * Set IRQ line state
 ****************************************************************************/
#ifdef Z80_MSX
void z80_msx_set_irq_line(int irqline, int state)
#else
void z80_set_irq_line(int irqline, int state)
#endif
{
    SMS_Z_CACHE;
	if (irqline == IRQ_LINE_NMI)
	{
		if( Z80.nmi_state == state ) return;

        LOG(("Z80 #%d set_irq_line (NMI) %d\n", 0, state));
		Z80.nmi_state = state;
		if( state == CLEAR_LINE ) return;

        LOG(("Z80 #%d take NMI\n", 0));
		_PPC = -1;			/* there isn't a valid previous program counter */
		LEAVE_HALT; 		/* Check if processor was halted */

		_IFF1 = 0;
		PUSH( PC );
		_PCD = 0x0066;
		Z80.extra_cycles += 11;
	}
	else
	{
        LOG(("Z80 #%d set_irq_line %d\n",0 , state));
		Z80.irq_state = state;
		if( state == CLEAR_LINE ) return;

		if( Z80.irq_max )
		{
			int daisychain, device, int_state;
			daisychain = (*Z80.irq_callback)(irqline);
			device = daisychain >> 8;
			int_state = daisychain & 0xff;
            LOG(("Z80 #%d daisy chain $%04x -> device %d, state $%02x",0, daisychain, device, int_state));

			if( Z80.int_state[device] != int_state )
			{
				LOG((" change\n"));
				/* set new interrupt status */
				Z80.int_state[device] = int_state;
				/* check interrupt status */
				Z80.request_irq = Z80.service_irq = -1;

				/* search higher IRQ or IEO */
				for( device = 0 ; device < Z80.irq_max ; device ++ )
				{
					/* IEO = disable ? */
					if( Z80.int_state[device] & Z80_INT_IEO )
					{
						Z80.request_irq = -1;		/* if IEO is disable , masking lower IRQ */
						Z80.service_irq = device;	/* set highest interrupt service device */
					}
					/* IRQ = request ? */
					if( Z80.int_state[device] & Z80_INT_REQ )
						Z80.request_irq = device;
				}
                LOG(("Z80 #%d daisy chain service_irq $%02x, request_irq $%02x\n", 0, Z80.service_irq, Z80.request_irq));
				if( Z80.request_irq < 0 ) return;
			}
			else
			{
				LOG((" no change\n"));
				return;
			}
		}
		take_interrupt();
	}
}

/****************************************************************************
 * Set IRQ vector callback
 ****************************************************************************/
#ifdef Z80_MSX
void z80_msx_set_irq_callback(int (*callback)(int))
#else
void z80_set_irq_callback(int (*callback)(int))
#endif
{
    SMS_Z_CACHE;
    LOG(("Z80 #%d set_irq_callback $%08x\n",0 , (int)callback));
	Z80.irq_callback = callback;
}


void z80_reset_cycle_count(void)
{
    z80_cycle_count = 0;
}

int z80_get_elapsed_cycles(void)
{
    SMS_Z_CACHE;
    if(z80_exec == 1)
    {
        // inside execution loop
        int new = z80_requested_cycles - z80_ICount;
        return z80_cycle_count + new;
    }

    return z80_cycle_count;
}

