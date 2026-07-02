/*
    e32smsemu_main.cpp  --  Stage 1 skeleton of the E32SMS emulator EXE.

    Combines:
      * The proven vidtest v5 video path: fullscreen window + focus gating +
        direct VRAM write at iScreenAddress+32 (12bpp palette skip), stride
        1280, 640x200, EColor4K (RGB444).
      * SMS Plus core brought up with PLAIN GLOBALS (SMS_GLOBALS build), i.e.
        no SMS_State() accessor. Init sequence ported from the old appui.
      * A game loop: system_frame() -> convert 256x192 RGB565 bitmap to RGB444
        -> blit centered into the 640x200 framebuffer. Naive C blit for now;
        ARM blitter comes later.

    ROM path is hardcoded for this milestone (fast path to a playable picture);
    a launcher .app + RProcess args come in a later stage.

    The SMS core is C; this file is C++ and calls it through extern "C".
*/

#include <e32base.h>
#include <e32std.h>
#include <e32svr.h>
#include <w32std.h>
#include <f32file.h>

// ---- SMS core (C) ----------------------------------------------------------
// E32_RGB444 is defined build-wide via the MMP (MACRO E32_RGB444) so that
// palette_sync() in render.c also builds the palette in RGB444.
extern "C" {
#include "shared.h"          // pulls sms.h/vdp.h/system.h/render.h + globals
void system_init(void);
void system_poweron(void);
void system_reset(void);
void system_frame(int skip_render);
int  load_rom(char *filename);
void render_reset(void);
// direct-to-VRAM output hooks (defined in sms_globals.c)
extern unsigned char *g_vram_base;
extern int            g_vram_stride;
extern int            g_vram_dx;
extern int            g_vram_dy;
extern int            g_vram_enable;
}

// ---- profiling bridges the core references (system.c) ----------------------
// system_frame() already brackets z80_execute() and render_line() with these.
// Turn them into real microsecond accumulators so we can split the ~50ms core
// time into Z80-execution vs VDP-line-render. ProfNow returns absolute us.
static TInt gProfZ80us = 0;    // accumulated within current 100-frame window
static TInt gProfRenderus = 0;
extern "C" unsigned int ProfNow(void)
    {
    TTime t; t.HomeTime();
    // low 32 bits of microseconds since 0AD is fine for delta math (wraps
    // ~every 71 min; our deltas are microseconds apart, so safe)
    return (unsigned int)t.Int64().Low();
    }
extern "C" void ProfZ80Add(unsigned int d)    { gProfZ80us += (TInt)d; }
extern "C" void ProfRenderAdd(unsigned int d)  { gProfRenderus += (TInt)d; }
static TInt gProfRemapus = 0;
extern "C" void ProfRemapAdd(unsigned int d)   { gProfRemapus += (TInt)d; }

// ---------------------------------------------------------------------------
_LIT(KLog,  "C:\\e32smsemu.log");
_LIT(KRom,  "C:\\System\\Apps\\e32smsemu\\rom.sms");

static RFs gFs; static RFile gFile; static TBool gOpen=EFalse;
static void LO(){ if(gFs.Connect()==KErrNone && gFile.Replace(gFs,KLog,EFileWrite|EFileShareAny)==KErrNone) gOpen=ETrue; }
static void LL(const TDesC8&a){ if(gOpen){ gFile.Write(a); gFile.Flush(); } }
static void LKV(const TDesC8&k,TInt v){ TBuf8<80>b;b.Append(k);TBuf8<24>n;n.Format(_L8("=%d\r\n"),v);b.Append(n);LL(b); }
static void LC(){ if(gOpen){ gFile.Flush(); gFile.Close(); gOpen=EFalse; } gFs.Close(); }

// ---- window-server resources (vidtest v5) ---------------------------------
static RWsSession       gWs;
static RWindowGroup     gGrp;
static RWindow          gWin;
static CWsScreenDevice* gScr = 0;
static CWindowGc*       gGc  = 0;
static TInt             gGrpId = 0;
static const TUint32    KWinHandle = 0x736d7377;

