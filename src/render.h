
#ifndef _RENDER_H_
#define _RENDER_H_

/* Pack RGB data into a 16-bit RGB 5:6:5 format */
#if defined(SMS_GLOBALS) && defined(E32_RGB444)
/* 9210i screen is EColor4K (RGB444): 4 bits per channel in low 12 bits. */
#define MAKE_PIXEL(r,g,b)   ((((r) & 0xF0) << 4) | ((g) & 0xF0) | (((b) & 0xF0) >> 4))
#else
/* Pack RGB data into a 16-bit RGB 5:6:5 format */
#define MAKE_PIXEL(r,g,b)   (((r << 8) & 0xF800) | ((g << 3) & 0x07E0) | ((b >> 3) & 0x001F))
#endif

/* Used for blanking a line in whole or in part */
#define BACKDROP_COLOR      (0x10 | (vdp.reg[7] & 0x0F))

/* Global data -- moved to heap state struct (EKA1). */
#ifndef SMS_STATE_DEFINING
#include "sms_state.h"
#endif

void render_shutdown(void);
void render_init(void);
void render_reset(void);
void render_line(int line);
void render_bg_sms(int line);
void render_obj_sms(int line);
void update_bg_pattern_cache(void);
void palette_sync(int index, int force);
void remap_8_to_16(int line);

/* E32SMS direct-to-VRAM output hooks (set by the EXE main under focus gating).
   When g_vram_enable is non-zero, remap_8_to_16() writes finished RGB444
   pixels straight into video memory at the centered destination, eliminating
   the separate blit pass. When zero, it falls back to bitmap.data. */
#if defined(SMS_GLOBALS)
extern unsigned char *g_vram_base;   /* VRAM framebuffer (palette already skipped) */
extern int            g_vram_stride;  /* bytes per VRAM scanline (1280) */
extern int            g_vram_dx;      /* destination x offset (centering) */
extern int            g_vram_dy;      /* destination y offset (centering) */
extern int            g_vram_enable;  /* 1 = write to VRAM, 0 = bitmap.data */
#endif


#endif /* _RENDER_H_ */
