# AREDN-Phonebook Testing Guide

## Current Implementation Status

### ✅ Phase 0: Software Health Monitoring (Complete)
- Software health tracking (CPU, memory, threads, crashes)
- Health JSON export to `/tmp/meshmon_health.json`
- Crash reporting to `/tmp/meshmon_crashes.json`
- CGI endpoints: `/cgi-bin/health`, `/cgi-bin/crash`

### ✅ Phase 1: Network Monitoring (Complete and Integrated)
- Mesh monitor module fully implemented
- Configuration parser, routing adapter, probe engine complete
- OLSR jsoninfo HTTP client implemented
- Babel routing daemon support via Unix socket
- RFC3550 metrics calculation (RTT, jitter, packet loss)
- Network JSON export to `/tmp/meshmon_network.json`
- CGI endpoint: `/cgi-bin/network`
- **Integrated into build** - ready for testing

### ✅ Phase 2: Path Quality Analysis (Complete)
- Hop-by-hop path reconstruction from routing tables
- Per-hop ETX, LQ, NLQ metrics from OLSR
- Link type classification (RF, tunnel, ethernet)
- Path quality data included in network JSON
- Supports both OLSR and Babel routing protocols

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
  "routing_daemon": "olsr",
  "lat": "47.123456",
  "lon": "8.654321",
  "grid_square": "JN47xe",
  "hardware_model": "MikroTik RouterBOARD 952Ui-5ac2nD",
  "firmware_version": "3.24.10.0",
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

### 6. Test Node Static Information (Geographic Location)

```bash
# Check if sysinfo.json is accessible
curl http://localhost:8080/cgi-bin/sysinfo.json

# Check if static node info was fetched during startup
logread | grep "Node info:"

# Expected output:
# daemon.info AREDN-Phonebook[PID]: SOFTWARE_HEALTH: Node info: lat=47.123456, lon=8.654321, grid=JN47xe, model=MikroTik..., fw=3.24.10.0

# Verify static info in health JSON
cat /tmp/meshmon_health.json | grep -E "lat|lon|grid_square|hardware_model|firmware_version"
```

**Expected fields:**
```json
"routing_daemon": "olsr",
"lat": "47.123456",
"lon": "8.654321",
"grid_square": "JN47xe",
"hardware_model": "MikroTik RouterBOARD 952Ui-5ac2nD",
"firmware_version": "3.24.10.0"
```

**Note:** If sysinfo.json is not available, fields will show "unknown" but monitoring will still work.

### 7. Test Crash Detection

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

## Testing Phase 1 & 2 (Network Monitoring - INTEGRATED)

✅ **Phase 1 and Phase 2 are now fully integrated and ready for testing.**

Phase 1 covers basic network probing and metrics. Phase 2 adds hop-by-hop path analysis with ETX/LQ/NLQ metrics.

### 1. Configuration

Edit `/etc/sipserver.conf` and add the mesh_monitor section:

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

### 2. Deploy and Restart

```bash
# Upload new ipk to router
scp aredn-phonebook_*.ipk root@your-node.local.mesh:/tmp/

# SSH to router
ssh root@your-node.local.mesh

# Install updated package
opkg remove aredn-phonebook
opkg install /tmp/aredn-phonebook_*.ipk

# Restart service
/etc/init.d/AREDN-Phonebook restart
```

### 3. Test Network Monitoring

