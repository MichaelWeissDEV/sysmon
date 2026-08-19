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
