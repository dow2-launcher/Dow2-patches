// http_mac.h - one HTTPS GET with the system's own network stack (CFNetwork: CFHTTPMessage + CFReadStream), for the Mac add-ons
// that ask our servers (LobbyStats). No library of ours: CoreFoundation and CFNetwork are the system's (the game has them loaded).
// Blocking - call it from a worker thread only, never from the game's thread. The whole request is bounded by `timeout_ms`
// (the stream is polled, not waited on). Returns the HTTP status (0 = no answer), the body in *buf (grown up to maxlen,
// 0-terminated), its length in *got. CFReadStreamCreateForHTTPRequest is deprecated since 10.11 but present on every macOS the
// game runs on; it follows redirects, uses the system proxy settings and checks the TLS certificate as Safari does.
#ifndef HTTP_MAC_H
#define HTTP_MAC_H
#include <CoreFoundation/CoreFoundation.h>
#include <CFNetwork/CFNetwork.h>
#include <mach/mach_time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

static uint64_t hm_ms(void) {
    static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb);
    return mach_absolute_time() / 1000000 * tb.numer / tb.denom;
}
static int http_mac_get(const char *url, const char *agent, char **buf, int *cap, int maxlen, int *got, int timeout_ms) {
    int code = 0; *got = 0;
    CFURLRef u = CFURLCreateWithBytes(NULL, (const UInt8 *)url, (CFIndex)strlen(url), kCFStringEncodingUTF8, NULL);
    if (!u) return 0;
    CFHTTPMessageRef rq = CFHTTPMessageCreateRequest(NULL, CFSTR("GET"), u, kCFHTTPVersion1_1);
    CFRelease(u);
    if (!rq) return 0;
    CFStringRef ag = CFStringCreateWithCString(NULL, agent, kCFStringEncodingUTF8);
    if (ag) { CFHTTPMessageSetHeaderFieldValue(rq, CFSTR("User-Agent"), ag); CFRelease(ag); }
    CFReadStreamRef s = CFReadStreamCreateForHTTPRequest(NULL, rq);
    CFRelease(rq);
    if (!s) return 0;
    CFReadStreamSetProperty(s, kCFStreamPropertyHTTPShouldAutoredirect, kCFBooleanTrue);
    uint64_t end = hm_ms() + (uint64_t)timeout_ms;
    int ok = 0;
    if (CFReadStreamOpen(s)) {
        for (;;) {
            if (*cap - *got < 16 * 1024 + 1) {                      // room for the next piece
                int nc = *cap ? *cap * 2 : 64 * 1024; if (nc > maxlen + 1) nc = maxlen + 1;
                if (nc <= *cap) break;                              // maxlen reached: the rest is cut (a JSON then fails to parse)
                char *nb = (char *)realloc(*buf, (size_t)nc); if (!nb) break;
                *buf = nb; *cap = nc;
            }
            CFStreamStatus st = CFReadStreamGetStatus(s);
            if (st == kCFStreamStatusAtEnd) { ok = 1; break; }
            if (st == kCFStreamStatusError || st == kCFStreamStatusClosed) break;
            if (CFReadStreamHasBytesAvailable(s)) {
                CFIndex n = CFReadStreamRead(s, (UInt8 *)*buf + *got, *cap - 1 - *got);
                if (n < 0) break;
                if (n == 0) { ok = 1; break; }
                *got += (int)n;
                continue;
            }
            if (hm_ms() > end) break;                               // too slow: given up (the caller tries the next mirror)
            usleep(10000);
        }
        CFTypeRef h = CFReadStreamCopyProperty(s, kCFStreamPropertyHTTPResponseHeader);
        if (h) { code = (int)CFHTTPMessageGetResponseStatusCode((CFHTTPMessageRef)h); CFRelease(h); }
        if (!ok && code) code = 0;                                  // cut off: not an answer
    }
    CFReadStreamClose(s); CFRelease(s);
    if (*buf) (*buf)[*got] = 0;
    return code;
}
#pragma clang diagnostic pop
#endif