```bash
# Check if mesh monitor initialized
logread | grep "MESH_MONITOR"

# Expected output:
# daemon.info AREDN-Phonebook[PID]: MESH_MONITOR: Initializing mesh monitor
# daemon.info AREDN-Phonebook[PID]: MESH_MONITOR: Mesh monitor initialized successfully
# daemon.info AREDN-Phonebook[PID]: MESH_MONITOR: Starting mesh monitor thread...
# daemon.info AREDN-Phonebook[PID]: MESH_MONITOR: Mesh monitor thread started successfully

# Check if routing daemon detected
logread | grep "ROUTING_ADAPTER"

# Expected output:
# daemon.info AREDN-Phonebook[PID]: ROUTING_ADAPTER: Detected OLSR routing daemon

# Check if probe responder is running
netstat -ulnp | grep 40050

# Expected output:
# udp        0      0 0.0.0.0:40050           0.0.0.0:*               PID/aredn-phonebook

# Wait for first probe cycle (40 seconds)
sleep 45

# Check if network JSON was created
ls -la /tmp/meshmon_network.json

# View network monitoring data
cat /tmp/meshmon_network.json | json_pp

# Test network CGI endpoint
curl http://localhost/cgi-bin/network

# Test from another mesh node
curl http://your-node.local.mesh/cgi-bin/network
```

**Expected network JSON structure:**
```json
{
  "schema": "meshmon.v1",
  "type": "network_status",
  "node": "YOUR-NODE-NAME",
  "sent_at": "2025-09-30T19:30:00Z",
  "routing_daemon": "olsr",
  "probe_count": 2,
  "probes": [
    {
      "dst_node": "NEIGHBOR-NODE-1",
      "dst_ip": "10.54.1.1",
      "timestamp": "2025-09-30T19:29:55Z",
      "routing_daemon": "olsr",
      "rtt_ms_avg": 12.34,
      "jitter_ms": 1.23,
      "loss_pct": 0.0,
      "hop_count": 2,
      "path": [
        {
          "node": "HOP-1-NODE",
          "interface": "wlan0",
          "link_type": "RF",
          "lq": 1.0,
          "nlq": 0.98,
          "etx": 1.02,
          "rtt_ms": 0.0
        },
        {
          "node": "NEIGHBOR-NODE-1",
          "interface": "wlan0",
          "link_type": "RF",
          "lq": 0.95,
          "nlq": 1.0,
          "etx": 1.05,
          "rtt_ms": 0.0
        }
      ]
    }
  ]
}
```

### 4. Test Routing Daemon Detection

```bash
# Check which routing daemon was detected
logread | grep "Detected.*routing daemon"

# Expected output (OLSR):
# daemon.info AREDN-Phonebook[PID]: ROUTING_ADAPTER: Detected OLSR routing daemon

# Expected output (Babel):
# daemon.info AREDN-Phonebook[PID]: ROUTING_ADAPTER: Detected Babel routing daemon

# Verify routing daemon in health JSON
cat /tmp/meshmon_health.json | grep routing_daemon

# Verify routing daemon in network JSON
cat /tmp/meshmon_network.json | grep routing_daemon
```

### 5. Test OLSR Integration

```bash
# Check if OLSR is running
ps | grep olsrd

# Test OLSR jsoninfo API directly (limit output to avoid flooding)
curl -s http://127.0.0.1:9090/neighbors
curl -s http://127.0.0.1:9090/routes | head -20
curl -s http://127.0.0.1:9090/topology | head -20

# Get OLSR summary
echo "Total routes: $(curl -s http://127.0.0.1:9090/routes | grep -c destinationIP)"
echo "Direct neighbors: $(curl -s http://127.0.0.1:9090/neighbors | grep -c neighborIP)"

# Check if mesh monitor is querying OLSR
logread | grep "OLSR"
```

### 6. Test Babel Integration

```bash
# Check if Babel is running
ps | grep babeld

# Check if Babel control socket exists
ls -la /var/run/babeld.sock

# If socket doesn't exist, check Babel config for local-port
cat /etc/babel.conf | grep -E "local-port|enable-timestamps"

# Test Babel commands manually (requires control socket)
# Note: Babel needs 'local-port /var/run/babeld.sock' in config
echo "dump" | nc -U /var/run/babeld.sock 2>/dev/null || echo "Babel control socket not available"

# Alternative: Check if Babel is detected even without socket
logread | grep "Babel"
```

