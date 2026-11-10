// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Sl3Api.framework replacement for macOS (Universal arm64 + x86_64).
 * Bridges Rane's Sl3Api C/C++ ABI (used by Serato DJ Pro <= 2.5.5,
 * SL 3 Audio Control Panel.prefPane, and Sl3SettingsService) to
 * the userspace SL3Device.driver CoreAudio HAL plug-in via custom
 * AudioObject properties ('sl3h' and 'sl3s').
 */
#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>
#include <CoreAudio/CoreAudio.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define kSL3PropHIDRequest 'sl3h'
#define kSL3PropStatus     'sl3s'
#define HID_REPORT 64

typedef struct {
    uint32_t present;
    uint32_t is_c0;
    uint32_t buffer_ms;
    uint32_t phono_seq;
    uint32_t thru_seq;
    uint32_t ovl_seq;
    uint8_t  phono[4];
    uint8_t  thru[4];
    uint8_t  ovl[8];
} sl3_hal_status_t;

typedef void (*sl3_ovl_cb_t)(const uint8_t *ovl6);
typedef void (*sl3_phono_cb_t)(const uint8_t *phono3);
typedef void (*sl3_thru_cb_t)(const uint8_t *thru4);
typedef void (*sl3_usb_cb_t)(const uint32_t *status2);

typedef struct {
    int in_use;
    sl3_ovl_cb_t   ovl_cb;
    sl3_phono_cb_t phono_cb;
    sl3_thru_cb_t  thru_cb;
    sl3_usb_cb_t   usb_cb;
} sl3_client_t;

#define MAX_CLIENTS 32
static sl3_client_t g_clients[MAX_CLIENTS];
static pthread_mutex_t g_api_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_last_buffer_ms = 5;
static dispatch_queue_t g_cb_q;
static dispatch_source_t g_cb_timer;
static int g_have_prev_st;
static sl3_hal_status_t g_prev_st;

static int sl3_on_usb(void) {
    CFMutableDictionaryRef m = IOServiceMatching("IOUSBHostDevice");
    if (!m) return 0;
    NSDictionary *p = @{@"idVendor": @0x1cc5, @"idProduct": @0x0001};
    CFDictionarySetValue(m, CFSTR(kIOPropertyMatchKey), (__bridge CFDictionaryRef)p);
    io_service_t s = IOServiceGetMatchingService(kIOMainPortDefault, m);
    if (!s) return 0;
    IOObjectRelease(s);
    return 1;
}

static AudioObjectID find_sl3_device_once(void) {
    AudioObjectPropertyAddress pa = {
        kAudioHardwarePropertyTranslateUIDToDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    CFStringRef uid = CFSTR("SL3Device_UID");
    AudioObjectID dev = kAudioObjectUnknown;
    UInt32 sz = sizeof(dev);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &pa, sizeof(uid), &uid, &sz, &dev) == noErr &&
        dev != kAudioObjectUnknown) {
        return dev;
    }
    AudioObjectPropertyAddress dpa = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    UInt32 list_sz = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &dpa, 0, NULL, &list_sz) != noErr || list_sz == 0)
        return kAudioObjectUnknown;
    int n = list_sz / sizeof(AudioObjectID);
    AudioObjectID devs[64];
    if (n > 64) n = 64;
    list_sz = n * sizeof(AudioObjectID);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &dpa, 0, NULL, &list_sz, devs) != noErr)
        return kAudioObjectUnknown;
    for (int i = 0; i < n; i++) {
        AudioObjectPropertyAddress upa = {
            kAudioDevicePropertyDeviceUID,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        CFStringRef duid = NULL;
        UInt32 usz = sizeof(duid);
        if (AudioObjectGetPropertyData(devs[i], &upa, 0, NULL, &usz, &duid) == noErr && duid) {
            Boolean match = CFEqual(duid, CFSTR("SL3Device_UID"));
            CFRelease(duid);
            if (match) return devs[i];
        }
    }
    return kAudioObjectUnknown;
}

static AudioObjectID find_sl3_device(void) {
    AudioObjectID dev = find_sl3_device_once();
    if (dev != kAudioObjectUnknown || !sl3_on_usb()) return dev;
    /* Wait up to 2 seconds if the SL3 was just plugged in and the HAL is still opening it */
    for (int i = 0; i < 40 && sl3_on_usb(); i++) {
        usleep(50000);
        dev = find_sl3_device_once();
        if (dev != kAudioObjectUnknown) return dev;
    }
    return kAudioObjectUnknown;
}

