/*
    render.c --
    Display rendering.
*/

#include "shared.h"

/* E32SMS / ARM920T (Nokia 9210i) fix: render_bg_sms() builds linebuf_ptr as
   (uint32*)&linebuf[0 - shift], which is NOT 4-byte aligned whenever the fine
   horizontal scroll (hscroll & 7) is non-zero. The default read_dword/
   write_dword do *(uint32*)address, i.e. an unaligned 32-bit access. x86 (WINS)
   tolerates this, but ARM920T raises a Data Abort -> KERN-EXEC 3. This only
   triggers once the ROM enables the background AND sets a non-zero fine scroll,
   which is why it ran for many frames before faulting.
   render.c already ships an alignment-safe byte-wise read_dword/write_dword,
   gated behind ALIGN_DWORD. Define it here so that path is compiled in. The
   aligned fast-path (*(uint32*)address) is still used when the address is
   4-byte aligned, so there is no cost for aligned accesses.
   LSB_FIRST resolves to 1 via shared.h -> z80.h -> osd_cpu.h, so the byte-wise
   path assembles the dword in correct little-endian order. */
#define ALIGN_DWORD 1

/* All of render.c's writable tables (sms_cram_expand_table, gg_cram_expand_table,
   render_bg, render_obj, linebuf, internal_buffer, pixel, bg_name_dirty,
   bg_name_list, bg_list_index, bg_pattern_cache, lut, bp_lut) now live in the
   heap state struct (sms_state.h).  render_bg/render_obj are set to their SMS
   handlers in render_reset(); linebuf is pointed at the current bitmap row in
   render_line().  Only the const attribute-expansion table stays here. */

/* Attribute expansion table */
static const uint32 atex[4] =
{
    0x00000000,
    0x10101010,
    0x20202020,
    0x30303030,
};

/* Macros to access memory 32-bits at a time (from MAME's drawgfx.c) */

#ifdef ALIGN_DWORD

static __inline__ uint32 read_dword(void *address)
{
    if ((uint32)address & 3)
	{
#ifdef LSB_FIRST  /* little endian version */
        return ( *((uint8 *)address) +
                (*((uint8 *)address+1) << 8)  +
                (*((uint8 *)address+2) << 16) +
                (*((uint8 *)address+3) << 24) );
#else             /* big endian version */
        return ( *((uint8 *)address+3) +
                (*((uint8 *)address+2) << 8)  +
                (*((uint8 *)address+1) << 16) +
                (*((uint8 *)address)   << 24) );
#endif
	}
	else
        return *(uint32 *)address;
}


static __inline__ void write_dword(void *address, uint32 data)
{
    if ((uint32)address & 3)
	{
#ifdef LSB_FIRST
            *((uint8 *)address) =    data;
            *((uint8 *)address+1) = (data >> 8);
            *((uint8 *)address+2) = (data >> 16);
            *((uint8 *)address+3) = (data >> 24);
#else
            *((uint8 *)address+3) =  data;
            *((uint8 *)address+2) = (data >> 8);
            *((uint8 *)address+1) = (data >> 16);
            *((uint8 *)address)   = (data >> 24);
#endif
		return;
  	}
  	else
        *(uint32 *)address = data;
}
#else
#define read_dword(address) *(uint32 *)address
#define write_dword(address,data) *(uint32 *)address=data
#endif


/****************************************************************************/


void render_shutdown(void)
{
}

