#include <stdio.h>
#include <stdint.h>
#include <CoreAudio/CoreAudio.h>

extern void *sl3_open(void);
extern void sl3_close(void *h);
extern int sl3_get_usb_port_status(void *h, uint32_t *st);
extern int sl3_is_C0_device(void *h, int *is_c0);
extern int sl3_get_firmware_version(void *h, uint32_t *ver);
extern int sl3_get_parameter(void *h, int id, int *val);
extern int sl3_get_audio_controls(void *h, int off, uint8_t *buf, int len);
extern int sl3_driver_timer(void *h, const uint8_t *in8, int in_len, uint8_t *out8, int out_len);
extern int sl3_set_buffer_millisecs(void *h, int ms);
extern int sl3_get_buffer_millisecs(void *h, int *ms);

int main(void) {
    AudioObjectPropertyAddress pa = {
        kAudioHardwarePropertyTranslateUIDToDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    CFStringRef uid = CFSTR("SL3Device_UID");
    AudioObjectID dev = kAudioObjectUnknown;
    UInt32 sz = sizeof(dev);
    OSStatus st = AudioObjectGetPropertyData(kAudioObjectSystemObject, &pa, sizeof(uid), &uid, &sz, &dev);
    printf("TranslateUIDToDevice -> st=0x%x dev=%u\n", (unsigned)st, (unsigned)dev);

    AudioObjectPropertyAddress spa = { 'sl3s', kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    Boolean has_s = AudioObjectHasProperty(dev, &spa);
    AudioObjectPropertyAddress hpa = { 'sl3h', kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    Boolean has_h = AudioObjectHasProperty(dev, &hpa);
    printf("HasProperty: sl3s=%d sl3h=%d\n", has_s, has_h);

    void *h = sl3_open();
    printf("sl3_open() -> %p\n", h);
    if (!h) return 1;

    int ms = 0;
    sl3_set_buffer_millisecs(h, 5);
    sl3_get_buffer_millisecs(h, &ms);
    printf("buffer_ms after set(5): %d\n", ms);

    uint32_t usb_st[2] = {0, 0};
    int r = sl3_get_usb_port_status(h, usb_st);
    printf("sl3_get_usb_port_status -> ret=%d status=[%u, %u]\n", r, usb_st[0], usb_st[1]);

    int is_c0 = -1;
    r = sl3_is_C0_device(h, &is_c0);
    printf("sl3_is_C0_device -> ret=%d is_c0=%d\n", r, is_c0);

    uint32_t fw[2] = {0, 0};
    r = sl3_get_firmware_version(h, fw);
    printf("sl3_get_firmware_version -> ret=%d ver=%u.%02u\n", r, fw[0], fw[1]);

    int rate = 0;
    r = sl3_get_parameter(h, 0, &rate);
    printf("sl3_get_parameter(0) -> ret=%d rate=%d Hz\n", r, rate);

    uint8_t ctrl[22] = {0};
    r = sl3_get_audio_controls(h, 0, ctrl, 22);
    printf("sl3_get_audio_controls -> ret=%d phono=[%u,%u,%u] thru=[%u,%u,%u]\n",
           r, ctrl[0], ctrl[1], ctrl[2], ctrl[8], ctrl[14], ctrl[20]);

    uint8_t ch[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    uint8_t resp[8] = {0};
    r = sl3_driver_timer(h, ch, 8, resp, 8);
    printf("sl3_driver_timer -> ret=%d resp=[%02x %02x %02x %02x %02x %02x %02x %02x]\n",
           r, resp[0], resp[1], resp[2], resp[3], resp[4], resp[5], resp[6], resp[7]);

    sl3_close(h);
    return 0;
}
