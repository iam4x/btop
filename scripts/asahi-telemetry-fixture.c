// SPDX-License-Identifier: MIT
// Test-only ioctl fixture. Never used by the installed btop launcher.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <drm/asahi_drm.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "../src/linux/asahi_telemetry.h"

int ioctl(int fd, unsigned long request, ...) {
    va_list args;
    va_start(args, request);
    void *argument = va_arg(args, void *);
    va_end(args);
    static int (*original)(int, unsigned long, ...);
    static unsigned calls;
    if (!original) original = dlsym(RTLD_NEXT, "ioctl");
    if (request == BTOP_IOCTL_ASAHI_TELEMETRY) {
        struct btop_asahi_telemetry *sample = argument;
        memset(sample, 0, sizeof(*sample));
        const char *mode = getenv("BTOP_FIXTURE_MODE");
        sample->abi_version = 2;
        sample->valid = 15;
        sample->utilization_milli_percent = 25000;
        sample->frequency_khz = 648000;
        sample->power_microwatts = 1234000;
        sample->temperature_millicelsius = 47000;
        calls++;
        if (mode && strcmp(mode, "expire") == 0 && calls > 4)
            sample->valid = 0;
        if (mode && strcmp(mode, "temperature") == 0)
            sample->valid = 8;
        if (mode && strcmp(mode, "off") == 0) {
            sample->valid = (1 << 16) | 7;
            sample->utilization_milli_percent = 0;
            sample->frequency_khz = 0;
            sample->power_microwatts = 0;
        }
        return 0;
    }
    int result = original(fd, request, argument);
    if (result == 0 && request == DRM_IOCTL_ASAHI_GET_PARAMS) {
        struct drm_asahi_get_params *query = argument;
        if (query->param_group == 0 && query->size >= sizeof(__u64)) {
            __u64 *features = (__u64 *)(unsigned long)query->pointer;
            *features |= BTOP_ASAHI_FEATURE_TELEMETRY;
        }
    }
    return result;
}
