/*
    drz80_glue.c -- adapter layer between SMS Plus and DrZ80 (ARM asm Z80 core).

    Purpose: replace the C interpreter (z80.c) with DrZ80 without the rest of
    SMS Plus noticing. We expose the SAME function names SMS Plus already calls
    (z80_init/z80_reset/z80_execute/z80_set_irq_line/z80_get_reg/...), but drive
    DrZ80 underneath. Selected at build time by the USE_DRZ80 macro; when it is
    NOT defined this file compiles to nothing and the original z80.c is used.

    Modeled on PicoDrive's pico/z80if.c by notaz, adapted to SMS Plus's memory
    model (cpu_readmap[64] of 1KB pointers + cpu_writemem16/port function ptrs).

    Memory table format expected by DrZ80's built-in XMAP reader:
      map[i] = (base_ptr - start_addr) >> 1        for RAM/ROM (direct pointer)
      map[i] = (handler_fn >> 1) | MAP_FLAG        for I/O-like handler regions
    Read:  data = *(u8*)((map[i] << 1) + addr)     (RAM/ROM)
           data = ((fn*)(map[i] << 1))(addr)        (handler)
    Everything is >>1 so bit31 (MAP_FLAG) is free as the "is a function" flag.
*/

#include "shared.h"

#ifdef USE_DRZ80

#include "drz80.h"

/* DrZ80 uses 1KB pages (Z80_MEM_SHIFT=10), exactly like our cpu_readmap[64]. */
#define DRZ80_MEM_SHIFT 10
#define DRZ80_MAP_SIZE  (0x10000 >> DRZ80_MEM_SHIFT)   /* 64 entries */
#define DRZ80_MAP_FLAG  ((unsigned int)1 << 31)

struct DrZ80 drZ80;

static unsigned int drz80_read_map[DRZ80_MAP_SIZE];
static unsigned int drz80_write_map[DRZ80_MAP_SIZE];

/* SMS Plus globals we bridge to (declared in sms_extern.h) */
extern uint8*  cpu_readmap[64];
extern void  (*cpu_writemem16)(int address, int data);
extern uint8   (*cpu_readport16)(uint16 port);
extern void  (*cpu_writeport16)(uint16 port, uint8 data);

/* IRQ callback SMS Plus installs */
static int (*sms_irq_cb)(int) = NULL;

/* -------------------------------------------------------------------------
   Memory handler functions handed to DrZ80 (called for the write path and
   for I/O). Reads go direct through the map (pointers), so we only need
   handler functions for writes and ports.
   ------------------------------------------------------------------------- */

static unsigned char drz80_read8(unsigned short a)
{
    /* Direct read via SMS Plus map (mirrors cpu_readmem16). */
    return cpu_readmap[a >> DRZ80_MEM_SHIFT][a & 0x03FF];
}

static unsigned short drz80_read16(unsigned short a)
{
    unsigned char lo = cpu_readmap[a >> DRZ80_MEM_SHIFT][a & 0x03FF];
    unsigned short a1 = a + 1;
    unsigned char hi = cpu_readmap[a1 >> DRZ80_MEM_SHIFT][a1 & 0x03FF];
    return lo | (hi << 8);
}

static void drz80_write8(unsigned char d, unsigned short a)
{
    cpu_writemem16(a, d);
}

static void drz80_write16(unsigned short d, unsigned short a)
{
    cpu_writemem16(a, d & 0xFF);
    cpu_writemem16(a + 1, (d >> 8) & 0xFF);
}

static unsigned char drz80_in(unsigned short p)
{
    return cpu_readport16(p & 0xFF);
}

static void drz80_out(unsigned short p, unsigned char d)
{
    cpu_writeport16(p & 0xFF, d);
}

/* -------------------------------------------------------------------------
   PC/SP rebasing. DrZ80 keeps PC and SP as DIRECT host pointers
   (membase + z80addr). On any jump/call/ret or SP load, DrZ80 calls these to
   recompute the base. We derive the base from cpu_readmap. Modeled on
   PicoDrive drz80_load_pcsp / dz80_rebase_pc.
   For opcode fetch DrZ80 needs a contiguous pointer; cpu_readmap gives 1KB
   blocks that are contiguous within a bank, which is enough (DrZ80 re-bases
   when crossing into a new page via its XMAP path).
   ------------------------------------------------------------------------- */

static unsigned int drz80_rebase_pc(unsigned short pc)
{
    /* base pointer for the 1KB page holding pc, minus the page origin, so that
       base + pc points at the right byte. cpu_readmap[i] already points at the
       block start, i.e. corresponds to (i<<10); subtract that page origin. */
    uint8 *blk = cpu_readmap[pc >> DRZ80_MEM_SHIFT];
    unsigned int base = (unsigned int)blk - ((pc >> DRZ80_MEM_SHIFT) << DRZ80_MEM_SHIFT);
    drZ80.Z80PC_BASE = base;
    return base + pc;
}