/* Initialize the rendering data */
void render_init(void)
{
    int i, j;
    int bx, sx, b, s, bp, bf, sf, c;

    make_tms_tables();

    /* Generate 64k of data for the look up table */
    for(bx = 0; bx < 0x100; bx++)
    {
        for(sx = 0; sx < 0x100; sx++)
        {
            /* Background pixel */
            b  = (bx & 0x0F);

            /* Background priority */
            bp = (bx & 0x20) ? 1 : 0;

            /* Full background pixel + priority + sprite marker */
            bf = (bx & 0x7F);

            /* Sprite pixel */
            s  = (sx & 0x0F);

            /* Full sprite pixel, w/ palette and marker bits added */
            sf = (sx & 0x0F) | 0x10 | 0x40;

            /* Overwriting a sprite pixel ? */
            if(bx & 0x40)
            {
                /* Return the input */
                c = bf;
            }
            else
            {
                /* Work out priority and transparency for both pixels */
                if(bp)
                {
                    /* Underlying pixel is high priority */
                    if(b)
                    {
                        c = bf | 0x40;
                    }
                    else
                    {
                        
                        if(s)
                        {
                            c = sf;
                        }
                        else
                        {
                            c = bf;
                        }
                    }
                }
                else
                {
                    /* Underlying pixel is low priority */
                    if(s)
                    {
                        c = sf;
                    }
                    else
                    {
                        c = bf;
                    }
                }
            }

            /* Store result */
            lut[(bx << 8) | (sx)] = c;
        }
    }

    /* Make bitplane to pixel lookup table */
    for(i = 0; i < 0x100; i++)
    for(j = 0; j < 0x100; j++)
    {
        int x;
        uint32 out = 0;
        for(x = 0; x < 8; x++)
        {
            out |= (j & (0x80 >> x)) ? (uint32)(8 << (x << 2)) : 0;
            out |= (i & (0x80 >> x)) ? (uint32)(4 << (x << 2)) : 0;
        }
#if LSB_FIRST
        bp_lut[(j << 8) | (i)] = out;
#else
        bp_lut[(i << 8) | (j)] = out;
#endif
    }

    for(i = 0; i < 4; i++)
    {
        uint8 c = i << 6 | i << 4 | i << 2 | i;
        sms_cram_expand_table[i] = c;
    }

    for(i = 0; i < 16; i++)
    {
        uint8 c = i << 4 | i;
        gg_cram_expand_table[i] = c;        
    }

    render_reset();

}


/* Reset the rendering data */
void render_reset(void)
{
    int i;

    /* Clear display bitmap */
    memset(bitmap.data, 0, bitmap.pitch * bitmap.height);

    /* Clear palette */
    for(i = 0; i < PALETTE_SIZE; i++)
    {
        palette_sync(i, 1);
    }

    /* Invalidate pattern cache */
    memset(bg_name_dirty, 0, sizeof(bg_name_dirty));
    memset(bg_name_list, 0, sizeof(bg_name_list));
    bg_list_index = 0;
    memset(bg_pattern_cache, 0, sizeof(bg_pattern_cache));

    /* Pick render routine */
    render_bg = render_bg_sms;
    render_obj = render_obj_sms;
}


/* Draw a line of the display */
void render_line(int line)
{
    extern unsigned int ProfNow(void);
    extern void ProfRemapAdd(unsigned int);
    extern void ProfCacheAdd(unsigned int);
    extern void ProfBgAdd(unsigned int);
    extern void ProfObjAdd(unsigned int);
    unsigned int _rt0, _rt1;
    unsigned int _pt0, _pt1;

    /* Ensure we're within the viewport range */
    if(line >= vdp.height)
        return;

    linebuf = (bitmap.depth == 8) ? &bitmap.data[(line * bitmap.pitch)] : &internal_buffer[8];

    /* Update pattern cache (timed) */
    _pt0 = ProfNow();
    update_bg_pattern_cache();
    _pt1 = ProfNow();
    ProfCacheAdd(_pt1 - _pt0);

    /* Blank line (full width) */
    if(!(vdp.reg[1] & 0x40))
    {
        memset(linebuf, BACKDROP_COLOR, bitmap.width);
    }
    else
    {
        /* Draw background (timed) */
        if(render_bg != NULL)
        {
            _pt0 = ProfNow();
            render_bg(line);
            _pt1 = ProfNow();
            ProfBgAdd(_pt1 - _pt0);
        }

        /* Draw sprites (timed) */
        if(render_obj != NULL)
        {
            _pt0 = ProfNow();
            render_obj(line);
            _pt1 = ProfNow();
            ProfObjAdd(_pt1 - _pt0);
        }

        /* Blank leftmost column of display */
        if(vdp.reg[0] & 0x20)
        {
            memset(linebuf, BACKDROP_COLOR, 8);
        }
    }

    if(bitmap.depth != 8)
    {
        _rt0 = ProfNow();
        remap_8_to_16(line);
        _rt1 = ProfNow();
        ProfRemapAdd(_rt1 - _rt0);
    }
}


