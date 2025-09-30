# AREDN-Phonebook Testing Guide

## Current Implementation Status

### ✅ Phase 0: Software Health Monitoring (Complete)
- Software health tracking (CPU, memory, threads, crashes)
- Health JSON export to `/tmp/meshmon_health.json`
- Crash reporting to `/tmp/meshmon_crashes.json`
- CGI endpoints: `/cgi-bin/health`, `/cgi-bin/crash`

### 🚧 Phase 1: Network Monitoring (Foundation Complete, Not Integrated)
- Mesh monitor module structure created
- Configuration parser, routing adapter, probe engine implemented
- **Not yet integrated into build** - requires Makefile updates

## Testing Phase 0 (Software Health - Currently Deployed)

### Prerequisites
- AREDN router with phonebook installed
- SSH access to router
- Basic understanding of shell commands

### 1. Test Health Monitoring Initialization

```bash
# SSH to router
ssh root@your-node.local.mesh

# Check if phonebook is running
ps | grep aredn-phonebook

# Check if health monitoring is enabled
cat /etc/sipserver.conf | grep -A 10 "\[health\]"

# Restart phonebook service
/etc/init.d/AREDN-Phonebook restart

# Check logs for health initialization
logread | grep "SOFTWARE_HEALTH"
```

**Expected output:**
```
daemon.info AREDN-Phonebook[PID]: Initializing software health monitoring
daemon.info AREDN-Phonebook[PID]: Software health monitoring initialized (PID: XXXX, Initial RSS: X MB)
```

### 2. Test Health JSON Export

```bash
# Wait 60 seconds for first export
sleep 60

# Check if health JSON exists
ls -la /tmp/meshmon_health.json

# View health JSON
cat /tmp/meshmon_health.json | json_pp
```

**Expected JSON structure:**
```json
{
  "schema": "meshmon.v1",
  "type": "agent_health",
  "node": "YOUR-NODE-NAME",
  "sent_at": "2025-09-30T19:17:20Z",
  "cpu_pct": 0.0,
  "mem_mb": 0.2,
  "queue_len": 0,
  "uptime_seconds": 123,
  "restart_count": 0,
  "threads_responsive": true,
  "health_score": 100.0,
  "checks": {
    "memory_stable": true,
    "no_recent_crashes": true,
    "sip_service_ok": true,
    "phonebook_current": true
  },
  "sip_service": {
    "active_calls": 0,
    "registered_users": 0
  },
  "monitoring": {
    "probe_queue_depth": 0,
    "last_probe_sent": "N/A"
  }
}
```

### 3. Test CGI Endpoints

```bash
# Test health endpoint
curl http://localhost/cgi-bin/health

# Test crash endpoint (should return empty array if no crashes)
curl http://localhost/cgi-bin/crash

# Test from another node on the mesh
curl http://your-node.local.mesh/cgi-bin/health
```

### 4. Test Thread Monitoring

```bash
# Phonebook should have 4 threads running:
# - main (SIP processing)
# - fetcher (phonebook updates)
# - updater (user status)
# - safety (passive safety)

# The health_score should be 100.0 if all threads responsive
cat /tmp/meshmon_health.json | grep threads_responsive
# Should show: "threads_responsive": true
```

### 5. Test Memory Monitoring

```bash
# Monitor memory over time
for i in {1..5}; do
  echo "=== Check $i ==="
  cat /tmp/meshmon_health.json | grep mem_mb
  sleep 60
done
```

**Expected:** Memory should be stable (not growing continuously)

### 6. Test Crash Detection

**WARNING: This will intentionally crash the phonebook!**

```bash
# Send crash signal (SIGSEGV) to phonebook
kill -11 $(pidof aredn-phonebook)

# Wait a moment, service should restart
sleep 5

# Check crash report
cat /tmp/meshmon_crashes.json

# Check if crash was logged
logread | grep "CRASH DETECTED"
```

**Expected crash report:**
```json
[
  {
    "schema": "meshmon.v1",
    "type": "crash_report",
    "node": "YOUR-NODE-NAME",
    "sent_at": "2025-09-30T19:20:00Z",
    "crash_time": "2025-09-30T19:20:00Z",
    "signal": 11,
    "signal_name": "SIGSEGV",
    "reason": "Signal 11 at ...",
    "restart_count": 0,
    "uptime_before_crash": 300
  }
]
```

## Testing Phase 1 (Network Monitoring - NOT YET INTEGRATED)

⚠️ **Phase 1 code exists but is not integrated into the build yet.**

To enable Phase 1 for testing, you need to:

### 1. Update Makefile

Add mesh_monitor source files to the build:

```makefile
# Edit Phonebook/Makefile, add to Build/Compile section:
$(PKG_BUILD_DIR)/mesh_monitor/mesh_monitor.c \
$(PKG_BUILD_DIR)/mesh_monitor/monitor_config.c \
$(PKG_BUILD_DIR)/mesh_monitor/routing_adapter.c \
$(PKG_BUILD_DIR)/mesh_monitor/probe_engine.c \
```

