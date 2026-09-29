// SPDX-License-Identifier: MIT
// Local experimental Asahi telemetry ABI v1, mirrored from the kernel patch.
#pragma once
#include <linux/types.h>
#include <sys/ioctl.h>

struct btop_asahi_telemetry {
	/** @abi_version: Format version, currently 2. */
	__u32 abi_version;
	/** @valid: Bits 0..3: utilization, frequency, power, temperature; bit 16: off. */
	__u32 valid;
	/** @timestamp_ns: CLOCK_BOOTTIME time of the query. */
	__u64 timestamp_ns;
	/** @sample_age_ns: Ages in the order above; U64_MAX if no sample exists. */
	__u64 sample_age_ns[4];
	/** @utilization_milli_percent: Firmware performance-controller utilization, 0..100000. */
	__u32 utilization_milli_percent;
	/** @frequency_khz: Firmware-reported core frequency in kHz. */
	__u32 frequency_khz;
	/** @power_microwatts: Firmware average GPU power estimate in microwatts. */
	__u64 power_microwatts;
	/** @temperature_millicelsius: Firmware GPU temperature in millidegrees Celsius. */
	__s32 temperature_millicelsius;
	/** @reserved: Zero. */
	__u32 reserved;
};

#define BTOP_IOCTL_ASAHI_TELEMETRY _IOR('d', 0x4b, struct btop_asahi_telemetry)

#define BTOP_ASAHI_FEATURE_TELEMETRY (1ULL << 63)
