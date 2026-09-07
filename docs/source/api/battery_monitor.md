# battery_monitor

Reads battery charge, health and the current power source.

See [Stats Reference](stats) for the data structures returned by this monitor.

```cpp
#include "sysmon/battery_monitor.hpp"
```

On a machine without a battery, `BatteryStats::present` stays `false` and every
optional field stays empty; the renderers then omit the section entirely rather
than printing zeroes.

| Platform | Source |
|----------|--------|
| Linux | `/sys/class/power_supply/BAT*` |
| macOS | IOKit `AppleSmartBattery` |
| Windows | `GetSystemPowerStatus` (charge and runtime only) |

> Full Doxygen API documentation is generated automatically when building with `cmake -DCMAKE_BUILD_TYPE=Release` and Doxygen installed.
