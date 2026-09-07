# Usage & Modes

`sysmon` can be used as an interactive live TUI dashboard, a continuous terminal stream, or a one-shot snapshot tool.

---

## 1. Live Interactive TUI Dashboard (Default)

Launch the full-screen interactive monitor:

```bash
sysmon
```

### In-TUI Interactive Controls

Press any of the following keys at runtime to toggle components on and off immediately:

- **`c`** : Toggle individual CPU Core usage bars
- **`g`** : Toggle GPU / Graphics stats and VRAM / Unified Memory
- **`n`** : Toggle Network interfaces and live sparklines
- **`v`** : Toggle Active Network Connections table
- **`p`** : Toggle Top Processes table
- **`t`** : Toggle Temperatures and Hardware Sensors
- **`d`** : Toggle Filesystems & Disk I/O read/write rates
- **`b`** : Toggle Battery & Power
- **`o`** : Cycle the process sort order (cpu → mem → time → pid → name)
- **`m`** : Toggle **Compact Mode** (switches between full granular view and summary dashboard)
- **`s`** : **Save** your current display choices to the config file location
- **`r`** : Force immediate refresh
- **`q`** / **`ESC`** : Quit

---

## 2. One-Shot Snapshot Mode

Capture all metrics at a single moment and print clean formatted text to stdout:

```bash
sysmon --once
```

### Use in Shell Scripts & Cron Jobs

```bash
# Append system metrics every 5 minutes to a log file
sysmon --once >> /var/log/system_health.log

# Extract only CPU and GPU metrics
sysmon --once | grep -A 8 "^CPU\|^GPU"
```

---

## 3. Streaming CLI Mode (No ANSI TUI)

Stream continuously without full-screen redraws (ideal for simple serial terminals or remote logging):

```bash
sysmon --no-tui --interval 1
```

---

## 4. Customizing Dashboard Content via CLI Flags

You can customize what is shown directly on the command line:

```bash
# Show only CPU, GPU, and Network (hide disks, processes, connections)
sysmon --no-disk --no-proc --no-conn

# Start in compact summary mode
sysmon --compact

# Show top 50 processes with a fast 1-second refresh rate
sysmon --limit 50 --interval 1

# Load custom config profile
sysmon --config /etc/sysmon/server-profile.conf
```


---

## 3. JSON Output

`--json` emits one complete snapshot as a JSON document and exits. It carries
every field sysmon collects, including the ones the TUI and text views leave out
for space.

```bash
sysmon --json
sysmon --json-compact          # one line, suitable for appending to a log
```

### The export is not a view

The visibility toggles (`--no-proc`, `--no-gpu`, ...) and the config file's
`proc_limit` / `connections_limit` shape the dashboard, not the export. If they
did shape it, `"processes": []` would mean "none exist" on one machine and "the
config file hid them" on another, and a stale `proc_limit = 5` in
`~/.config/sysmon/sysmon.conf` would silently truncate every snapshot a
monitoring pipeline collected.

So `--json` always collects every section in full. An explicit `--limit N` on
the command line is a deliberate request and is still honoured.

### The null contract

A metric the platform cannot measure through a stable, unprivileged API is
emitted as `null`, never as `0`. This lets a consumer tell "not supported on
this platform" apart from "measured, and the value is zero":

```json
{
  "cpu": {
    "usage_percent": 18.40,
    "frequency_mhz": null,
    "temperature_celsius": null,
    "thermal_pressure": "Nominal"
  }
}
```

### Recipes

```bash
sysmon --json | jq '.cpu.usage_percent'
sysmon --json | jq '.disks[] | select(.usage_percent > 90) | .mountpoint'
sysmon --json | jq '.network.interfaces[] | select(.is_up) | {name, speed_mbps}'
sysmon --json | jq '.processes | sort_by(-.mem_rss_bytes) | .[0:5] | .[].name'
```

Sampling into a newline-delimited JSON file:

```bash
while sleep 10; do sysmon --json-compact; done >> metrics.ndjson
```

---

## 4. Sorting and Filtering Processes

```bash
sysmon --sort mem --limit 10        # ten largest processes by resident memory
sysmon --sort time                  # by accumulated CPU time, like `ps -o time`
sysmon --once --sort name
```

Valid sort keys are `cpu`, `mem`, `pid`, `name` and `time`.

---

## 5. Network Detail

```bash
sysmon --net-details        # MAC, MTU, duplex, lifetime totals, errors, drops
sysmon --all-interfaces     # include interfaces that have never carried traffic
sysmon --listen             # include listening sockets in the connection table
```

---

## Exit Status

| Status | Meaning |
|--------|---------|
| `0` | Success |
| `2` | Invalid command line (unknown option, or a bad value for one) |

Unknown options are an error rather than being silently ignored, so a typo such
as `--limt 5` fails loudly instead of appearing to work.