static int hal_get_status(sl3_hal_status_t *st) {
    AudioObjectID dev = find_sl3_device();
    if (dev == kAudioObjectUnknown) {
        memset(st, 0, sizeof(*st));
        st->buffer_ms = g_last_buffer_ms;
        return -1;
    }
    AudioObjectPropertyAddress pa = {
        kSL3PropStatus,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    CFDataRef st_cf = NULL;
    UInt32 sz = sizeof(st_cf);
    if (AudioObjectGetPropertyData(dev, &pa, 0, NULL, &sz, &st_cf) != noErr || !st_cf) {
        memset(st, 0, sizeof(*st));
        st->present = 1;
        st->buffer_ms = g_last_buffer_ms;
        return 0;
    }
    if (CFGetTypeID(st_cf) == CFDataGetTypeID() && CFDataGetLength(st_cf) >= (CFIndex)sizeof(*st)) {
        memcpy(st, CFDataGetBytePtr(st_cf), sizeof(*st));
    } else {
        memset(st, 0, sizeof(*st));
        st->present = 1;
        st->buffer_ms = g_last_buffer_ms;
    }
    CFRelease(st_cf);
    if (st->buffer_ms >= 1 && st->buffer_ms <= 50)
        g_last_buffer_ms = st->buffer_ms;
    return 0;
}

static int hal_hid_request(uint8_t cmd, const uint8_t *payload, int len, uint8_t *reply) {
    if (len < 0 || len > HID_REPORT - 5) return -1;
    AudioObjectID dev = find_sl3_device();
    if (dev == kAudioObjectUnknown) return -1;
    uint8_t qual[HID_REPORT];
    memset(qual, 0, sizeof(qual));
    qual[0] = cmd;
    qual[1] = (uint8_t)len;
    if (len > 0 && payload) memcpy(qual + 2, payload, len);
    AudioObjectPropertyAddress pa = {
        kSL3PropHIDRequest,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    CFDataRef req_cf = CFDataCreate(NULL, qual, sizeof(qual));
    CFDataRef rep_cf = NULL;
    UInt32 sz = sizeof(rep_cf);
    OSStatus err = AudioObjectGetPropertyData(dev, &pa, sizeof(req_cf), &req_cf, &sz, &rep_cf);
    CFRelease(req_cf);
    if (err == noErr && rep_cf) {
        if (CFGetTypeID(rep_cf) == CFDataGetTypeID() && CFDataGetLength(rep_cf) >= HID_REPORT) {
            memcpy(reply, CFDataGetBytePtr(rep_cf), HID_REPORT);
            CFRelease(rep_cf);
            return 0;
        }
        CFRelease(rep_cf);
    }
    return -1;
}

static sl3_client_t *get_client(void *handle) {
    uintptr_t idx = (uintptr_t)handle;
    if (idx < 1 || idx > MAX_CLIENTS) return NULL;
    sl3_client_t *c = &g_clients[idx - 1];
    return c->in_use ? c : NULL;
}

static void poll_callbacks(void) {
    sl3_hal_status_t cur;
    hal_get_status(&cur);

    pthread_mutex_lock(&g_api_lock);
    int first = !g_have_prev_st;
    sl3_hal_status_t prev = g_prev_st;
    g_prev_st = cur;
    g_have_prev_st = 1;

    int usb_changed   = !first && (cur.present != prev.present);
    int phono_changed = !first && (cur.phono_seq != prev.phono_seq || memcmp(cur.phono, prev.phono, 3) != 0);
    int thru_changed  = !first && (cur.thru_seq != prev.thru_seq || memcmp(cur.thru, prev.thru, 4) != 0);
    int ovl_changed   = !first && (cur.ovl_seq != prev.ovl_seq);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!g_clients[i].in_use) continue;
        sl3_client_t snap = g_clients[i];
        pthread_mutex_unlock(&g_api_lock);

        uint32_t usb_st[2] = { cur.present ? 1 : 0, 0 };
        if (usb_changed && snap.usb_cb)     snap.usb_cb(usb_st);
        if (phono_changed && snap.phono_cb) snap.phono_cb(cur.phono);
        if (thru_changed && snap.thru_cb)   snap.thru_cb(cur.thru);
        if (ovl_changed && snap.ovl_cb)     snap.ovl_cb(cur.ovl);

        pthread_mutex_lock(&g_api_lock);
    }
    pthread_mutex_unlock(&g_api_lock);
}