/* Draw the Master System background */
void render_bg_sms(int line)
{
    /* E32SMS perf: vdp / linebuf / bg_pattern_cache each expand to SMS_State()
       (cross-module accessor). The column loop runs 32x/line and touched them
       repeatedly. Cache each into a local ONCE (macro used on the RHS so it
       expands cleanly to SMS_State()->X), then the loop reads only locals. */
    vdp_t  *vp = &vdp;
    uint8  *lb = linebuf;
    uint8  *bgpc = bg_pattern_cache;

    int locked = 0;
    int yscroll_mask = (vp->extended) ? 256 : 224;
    int v_line = (line + vp->reg[9]) % yscroll_mask;
    int v_row  = (v_line & 7) << 3;
    int hscroll = ((vp->reg[0] & 0x40) && (line < 0x10)) ? 0 : (0x100 - vp->reg[8]);
    int column = 0;
    uint16 attr;
    uint16 *nt = (uint16 *)&vp->vram[vp->ntab + ((v_line >> 3) << 6)];
    int nt_scroll = (hscroll >> 3);
    int shift = (hscroll & 7);
    uint32 atex_mask;
    uint32 *cache_ptr;
    uint32 *linebuf_ptr = (uint32 *)&lb[0 - shift];

    /* Draw first column (clipped) */
    if(shift)
    {
        int x;

        for(x = shift; x < 8; x++)
            lb[(0 - shift) + (x)] = 0;

        column++;
    }

    /* Draw a line of the background */
    for(; column < 32; column++)
    {
        /* Stop vertical scrolling for leftmost eight columns */
        if((vp->reg[0] & 0x80) && (!locked) && (column >= 24))
        {
            locked = 1;
            v_row = (line & 7) << 3;
            nt = (uint16 *)&vp->vram[((vp->reg[2] << 10) & 0x3800) + ((line >> 3) << 6)];
        }

        /* Get name table attribute word */
        attr = nt[(column + nt_scroll) & 0x1F];

#ifndef LSB_FIRST
        attr = (((attr & 0xFF) << 8) | ((attr & 0xFF00) >> 8));
#endif
        /* Expand priority and palette bits */
        atex_mask = atex[(attr >> 11) & 3];

        /* Point to a line of pattern data in cache */
        cache_ptr = (uint32 *)&bgpc[((attr & 0x7FF) << 6) | (v_row)];
        
        /* Copy the left half, adding the attribute bits in */
        write_dword( &linebuf_ptr[(column << 1)] , read_dword( &cache_ptr[0] ) | (atex_mask));

        /* Copy the right half, adding the attribute bits in */
        write_dword( &linebuf_ptr[(column << 1) | (1)], read_dword( &cache_ptr[1] ) | (atex_mask));
    }

    /* Draw last column (clipped) */
    if(shift)
    {
        int x, c, a;

        uint8 *p = &lb[(0 - shift)+(column << 3)];

        attr = nt[(column + nt_scroll) & 0x1F];

#ifndef LSB_FIRST
        attr = (((attr & 0xFF) << 8) | ((attr & 0xFF00) >> 8));
#endif
        a = (attr >> 7) & 0x30;

        for(x = 0; x < shift; x++)
        {
            c = bgpc[((attr & 0x7FF) << 6) | (v_row) | (x)];
            p[x] = ((c) | (a));
        }
    }
}




