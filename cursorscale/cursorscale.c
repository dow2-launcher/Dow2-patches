// CursorScale.dll - a bigger mouse cursor for high resolutions (Dawn of War II: Retribution 3.19, vanilla and Elite).
// Only the cursor changes size; the game's interface scale and everything else stay as they are.
//
// How the game draws its cursor (DOW2.exe 3.19 + Platform.dll):
//  - DATA:UI/cursor/cursorlist.lua lists 45 cursors (crsr, mv, atk_mv, ablty_tgt_*, cptr_pnt, grsn, ui-interactive, jumbo ...),
//    each a 48x48 TGA with a hotspot. DOW2.exe 0x0068CCC0 loads one (it insists on 48x48: "Cursor image is not %ix%i pixels!"),
//    copies the pixels into Plat::Input::CursorImage {hotX, hotY, 48*48 BGRA} and calls Plat::Input::CreateCursor (Platform.dll
//    export, -> the input object's vtable+0x5C = Platform+0x41E0). 0x0068CF10 loads the default one the same way.
//  - Platform+0x29D0 puts the pixels into a 48x48 32-bit DIB section (BITMAPV5, alpha) plus a 48x48 mono mask, Platform+0x4240
//    makes a Windows cursor of them with user32 CreateIconIndirect and keeps the HCURSOR in a vector of the input object
//    (+0x14 begin, +0x18 end, +0x20 current index). Plat::Input::SetCursor(i) and the window's WM_SETCURSOR call user32 SetCursor
//    with an entry of that vector. So it is an ordinary Windows (hardware) cursor - no Direct3D cursor, no sprite in the renderer.
//
// CursorScale (nothing is drawn by us, the game shows its own cursor through its own path):
//  - Platform.dll's import of CreateIconIndirect is pointed at our function: the game's 48x48 picture is scaled to N x N
//    (premultiplied-alpha bicubic, or nearest for a crisp look) and the hotspot with it, then the real CreateIconIndirect makes
//    the cursor. Every cursor state goes through it (normal, move, attack, capture, garrison, abilities, disabled, UI, jumbo).
//  - cursors made before we were loaded are rebuilt in place in the input object's vector (from their original pictures).
//  - Scale=auto: by the game's own vertical resolution = its window's client height (1080 -> 1.0, 1440 -> 1.33, 2160 -> 2.0, i.e.
//    height/1080, 1.0 ... 4.0), not the desktop's; it follows a resolution change (Options, fullscreen/windowed) once a second.
// Not simulation: no effect on sync, replays or other players.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define BASE_PX     48
#define MAX_CUR     256
#define RVA_INPUTOBJ_CREATE 0x41E0   // Platform.dll: the input object's CreateCursor (vtable+0x5C)
#define RVA_CREATECURSOR    0x7700   // Platform.dll export ?CreateCursor@Input@Plat@@YGHABUCursorImage@12@@Z

typedef HICON (WINAPI *CIIFn)(PICONINFO);
static CIIFn g_real_cii;
static DWORD *g_iat_slot;
static wchar_t g_log[MAX_PATH];
static int g_auto = 1, g_sharp = 0;
static double g_fixed = 1.0;
static volatile int g_px = BASE_PX;           // current cursor size in pixels (48 = the game's own)
static unsigned char *g_plat;
static unsigned char **g_inputobj_ptr;        // Platform's pointer to the input object
static CRITICAL_SECTION g_cs;
static HWND g_hwnd;

typedef struct { HCURSOR h; int w, hgt, hx, hy; unsigned int *px; } Orig;   // a cursor we made and its original picture
static Orig g_tab[MAX_CUR * 4]; static int g_ntab;
static HCURSOR g_old[MAX_CUR]; static int g_nold;                         // previous generation, destroyed at the next rebuild

static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}