static void start_cb_timer_locked(void) {
    if (g_cb_timer) return;
    if (!g_cb_q) g_cb_q = dispatch_queue_create("com.rane.sl3api.cb", DISPATCH_QUEUE_SERIAL);
    g_cb_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, g_cb_q);
    dispatch_source_set_timer(g_cb_timer, dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC),
                              100 * NSEC_PER_MSEC, 20 * NSEC_PER_MSEC);
    dispatch_source_set_event_handler(g_cb_timer, ^{ poll_callbacks(); });
    dispatch_resume(g_cb_timer);
}

static void stop_cb_timer_if_idle_locked(void) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].in_use) return;
    }
    if (g_cb_timer) {
        dispatch_source_cancel(g_cb_timer);
        g_cb_timer = nil;
    }
    g_have_prev_st = 0;
}

/* ---- Exported C / C++ API ---- */

__attribute__((visibility("default")))
void *sl3_open_private_impl(int buf_ms) __asm__("__Z16sl3_open_privatei");
void *sl3_open_private_impl(int buf_ms) {
    pthread_mutex_lock(&g_api_lock);
    if (buf_ms >= 1 && buf_ms <= 50) g_last_buffer_ms = buf_ms;
    void *h = NULL;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!g_clients[i].in_use) {
            memset(&g_clients[i], 0, sizeof(sl3_client_t));
            g_clients[i].in_use = 1;
            h = (void *)(uintptr_t)(i + 1);
            break;
        }
    }
    if (h) start_cb_timer_locked();
    pthread_mutex_unlock(&g_api_lock);
    return h;
}

__attribute__((visibility("default")))
void *RaneOpenPrivate_impl(int buf_ms) __asm__("__Z15RaneOpenPrivatei");
void *RaneOpenPrivate_impl(int buf_ms) {
    return sl3_open_private_impl(buf_ms);
}

__attribute__((visibility("default")))
void *sl3_open(void) {
    return sl3_open_private_impl(0);
}

__attribute__((visibility("default")))
void *RaneOpen_impl(void) __asm__("__Z8RaneOpenv");
void *RaneOpen_impl(void) {
    return sl3_open_private_impl(0);
}

__attribute__((visibility("default")))
void sl3_close(void *handle) {
    pthread_mutex_lock(&g_api_lock);
    sl3_client_t *c = get_client(handle);
    if (c) {
        memset(c, 0, sizeof(*c));
        stop_cb_timer_if_idle_locked();
    }
    pthread_mutex_unlock(&g_api_lock);
}

__attribute__((visibility("default")))
int sl3_get_last_buffer_ms_impl(void) __asm__("__Z32sl3_get_last_buffer_millisecondsv");
int sl3_get_last_buffer_ms_impl(void) {
    return g_last_buffer_ms;
}

__attribute__((visibility("default")))
int sl3_private_last_conn_err_impl(void) __asm__("__Z33sl3_private_last_connection_errorv");
int sl3_private_last_conn_err_impl(void) {
    return 0;
}

__attribute__((visibility("default")))
int sl3_get_usb_port_status(void *handle, uint32_t *status) {
    if (!handle || !status) return 8;
    sl3_hal_status_t st;
    hal_get_status(&st);
    status[0] = st.present ? 1 : 0;
    status[1] = 0;
    return 0;
}

__attribute__((visibility("default")))
int sl3_is_C0_device(void *handle, int *is_c0_out) {
    if (!handle || !is_c0_out) return 8;
    sl3_hal_status_t st;
    if (hal_get_status(&st) != 0 || !st.present) return 4;
    *is_c0_out = 0;
    return 0;
}