static TInt ConstructWindow()
    {
    TInt e;
    e = gWs.Connect(); if (e) return e;
    gScr = new CWsScreenDevice(gWs); if (!gScr) return KErrNoMemory;
    e = gScr->Construct(); if (e) return e;
    e = gScr->CreateContext(gGc); if (e) return e;
    gGrp = RWindowGroup(gWs);
    e = gGrp.Construct(KWinHandle); if (e) return e;
    gGrp.SetOrdinalPosition(0);
    gWin = RWindow(gWs);
    e = gWin.Construct(gGrp, KWinHandle + 1); if (e) return e;
    gWin.SetBackgroundColor(KRgbBlack);
    gWin.Activate();
    gWin.SetSize(gScr->SizeInPixels());
    gWin.SetVisible(ETrue);
    gGrpId = gGrp.Identifier();
    gWs.Flush();
    return KErrNone;
    }
static void FreeWindow()
    {
    if (gGc) { delete gGc; gGc = 0; }
    if (gScr) { delete gScr; gScr = 0; }
    if (gWin.WsHandle()) gWin.Close();
    if (gGrp.WsHandle()) gGrp.Close();
    gWs.Close();
    }

// ---- screen geometry (confirmed 9210i) ------------------------------------
static const TInt SCR_W = 640, SCR_H = 200, SCR_STRIDE = 1280;
static TUint8* gVram = 0;   // = iScreenAddress + 32 (palette skip)

// SMS visible area is 256x192. Center it in 640x200.
static const TInt SMS_W = 256, SMS_H = 192;
static const TInt DST_X = (SCR_W - SMS_W) / 2;   // = 192
static const TInt DST_Y = (SCR_H - SMS_H) / 2;   // = 4

// NOTE: the old To444()/BlitFrame() are gone. The core's remap_8_to_16() now
// writes finished RGB444 pixels straight into VRAM (centered) via the
// g_vram_* hooks, so there is no separate convert+blit pass anymore. The
// palette is already RGB444 (MAKE_PIXEL under E32_RGB444).

// ---- core bring-up (ported from old appui, now using globals) -------------
static TInt LoadRomFile()
    {
    RFs fs; TInt e = fs.Connect(); if (e) return e;
    RFile f;
    e = f.Open(fs, KRom, EFileRead);
    if (e != KErrNone) { fs.Close(); return e; }
    TInt size = 0; f.Size(size);
    if (size <= 0) { f.Close(); fs.Close(); return KErrCorrupt; }

    TInt romAlloc = ((size + 0x3FFF) / 0x4000) * 0x4000;
    if (romAlloc < 0x8000) romAlloc = 0x8000;
    cart.rom = (unsigned char*)User::Alloc(romAlloc);
    if (!cart.rom) { f.Close(); fs.Close(); return KErrNoMemory; }
    Mem::Fill(cart.rom, romAlloc, 0xFF);
    TPtr8 ptr(cart.rom, 0, size);
    e = f.Read(ptr);
    f.Close(); fs.Close();
    if (e != KErrNone) return e;

    cart.pages = (unsigned char)(romAlloc / 0x4000);
    if (cart.pages == 0) cart.pages = 1;
    cart.crc = 0; cart.sram_crc = 0; cart.mapper = 1; // MAPPER_SEGA
    return KErrNone;
    }

static TInt CoreInit()
    {
    // bitmap: 256x256, depth 16, granularity 2
    bitmap.width = 256; bitmap.height = 256; bitmap.depth = 16;
    bitmap.granularity = 2;
    bitmap.pitch = bitmap.width * bitmap.granularity;
    bitmap.data = (unsigned char*)User::Alloc(bitmap.pitch * bitmap.height);
    if (!bitmap.data) return KErrNoMemory;
    Mem::FillZ(bitmap.data, bitmap.pitch * bitmap.height);

    TInt e = LoadRomFile();
    if (e != KErrNone) return e;

    sms.console = 0x20;    // CONSOLE_SMS
    sms.display = 0;       // DISPLAY_NTSC
    sms.territory = 1;     // TERRITORY_EXPORT
    sms.use_fm = 0;

    system_init();
    system_poweron();
    system_reset();
    return KErrNone;
    }