### 2. Wire into main.c

```c
// Add to main.c includes:
#include "mesh_monitor/mesh_monitor.h"

// Add after software_health_init():
mesh_monitor_config_t monitor_config;
if (mesh_monitor_init(&monitor_config) == 0) {
    if (is_mesh_monitor_enabled()) {
        pthread_t monitor_tid;
        pthread_create(&monitor_tid, NULL, mesh_monitor_thread, NULL);
    }
}
```

### 3. Add Configuration

Edit `/etc/sipserver.conf`:

```ini
[mesh_monitor]
enabled = 1
mode = lightweight
network_status_interval_s = 40
probe_window_s = 5
neighbor_targets = 2
probe_port = 40050
routing_daemon = auto
```

### 4. Test Network Monitoring (Once Integrated)

```bash
# Check if mesh monitor initialized
logread | grep "MESH_MONITOR"

# Check if routing daemon detected
logread | grep "Detected.*routing daemon"

# Check if probe responder is running
netstat -ulnp | grep 40050

# TODO: Test probe cycle (not yet fully implemented)
# TODO: Test OLSR integration (stub only)
# TODO: Test metrics calculation (stub only)
```

## Quick Validation Checklist

### Phase 0 (Current)
- [ ] Health JSON file exists at `/tmp/meshmon_health.json`
- [ ] Health JSON updates every 60 seconds
- [ ] `threads_responsive` is `true`
- [ ] `health_score` is > 90.0
- [ ] `/cgi-bin/health` returns valid JSON
- [ ] `/cgi-bin/crash` returns `[]` or crash reports
- [ ] Memory usage is stable over 10 minutes
- [ ] After restart, health state recovers

### Phase 1 (Future - After Integration)
- [ ] Mesh monitor initializes without errors
- [ ] Routing daemon (OLSR/Babel) detected
- [ ] Probe responder listening on UDP port 40050
- [ ] Probe cycles run every 40 seconds
- [ ] Network metrics calculated (RTT, jitter, loss)
- [ ] `/tmp/meshmon_network.json` created
- [ ] `/cgi-bin/network` returns probe results

## Troubleshooting

### Health JSON Not Created

```bash
# Check if health monitoring enabled
cat /etc/sipserver.conf | grep -A 5 "\[health\]"

# Check logs for errors
logread | grep -i error | grep -i health

# Check if service is running
/etc/init.d/AREDN-Phonebook status
```

### CGI Endpoints Return Errors

```bash
# Check if uhttpd is running
/etc/init.d/uhttpd status

# Check CGI script permissions
ls -la /www/cgi-bin/health
# Should be: -rwxr-xr-x (executable)

# Test CGI script directly
/www/cgi-bin/health
```

### Memory Growing Continuously

```bash
# Check for leak detection
cat /tmp/meshmon_health.json | grep leak_suspected

# Monitor RSS over time
for i in {1..10}; do
  cat /tmp/meshmon_health.json | grep mem_mb
  sleep 60
done
```

## Performance Benchmarks

### Expected Resource Usage (Phase 0)
- **Binary Size:** ~1.1 MB (static)
- **RAM (idle):** 8-10 MB
- **CPU (average):** <2%
- **Flash writes:** 1-2/day
- **Network:** Negligible (only responds to HTTP requests)

### Expected Resource Usage (Phase 1 - When Enabled)
- **Binary Size:** ~1.4 MB (+300 KB)
- **RAM (idle):** 12-14 MB (+4 MB for probe buffers)
- **CPU (average):** <5%
- **Network:** ~10 KB/s during probe windows (5 seconds every 40 seconds)

## Integration Testing with External Tools

### Query Health from Monitoring System

```bash
# From any machine on the mesh network
curl http://node.local.mesh/cgi-bin/health

# Parse with jq
curl -s http://node.local.mesh/cgi-bin/health | jq '.health_score'
```

### Simulate Monitoring Collection

```python
#!/usr/bin/env python3
import requests
import json
import time

nodes = ['node1.local.mesh', 'node2.local.mesh']

while True:
    for node in nodes:
        try:
            r = requests.get(f'http://{node}/cgi-bin/health', timeout=5)
            health = r.json()
            print(f"{node}: Score={health['health_score']}, "
                  f"Mem={health['mem_mb']}MB, "
                  f"Responsive={health['threads_responsive']}")
        except Exception as e:
            print(f"{node}: ERROR - {e}")

    time.sleep(60)
```

## Next Steps

1. ✅ Verify Phase 0 works on your AREDN router
2. 🔧 Complete Phase 1 integration (Makefile + main.c)
3. 🧪 Implement remaining stubs (OLSR client, metrics calculator)
4. 🔍 Test on multi-hop mesh network
5. 📊 Build centralized collector (optional)
