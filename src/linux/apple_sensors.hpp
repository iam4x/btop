#pragma once

#include <string>

namespace AppleSensors {

void init();
bool available();
bool has_cpu_temp();
long long cpu_temp();
float system_watts();
std::string summary();

#ifdef GPU_SUPPORT
void init_gpu();
void collect_gpu();
void shutdown_gpu();
#endif

}