GLDEF_C TInt E32Main()
    {
    CTrapCleanup* cleanup = CTrapCleanup::New();
    if (!cleanup) return KErrNoMemory;

    LO();
    LL(_L8("=== e32smsemu stage1 ===\r\n"));

    // 1) screen
    TScreenInfoV01 si; TPckg<TScreenInfoV01> p(si);
    UserSvr::ScreenInfo(p);
    if (!si.iScreenAddressValid || !si.iScreenAddress)
        { LL(_L8("no screen\r\n")); LC(); delete cleanup; return KErrNotSupported; }
    gVram = (TUint8*)si.iScreenAddress + 32;   // palette skip (12bpp)

    // Point the core's direct-VRAM output at the centered destination.
    // (g_vram_enable is toggled per-frame under focus gating below.)
    g_vram_base   = gVram;
    g_vram_stride = SCR_STRIDE;
    g_vram_dx     = DST_X;
    g_vram_dy     = DST_Y;
    g_vram_enable = 0;

    TInt e = ConstructWindow();
    LKV(_L8("window"), e);
    if (e != KErrNone) { LC(); delete cleanup; return e; }

    // 2) core
    e = CoreInit();
    LKV(_L8("coreInit"), e);
    if (e != KErrNone)
        {
        LL(_L8("core init failed (rom missing?)\r\n"));
        LC(); FreeWindow(); delete cleanup; return e;
        }
    LL(_L8("core up; entering loop\r\n"));
    LC();

    // 3) game loop. Runs continuously (the 600-frame cap was only a test
    //    safety). Exit when our window group loses focus for a sustained
    //    period, so there's still a clean exit path without a battery pull.
    //
    //    The core now writes RGB444 straight to VRAM inside system_frame()
    //    (via remap_8_to_16 + the g_vram_* hooks), so there's no separate
    //    blit pass. We gate it by setting g_vram_enable only while focused.
    TInt lostFocus = 0;
    TInt fpsFrames = 0;
    TTime fpsT0; fpsT0.HomeTime();
    TInt accCore = 0;   // system_frame() incl. direct-to-VRAM output

    for (;;)
        {
        TBool focused = (gWs.GetFocusWindowGroup() == gGrpId);
        g_vram_enable = focused ? 1 : 0;   // gate VRAM writes by focus

        TTime tA; tA.HomeTime();
        system_frame(0);   // run one SMS frame; renders + outputs to VRAM
        TTime tB; tB.HomeTime();
        accCore += (TInt)tB.MicroSecondsFrom(tA).Int64().Low();

        if (focused)
            {
            lostFocus = 0;
            // We write to VRAM directly, so we do NOT need to Invalidate the
            // window or Flush the WS every frame (that was costing ~6ms/frame
            // talking to the window server). We only Flush occasionally so the
            // WS event queue is serviced and focus changes are still noticed.
            if ((fpsFrames & 31) == 0)
                gWs.Flush();
            }
        else
            {
            // not focused: VRAM writes are gated off above. Count consecutive
            // misses; after a while assume the user left and exit cleanly.
            if (++lostFocus > 120) break;   // ~ a couple seconds of no focus
            User::After(16000);
            }

        // ---- FPS measurement via absolute microsecond clock ----
        // TickCount rate is ambiguous on this hardware; TTime is unambiguous.
        // On this old SDK TInt64 is a CLASS, so avoid 64-bit arithmetic:
        // microseconds for 100 frames fit comfortably in a 32-bit TInt
        // (~4-8 million at 12-25 fps), so extract the low 32 bits and use TInt.
        if (++fpsFrames >= 100)
            {
            TTime now; now.HomeTime();
            TTimeIntervalMicroSeconds delta = now.MicroSecondsFrom(fpsT0);
            TInt64 us64 = delta.Int64();
            TInt us = (TInt)us64.Low();      // low 32 bits; fits our range
            // fps*10 without 64-bit math: 100 frames -> fps10 = 1e9 / us
            TInt fps10 = 0;
            if (us > 0) fps10 = 1000000000 / us;   // (100 * 1e6 * 10) / us
            TBuf8<160> b;
            b.Format(_L8("frames=100 us=%d fps10=%d core_us=%d z80_us=%d rend_us=%d remap_us=%d\r\n"),
                     us, fps10, accCore, gProfZ80us, gProfRenderus, gProfRemapus);
            if (gFs.Connect() == KErrNone)
                {
                RFile lf;
                if (lf.Open(gFs, KLog, EFileWrite|EFileShareAny) == KErrNone ||
                    lf.Create(gFs, KLog, EFileWrite|EFileShareAny) == KErrNone)
                    { TInt pos=0; lf.Seek(ESeekEnd,pos); lf.Write(b); lf.Flush(); lf.Close(); }
                gFs.Close();
                }
            fpsFrames = 0;
            fpsT0 = now;
            accCore = 0;
            gProfZ80us = 0;
            gProfRenderus = 0;
            gProfRemapus = 0;
            }
        }

    FreeWindow();
    delete cleanup;
    return KErrNone;
    }
