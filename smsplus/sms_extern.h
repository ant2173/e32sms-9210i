/*
    sms_extern.h  --  EXE build (SMS_GLOBALS): extern declarations of every
    emulator global defined in sms_globals.c.

    Included by sms_state.h in place of the heap-redirection macros when
    SMS_GLOBALS is defined. Engine .c files thus see real globals with the
    expected names and zero source changes.

    Keep field list in sync with sms_globals.c and SMS_State_t.
*/
#ifndef _SMS_EXTERN_H_
#define _SMS_EXTERN_H_

#ifdef __cplusplus
extern "C" {
#endif

/* ---- system.c ---- */
extern bitmap_t  bitmap;
extern cart_t    cart;
extern input_t   input;

/* ---- sms.c ---- */
extern sms_t     sms;
extern uint8     dummy_write[0x400];
extern uint8     dummy_read[0x400];

/* ---- vdp.c ---- */
extern vdp_t     vdp;

/* ---- memz80.c ---- */
extern uint8     data_bus_pullup;
extern uint8     data_bus_pulldown;

/* ---- loadrom.c ---- */
extern char      game_name[];

/* ---- render.c ---- */
extern uint8     sms_cram_expand_table[4];
extern uint8     gg_cram_expand_table[16];
extern void    (*render_bg)(int line);
extern void    (*render_obj)(int line);
extern uint8*    linebuf;
extern uint8     internal_buffer[0x200];
extern uint16    pixel[];
extern uint8     bg_name_dirty[0x200];
extern uint16    bg_name_list[0x200];
extern uint16    bg_list_index;
extern uint8     bg_pattern_cache[0x20000];
extern uint8     lut[0x10000];
extern uint32    bp_lut[0x10000];

/* ---- tms.c ---- */
extern int        text_counter;
extern uint8      tms_lookup[16][256][2];
extern uint8      mc_lookup[16][256][8];
extern uint8      txt_lookup[256][2];
extern uint8      bp_expand[256][8];
extern uint8      tms_obj_lut[16*256];
extern int        sprites_found;
extern tms_sprite sprites[4];

/* ---- pio.c ---- */
extern io_state   io_lut[2][256];
extern io_state  *io_current;

/* ---- z80.c (long names shared with sms.c/memz80.c/state.c) ---- */
extern int        z80_exec;
extern int        z80_cycle_count;
extern int        z80_requested_cycles;
extern int        after_EI;
extern uint8*     cpu_readmap[64];
extern uint8*     cpu_writemap[64];
extern void    (*cpu_writemem16)(int address, int data);
extern void    (*cpu_writeport16)(uint16 port, uint8 data);
extern uint8   (*cpu_readport16)(uint16 port);
extern Z80_Regs*  Z80_Context;

#ifdef __cplusplus
}
#endif

#endif /* _SMS_EXTERN_H_ */
