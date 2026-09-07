# Usage & Modes

`sysmon` can be used as an interactive live TUI dashboard, a continuous terminal stream, or a one-shot snapshot tool.

---

## 1. Live Interactive TUI Dashboard (Default)

Launch the full-screen interactive monitor:

```bash
sysmon
```

### Views

The dashboard opens on an overview of every enabled section. A digit switches to
a full-screen view of one subsystem, which has room for the fields the overview
leaves out and scrolls when the list is longer than the screen.

| Key | View | What the focus view adds |
|-----|------|--------------------------|
| `0` | Overview | Every enabled section |
| `1` | CPU | Socket/core/thread topology, P and E clusters, cache sizes, every core in a scrollable table, context-switch and interrupt rates, instruction-set flags |
| `2` | Memory | Active, inactive, wired, compressed, shared, slab and dirty; commit charge; page-fault, paging and swap rates; the processes holding the most memory |
| `3` | GPU | Per adapter: driver, core count, VRAM total/used/free, core and memory clocks, encoder and decoder load, temperature, power, fan |
| `4` | Disk | Inode usage, mount options, read-only and removable flags; per-device IOPS, utilisation, average latency and queue depth; the processes doing the I/O |
| `5` | Network | Per interface MAC, MTU, duplex, link speed, lifetime totals, errors and drops; default gateway, DNS servers, socket census; upload by process |
| `6` | Connections | Every socket with its state and owning process, scrollable |
| `7` | Processes | The whole process table, scrollable, with CPU time and per-process disk I/O |
| `8` | Sensors | Every temperature sensor with its high and critical thresholds, fan tachometers, and full battery detail |

`Tab` cycles the views in that order. `Esc` steps back to the overview, and
quits only from the overview — so a mistyped view change is not a quit.

### Inspecting one process

In the process view, `↑`/`↓` move a cursor and `Enter` opens an inspector for
the selected process. The cursor follows the process, not the row: the table
re-sorts on every refresh, so a fixed row would select a different process each
frame.

The inspector shows the full command line, parent PID, nice level, accumulated
CPU time, resident and virtual memory, disk read and write rates alongside the
lifetime totals, the process's own network connections, and the files, sockets
and pipes it currently has open. Descriptor tables are read for the inspected
process only — walking several hundred processes' descriptors once per refresh
would cost more than every other monitor combined.

Another user's process is refused by the kernel. That is reported as
`permission denied`, distinct from an empty list, because "this process has no
files open" and "you may not look" are different answers.

### Density

Four levels, changed live with `+` and `-`:

| Level | Meaning |
|-------|---------|
| `compact` | One line per subsystem — fits a small pane |
| `normal` | The default dashboard |
| `detailed` | Every field a section has a layout for |
| `full` | Detailed plus the long tails: all cores, all sensors, CPU flags, command lines |

`m` toggles between compact and normal. The level applies to the plain-text
output as well, so `--once --no-tui --detail full` prints everything sysmon can
lay out as text.

### All the interactive keys

| Key | Action |
|-----|--------|
| `0`–`8` | Switch to that view |
| `Tab` | Next view |
| `Esc` | Back to the overview; quits from the overview |
| `+` / `-` | More / less detail |
| `m` | Toggle compact density |
| `↑` `↓` / `k` `j` | Move the cursor in a list |
| `PgUp` / `PgDn` | Page through a list |
| `Home` / `End` | Jump to the start / end of a list |
| `Enter` | Inspect the selected process |
| `a` | Show all — lift the process and connection limits |
| `c` | Toggle individual CPU core bars |
| `g` | Toggle GPU and VRAM |
| `n` | Toggle network interfaces and sparklines |
| `v` | Toggle the connections table |
| `p` | Toggle the process table |
| `t` | Toggle temperatures and sensors |
| `d` | Toggle filesystems and disk I/O |
| `b` | Toggle battery and power |
| `o` | Cycle the process sort order (cpu → mem → time → pid → name) |
| `s` | Save the current display choices to the config file |
| `r` | Force an immediate refresh |
| `q` / `Ctrl+C` | Quit |

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

# Open directly on one subsystem, at full detail
sysmon --view processes --detail full
sysmon --view network --detail detailed

# Every process and every connection, no limits
sysmon --all

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