// ---- picture scaling: BGRA (straight alpha) -> BGRA, outside of the picture is transparent ----
static double cubic(double x) {                                   // Catmull-Rom (a = -0.5)
    x = fabs(x);
    if (x < 1) return (1.5 * x - 2.5) * x * x + 1;
    if (x < 2) return ((-0.5 * x + 2.5) * x - 4) * x + 2;
    return 0;
}
static void scale_pic(const unsigned int *src, int sw, int sh, unsigned int *dst, int dw, int dh, int sharp) {
    double fx = (double)sw / dw, fy = (double)sh / dh;
    float *pm = (float *)malloc(sizeof(float) * 4 * sw * sh);     // premultiplied
    for (int i = 0; i < sw * sh; i++) {
        unsigned int p = src[i]; float a = (p >> 24) / 255.f;
        pm[i * 4 + 0] = (p & 255) * a; pm[i * 4 + 1] = ((p >> 8) & 255) * a; pm[i * 4 + 2] = ((p >> 16) & 255) * a;
        pm[i * 4 + 3] = a * 255.f;
    }
    for (int y = 0; y < dh; y++) for (int x = 0; x < dw; x++) {
        double c[4] = {0, 0, 0, 0};
        double sx = (x + 0.5) * fx - 0.5, sy = (y + 0.5) * fy - 0.5;
        if (sharp) {
            int ix = (int)floor((x + 0.5) * fx), iy = (int)floor((y + 0.5) * fy);
            if (ix >= sw) ix = sw - 1; if (iy >= sh) iy = sh - 1;
            for (int k = 0; k < 4; k++) c[k] = pm[(iy * sw + ix) * 4 + k];
        } else {
            int x0 = (int)floor(sx), y0 = (int)floor(sy);
            for (int j = -1; j <= 2; j++) {
                int yy = y0 + j; if (yy < 0 || yy >= sh) continue;
                double wy = cubic(sy - yy);
                for (int i = -1; i <= 2; i++) {
                    int xx = x0 + i; if (xx < 0 || xx >= sw) continue;
                    double w = wy * cubic(sx - xx); const float *q = pm + (yy * sw + xx) * 4;
                    c[0] += w * q[0]; c[1] += w * q[1]; c[2] += w * q[2]; c[3] += w * q[3];
                }
            }
        }
        double a = c[3]; if (a < 0) a = 0; if (a > 255) a = 255;
        unsigned int out = 0;
        if (a >= 0.5) {
            unsigned int ch[3];
            for (int k = 0; k < 3; k++) { double v = c[k]; if (v < 0) v = 0; if (v > a) v = a; ch[k] = (unsigned int)(v * 255.0 / a + 0.5); if (ch[k] > 255) ch[k] = 255; }
            out = ((unsigned int)(a + 0.5) << 24) | (ch[2] << 16) | (ch[1] << 8) | ch[0];
        }
        dst[y * dw + x] = out;
    }
    free(pm);
}