**Note about Babel:** AREDN's default Babel configuration may not enable the control socket (`/var/run/babeld.sock`). The mesh monitor will detect Babel is running via the PID file (`/var/run/babeld.pid`) but won't be able to query routing tables without the control socket. This is a known limitation - Babel support requires the control socket to be configured in `/etc/babel.conf`:

```conf
# Add to /etc/babel.conf for full Babel support
local-port /var/run/babeld.sock
enable-timestamps true
```

Without the control socket, the mesh monitor will:
- ✅ Detect Babel is running
- ✅ Report `routing_daemon: "babel"`
- ❌ Cannot query neighbors or routes
- ❌ Phase 2 hop-by-hop analysis not available

### 7. Test Phase 2 Path Analysis

```bash
# Check for hop-by-hop path information in network JSON
cat /tmp/meshmon_network.json | json_pp | grep -A 20 "path"

# Verify hop_count is populated
cat /tmp/meshmon_network.json | grep hop_count

# Check for link type classification
cat /tmp/meshmon_network.json | grep link_type

# Verify ETX metrics are present
cat /tmp/meshmon_network.json | grep etx
```

**Expected hop information:**
- `node`: Node name for each hop
- `interface`: Network interface (wlan0, eth0, tun0, etc.)
- `link_type`: RF (wireless), tunnel, ethernet, or bridge
- `lq`: Link quality (0.0-1.0)
- `nlq`: Neighbor link quality (0.0-1.0)
- `etx`: Expected transmission count (lower is better)

### 8. Test UDP Probe Engine

```bash
# Check if probe responder is listening
netstat -ulnp | grep 40050

# Expected output:
# udp  0  0 0.0.0.0:40050  0.0.0.0:*  PID/aredn-phonebook

# Send test probe manually (from another node)
echo "test" | nc -u your-node.local.mesh 40050

# Check probe logs
logread | grep "PROBE_ENGINE"

# Monitor probe activity
logread -f | grep "Probing neighbor"
```


## Quick Validation Checklist

### Phase 0 (Software Health)
- [ ] Health JSON file exists at `/tmp/meshmon_health.json`
- [ ] Health JSON updates every 60 seconds
- [ ] `routing_daemon` field shows "olsr" or "babel"
- [ ] `lat`, `lon`, `grid_square` fields populated (or "unknown")
- [ ] `hardware_model` and `firmware_version` populated
- [ ] `threads_responsive` is `true`
- [ ] `health_score` is > 90.0
- [ ] `/cgi-bin/health` returns valid JSON
- [ ] `/cgi-bin/crash` returns `[]` or crash reports
- [ ] Memory usage is stable over 10 minutes
- [ ] After restart, health state recovers

### Phase 1 (Network Monitoring)
- [ ] Mesh monitor initializes without errors
- [ ] Routing daemon (OLSR/Babel) detected correctly
- [ ] Probe responder listening on UDP port 40050
- [ ] Probe cycles run every 40 seconds (configurable)
- [ ] Network metrics calculated (RTT, jitter, loss per RFC3550)
- [ ] `/tmp/meshmon_network.json` created and updates
- [ ] `/cgi-bin/network` returns valid JSON probe results
- [ ] OLSR jsoninfo neighbors queried successfully (if OLSR)
- [ ] Babel socket commands working (if Babel)
- [ ] Probe responses received and metrics accurate
- [ ] `routing_daemon` field in network JSON matches detected daemon

### Phase 2 (Path Quality Analysis)
- [ ] Hop-by-hop path data included in network JSON
- [ ] `hop_count` populated for each probe
- [ ] `path` array contains hop information
- [ ] Link type classification working (RF, tunnel, ethernet)
- [ ] ETX, LQ, NLQ metrics present for each hop
- [ ] Multi-hop paths reconstructed correctly
- [ ] Works with both OLSR and Babel routing daemons

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

### Geographic Location Shows "unknown"

