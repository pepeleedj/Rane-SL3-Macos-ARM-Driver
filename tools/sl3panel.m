// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * sl3panel: Native Apple Silicon (arm64) CLI control panel for the Rane SL3.
 * Uses IOUSBHost directly (no libusb required) and implements the full
 * reverse-engineered Rane SL3 control protocol from Sl3Api.framework &
 * SL 3 Audio Control Panel.prefPane.
 */
#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>
#import <IOUSBHost/IOUSBHost.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define VID 0x1cc5
#define PID 0x0001
#define IF_HID 3
#define EP_HID_OUT 0x01
#define EP_HID_IN  0x81
#define HID_REPORT 64

static uint32_t g_seq = 1;

static io_service_t usb_find(const char *cls, int ifnum) {
    CFMutableDictionaryRef m = IOServiceMatching(cls);
    NSMutableDictionary *p = [@{@"idVendor": @VID, @"idProduct": @PID} mutableCopy];
    if (ifnum >= 0) p[@"bInterfaceNumber"] = @(ifnum);
    CFDictionarySetValue(m, CFSTR(kIOPropertyMatchKey), (__bridge CFDictionaryRef)p);
    return IOServiceGetMatchingService(kIOMainPortDefault, m);
}

static int hid_cmd(IOUSBHostPipe *hout, IOUSBHostPipe *hin, NSMutableData *dout, NSMutableData *din,
                   uint8_t cmd, const uint8_t *payload, int len, uint8_t *reply) {
    uint8_t *out = dout.mutableBytes;
    uint32_t seq = g_seq++;
    memset(out, 0, HID_REPORT);
    out[0] = cmd;
    memcpy(out + 1, &seq, 4);
    if (len > 0) memcpy(out + 5, payload, len);

    NSUInteger sent = 0;
    NSError *err = nil;
    if (![hout sendIORequestWithData:dout bytesTransferred:&sent completionTimeout:0 error:&err]) {
        fprintf(stderr, "Error enviando comando 0x%02x: %s\n", cmd, err ? err.localizedDescription.UTF8String : "desconocido");
        return -1;
    }

    for (int tries = 0; tries < 20; tries++) {
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        __block IOReturn st = kIOReturnError;
        __block NSUInteger got = 0;
        if (![hin enqueueIORequestWithData:din completionTimeout:0 error:&err
                         completionHandler:^(IOReturn s2, NSUInteger n2) {
                             st = s2; got = n2;
                             dispatch_semaphore_signal(sem);
                         }]) {
            return -1;
        }
        if (dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC))) {
            [hin abortWithError:nil];
            dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
        }
        if (st != kIOReturnSuccess || got < 5) continue;
        memcpy(reply, din.bytes, HID_REPORT);
        uint32_t rseq = 0;
        memcpy(&rseq, reply + 1, 4);
        if (reply[0] == cmd && rseq == seq) return 0;
    }
    return -1;
}

static double idx_to_mv(uint8_t idx) {
    if (idx > 15) idx = 15;
    return (double)idx * 0.5 + 2.5;
}

static int mv_to_idx(double mv) {
    int idx = (int)llround((mv - 2.5) / 0.5);
    if (idx < 0) idx = 0;
    if (idx > 15) idx = 15;
    return idx;
}

static void print_status(IOUSBHostPipe *hout, IOUSBHostPipe *hin, NSMutableData *dout, NSMutableData *din) {
    uint8_t reply[HID_REPORT];

    printf("============================================================\n");
    printf("  Rane SL3 — Panel de Control Nativo Apple Silicon (arm64)\n");
    printf("============================================================\n");

    if (hid_cmd(hout, hin, dout, din, 0x01, NULL, 0, reply) == 0) {
        printf("  Firmware SL3:       v%u.%02u\n", reply[5], reply[6]);
    }
    if (hid_cmd(hout, hin, dout, din, 0x30, NULL, 0, reply) == 0) {
        uint16_t rate = ((uint16_t)reply[5] << 8) | reply[6];
        printf("  Frecuencia actual:  %u Hz\n", rate);
    }
    if (hid_cmd(hout, hin, dout, din, 0x35, NULL, 0, reply) == 0) {
        printf("  Overload (Ch 1-6):  [%u %u | %u %u | %u %u]\n",
               reply[5], reply[6], reply[7], reply[8], reply[9], reply[10]);
    }
    if (hid_cmd(hout, hin, dout, din, 0x32, NULL, 0, reply) == 0) {
        const uint8_t *c = reply + 5;
        const char *decks[3] = {"Aux (Ch 1-2)       ", "Left Deck (Ch 3-4) ", "Right Deck (Ch 5-6)"};
        const int sens_idx[3] = {3, 9, 15};
        const int sw_idx[3]   = {8, 14, 20};
        printf("------------------------------------------------------------\n");
        printf("  Deck                 Switch Físico   Sensibilidad Phono   Salida\n");
        printf("------------------------------------------------------------\n");
        for (int i = 0; i < 3; i++) {
            uint8_t phono = c[i];
            uint8_t sens  = c[sens_idx[i]];
            uint8_t outsw = c[sw_idx[i]];
            printf("  %s  %-14s  %4.1f mV (idx %2u)     %s\n",
                   decks[i],
                   phono ? "PHONO" : "LINE / CD",
                   idx_to_mv(sens), sens,
                   outsw ? "USB Audio (01)" : "Analog Thru (00)");
        }
        printf("------------------------------------------------------------\n");
        printf("  Bloque raw (22B):   ");
        for (int i = 0; i < 22; i++) {
            printf("%s%02x", (i == 3 || i == 9 || i == 15 || i == 21) ? " | " : (i ? " " : ""), c[i]);
        }
        printf("\n============================================================\n");
    }
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Uso: %s [opciones]\n"
        "  status                         Muestra estado completo de la Rane SL3 (por defecto)\n"
        "  --rate <44100|48000>           Cambia la frecuencia de muestreo (cmd 0x31)\n"
        "  --left-sens <2.5..10.0>        Sensibilidad Phono Left Deck en mV (pasos de 0.5 mV)\n"
        "  --right-sens <2.5..10.0>       Sensibilidad Phono Right Deck en mV (pasos de 0.5 mV)\n"
        "  --aux-sens <2.5..10.0>         Sensibilidad Phono Aux en mV (pasos de 0.5 mV)\n"
        "  --left-mode <usb|thru>         Conmutador de salida Left Deck (offset 14)\n"
        "  --right-mode <usb|thru>        Conmutador de salida Right Deck (offset 20)\n"
        "  --aux-mode <usb|thru>          Conmutador de salida Aux (offset 8)\n",
        prog);
}