// ---- cursor building ----
static HCURSOR make_cursor(const Orig *o, int px) {
    int dw = o->w * px / BASE_PX, dh = o->hgt * px / BASE_PX;
    if (dw < 1) dw = 1; if (dh < 1) dh = 1;
    BITMAPV5HEADER bh; memset(&bh, 0, sizeof bh);                // the same kind of bitmap the game makes, top-down
    bh.bV5Size = sizeof bh; bh.bV5Width = dw; bh.bV5Height = -dh; bh.bV5Planes = 1; bh.bV5BitCount = 32;
    bh.bV5Compression = BI_BITFIELDS; bh.bV5RedMask = 0xFF0000; bh.bV5GreenMask = 0xFF00; bh.bV5BlueMask = 0xFF;
    bh.bV5AlphaMask = 0xFF000000;
    void *bits = NULL; HDC dc = GetDC(NULL);
    HBITMAP col = CreateDIBSection(dc, (BITMAPINFO *)&bh, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    if (!col || !bits) { if (col) DeleteObject(col); return NULL; }
    if (dw == o->w && dh == o->hgt) memcpy(bits, o->px, 4 * dw * dh);
    else scale_pic(o->px, o->w, o->hgt, (unsigned int *)bits, dw, dh, g_sharp);
    GdiFlush();
    int mstride = ((dw + 15) / 16) * 2; void *mb = calloc(mstride, dh);
    HBITMAP mask = CreateBitmap(dw, dh, 1, 1, mb); free(mb);
    ICONINFO ii; ii.fIcon = FALSE;
    ii.xHotspot = (DWORD)floor((o->hx + 0.5) * dw / o->w); ii.yHotspot = (DWORD)floor((o->hy + 0.5) * dh / o->hgt);
    if (ii.xHotspot >= (DWORD)dw) ii.xHotspot = dw - 1; if (ii.yHotspot >= (DWORD)dh) ii.yHotspot = dh - 1;
    ii.hbmMask = mask; ii.hbmColor = col;
    HCURSOR h = (HCURSOR)g_real_cii(&ii);
    DeleteObject(col); DeleteObject(mask);
    return h;
}
// the picture of a bitmap as top-down BGRA; 0 if it is not a picture we handle
static unsigned int *read_pic(HBITMAP hb, int *w, int *h) {
    BITMAP bm; if (!hb || !GetObjectW(hb, sizeof bm, &bm)) return NULL;
    if (bm.bmWidth < 8 || bm.bmWidth > 128 || bm.bmHeight < 8 || bm.bmHeight > 128) return NULL;
    *w = bm.bmWidth; *h = bm.bmHeight;
    unsigned int *px = (unsigned int *)malloc(4 * *w * *h);
    DIBSECTION ds;                                                // the game's 32-bit DIB section: its bits as they are (alpha)
    if (GetObjectW(hb, sizeof ds, &ds) == sizeof ds && ds.dsBm.bmBits && ds.dsBm.bmBitsPixel == 32) {
        GdiFlush();
        for (int y = 0; y < *h; y++)
            memcpy(px + y * *w, (unsigned char *)ds.dsBm.bmBits + ds.dsBm.bmWidthBytes * (ds.dsBmih.biHeight > 0 ? *h - 1 - y : y), 4 * *w);
        return px;
    }
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = *w; bi.bmiHeader.biHeight = -*h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(NULL); int ok = GetDIBits(dc, hb, 0, *h, px, &bi, DIB_RGB_COLORS);
    ReleaseDC(NULL, dc);
    if (!ok) { free(px); return NULL; }
    return px;
}
// the picture of an existing cursor (one the game made before we were loaded): drawn on black and on white, alpha = the
// difference - the same on Windows and Wine, whatever bitmap kind the system keeps the cursor in
static unsigned int *read_cursor(HCURSOR c, int *w, int *h, int *hx, int *hy) {
    ICONINFO ii; if (!GetIconInfo(c, &ii)) return NULL;
    BITMAP bm; int ok = 0;
    if (ii.hbmColor && GetObjectW(ii.hbmColor, sizeof bm, &bm)) { *w = bm.bmWidth; *h = bm.bmHeight; ok = 1; }
    else if (ii.hbmMask && GetObjectW(ii.hbmMask, sizeof bm, &bm)) { *w = bm.bmWidth; *h = bm.bmHeight / 2; ok = 1; }
    *hx = ii.xHotspot; *hy = ii.yHotspot;
    if (ii.hbmColor) DeleteObject(ii.hbmColor); if (ii.hbmMask) DeleteObject(ii.hbmMask);
    if (!ok || *w < 8 || *w > 512 || *h < 8 || *h > 512) return NULL;
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = *w; bi.bmiHeader.biHeight = -*h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    HDC sdc = GetDC(NULL), dc = CreateCompatibleDC(sdc); ReleaseDC(NULL, sdc);
    unsigned int *k = NULL, *wt = NULL;
    HBITMAP bk = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&k, NULL, 0);
    HBITMAP bw = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&wt, NULL, 0);
    unsigned int *px = NULL;
    if (bk && bw && k && wt) {
        memset(k, 0, 4 * *w * *h); memset(wt, 0xFF, 4 * *w * *h);
        HGDIOBJ o = SelectObject(dc, bk); DrawIconEx(dc, 0, 0, c, *w, *h, 0, NULL, DI_NORMAL);
        SelectObject(dc, bw); DrawIconEx(dc, 0, 0, c, *w, *h, 0, NULL, DI_NORMAL); SelectObject(dc, o); GdiFlush();
        px = (unsigned int *)malloc(4 * *w * *h);
        for (int i = 0; i < *w * *h; i++) {
            int a = 255, ch[3];
            for (int j = 0; j < 3; j++) { int d = 255 - (int)((wt[i] >> (8 * j)) & 255) + (int)((k[i] >> (8 * j)) & 255); if (d < a) a = d; }
            if (a < 0) a = 0;
            for (int j = 0; j < 3; j++) { int v = (k[i] >> (8 * j)) & 255; ch[j] = a ? (v * 255 + a / 2) / a : 0; if (ch[j] > 255) ch[j] = 255; }
            px[i] = a ? ((unsigned int)a << 24) | (ch[2] << 16) | (ch[1] << 8) | ch[0] : 0;
        }
    }
    if (bk) DeleteObject(bk); if (bw) DeleteObject(bw); DeleteDC(dc);
    return px;
}

