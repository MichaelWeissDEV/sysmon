# json_renderer

Serialises a complete [`Snapshot`](stats) as JSON.

```cpp
#include "sysmon/json_renderer.hpp"
```

Every metric sysmon collects appears here, including the ones the text and TUI
renderers omit for space.

## The null contract

A metric a platform cannot measure through a stable, unprivileged API is emitted
as `null`, never as `0`. This is what lets a consumer distinguish "not supported
on this platform" from "measured, and the value is zero". Non-finite doubles
(`NaN`, infinity) are not valid JSON and also degrade to `null` rather than
producing a document no parser will accept.

```bash
sysmon --json | jq '.cpu.usage_percent'
sysmon --json-compact >> metrics.ndjson
```

> Full Doxygen API documentation is generated automatically when building with `cmake -DCMAKE_BUILD_TYPE=Release` and Doxygen installed.