int main(int argc, char **argv) {
    @autoreleasepool {
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                usage(argv[0]);
                return 0;
            }
        }

        io_service_t dsvc = usb_find("IOUSBHostDevice", -1);
        if (!dsvc) {
            fprintf(stderr, "Rane SL3 (1cc5:0001) no detectada por USB. Conéctala y asegúrate de darle alimentación.\n");
            return 1;
        }
        dispatch_queue_t q = dispatch_queue_create("sl3.panel", DISPATCH_QUEUE_SERIAL);
        io_service_t isvc = usb_find("IOUSBHostInterface", IF_HID);
        if (!isvc) {
            NSError *cfgErr = nil;
            IOUSBHostDevice *dev = [[IOUSBHostDevice alloc] initWithIOService:dsvc options:IOUSBHostObjectInitOptionsNone queue:q error:&cfgErr interestHandler:nil];
            if (!dev || ![dev configureWithValue:1 matchInterfaces:YES error:&cfgErr]) {
                fprintf(stderr, "No se pudo activar la configuración USB 1: %s\n", cfgErr ? cfgErr.localizedDescription.UTF8String : "error");
                IOObjectRelease(dsvc);
                return 1;
            }
            [dev destroy];
            for (int i = 0; i < 30 && !(isvc = usb_find("IOUSBHostInterface", IF_HID)); i++) usleep(100000);
        }
        IOObjectRelease(dsvc);
        if (!isvc) {
            fprintf(stderr, "Interfaz 3 de la Rane SL3 no disponible.\n");
            return 1;
        }
        NSError *err = nil;
        IOUSBHostInterface *hid = [[IOUSBHostInterface alloc] initWithIOService:isvc
                                                                        options:IOUSBHostObjectInitOptionsNone
                                                                          queue:q
                                                                          error:&err
                                                                interestHandler:nil];
        IOObjectRelease(isvc);
        if (!hid) {
            fprintf(stderr, "No se pudo abrir la interfaz 3 (¿está en uso por otro proceso?): %s\n",
                    err ? err.localizedDescription.UTF8String : "error");
            return 1;
        }

        IOUSBHostPipe *hout = [hid copyPipeWithAddress:EP_HID_OUT error:&err];
        IOUSBHostPipe *hin  = [hid copyPipeWithAddress:EP_HID_IN error:&err];
        NSMutableData *dout = [hid ioDataWithCapacity:HID_REPORT error:&err];
        NSMutableData *din  = [hid ioDataWithCapacity:HID_REPORT error:&err];
        if (!hout || !hin || !dout || !din) {
            fprintf(stderr, "Error inicializando pipes HID.\n");
            [hid destroy];
            return 1;
        }

        uint8_t reply[HID_REPORT];
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "status")) {
                continue;
            } else if (!strcmp(argv[i], "--rate") && i + 1 < argc) {
                int rate = atoi(argv[++i]);
                if (rate != 44100 && rate != 48000) {
                    fprintf(stderr, "Frecuencia inválida: usa 44100 o 48000.\n");
                } else {
                    uint8_t p[2] = {(uint8_t)(rate >> 8), (uint8_t)(rate & 0xff)};
                    hid_cmd(hout, hin, dout, din, 0x31, p, 2, reply);
                }
            } else if ((!strcmp(argv[i], "--aux-sens") || !strcmp(argv[i], "--left-sens") || !strcmp(argv[i], "--right-sens")) && i + 1 < argc) {
                uint8_t offset = !strcmp(argv[i], "--aux-sens") ? 3 : (!strcmp(argv[i], "--left-sens") ? 9 : 15);
                double mv = atof(argv[++i]);
                uint8_t idx = (uint8_t)mv_to_idx(mv);
                uint8_t p[3] = {offset, 1, idx};
                hid_cmd(hout, hin, dout, din, 0x33, p, 3, reply);
            } else if ((!strcmp(argv[i], "--aux-mode") || !strcmp(argv[i], "--left-mode") || !strcmp(argv[i], "--right-mode")) && i + 1 < argc) {
                uint8_t offset = !strcmp(argv[i], "--aux-mode") ? 8 : (!strcmp(argv[i], "--left-mode") ? 14 : 20);
                uint8_t val = !strcasecmp(argv[++i], "usb") ? 1 : 0;
                uint8_t p[3] = {offset, 1, val};
                hid_cmd(hout, hin, dout, din, 0x33, p, 3, reply);
            } else {
                usage(argv[0]);
                [hid destroy];
                return 2;
            }
        }

        print_status(hout, hin, dout, din);
        [hid destroy];
    }
    return 0;
}