// Dump=1 (tests): every cursor made is written as a 32-bit BMP with alpha into CursorScale_dump\ next to the DLL
static int g_dump; static wchar_t g_dumpdir[MAX_PATH];
static void dump(const Orig *o, HCURSOR c) {
    if (!g_dump || !c) return;
    int w, h, hx, hy; unsigned int *px = read_cursor(c, &w, &h, &hx, &hy);
    if (!px) return;
    static int n; wchar_t fn[MAX_PATH + 64];
    _snwprintf(fn, MAX_PATH + 63, L"%ls\\c%03d_%dx%d_hot%d_%d.bmp", g_dumpdir, ++n, w, h, hx, hy);
    FILE *f = _wfopen(fn, L"wb");
    if (f) {
        BITMAPV5HEADER bh; memset(&bh, 0, sizeof bh);
        bh.bV5Size = sizeof bh; bh.bV5Width = w; bh.bV5Height = -h; bh.bV5Planes = 1; bh.bV5BitCount = 32;
        bh.bV5Compression = BI_BITFIELDS; bh.bV5RedMask = 0xFF0000; bh.bV5GreenMask = 0xFF00; bh.bV5BlueMask = 0xFF;
        bh.bV5AlphaMask = 0xFF000000; bh.bV5CSType = 0x73524742;
        BITMAPFILEHEADER fh; memset(&fh, 0, sizeof fh); fh.bfType = 0x4D42;
        fh.bfOffBits = sizeof fh + sizeof bh; fh.bfSize = fh.bfOffBits + 4 * w * h;
        fwrite(&fh, sizeof fh, 1, f); fwrite(&bh, sizeof bh, 1, f); fwrite(px, 4, w * h, f); fclose(f);
    }
    free(px); (void)o;
}
static Orig *tab_find(HCURSOR h) { for (int i = 0; i < g_ntab; i++) if (g_tab[i].h == h) return &g_tab[i]; return NULL; }
static Orig *tab_add(HCURSOR h, unsigned int *px, int w, int hgt, int hx, int hy) {
    if (g_ntab >= (int)(sizeof g_tab / sizeof g_tab[0])) return NULL;
    Orig *o = &g_tab[g_ntab++]; o->h = h; o->px = px; o->w = w; o->hgt = hgt; o->hx = hx; o->hy = hy; return o;
}

// Platform.dll's CreateIconIndirect: the game's 48x48 cursor picture -> our size
static HICON WINAPI cs_create_icon_indirect(PICONINFO ii) {
    if (!ii || ii->fIcon || !ii->hbmColor) return g_real_cii(ii);
    int w, h; unsigned int *px = read_pic(ii->hbmColor, &w, &h);
    if (!px) return g_real_cii(ii);
    Orig o = {NULL, w, h, (int)ii->xHotspot, (int)ii->yHotspot, px};
    EnterCriticalSection(&g_cs);
    int size = g_px;
    HCURSOR c = make_cursor(&o, size);
    if (!c) { LeaveCriticalSection(&g_cs); free(px); logmsg("could not make a %dpx cursor - the game's own one is used", size); return g_real_cii(ii); }
    Orig *t = tab_add(c, px, w, h, o.hx, o.hy);
    static int n; n++;
    if (t) dump(t, c);
    LeaveCriticalSection(&g_cs);
    if (n <= 2 || n % 15 == 0) logmsg("cursor #%d: %dx%d hotspot %d,%d -> %dx%d", n, w, h, o.hx, o.hy, w * size / BASE_PX, h * size / BASE_PX);
    return c;
}