/* Draw sprites */
void render_obj_sms(int line)
{
    /* E32SMS perf: cache the SMS_State() macros used across the 64-sprite loop
       into locals once (macros on the RHS expand cleanly to SMS_State()->X). */
    vdp_t  *vp   = &vdp;
    uint8  *lb   = linebuf;
    uint8  *bgpc = bg_pattern_cache;
    uint8  *lutp = lut;

    int i;
    uint8 collision_buffer = 0;

    /* Sprite count for current line (8 max.) */
    int count = 0;

    /* Sprite dimensions */
    int width = 8;
    int height = (vp->reg[1] & 0x02) ? 16 : 8;

    /* Pointer to sprite attribute table */
    uint8 *st = (uint8 *)&vp->vram[vp->satb];

    /* Adjust dimensions for double size sprites */
    if(vp->reg[1] & 0x01)
    {
        width *= 2;
        height *= 2;
    }

    /* Draw sprites in front-to-back order */
    for(i = 0; i < 64; i++)
    {
        /* Sprite Y position */
        int yp = st[i];

        /* Found end of sprite list marker for non-extended modes? */
        if(vp->extended == 0 && yp == 208)
            goto end;

        /* Actual Y position is +1 */
        yp++;

        /* Wrap Y coordinate for sprites > 240 */
        if(yp > 240) yp -= 256;

        /* Check if sprite falls on current line */
        if((line >= yp) && (line < (yp + height)))
        {
            uint8 *linebuf_ptr;

            /* Width of sprite */
            int start = 0;
            int end = width;

            /* Sprite X position */
            int xp = st[0x80 + (i << 1)];

            /* Pattern name */
            int n = st[0x81 + (i << 1)];

            /* Bump sprite count */
            count++;

            /* Too many sprites on this line ? */
            if(count == 9)
            {
                vp->status |= 0x40;                
                goto end;
            }

            /* X position shift */
            if(vp->reg[0] & 0x08) xp -= 8;

            /* Add MSB of pattern name */
            if(vp->reg[6] & 0x04) n |= 0x0100;

            /* Mask LSB for 8x16 sprites */
            if(vp->reg[1] & 0x02) n &= 0x01FE;

            /* Point to offset in line buffer */
            linebuf_ptr = (uint8 *)&lb[xp];

            /* Clip sprites on left edge */
            if(xp < 0)
            {
                start = (0 - xp);
            }

            /* Clip sprites on right edge */
            if((xp + width) > 256)        
            {
                end = (256 - xp);
            }

            /* Draw double size sprite */
            if(vp->reg[1] & 0x01)
            {
                int x;
                uint8 *cache_ptr = (uint8 *)&bgpc[(n << 6) | (((line - yp) >> 1) << 3)];

                /* Draw sprite line */
                for(x = start; x < end; x++)
                {
                    /* Source pixel from cache */
                    uint8 sp = cache_ptr[(x >> 1)];
    
                    /* Only draw opaque sprite pixels */
                    if(sp)
                    {
                        /* Background pixel from line buffer */
                        uint8 bg = linebuf_ptr[x];
    
                        /* Look up result */
                        linebuf_ptr[x] = lutp[(bg << 8) | (sp)];
    
                        /* Update collision buffer */
                        collision_buffer |= bg;
                    }
                }
            }
            else /* Regular size sprite (8x8 / 8x16) */
            {
                int x;
                uint8 *cache_ptr = (uint8 *)&bgpc[(n << 6) | ((line - yp) << 3)];

                /* Draw sprite line */
                for(x = start; x < end; x++)
                {
                    /* Source pixel from cache */
                    uint8 sp = cache_ptr[x];
    
                    /* Only draw opaque sprite pixels */
                    if(sp)
                    {
                        /* Background pixel from line buffer */
                        uint8 bg = linebuf_ptr[x];
    
                        /* Look up result */
                        linebuf_ptr[x] = lutp[(bg << 8) | (sp)];
    
                        /* Update collision buffer */
                        collision_buffer |= bg;
                    }
                }
            }
        }
    }
end:
    /* Set sprite collision flag */
    if(collision_buffer & 0x40)
        vp->status |= 0x20;
}



void update_bg_pattern_cache(void)
{
    int i;
    uint8 x, y;
    uint16 name;

    if(!bg_list_index) return;

    {
    /* E32SMS perf: cache the SMS_State() macros used in the rebuild loops. */
    uint8  *bgnd  = bg_name_dirty;
    uint16 *bgnl  = bg_name_list;
    uint8  *bgpc  = bg_pattern_cache;
    vdp_t  *vp    = &vdp;
    uint32 *bplut = bp_lut;
    int     limit = bg_list_index;

    for(i = 0; i < limit; i++)
    {
        name = bgnl[i];
        bgnl[i] = 0;

        for(y = 0; y < 8; y++)
        {
            if(bgnd[name] & (1 << y))
            {
                uint8 *dst = &bgpc[name << 6];

                uint16 bp01 = *(uint16 *)&vp->vram[(name << 5) | (y << 2) | (0)];
                uint16 bp23 = *(uint16 *)&vp->vram[(name << 5) | (y << 2) | (2)];
                uint32 temp = (bplut[bp01] >> 2) | (bplut[bp23]);

                for(x = 0; x < 8; x++)
                {
                    uint8 c = (temp >> (x << 2)) & 0x0F;
                    dst[0x00000 | (y << 3) | (x)] = (c);
                    dst[0x08000 | (y << 3) | (x ^ 7)] = (c);
                    dst[0x10000 | ((y ^ 7) << 3) | (x)] = (c);
                    dst[0x18000 | ((y ^ 7) << 3) | (x ^ 7)] = (c);
                }
            }
        }
        bgnd[name] = 0;
    }
    }
    bg_list_index = 0;
}


