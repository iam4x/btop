// SPDX-License-Identifier: MIT
#include <stddef.h>
#include <asahi_drm.h>
#include "../src/linux/asahi_telemetry.h"

_Static_assert(sizeof(struct drm_asahi_get_telemetry) == sizeof(struct btop_asahi_telemetry), "ABI size");
_Static_assert(DRM_IOCTL_ASAHI_GET_TELEMETRY == BTOP_IOCTL_ASAHI_TELEMETRY, "ioctl number");
#define CHECK(field) _Static_assert(offsetof(struct drm_asahi_get_telemetry, field) == offsetof(struct btop_asahi_telemetry, field), #field)
CHECK(abi_version);
CHECK(valid);
CHECK(timestamp_ns);
CHECK(sample_age_ns);
CHECK(utilization_milli_percent);
CHECK(frequency_khz);
CHECK(power_microwatts);
CHECK(temperature_millicelsius);
CHECK(reserved);

int main(void) { return 0; }