// the input object's vector of cursors (begin, end), checked against the Platform.dll we know
static HCURSOR **cursor_vec(HCURSOR ***end) {
    unsigned char *obj = g_inputobj_ptr ? *g_inputobj_ptr : NULL;
    if (!obj || IsBadReadPtr(obj, 0x24)) return NULL;
    void **vt = *(void ***)obj;
    if (IsBadReadPtr(vt, 0x60) || vt[0x5C / 4] != (void *)(g_plat + RVA_INPUTOBJ_CREATE)) return NULL;
    HCURSOR *b = *(HCURSOR **)(obj + 0x14), *e = *(HCURSOR **)(obj + 0x18);
    if (!b || e < b || e - b > MAX_CUR) return NULL;
    *end = (HCURSOR **)e; return (HCURSOR **)b;
}
// rebuild every cursor of the vector at the current size (cursors made before us: from their handles)
static void rebuild(const char *why) {
    HCURSOR **endp, **b = cursor_vec(&endp);
    if (!b) return;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_nold; i++) {
        DestroyCursor(g_old[i]);
        for (int k = 0; k < g_ntab; k++) if (g_tab[k].h == g_old[i]) { g_tab[k] = g_tab[--g_ntab]; break; }   // pictures are shared
    }
    g_nold = 0;
    int n = 0, fresh = 0, fail = 0;
    for (HCURSOR *p = (HCURSOR *)b; p < (HCURSOR *)endp; p++) {
        HCURSOR h = *p; if (!h) continue;
        Orig *o = tab_find(h); int ours = o != NULL;
        if (!o) {                                                 // made before we were loaded
            int w, hg, hx, hy; unsigned int *px = read_cursor(h, &w, &hg, &hx, &hy);
            if (!px) { fail++; continue; }
            o = tab_add(h, px, w, hg, hx, hy); fresh++;
            if (!o) { free(px); fail++; continue; }
            if (w * g_px / BASE_PX == w) continue;                // already the right size
        }
        HCURSOR c = make_cursor(o, g_px);
        if (!c) { fail++; continue; }
        Orig *no = tab_add(c, o->px, o->w, o->hgt, o->hx, o->hy);
        if (!no) { DestroyCursor(c); fail++; continue; }
        dump(no, c);
        *p = c; n++;
        if (ours && g_nold < MAX_CUR) g_old[g_nold++] = h;        // ours: destroyed at the next rebuild (the game moved on by then)
    }
    LeaveCriticalSection(&g_cs);
    if (n || fail) logmsg("%s: %d cursors rebuilt at %dpx (%d made before CursorScale was loaded, %d failed)", why, n, g_px, fresh, fail);
    if (g_hwnd) { POINT pt; if (GetCursorPos(&pt)) SetCursorPos(pt.x, pt.y); }   // the game sets its cursor again on the move
}

static BOOL CALLBACK find_win(HWND h, LPARAM _) {
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId() || GetWindow(h, GW_OWNER) || !IsWindowVisible(h)) return TRUE;
    wchar_t c[64]; GetClassNameW(h, c, 64); if (!wcscmp(c, L"ConsoleWindowClass")) return TRUE;
    g_hwnd = h; return FALSE;
}
// the game's own vertical resolution: the height of its window's client area. Fullscreen = the mode the game asked for, windowed =
// the window; NOT the desktop or monitor (08.11: on a 3840x2160 desktop a 1280x800 game got the 4K size). 0 = not known yet (no
// window, minimized, a tiny splash) - the cursor size then stays as it is.
static int game_height(void) {
    RECT r;
    if (!g_hwnd || IsIconic(g_hwnd) || !GetClientRect(g_hwnd, &r) || r.bottom - r.top < 200 || r.right - r.left < 320) return 0;
    return r.bottom - r.top;
}
static int size_for(double s) {
    if (s < 1.0) s = 1.0; if (s > 4.0) s = 4.0;
    int px = (int)(BASE_PX * s + 0.5);
    return px & ~1;                                               // even sizes keep the centre hotspot centred
}
static double auto_scale(int h) { return h / 1080.0; }

static int hook_iat(void) {
    HMODULE plat = GetModuleHandleA("Platform.dll");
    if (!plat) return 0;
    unsigned char *b = (unsigned char *)plat;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)b; IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(b + dos->e_lfanew);
    // the CreateCursor export must be the Platform.dll of 3.19: mov ecx,[obj]; mov eax,[ecx]; mov eax,[eax+5Ch]; jmp eax
    unsigned char *cc = (unsigned char *)GetProcAddress(plat, "?CreateCursor@Input@Plat@@YGHABUCursorImage@12@@Z");
    static const unsigned char pat[] = {0x8B, 0x01, 0x8B, 0x40, 0x5C, 0xFF, 0xE0};
    if (cc != b + RVA_CREATECURSOR || cc[0] != 0x8B || cc[1] != 0x0D || memcmp(cc + 6, pat, sizeof pat)) {
        logmsg("Platform.dll is not the one of 3.19 (CreateCursor at %p) - not changing the cursor", cc); return -1; }
    g_plat = b; g_inputobj_ptr = *(unsigned char ***)(cc + 2);
    IMAGE_DATA_DIRECTORY *dd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    void *real = (void *)GetProcAddress(GetModuleHandleA("user32.dll"), "CreateIconIndirect");
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(b + dd->VirtualAddress); d->Name; d++) {
        if (_stricmp((char *)(b + d->Name), "user32.dll")) continue;
        IMAGE_THUNK_DATA32 *nm = (IMAGE_THUNK_DATA32 *)(b + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        IMAGE_THUNK_DATA32 *at = (IMAGE_THUNK_DATA32 *)(b + d->FirstThunk);
        for (; nm->u1.AddressOfData; nm++, at++) {
            if (nm->u1.Ordinal & IMAGE_ORDINAL_FLAG32) continue;
            IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME *)(b + nm->u1.AddressOfData);
            if (strcmp((char *)ibn->Name, "CreateIconIndirect")) continue;
            if ((void *)at->u1.Function != real) return 0;        // imports not bound yet
            g_real_cii = (CIIFn)real; g_iat_slot = (DWORD *)&at->u1.Function;
            DWORD old; VirtualProtect(g_iat_slot, 4, PAGE_READWRITE, &old);
            *g_iat_slot = (DWORD)cs_create_icon_indirect; VirtualProtect(g_iat_slot, 4, old, &old);
            return 1;
        }
    }
    logmsg("Platform.dll does not import CreateIconIndirect - not changing the cursor"); return -1;
}