/* Update a palette entry */
void palette_sync(int index, int force)
{
    int r, g, b;

    // unless we are forcing an update,
    // if not in mode 4, exit


    if(IS_SMS && !force && ((vdp.reg[0] & 4) == 0) )
        return;

    if(IS_GG)
    {
        /* ----BBBBGGGGRRRR */
        r = (vdp.cram[(index << 1) | (0)] >> 0) & 0x0F;
        g = (vdp.cram[(index << 1) | (0)] >> 4) & 0x0F;
        b = (vdp.cram[(index << 1) | (1)] >> 0) & 0x0F;
    
        r = gg_cram_expand_table[r];
        g = gg_cram_expand_table[g];
        b = gg_cram_expand_table[b];
    }
    else
    {
        /* --BBGGRR */
        r = (vdp.cram[index] >> 0) & 3;
        g = (vdp.cram[index] >> 2) & 3;
        b = (vdp.cram[index] >> 4) & 3;
    
        r = sms_cram_expand_table[r];
        g = sms_cram_expand_table[g];
        b = sms_cram_expand_table[b];
    }
    
    bitmap.pal.color[index][0] = r;
    bitmap.pal.color[index][1] = g;
    bitmap.pal.color[index][2] = b;

    pixel[index] = MAKE_PIXEL(r, g, b);

    bitmap.pal.dirty[index] = bitmap.pal.update = 1;
}

void remap_8_to_16(int line)
{
    /* E32SMS perf: 'bitmap', 'pixel', 'internal_buffer' are macros that each
       expand to SMS_State() -- a CCoeEnv::Static()->AppUi()->Document()->
       SmsState() chain. The old loop body invoked them ~8x per pixel, i.e.
       hundreds of thousands of accessor calls per frame; this single pass was
       ~90% of all render time. Capture each macro into a local ONCE (one
       SMS_State() apiece), then the inner loop touches only plain pointers.
       NOTE: we must NOT write st->pixel etc. -- 'pixel'/'internal_buffer' are
       macros and would expand inside the member reference. Use the macros
       directly on the RHS so they expand cleanly to SMS_State()->member.
       The old line==20 red debug stripe is removed. */
    uint16 *pal = pixel;
    uint8  *ib  = internal_buffer;
    int x0   = bitmap.viewport.x;
    int xend  = bitmap.viewport.w + x0;
    int i;

#if defined(SMS_GLOBALS)
    /* E32SMS: when VRAM output is enabled, write finished RGB444 pixels
       straight into video memory at the centered destination -- this single
       pass replaces BOTH the old remap (8->16 into bitmap.data) AND the
       separate blit pass in the EXE main. Palette 'pixel' is already RGB444
       (see MAKE_PIXEL under E32_RGB444). */
    if(g_vram_enable && g_vram_base)
    {
        uint8  *ibp = ib + 8 + x0;               /* source index, +8 origin   */
        uint32 *d32 = (uint32 *)(g_vram_base
                       + (g_vram_dy + line) * g_vram_stride
                       + (g_vram_dx + x0) * 2);
        int w  = xend - x0;

#if defined(E32_ASM_REMAP)
        /* ARM assembly blitter: index -> palette -> paired burst write to VRAM.
           Handles the whole line (including any odd tail) in asmblit.S. */
        extern void asm_remap_line(unsigned short* dst, const unsigned char* src,
                                   const unsigned short* pal, int npix);
        asm_remap_line((unsigned short*)d32, ibp, pal, w);
        return;
#else
        /* C paired-write fallback: pack two RGB444 pixels per 32-bit word. */
        int wp = w >> 1;
        int k;
        for(k = 0; k < wp; k++)
        {
            uint32 lo = pal[ ibp[0] & PIXEL_MASK ];
            uint32 hi = pal[ ibp[1] & PIXEL_MASK ];
            *d32++ = lo | (hi << 16);
            ibp += 2;
        }
        if(w & 1)   /* odd tail pixel */
        {
            *((uint16 *)d32) = pal[ ibp[0] & PIXEL_MASK ];
        }
        return;
#endif
    }
#endif

    {
    /* Fallback: original path into bitmap.data (used by the .app build, or
       when VRAM output is gated off). */
    uint16 *p   = (uint16 *)&bitmap.data[(line * bitmap.pitch)];
    for(i = x0; i < xend; i++)
    {
        p[i] = pal[ ib[i + 8] & PIXEL_MASK ];
    }
    }
}
