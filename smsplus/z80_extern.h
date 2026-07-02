/*
    z80_extern.h  --  EXE build (SMS_GLOBALS): Z80-internal globals as direct
    names, replacing the _Z pointer-cache macros used in the .app/DLL build.

    In the EXE, z80regs/EA/SZ/... are real globals, so the per-opcode _Z cache
    is unnecessary -- direct global access is already fast. SMS_Z_CACHE becomes
    a no-op; the short names map straight onto the globals defined in
    sms_globals.c.

    Included ONLY by z80.c (same as z80_state.h), when SMS_GLOBALS is defined.
*/
#ifndef _Z80_EXTERN_H_
#define _Z80_EXTERN_H_

/* No per-opcode cache in the EXE build. But SMS_Z_CACHE is invoked as
   `SMS_Z_CACHE;` as the FIRST line of many z80.c functions, BEFORE other
   local declarations. In C89 (gcc 2.9) declarations must precede statements,
   so the macro must expand to a DECLARATION ONLY (no trailing statement),
   otherwise the real declarations that follow become "code after statement"
   -> parse error. A single unused dummy declaration is safe; the following
   `SMS_Z_CACHE;` adds the terminating `;`. Mark unused to quiet the warning
   where the compiler supports it; gcc 2.9 just emits a benign warning. */
#define SMS_Z_CACHE   int _z_cache_dummy

/* Short Z80-internal names -> real globals (defined in sms_globals.c). */
#define Z80           z80regs

extern Z80_Regs z80regs;
extern int      z80_ICount;
extern uint32   EA;
extern uint8    SZ[256];
extern uint8    SZ_BIT[256];
extern uint8    SZP[256];
extern uint8    SZHV_inc[256];
extern uint8    SZHV_dec[256];
extern uint8*   SZHVC_add;
extern uint8*   SZHVC_sub;
/* cpu_readmap/cpu_writemap/cpu_writemem16 are declared extern in sms_extern.h
   and used directly here (no _Z override). */

#endif /* _Z80_EXTERN_H_ */
