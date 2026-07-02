/*
    z80_state.h  --  Z80-internal global -> heap-state access macros.

    These macros use very short names (Z80, EA, SZ, ...) that would be
    dangerous to expose to C++ translation units or Symbian SDK headers.
    Therefore they are NOT placed in sms_state.h; this header is included
    ONLY by z80.c, after shared.h (so SMS_State() and the struct are known).

    z80.c builds its register-access macros (_PC, _F, _A, ...) on top of the
    bare name `Z80`, and its flag lookups on SZ/SZP/SZHV_*; redirecting those
    bare names to the heap struct redirects the whole core with no per-site
    edits.  z80_ICount is the real storage name (Z80_ICOUNT is z80.c's own
    alias macro for it).
*/
#ifndef _Z80_STATE_H_
#define _Z80_STATE_H_

#if defined(SMS_GLOBALS)
#include "z80_extern.h"
#elif !defined(SMS_STATE_DEFINING)

/* E32SMS perf: the Z80 core touches these names several times per opcode.
   They used to expand to SMS_State()->X, and SMS_State() is a cross-module
   extern "C" accessor (CCoeEnv::Static()->AppUi()->Document()->SmsState())
   that cannot inline into z80.c -- tens of millions of such calls per second,
   ~80% of frame time. They now read a LOCAL cached pointer _Z that each
   function sets ONCE on entry via the SMS_Z_CACHE macro. _Z is a plain local,
   so no writable static data is introduced (EKA1-safe). */
#define SMS_Z_CACHE          SMS_State_t* _Z = SMS_State()
#define Z80                  (_Z->z80regs)
#define z80_ICount           (_Z->z80_ICount)
#define EA                   (_Z->EA)
#define SZ                   (_Z->SZ)
#define SZ_BIT               (_Z->SZ_BIT)
#define SZP                  (_Z->SZP)
#define SZHV_inc             (_Z->SZHV_inc)
#define SZHV_dec             (_Z->SZHV_dec)
#define SZHVC_add            (_Z->SZHVC_add)
#define SZHVC_sub            (_Z->SZHVC_sub)

/* E32SMS perf, phase 2: the memory map tables are hit on EVERY Z80 memory
   access (opcode fetch, operand read, every RM/WM) -- more often than the
   registers above. They are declared in sms_state.h as SMS_State()->X (used by
   render.c, vdp.c, etc.), so we cannot change them globally. Here, in z80.c
   ONLY, we override them to read the local _Z cache. Every opcode and helper
   already declares _Z via SMS_Z_CACHE, so this is in scope wherever these are
   used inside the core. */
#undef  cpu_readmap
#undef  cpu_writemap
#undef  cpu_writemem16
#define cpu_readmap          (_Z->cpu_readmap)
#define cpu_writemap         (_Z->cpu_writemap)
#define cpu_writemem16       (_Z->cpu_writemem16)

#endif /* !SMS_STATE_DEFINING */

#endif /* _Z80_STATE_H_ */
