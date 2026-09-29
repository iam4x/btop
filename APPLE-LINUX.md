# Apple Linux build

Local branch: `apple-linux-monitoring`, based on upstream `7b1f128`.

This build adds the following on Macs using the Asahi driver:

- Total system power from the `macsmc_hwmon` sensor named `Total System Power`.
  The CPU panel labels this value `Sys`. The top-right battery bar shows only
  percentage and charging status, without watts, to avoid repeating it.
- CPU temperature from the hottest `CPU Die Tp??` sensor and GPU temperature
  from the hottest `GPU Die Tg??` sensor. The `-asahi-telemetry3` kernel
  exposes these (see `../linux-asahi/GPU-TELEMETRY.md`). Individual die sensors
  can also be picked as `cpu_sensor` in the options menu. The GPU die sensors
  read a constant 9.199 °C while the GPU is powered off, so readings under
  20 °C are ignored and the last real GPU temperature is kept.
- A GPU load generator for testing: `uv run --with moderngl --with glcontext
  scripts/gpu-load.py [seconds] [iterations]`.
- A row of labeled SMC component temperatures and fan speeds on the CPU panel.
  These temperatures use Celsius, indicated by `C`.
- An Apple GPU panel, detected through the DRM driver's sysfs entry.
  When the Asahi device query is available, it shows the GPU generation,
  enabled core count, maximum clock and unified memory architecture.
- Temperature discovery restricted to `temp*_input`. Faulting or unreadable
  sensors are skipped. Power, voltage and fan readings cannot become CPU
  temperatures. Apple systems no longer choose a random component as the
  CPU temperature when automatic selection fails.

## Limits on this machine

The M1 Pro with kernel `7.1.13-3-2-ARCH` exposes battery, charger, SSD and Wi-Fi
temperatures, plus both fans and total system power. CPU and GPU die
temperatures are not exposed through hwmon or thermal zones.

The GPU query reports Apple G13S, 16 cores and a maximum clock of 1296 MHz.
That is a hardware limit, not the current operating frequency. This driver
does not expose live utilization through DRM fdinfo or a devfreq/sysfs
counter. This patch therefore displays `live GPU counters unavailable`.
The companion kernel patch in `../linux-asahi` implements a read-only telemetry
interface. With that kernel, this build reads firmware utilization, current
clock, estimated GPU power and GPU temperature. On the stock kernel it
continues to report unavailable counters.

## Build and install

```sh
bash scripts/build-apple.sh
```

This builds with GPU support and installs to `~/.local/bin/btop`. The packaged
version remains at `/usr/bin/btop`. `INTEL_GPU_SUPPORT=true` includes the Intel
helper objects required by upstream's Linux GPU collector on this ARM build.

The local shell configuration puts `~/.local/bin` first, and the user desktop
entry points directly to this binary. Open a new terminal for the shell PATH
change, or invoke `~/.local/bin/btop` directly.

The btop config enables `gpu0`. Press `5` to toggle the GPU panel.

## Verify

```sh
uv run --with pyte python scripts/verify-apple.py --output verification/apple-host
```

Run this on the desktop host to include the GPU device query. The script
launches btop in a real pseudo-terminal with an isolated temporary config,
captures its rendered screen at 160×50, 100×40 and 80×24, checks the sensor
labels and power display, tests both CPU positions, and checks clean exit.
Screenshots as terminal text and ANSI recordings are saved to the output
directory. The verification directory is ignored by Git.

To use the distro build explicitly, run `/usr/bin/btop`. Configuration backups
from installation are beside the original files, with a `.before-asahi-`
timestamp suffix.

## Experimental kernel telemetry

Version `1.4.7-asahi.5` reads the local kernel extension only when Asahi's
GET_PARAMS advertises feature bit 63 and the query returns ABI version 2.

The firmware only sends measurements while the GPU is powered, and the kernel
expires them after two seconds. An idle GPU therefore has no valid samples
most of the time. btop treats expired load and power as an idle GPU, `0%` and
`0.00W`, instead of hiding the GPU. It keeps the last load while fresh power
samples arrive without a load sample, which happens right after a power
transition. The temperature is shown after the first valid reading and then
keeps the last value while the GPU is off. The clock is shown only after the
firmware reports a nonzero frequency. Power meters scale to observed peak GPU
power. GPU memory accounting is not added by this patch.

GPU temperature in the CPU panel no longer depends on a CPU temperature sensor.
This Mac has none, so the GPU temperature was previously never shown there.

Verified on `7.1.13-asahi-telemetry2+` with a headless EGL load: about 99%
load, 16.6 W and 60 °C, then 0% and 0.00W when the load stops.

Known kernel limitation: `HwDataA.freq_mhz` always reads 0 on this firmware,
so no GPU clock is displayed. The live clock would have to come from
`actual_pstate` mapped through `dyncfg.pwr.perf_states[].freq_hz`.

See [the kernel notes](../linux-asahi/GPU-TELEMETRY.md) for build, installation
and units.

The fixture in `scripts/asahi-telemetry-fixture.c` is exclusively for tests.
It is never loaded by the normal executable or desktop entry.