__attribute__((visibility("default")))
int sl3_driver_timer(void *handle, const uint8_t *in8, int in_len, uint8_t *out8, int out_len) {
    if (!handle || !in8 || !out8 || in_len != 8 || out_len != 8) return 8;
    uint8_t reply[HID_REPORT];
    if (hal_hid_request(0x37, in8, 8, reply) != 0) return 4;
    memcpy(out8, reply + 5, 8);
    return 0;
}

__attribute__((visibility("default")))
int sl3_get_audio_controls(void *handle, int offset, uint8_t *out_buf, int len) {
    if (!handle || !out_buf || offset < 0 || offset > 21 || len <= 0 || offset + len > 22) return 8;
    uint8_t reply[HID_REPORT];
    if (hal_hid_request(0x32, NULL, 0, reply) != 0) return 4;
    memcpy(out_buf, reply + 5 + offset, len);
    return 0;
}

__attribute__((visibility("default")))
int sl3_set_audio_controls(void *handle, int offset, const uint8_t *in_buf, int len) {
    if (!handle || !in_buf || offset < 0 || offset > 21 || len <= 0 || offset + len > 22) return 8;
    uint8_t payload[59];
    memset(payload, 0, sizeof(payload));
    payload[0] = (uint8_t)offset;
    payload[1] = (uint8_t)len;
    memcpy(payload + 2, in_buf, len);
    uint8_t reply[HID_REPORT];
    if (hal_hid_request(0x33, payload, 2 + len, reply) != 0) return 4;
    return 0;
}