static unsigned int drz80_rebase_sp(unsigned short sp)
{
    uint8 *blk = cpu_readmap[sp >> DRZ80_MEM_SHIFT];
    unsigned int base = (unsigned int)blk - ((sp >> DRZ80_MEM_SHIFT) << DRZ80_MEM_SHIFT);
    drZ80.Z80SP_BASE = base;
    return base + sp;
}

/* -------------------------------------------------------------------------
   Public interface -- SAME names SMS Plus already calls.
   ------------------------------------------------------------------------- */

void z80_init(void)
{
    memset(&drZ80, 0, sizeof(drZ80));
    drZ80.z80_rebasePC = drz80_rebase_pc;
    drZ80.z80_rebaseSP = drz80_rebase_sp;
    drZ80.z80_read8    = drz80_read8;
    drZ80.z80_read16   = drz80_read16;
    drZ80.z80_write8   = drz80_write8;
    drZ80.z80_write16  = drz80_write16;
    drZ80.z80_in       = drz80_in;
    drZ80.z80_out      = drz80_out;
    drZ80.z80_irq_callback = NULL;
}

void z80_reset(void *param)
{
    (void)param;
    drZ80.Z80I  = 0;
    drZ80.Z80IM = 0;
    drZ80.Z80IF = 0;
    drZ80.z80irqvector = 0xff0000;   /* RST 38h */
    drZ80.Z80A  = 0x00 << 24;
    drZ80.Z80F  = 0;
    drZ80.Z80BC = 0;
    drZ80.Z80DE = 0;
    drZ80.Z80HL = 0;
    drZ80.Z80IX = 0xFFFF;
    drZ80.Z80IY = 0xFFFF;
    /* PC starts at 0x0000; base from the map. */
    drZ80.Z80PC = drz80_rebase_pc(0x0000);
    /* SMS BIOS leaves SP near top of RAM; DrZ80 handles SP via mem funcs. */
    drZ80.Z80SP = drz80_rebase_sp(0xDFF0);
}

int z80_execute(int cycles)
{
    /* DrZ80Run returns the number of cycles NOT executed (like many cores),
       or the leftover -- we compute how many were actually run. PicoDrive uses
       the return as remaining; DrZ80 decrements drZ80.cycles. We pass cycles
       and read back how many are left to report consumed. */
    int left, done;
    drZ80.cycles = cycles;
    DrZ80Run(&drZ80, cycles);
    left = drZ80.cycles;
    done = cycles - left;   /* cycles actually executed */
    z80_cycle_count += done;
    return done;
}

unsigned z80_get_reg(int regnum)
{
    switch(regnum)
    {
        case Z80_PC:  return (drZ80.Z80PC - drZ80.Z80PC_BASE) & 0xFFFF;
        case Z80_SP:  return (drZ80.Z80SP - drZ80.Z80SP_BASE) & 0xFFFF;
        case Z80_AF:  return ((drZ80.Z80A >> 24) << 8) | (drZ80.Z80F & 0xFF);
        case Z80_BC:  return drZ80.Z80BC >> 16;
        case Z80_DE:  return drZ80.Z80DE >> 16;
        case Z80_HL:  return drZ80.Z80HL >> 16;
        case Z80_IX:  return drZ80.Z80IX & 0xFFFF;
        case Z80_IY:  return drZ80.Z80IY & 0xFFFF;
        case Z80_AF2: return ((drZ80.Z80A2 >> 24) << 8) | (drZ80.Z80F2 & 0xFF);
        case Z80_BC2: return drZ80.Z80BC2 >> 16;
        case Z80_DE2: return drZ80.Z80DE2 >> 16;
        case Z80_HL2: return drZ80.Z80HL2 >> 16;
        default:      return 0;
    }
}

void z80_set_irq_line(int irqline, int state)
{
    /* SMS: line IRQ (irqline 0) and NMI (IRQ_LINE_NMI). DrZ80 uses Z80_IRQ
       field + Z80IF flags. ASSERT_LINE raises, CLEAR_LINE lowers. */
    if(irqline == IRQ_LINE_NMI)
    {
        if(state == ASSERT_LINE)
            drZ80.Z80IF |= 8;   /* bit3 = NMI pending (per drz80.h) */
    }
    else
    {
        if(state == ASSERT_LINE)
            drZ80.Z80_IRQ = 1;
        else
            drZ80.Z80_IRQ = 0;
    }
}

void z80_set_irq_callback(int (*callback)(int))
{
    sms_irq_cb = callback;
}

void z80_reset_cycle_count(void)
{
    z80_cycle_count = 0;
}

int z80_get_elapsed_cycles(void)
{
    /* Used by vdp.c for hcounter approximation. z80_cycle_count is the running
       total (global in sms_globals.c); we bump it in z80_execute. */
    return z80_cycle_count;
}

/* NOTE: built with DRZ80_XMAP=0, so DrZ80 calls z80_read8/write8/etc. as
   FUNCTIONS (see drz80.S). That keeps integration simple and correct for the
   first bring-up: reads/writes go through the shims above into SMS Plus's
   cpu_readmap / cpu_writemem16 / ports. Once correct, switching DrZ80 to
   XMAP=1 with a packed pointer table is the speed optimization (direct memory
   reads without a function call per access). */

#endif /* USE_DRZ80 */