```bash
# Check if sysinfo.json is accessible
curl http://localhost:8080/cgi-bin/sysinfo.json

# If not accessible, check AREDN web interface
# Navigate to: http://your-node.local.mesh:8080/cgi-bin/status

# Check if location is configured in AREDN
# Navigate to: Setup > Basic Setup > Location section

# If sysinfo returns data, check logs for parsing errors
logread | grep "fetch_node_static_info"
```

### Routing Daemon Not Detected

```bash
# Check which routing daemon is actually running
ps | grep -E "olsrd|babeld"

# Check for PID files
ls -la /var/run/olsrd.pid
ls -la /var/run/babeld.pid

# Check logs for detection errors
logread | grep "ROUTING_ADAPTER"

# Manually test OLSR (if installed)
curl -s http://127.0.0.1:9090/neighbors | head -10

# Manually test Babel (if installed)
ls -la /var/run/babeld.sock
# If socket missing, Babel control socket is not configured
cat /etc/babel.conf | grep local-port
```

**Common Issue:** Babel is running but socket is missing. This is normal on AREDN - Babel's control socket is not enabled by default. The monitor will detect Babel but cannot query routing tables. OLSR works out of the box because it uses HTTP jsoninfo API.

### Network JSON Not Created

```bash
# Check if mesh monitor is enabled
cat /etc/sipserver.conf | grep -A 10 "\[mesh_monitor\]"

# Check if mesh monitor initialized
logread | grep "MESH_MONITOR"

# Check for routing adapter errors
logread | grep "ROUTING_ADAPTER"

# Check if neighbors were found
logread | grep "neighbors found"

# Force a probe cycle (wait 40+ seconds after restart)
sleep 45
ls -la /tmp/meshmon_network.json
```

### No Hop-by-Hop Path Data

```bash
# Check if routing daemon provides topology info
# For OLSR:
curl http://127.0.0.1:9090/topology
curl http://127.0.0.1:9090/routes

# For Babel:
echo "dump" | nc -U /var/run/babeld.sock

# Check logs for path reconstruction
logread | grep "Path to"

# Verify hop_count in network JSON
cat /tmp/meshmon_network.json | grep hop_count
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

1. ✅ ~~Verify Phase 0 works on your AREDN router~~
2. ✅ ~~Complete Phase 1 integration (Makefile + main.c)~~
3. ✅ ~~Implement OLSR client and metrics calculator~~
4. ✅ ~~Add Babel routing daemon support~~
5. ✅ ~~Implement Phase 2 hop-by-hop path analysis~~
6. ✅ ~~Add routing daemon identification to reports~~
7. ✅ ~~Add geographic location and hardware info~~
8. 🔍 **Test on multi-hop mesh network with real AREDN nodes**
9. 🧪 Validate Babel support on Babel-based mesh
10. 📊 Build centralized collector for network-wide monitoring (optional)
11. 🎨 Create web dashboard for visualization (optional)

## Summary of Features

**Phase 0 - Software Health:**
- ✅ CPU, memory, thread monitoring
- ✅ Crash detection and reporting
- ✅ Health scoring (0-100)
- ✅ Geographic location (lat/lon/grid_square)
- ✅ Hardware model and firmware version
- ✅ Routing daemon identification

**Phase 1 - Network Monitoring:**
- ✅ Auto-detect OLSR or Babel routing daemon
- ✅ UDP probe engine (RFC3550 metrics)
- ✅ RTT, jitter, packet loss measurement
- ✅ Neighbor discovery from routing tables
- ✅ Configurable probe intervals and targets

**Phase 2 - Path Quality:**
- ✅ Hop-by-hop path reconstruction
- ✅ Per-hop ETX, LQ, NLQ metrics
- ✅ Link type classification (RF, tunnel, ethernet)
- ✅ Multi-hop path analysis
- ✅ Works with both OLSR and Babel

**Data Export:**
- ✅ JSON export (meshmon.v1 schema)
- ✅ HTTP CGI endpoints (/cgi-bin/health, /cgi-bin/network, /cgi-bin/crash)
- ✅ Atomic file writes (crash-safe)
- ✅ Ready for centralized collection