__attribute__((visibility("default")))
int sl3_get_parameter(void *handle, int param_id, int *out_val) {
    if (!handle || !out_val) return 8;
    if (param_id != 0) return 2;
    uint8_t reply[HID_REPORT];
    if (hal_hid_request(0x30, NULL, 0, reply) == 0) {
        *out_val = ((int)reply[5] << 8) | (int)reply[6];
        if (*out_val == 44100 || *out_val == 48000) return 0;
    }
    AudioObjectID dev = find_sl3_device();
    if (dev == kAudioObjectUnknown) return 4;
    AudioObjectPropertyAddress pa = {
        kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    Float64 rate = 44100.0;
    UInt32 sz = sizeof(rate);
    if (AudioObjectGetPropertyData(dev, &pa, 0, NULL, &sz, &rate) != noErr) return 4;
    *out_val = (int)rate;
    return 0;
}

__attribute__((visibility("default")))
int sl3_set_parameter(void *handle, int param_id, int val) {
    if (!handle) return 8;
    if (param_id != 0) return 2;
    if (val != 44100 && val != 48000) return 8;
    AudioObjectID dev = find_sl3_device();
    if (dev == kAudioObjectUnknown) return 4;
    AudioObjectPropertyAddress pa = {
        kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    Float64 rate = (Float64)val;
    if (AudioObjectSetPropertyData(dev, &pa, 0, NULL, sizeof(rate), &rate) != noErr) return 4;
    return 0;
}

__attribute__((visibility("default")))
int sl3_get_firmware_version(void *handle, uint32_t *ver_out) {
    if (!handle || !ver_out) return 8;
    uint8_t reply[HID_REPORT];
    if (hal_hid_request(0x01, NULL, 0, reply) != 0) return 4;
    ver_out[0] = reply[5];
    ver_out[1] = reply[6];
    return 0;
}

__attribute__((visibility("default")))
int sl3_get_firmware_embedded_version(void *handle, uint32_t *ver_out) {
    if (!handle || !ver_out) return 8;
    ver_out[0] = 2;
    ver_out[1] = 50;
    ver_out[2] = 0;
    ver_out[3] = 0;
    return 0;
}

__attribute__((visibility("default")))
int sl3_get_api_version(uint32_t *ver_out) {
    if (!ver_out) return 8;
    ver_out[0] = 2;
    ver_out[1] = 0;
    ver_out[2] = 1;
    ver_out[3] = 2;
    return 0;
}

__attribute__((visibility("default")))
int sl3_get_driver_version(void *handle, uint32_t *ver_out) {
    if (!handle || !ver_out) return 8;
    ver_out[0] = 2;
    ver_out[1] = 0;
    ver_out[2] = 1;
    ver_out[3] = 2;
    return 0;
}

__attribute__((visibility("default")))
int sl3_get_vendor_id(void *handle, int *vid_out) {
    if (!handle || !vid_out) return 8;
    *vid_out = 0x1cc5;
    return 0;
}

__attribute__((visibility("default")))
int sl3_get_product_id(void *handle, int *pid_out) {
    if (!handle || !pid_out) return 8;
    *pid_out = 0x0001;
    return 0;
}

__attribute__((visibility("default")))
int sl3_get_buffer_millisecs(void *handle, int *ms_out) {
    if (!handle || !ms_out) return 8;
    sl3_hal_status_t st;
    if (hal_get_status(&st) == 0 && st.buffer_ms >= 1 && st.buffer_ms <= 50) {
        *ms_out = (int)st.buffer_ms;
    } else {
        *ms_out = g_last_buffer_ms;
    }
    return 0;
}

__attribute__((visibility("default")))
int sl3_set_buffer_millisecs(void *handle, int ms) {
    if (!handle || ms < 1 || ms > 50) return 8;
    g_last_buffer_ms = ms;
    AudioObjectID dev = find_sl3_device();
    if (dev != kAudioObjectUnknown) {
        AudioObjectPropertyAddress pa = {
            kSL3PropStatus,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        uint32_t v = (uint32_t)ms;
        CFDataRef v_cf = CFDataCreate(NULL, (const UInt8 *)&v, sizeof(v));
        AudioObjectSetPropertyData(dev, &pa, 0, NULL, sizeof(v_cf), &v_cf);
        CFRelease(v_cf);
    }
    return 0;
}

__attribute__((visibility("default")))
int sl3_get_overload_status(void *handle, uint8_t *ovl_out) {
    if (!handle || !ovl_out) return 8;
    uint8_t reply[HID_REPORT];
    if (hal_hid_request(0x35, NULL, 0, reply) != 0) return 4;
    memcpy(ovl_out, reply + 5, 6);
    return 0;
}

__attribute__((visibility("default")))
int sl3_get_status(void *handle, uint8_t *st_out) {
    if (!handle || !st_out) return 8;
    uint8_t reply[HID_REPORT];
    if (hal_hid_request(0x0a, NULL, 0, reply) != 0) return 4;
    *st_out = reply[5];
    return 0;
}

__attribute__((visibility("default")))
int sl3_set_overload_callback(void *handle, sl3_ovl_cb_t cb) {
    pthread_mutex_lock(&g_api_lock);
    sl3_client_t *c = get_client(handle);
    if (!c) { pthread_mutex_unlock(&g_api_lock); return 8; }
    c->ovl_cb = cb;
    pthread_mutex_unlock(&g_api_lock);
    return 0;
}

__attribute__((visibility("default")))
int sl3_set_phonoswitch_callback(void *handle, sl3_phono_cb_t cb) {
    pthread_mutex_lock(&g_api_lock);
    sl3_client_t *c = get_client(handle);
    if (!c) { pthread_mutex_unlock(&g_api_lock); return 8; }
    c->phono_cb = cb;
    pthread_mutex_unlock(&g_api_lock);
    return 0;
}

__attribute__((visibility("default")))
int sl3_set_thrustate_callback(void *handle, sl3_thru_cb_t cb) {
    pthread_mutex_lock(&g_api_lock);
    sl3_client_t *c = get_client(handle);
    if (!c) { pthread_mutex_unlock(&g_api_lock); return 8; }
    c->thru_cb = cb;
    pthread_mutex_unlock(&g_api_lock);
    return 0;
}

__attribute__((visibility("default")))
int sl3_set_usb_port_status_callback(void *handle, sl3_usb_cb_t cb) {
    pthread_mutex_lock(&g_api_lock);
    sl3_client_t *c = get_client(handle);
    if (!c) { pthread_mutex_unlock(&g_api_lock); return 8; }
    c->usb_cb = cb;
    pthread_mutex_unlock(&g_api_lock);
    return 0;
}

__attribute__((visibility("default")))
int sl3_set_alt_product_id(void *handle, int pid) {
    (void)pid;
    return handle ? 0 : 8;
}

__attribute__((visibility("default")))
int sl3_set_update_firmware(void *handle, const char *path) {
    (void)path;
    return handle ? 0 : 8;
}

__attribute__((visibility("default")))
int sl3_set_update_firmware_from_embedded(void *handle) {
    return handle ? 0 : 8;
}