static DWORD WINAPI worker(LPVOID _) {
    int r = 0;
    if (g_auto) { EnumWindows(find_win, 0); int gh = game_height(); if (gh) g_px = size_for(auto_scale(gh));
                  logmsg("game height %d -> cursor %dpx", gh, g_px); }
    for (int i = 0; i < 1200 && (r = hook_iat()) == 0; i++) Sleep(50);
    if (r != 1) { if (r == 0) logmsg("Platform.dll not loaded in 60 s - not changing the cursor"); return 0; }
    HCURSOR **e, **b = cursor_vec(&e);
    logmsg("hooked Platform.dll CreateIconIndirect; %d game cursors already made", b ? (int)(e - b) : -1);
    if (b && e > b) rebuild("cursors made before CursorScale was loaded");
    for (int last_h = -1;;) {                                    // window / screen resolution (auto) - once a second
        if (!g_hwnd || !IsWindow(g_hwnd)) { g_hwnd = NULL; EnumWindows(find_win, 0); }
        if (!g_auto) { if (g_hwnd) break; Sleep(500); continue; }
        int h = game_height();
        if (h && h != last_h) {
            int px = size_for(auto_scale(h));
            if (px != g_px) {
                logmsg("game height %d -> scale %.2f, cursor %dpx", h, auto_scale(h) < 1 ? 1.0 : auto_scale(h), px);
                g_px = px; rebuild("resolution change");
            }
            last_h = h;
        }
        Sleep(1000);
    }
    return 0;
}

static int already_loaded(const char *name) {
    char n[96]; wsprintfA(n, "Local\\dow2patch_%s_%lu", name, GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() == ERROR_ALREADY_EXISTS;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameW(h, g_log, MAX_PATH); wchar_t *sl = wcsrchr(g_log, L'\\'); if (sl) sl[1] = 0; else g_log[0] = 0;
    wchar_t ini[MAX_PATH]; wcscpy(ini, g_log); wcscat(ini, L"CursorScale.ini"); wcscat(g_log, L"CursorScale.log");
    if (already_loaded("CursorScale")) { logmsg("another CursorScale copy is already active in this game, this one stays idle"); return TRUE; }
    if (!GetPrivateProfileIntW(L"CursorScale", L"Enabled", 1, ini)) { logmsg("disabled in CursorScale.ini"); return TRUE; }
    wchar_t v[32]; GetPrivateProfileStringW(L"CursorScale", L"Scale", L"auto", v, 32, ini);
    if (_wcsicmp(v, L"auto") && _wtof(v) > 0) { g_auto = 0; g_fixed = _wtof(v); }
    GetPrivateProfileStringW(L"CursorScale", L"Filter", L"smooth", v, 32, ini);
    g_sharp = !_wcsicmp(v, L"sharp");
    g_dump = GetPrivateProfileIntW(L"CursorScale", L"Dump", 0, ini) != 0;
    if (g_dump) { wcscpy(g_dumpdir, g_log); wchar_t *d = wcsrchr(g_dumpdir, L'\\'); if (d) *d = 0; wcscat(g_dumpdir, L"\\CursorScale_dump");
                  CreateDirectoryW(g_dumpdir, NULL); }
    InitializeCriticalSection(&g_cs);
    if (g_auto) logmsg("CursorScale 1: scale auto (by the game resolution), filter %s", g_sharp ? "sharp" : "smooth");
    else { g_px = size_for(g_fixed); logmsg("CursorScale 1: scale %.2f -> %dpx, filter %s", g_fixed, g_px, g_sharp ? "sharp" : "smooth"); }
    CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    return TRUE;
}
