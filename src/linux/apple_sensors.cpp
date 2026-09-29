#include "apple_sensors.hpp"

#include <algorithm>
#include <bit>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>
#include <fmt/format.h>

#include "../btop_shared.hpp"
#include "../btop_tools.hpp"

#if defined(GPU_SUPPORT) && __has_include(<drm/asahi_drm.h>)
#include <drm/asahi_drm.h>
#include "asahi_telemetry.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#define BTOP_ASAHI_PARAMS
#endif

namespace AppleSensors {
namespace fs = std::filesystem;

namespace {
struct Reading {
	fs::path path;
	std::string label;
	bool temperature;
};

fs::path hwmon, power;
std::vector<Reading> readings;
//? SoC die sensors exposed by the patched macsmc_hwmon driver, labeled "CPU Die Tp01", "GPU Die Tg05", ...
std::vector<fs::path> cpu_die, gpu_die;
#ifdef BTOP_ASAHI_PARAMS
std::vector<std::pair<size_t, int>> gpu_devices;
static_assert(sizeof(btop_asahi_telemetry) == 72);
static_assert(DRM_ASAHI_SUBMIT + 1 == 0x0b);
#endif

std::optional<long long> read_number(const fs::path& path) {
	long long value;
	if (std::ifstream(path) >> value) return value;
	return std::nullopt;
}

long long hottest(const std::vector<fs::path>& paths) {
	long long result = -1;
	for (const auto& path : paths)
		if (const auto value = read_number(path)) result = std::max(result, *value / 1000);
	return result;
}

//? The GPU die sensors read a constant 9.199°C while the GPU is powered off
long long gpu_die_temp() {
	const auto temp = hottest(gpu_die);
	return temp >= 20 ? temp : -1;
}
}

void init() {
	hwmon.clear();
	power.clear();
	readings.clear();
	cpu_die.clear();
	gpu_die.clear();
	std::error_code ec;
	for (const auto& dir : fs::directory_iterator("/sys/class/hwmon", ec)) {
		if (Tools::readfile(dir.path() / "name") != "macsmc_hwmon") continue;
		hwmon = dir.path();
		for (const auto& file : fs::directory_iterator(hwmon, ec)) {
			const auto name = file.path().filename().string();
			if (not name.ends_with("_label")) continue;
			const auto label = Tools::readfile(file.path());
			const auto input = hwmon / (name.substr(0, name.size() - 6) + "_input");
			if (label == "Total System Power") power = input;
			if (label.starts_with("CPU Die ")) cpu_die.push_back(input);
			else if (label.starts_with("GPU Die ")) gpu_die.push_back(input);
			else if (name.starts_with("temp")) {
				std::string short_label = label;
				if (label == "NAND Flash Temperature") short_label = "SSD";
				else if (label == "Battery Hotspot") short_label = "Battery";
				else if (label == "Charge Regulator Temp") short_label = "Charger";
				else if (label == "WiFi/BT Module Temp") short_label = "WiFi";
				readings.push_back({input, short_label, true});
			}
		}
		for (const auto& file : fs::directory_iterator(hwmon, ec)) {
			const auto name = file.path().filename().string();
			if (name.starts_with("fan") and name.ends_with("_input"))
				readings.push_back({file.path(), "Fan" + name.substr(3, name.size() - 9), false});
		}
		std::ranges::sort(readings, {}, &Reading::label);
		break;
	}
}

bool available() { return not hwmon.empty(); }

bool has_cpu_temp() { return not cpu_die.empty(); }

long long cpu_temp() { return hottest(cpu_die); }

float system_watts() {
	const auto value = read_number(power);
	return value and *value >= 0 ? *value / 1'000'000.0f : -1.0f;
}

std::string summary() {
	std::string result;
	for (const auto& reading : readings) {
		if (not result.empty()) result += "  ";
		result += reading.label + ' ';
		const auto value = read_number(reading.path);
		if (not value) result += "N/A";
		else if (reading.temperature) result += fmt::format("{:.0f}C", *value / 1000.0);
		else result += fmt::format("{}rpm", *value);
	}
	return result;
}

#ifdef GPU_SUPPORT
void init_gpu() {
	std::error_code ec;
	for (const auto& card : fs::directory_iterator("/sys/class/drm", ec)) {
		const auto name = card.path().filename().string();
		if (not name.starts_with("card") or name.find('-') != std::string::npos) continue;
		const auto driver = fs::read_symlink(card.path() / "device/driver", ec);
		if (ec or driver.filename() != "asahi") continue;
		Gpu::gpu_info gpu{};
		gpu.supported_functions = {false, false, false, false, false, false,
		                          false, false, false, false, false, false};
		gpu.status = "Asahi: live GPU counters unavailable";
		std::string gpu_name = "Apple GPU (Asahi)";
#ifdef BTOP_ASAHI_PARAMS
		const int fd = open(("/dev/dri/" + name).c_str(), O_RDONLY | O_CLOEXEC);
		if (fd >= 0) {
			drm_asahi_params_global params{};
			drm_asahi_get_params query{};
			query.pointer = reinterpret_cast<uintptr_t>(&params);
			query.size = sizeof(params);
			if (ioctl(fd, DRM_IOCTL_ASAHI_GET_PARAMS, &query) == 0) {
				unsigned cores = 0;
				for (const auto mask : params.core_masks) cores += std::popcount(mask);
				gpu_name = fmt::format("Apple G{}{} ({} cores)", params.gpu_generation,
				                      static_cast<char>(params.gpu_variant), cores);
				gpu.hardware_info = fmt::format("Max clock {} MHz; unified memory",
				                                 params.max_frequency_khz / 1000);
			}
			btop_asahi_telemetry sample{};
			if ((params.features & BTOP_ASAHI_FEATURE_TELEMETRY)
			    and ioctl(fd, BTOP_IOCTL_ASAHI_TELEMETRY, &sample) == 0 and sample.abi_version == 2) {
				//? Load and power are always shown: a GPU without fresh firmware samples is idle.
				//? Clock and temperature are enabled once the firmware has reported a usable value.
				gpu.supported_functions.gpu_utilization = true;
				gpu.supported_functions.pwr_usage = true;
				gpu.gpu_percent.at("gpu-totals").push_back(0);
				gpu.gpu_percent.at("gpu-pwr-totals").push_back(0);
				gpu.pwr_usage = 0;
				gpu.gpu_clock_speed = 0;
				gpu.pwr_max_usage = 1000;
				if (const auto temp = gpu_die_temp(); temp >= 0) {
					gpu.supported_functions.temp_info = true;
					gpu.temp = {temp};
				}
				Gpu::gpu_pwr_total_max += gpu.pwr_max_usage;
				gpu.status.clear();
				gpu_devices.emplace_back(Gpu::gpus.size(), fd);
			} else close(fd);
		}
#endif
		Gpu::gpus.push_back(std::move(gpu));
		Gpu::gpu_names.push_back(std::move(gpu_name));
	}
}

void collect_gpu() {
#ifdef BTOP_ASAHI_PARAMS
	constexpr __u32 util_valid = 1, clock_valid = 2, power_valid = 4, temp_valid = 8;
	for (const auto& [index, fd] : gpu_devices) {
		auto& gpu = Gpu::gpus.at(index);
		auto& supported = gpu.supported_functions;
		btop_asahi_telemetry sample{};
		const bool success = ioctl(fd, BTOP_IOCTL_ASAHI_TELEMETRY, &sample) == 0 and sample.abi_version == 2;
		const auto valid = success ? sample.valid : 0;

		//* The firmware only reports while the GPU is powered, so expired samples mean an idle GPU.
		//* Fresh power samples without a utilization sample happen right after a power transition,
		//* while the kernel waits for a second shared-memory update; keep the last load then.
		auto& load = gpu.gpu_percent.at("gpu-totals");
		if (valid & util_valid)
			load.push_back(std::min(100u, sample.utilization_milli_percent / 1000));
		else
			load.push_back(valid & power_valid and not load.empty() ? load.back() : 0);

		gpu.pwr_usage = valid & power_valid ? static_cast<long long>(sample.power_microwatts / 1000) : 0;
		const auto previous_max = gpu.pwr_max_usage;
		gpu.pwr_max_usage = std::max(gpu.pwr_max_usage, gpu.pwr_usage);
		Gpu::gpu_pwr_total_max += gpu.pwr_max_usage - previous_max;
		gpu.gpu_percent.at("gpu-pwr-totals").push_back(gpu.pwr_usage * 100 / gpu.pwr_max_usage);

		const bool has_clock = valid & clock_valid and sample.frequency_khz > 0;
		gpu.gpu_clock_speed = has_clock ? sample.frequency_khz / 1000 : 0;

		//? Prefer the SMC die sensors, then the firmware temperature. Neither reports a real value
		//? while the GPU is powered off: keep the last reading then.
		const auto die_temp = gpu_die_temp();
		const bool has_temp = die_temp >= 0 or valid & temp_valid;
		if (has_temp and not supported.temp_info) gpu.temp.clear();
		if (die_temp >= 0) gpu.temp.push_back(die_temp);
		else if (has_temp) gpu.temp.push_back(sample.temperature_millicelsius / 1000);
		else gpu.temp.push_back(gpu.temp.back());

		if ((has_clock and not supported.gpu_clock) or (has_temp and not supported.temp_info)) {
			supported.gpu_clock |= has_clock;
			supported.temp_info |= has_temp;
			Cpu::redraw = true;
			if (index < Gpu::redraw.size()) Gpu::redraw[index] = true;
		}
	}
#endif
}

void shutdown_gpu() {
#ifdef BTOP_ASAHI_PARAMS
	for (const auto& [index, fd] : gpu_devices) close(fd);
	gpu_devices.clear();
#endif
}
#endif
}
