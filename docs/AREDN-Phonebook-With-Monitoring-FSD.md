# AREDN Phonebook — Functional Specification
## With Integrated Mesh Quality Monitoring

**Audience:** C developers and AREDN network operators
**Goal:** Enhance AREDN-Phonebook with lightweight mesh quality monitoring while maintaining its core emergency phonebook functionality
**Foundation:** Building upon AREDN-Phonebook v1.4.5 and mesh monitoring concepts v0.9

---

> **⚠️ SCOPE NOTE:** This document describes the **agent** component only. Backend services (remote collector, historical trending, web dashboards, alerting systems) are implemented in a separate project and are not part of this codebase. The agent exposes all data via local CGI endpoints for consumption by any backend system.

---

## Executive Summary

AREDN Phonebook Enhanced extends the existing emergency phonebook SIP server with integrated mesh network quality monitoring. This unified solution provides:

1. **Original Phonebook Features** - Emergency-resilient directory services for SIP phones
2. **Mesh Health Monitoring** - Real-time path quality metrics for network operators
3. **Unified Architecture** - Single daemon, shared resources, minimal overhead
4. **Emergency Focus** - Degradation modes ensure phonebook always works

The enhanced system maintains backward compatibility while adding optional monitoring that helps operators identify network issues before they affect emergency communications.

---

## 🎯 Implementation Status (as of 2025-09-30)

### ✅ Phase 0: Software Health Monitoring - **COMPLETE**
- [x] Health tracking (CPU, memory, threads, crashes)
- [x] Health scoring algorithm (0-100)
- [x] Crash detection and reporting
- [x] Memory leak detection
- [x] Thread responsiveness monitoring
- [x] Geographic location from sysinfo.json (lat/lon/grid_square)
- [x] Hardware model and firmware version tracking
- [x] Routing daemon identification (OLSR/Babel)
- [x] JSON export with meshmon.v1 schema
- [x] CGI endpoints: /cgi-bin/health, /cgi-bin/crash

### ✅ Phase 1: Network Monitoring - **COMPLETE**
- [x] Configuration parser for mesh_monitor section
- [x] Routing daemon auto-detection (OLSR/Babel)
- [x] OLSR jsoninfo HTTP client
- [x] Babel Unix domain socket client
- [x] Neighbor discovery from routing tables
- [x] UDP probe engine (sender and responder)
- [x] RFC3550 metrics calculation (RTT, jitter, packet loss)
- [x] Network JSON export with meshmon.v1 schema
- [x] CGI endpoint: /cgi-bin/network
- [x] Integrated into main.c and Makefile

### ✅ Phase 2: Hop-by-Hop Path Analysis - **COMPLETE**
- [x] OLSR topology parsing for path reconstruction
- [x] Babel route table parsing for multi-hop paths
- [x] Per-hop ETX, LQ, NLQ metrics from OLSR
- [x] Link type classification (RF, tunnel, ethernet, bridge)
- [x] Hop information included in network JSON
- [x] Works with both OLSR and Babel routing daemons

**All agent functionality is implemented and ready for production testing on AREDN networks.**

**Note:** Backend features (remote collector, historical trending, web dashboard, alerting) are handled by a separate backend project and are not part of this agent codebase.

---

## 1) Architecture Overview

### 1.1 Unified Process Model

```
AREDN-Phonebook-Enhanced (single process)
├── Core SIP Proxy Module (existing)
│   ├── REGISTER handler
│   ├── INVITE routing
│   └── User database
├── Phonebook Module (existing)
│   ├── CSV fetcher thread
│   ├── XML generator
│   └── Status updater thread
├── Passive Safety Module (existing)
│   └── Self-healing thread
└── Mesh Monitor Module (NEW)
    ├── Routing introspection
    ├── Path quality probes
    ├── Metrics calculator
    ├── Health reporter
    └── Software health tracker
```

### 1.2 Thread Architecture

- **Main Thread:** SIP message processing (unchanged)
- **Fetcher Thread:** Phonebook updates (unchanged)
- **Updater Thread:** User status management (unchanged)
- **Safety Thread:** Self-healing operations (unchanged)
- **Monitor Thread:** (NEW) Mesh quality measurements via dedicated uloop

### 1.3 Resource Sharing

- Shared logging system (`log_manager`)
- Shared configuration (`/etc/sipserver.conf` + new `mesh_monitor` section)
- Shared memory pools for efficiency
- Unified signal handling and lifecycle

---

## 2) Enhanced Feature Set

### 2.1 Core Phonebook Features (Preserved)
- ✅ Automatic phonebook fetching every 30 minutes
- ✅ Emergency boot with persistent storage
- ✅ Flash-friendly operation (minimal writes)
- ✅ SIP REGISTER/INVITE handling
- ✅ XML directory for Yealink phones
- ✅ Webhook endpoints (loadphonebook, showphonebook)
- ✅ Passive safety with self-healing

### 2.2 New Monitoring Features
- 🆕 **Mesh Path Quality:** Loss, RTT, jitter (RFC3550) measurements
- 🆕 **Routing Awareness:** OLSR/Babel link quality metrics
- 🆕 **Hop-by-Hop Analysis:** Per-link quality in multi-hop paths
- 🆕 **Network Health Dashboard:** JSON API for monitoring tools
- 🆕 **Degradation Detection:** Early warning for voice quality issues
- 🆕 **Historical Trending:** Rolling window statistics

### 2.3 Integration Benefits
- 📊 **Unified Health View:** Phonebook + network status in one place
- 🔄 **Correlated Metrics:** Link SIP registration failures to mesh issues
- 💾 **Shared Infrastructure:** Reuse existing HTTP/CGI endpoints
- 🛡️ **Emergency Priority:** Monitoring never impacts core phonebook

---

## 3) Module Specifications

### 3.1 Mesh Monitor Module Structure

```c
// New files in Phonebook/src/mesh_monitor/

mesh_monitor.h           // Public API
mesh_monitor.c           // Main coordinator
routing_adapter.c        // OLSR/Babel interface
probe_engine.c          // UDP probe sender/receiver
metrics_calculator.c    // RFC3550 jitter, loss, RTT
health_reporter.c       // JSON generation and batching
software_health.c       // Software health tracking
monitor_config.c        // Configuration parser
```

### 3.2 Integration Points

```c
// In main.c - Initialize monitoring after phonebook
if (g_mesh_monitor_enabled) {
    mesh_monitor_init(&config);
    pthread_create(&monitor_tid, NULL, mesh_monitor_thread, NULL);
}

// In passive_safety.c - Add monitoring health checks
void passive_monitor_recovery_check(void) {
    if (monitor_probe_stuck()) {
        restart_probe_engine();
    }
}

// In status_updater.c - Correlate SIP failures with mesh quality
if (registration_failed && mesh_path_degraded(peer)) {
    LOG_WARN("Registration failure likely due to mesh path issues");
}
```

---

## 4) Configuration Schema

### 4.1 Enhanced /etc/sipserver.conf

```ini
# Existing phonebook configuration
[phonebook]
fetch_interval_seconds = 1800
servers = pb1.local.mesh,pb2.local.mesh
flash_protection = 1

# New mesh monitoring section
[mesh_monitor]
enabled = 1
mode = lightweight        # lightweight | full | disabled

# Network status measurement
network_status_interval_s = 40  # How often to measure network status (UDP probes)
probe_window_s = 5              # Duration of each probe burst
neighbor_targets = 2            # Neighbors to probe per cycle
rotating_peer = 1               # Additional non-neighbor target
max_probe_kbps = 80             # Bandwidth limit per probe
probe_port = 40050              # UDP port for probes
dscp_ef = 1                     # Mark probes with DSCP EF (voice-like)

# Phonebook health status monitoring
phonebook_health_update_s = 60        # How often passive_safety updates /tmp/meshmon_health.json
phonebook_health_report_s = 14400     # Report to collector every 4 hours (or on significant change)
health_change_threshold_cpu = 20      # Report if CPU change > 20%
health_change_threshold_mem_mb = 10   # Report if memory change > 10 MB
crash_detection = 1                   # Monitor and report crashes immediately
thread_monitoring = 1                 # Track thread health and responsiveness
restart_threshold = 3                 # Max restarts before alerting

# Routing daemon integration
routing_daemon = auto           # auto | olsr | babel
routing_cache_s = 5             # Cache routing info for N seconds

# Remote reporting (optional)
network_status_report_s = 40    # Report network status every 40 seconds
collector_url =                 # Optional: external collector endpoint
```

### 4.2 Monitoring Modes

- **Disabled:** No monitoring overhead (default for low-memory nodes)
- **Lightweight:** Neighbor-only probes, minimal metrics
- **Full:** Complete path analysis with hop-by-hop metrics

---

## 5) JSON Wire Protocols

### 5.1 Enhanced Phonebook Status (Existing endpoint enhanced)

**Endpoint:** `GET /cgi-bin/showphonebook`

```json
{
  "phonebook": {
    "entries": 47,
    "last_updated": "2025-09-29T18:45:00Z",
    "source": "pb1.local.mesh"
  },
  "sip_status": {
    "registered_users": 12,
    "active_calls": 2,
    "uptime_seconds": 86400
  },
  "software_health": {
    "status": "healthy",
    "uptime_seconds": 86400,
    "restart_count": 0,
    "last_restart": null,
    "threads": {
      "main": {"responsive": true, "cpu_pct": 1.2},
      "fetcher": {"responsive": true, "last_heartbeat": "2025-09-29T18:45:00Z"},
      "monitor": {"responsive": true, "probe_success_rate": 98.5},
      "safety": {"responsive": true, "recoveries_performed": 2}
    },
    "memory": {
      "rss_mb": 8.2,
      "heap_mb": 6.1,
      "growth_rate_mb_per_hour": 0.1,
      "leak_suspected": false
    },
    "errors": {
      "crash_count_24h": 0,
      "last_crash": null,
      "sip_errors_per_hour": 1,
      "probe_failures_per_hour": 2
    }
  },
  "mesh_health": {
    "mode": "lightweight",
    "neighbors": 5,
    "avg_link_quality": 0.87,
    "problem_paths": [
      {"to": "node-K", "issue": "high_jitter", "jitter_ms": 45}
    ]
  }
}
```

### 5.2 Connection Check Query

**Endpoint:** `GET /cgi-bin/connectioncheck?target=W6XYZ-2` (NEW)

Queries existing network probe data for specific destination (does not trigger new probe):

**Response:**
```json
{
  "target": "W6XYZ-2",
  "last_probed": "2025-09-29T18:44:00Z",
  "status": "degraded",
  "path_result": {
    "rtt_ms_avg": 86.2,
    "jitter_ms": 15.3,
    "loss_pct": 2.1
  },
  "hop_result": {
    "hops": [
      {"seq": 0, "node": "node-A", "link_type": "RF", "etx": 1.19},
      {"seq": 1, "node": "node-D", "link_type": "tunnel", "etx": 1.0, "rtt_ms": 45.2},
      {"seq": 2, "node": "node-H", "link_type": "RF", "etx": 2.15, "warning": "High ETX"}
    ]
  },
  "recommendation": "Check node-H RF link (ETX=2.15)"
}
```

**If target not in recent probes:**
```json
{
  "target": "W6XYZ-2",
  "status": "no_data",
  "message": "No recent probe data available for this target"
}
```

---

## 6) Monitoring Access Architecture

### 6.1 Local-First Design Principle

**All monitoring data is accessible locally via CGI endpoints first.** Remote reporting to a centralized collector is optional and uses the same data.

### 6.2 Access Methods

#### 6.2.1 Local CGI Access (Primary, Always Available)

**Interface:** HTTP CGI scripts on the agent (`uhttpd` on OpenWrt)
**Endpoints:** See Section 5 for all endpoints
**Access:** `http://node.local.mesh/cgi-bin/...`
**Dependencies:** None - works standalone
**Use cases:**
- Local troubleshooting with `curl`
- Web dashboard on node itself
- Manual monitoring and diagnostics
- Emergency access when collector unavailable

**Benefits:**
- Zero network overhead
- Instant access to current state
- No external dependencies
- Works even if mesh partitioned

#### 6.2.2 Remote Reporting (Optional, Centralized Monitoring)

> **Note:** Remote reporting configuration is documented here for agent completeness, but the backend collector is implemented in a separate project.

**Interface:** HTTP POST to collector endpoint
**Endpoint:** `POST http://collector.local.mesh:5000/ingest` (example)
**Frequency:** Configurable (default: health every 5min, probes every 40s)
**Dependencies:** External backend collector (not part of this project)
**Use cases:**
- Mesh-wide visibility
- Historical trend analysis
- Automated issue detection
- Alerting and notifications

**Benefits:**
- Centralized dashboard (backend)
- Pattern detection across nodes (backend)
- Long-term data retention (backend)
- Automated alerting (backend)

### 6.3 Data Flow Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    AREDN Router Agent                    │
├─────────────────────────────────────────────────────────┤
│                                                           │
│  ┌─────────────┐  ┌──────────────┐  ┌─────────────┐    │
│  │ SIP Proxy   │  │ Health       │  │ Probe       │    │
│  │ (existing)  │  │ Monitor      │  │ Thread      │    │
│  │             │  │ (existing)   │  │ (optional)  │    │
│  └──────┬──────┘  └──────┬───────┘  └──────┬──────┘    │
│         │                │                  │            │
│         └────────────────┼──────────────────┘            │
│                          │                               │
│                    ┌─────▼──────┐                        │
│                    │ Agent      │                        │
│                    │ State      │                        │
│                    │ (RAM/tmp)  │                        │
│                    └─────┬──────┘                        │
│                          │                               │
│         ┌────────────────┼────────────────┐             │
│         │                │                │             │
│    ┌────▼────┐     ┌─────▼─────┐   ┌────▼────┐        │
│    │ CGI     │     │ State     │   │ Remote  │        │
│    │ Scripts │     │ File(s)   │   │ Reporter│        │
│    │         │     │ /tmp/*.json   │(optional)│        │
│    └────┬────┘     └───────────┘   └────┬────┘        │
│         │                                │             │
└─────────┼────────────────────────────────┼─────────────┘
          │ HTTP GET                       │ HTTP POST
          │ (local)                        │ (remote)
          ▼                                ▼
   ┌──────────────┐              ┌──────────────────┐
   │ Local Web    │              │ Pi Collector     │
   │ Browser/curl │              │ (AREDNmon)       │
   └──────────────┘              └──────────────────┘
```

### 6.4 Implementation Details

#### 6.4.1 State Storage

**Health state file:** `/tmp/meshmon_health.json` (updated every 60s by passive_safety_thread)

**Schema:** Unified format for both local CGI access and remote reporting (meshmon.v1 type: "agent_health")

**Note:** The "agent_health" message type represents the **Phonebook Health Status** - the health of the AREDN-Phonebook application running on this node.

```json
{
  "schema": "meshmon.v1",
  "type": "agent_health",
  "node": "W6ABC-1",
  "sent_at": "2025-09-29T18:45:00Z",
  "cpu_pct": 3.1,
  "mem_mb": 8.2,
  "queue_len": 0,
  "uptime_seconds": 86400,
  "restart_count": 0,
  "threads_responsive": true,
  "health_score": 95.5,
  "checks": {
    "memory_stable": true,
    "no_recent_crashes": true,
    "sip_service_ok": true,
    "phonebook_current": true
  },
  "sip_service": {
    "active_calls": 2,
    "registered_users": 15
  },
  "monitoring": {
    "probe_queue_depth": 0,
    "last_probe_sent": "2025-09-29T18:44:00Z"
  }
}
```

**Notes:**
- Uses `meshmon.v1` schema for consistency with remote reporting
- `queue_len` = monitoring probe queue depth (same as `monitoring.probe_queue_depth`)
- `threads_responsive` at top level (from architecture spec) + expanded `checks` object
- Can be sent as-is to collector or read by local CGI scripts

**Probe circular buffer:** In-memory (last 10-20 probes, ~10 KB)
**Crash history:** `/tmp/meshmon_crashes.json` (last 5 crashes, ~2 KB)

#### 6.4.2 CGI Implementation

**Option A: Shell Scripts (Lightweight, recommended for start)**
```bash
#!/bin/sh
# /www/cgi-bin/health

echo "Content-Type: application/json"
echo ""
cat /tmp/meshmon_health.json
```

**Option B: C Programs (High Performance)**
```c
// Compiled CGI binary
// Reads shared memory or queries agent directly
// More efficient for high-frequency polling
```

**Recommendation:** Start with shell scripts. Upgrade to C if CGI performance becomes bottleneck.

#### 6.4.3 Remote Reporter Thread (Optional)

When `remote_enabled=1` in config:

**Two separate reporting timers:**

```c
// Network status reporter - runs every 40 seconds
void *network_status_reporter_thread(void *arg) {
    while (1) {
        sleep(network_status_report_interval);

        // Read latest network probe results
        probe_result_t *probes = get_recent_probes();

        // Send path_result + hop_result messages
        for (int i = 0; i < probe_count; i++) {
            char *json = encode_path_result(&probes[i]);
            http_post(collector_url, json);
            free(json);

            json = encode_hop_result(&probes[i]);
            http_post(collector_url, json);
            free(json);
        }
    }
}

// Phonebook health reporter - runs every 4 hours OR on significant change
void *phonebook_health_reporter_thread(void *arg) {
    agent_health_t last_reported = {0};
    time_t last_report_time = 0;

    while (1) {
        sleep(60);  // Check every minute

        agent_health_t current = read_health_state();
        time_t now = time(NULL);
        bool should_report = false;

        // Scheduled report (every 4 hours)
        if (now - last_report_time >= phonebook_health_report_interval) {
            should_report = true;
        }

        // Event-driven: CPU change > threshold
        if (abs(current.cpu_pct - last_reported.cpu_pct) > cpu_change_threshold) {
            should_report = true;
        }

        // Event-driven: Memory change > threshold
        if (abs(current.mem_mb - last_reported.mem_mb) > mem_change_threshold) {
            should_report = true;
        }

        // Event-driven: Thread became unresponsive
        if (current.checks.threads_responsive != last_reported.checks.threads_responsive) {
            should_report = true;
        }

        // Event-driven: Restart occurred
        if (current.restart_count > last_reported.restart_count) {
            should_report = true;
        }

        if (should_report) {
            char *json = encode_agent_health(&current);
            http_post(collector_url, json);
            free(json);

            last_reported = current;
            last_report_time = now;
        }
    }
}
```

**Key principles:**
- CGI and remote reporters read from same source (no duplication)
- Network status: High frequency (40s) - network conditions change rapidly
- Phonebook health: Low frequency (4h) + event-driven - health changes slowly
- Reduces bandwidth: ~97% reduction in health reports while maintaining responsiveness

### 6.5 Configuration

```
# /etc/config/meshmon

config monitoring 'agent'
    option enabled '1'
    option node_id 'W6ABC-1'

    # Local CGI (always enabled if monitoring enabled)
    option cgi_enabled '1'

    # Remote reporting (optional)
    option remote_enabled '1'
    option collector_url 'http://collector.local.mesh:5000/ingest'
    option network_status_report_s '40'            # Network status every 40s
    option phonebook_health_report_s '14400'       # Phonebook health every 4h (or on change)
    option health_change_threshold_cpu '20'        # Report if CPU change > 20%
    option health_change_threshold_mem_mb '10'     # Report if memory change > 10 MB

    # Network probing (optional)
    option probing_enabled '1'
    option probe_neighbor_count '2'
    option probe_rotate_peers '1'
    option reduce_on_voip_calls '1'
```

### 6.6 Deployment Modes

**Mode 1: Local CGI Only (Minimal)**
- `cgi_enabled=1`, `remote_enabled=0`, `probing_enabled=0`
- Zero network overhead
- Manual monitoring only
- Best for: Single-node debugging

**Mode 2: Local + Remote Health (Hybrid Light)**
- `cgi_enabled=1`, `remote_enabled=1`, `probing_enabled=0`
- Reports agent health to collector
- No active probing
- Best for: Basic centralized monitoring

**Mode 3: Full Monitoring (Hybrid Complete)**
- `cgi_enabled=1`, `remote_enabled=1`, `probing_enabled=1`
- Local CGI + remote reporting + active probing
- Complete visibility local and centralized
- Best for: Production deployments

### 6.7 Memory Footprint

**Monitoring overhead:**
- Health state files: ~1-2 KB
- Probe circular buffer: ~10 KB (if probing enabled)
- Crash history: ~2 KB
- Remote reporter thread: ~100 KB stack
- **Total: ~13 KB data + 100 KB code**

**No historical data storage** - agent remains stateless for long-term trends.

---

## 7) Network Behavior & Resource Budgets

### 7.1 Enhanced Resource Targets

| Component | Original | With Monitoring | Notes |
|-----------|----------|-----------------|-------|
| Binary Size | ~800 KB | ~1.1 MB | +300 KB for monitoring |
| RAM (idle) | 4-6 MB | 8-10 MB | +4 MB for probe buffers |
| RAM (peak) | 8-10 MB | 14-16 MB | During probe windows |
| CPU (average) | <2% | <5% | MIPS single-core |
| Flash Writes | 1-2/day | 2-3/day | Quality history cache |
| Network BW | ~1 KB/s | ~10 KB/s | During probe windows |

### 6.2 Degradation Strategy

```
IF (memory < 64MB) THEN
    Disable monitoring entirely
ELSE IF (memory < 128MB) THEN
    Use lightweight mode (neighbors only)
ELSE
    Full monitoring available
```

---

## 7) Routing Daemon Integration

### 7.1 OLSR Integration (AREDN primary)

```c
// Query jsoninfo plugin
GET http://127.0.0.1:9090/routes
GET http://127.0.0.1:9090/neighbors

// Parse for phonebook correlation
typedef struct {
    char callsign[32];      // From phonebook
    char node_name[64];     // From OLSR
    float link_quality;      // LQ metric
    float neighbor_lq;       // NLQ metric
    float etx;              // Expected Transmission Count
    int is_registered;       // SIP registration status
} unified_peer_info_t;
```

### 7.2 Babel Support (Future AREDN)

- Control socket at `/var/run/babeld.sock`
- Text protocol parsing for routes and neighbors
- Automatic detection and fallback

### 7.3 Link Technology Detection (RF vs Tunnel)

**Goal:** Identify whether each hop uses wireless/RF or wired/tunnel technology to help isolate bottlenecks.

**Implementation Strategy:**

**Option 1: Query Routing Daemon Interface Info**
```c
// OLSR jsoninfo interfaces endpoint
GET http://127.0.0.1:9090/interfaces

// Response example:
{
  "interfaces": [
    {
      "name": "wlan0",
      "ipv4Address": "10.0.0.1",
      "olsr4": "UP"
    },
    {
      "name": "tun50",
      "ipv4Address": "172.16.50.1",
      "olsr4": "UP"
    }
  ]
}

// Map neighbor IPs to interface names
typedef struct {
    char neighbor_ip[16];
    char interface[16];      // "wlan0", "tun50", etc.
    char link_type[16];      // "RF", "tunnel", "ethernet"
} neighbor_interface_t;
```

**Option 2: Interface Name Pattern Matching**
```c
// Simple heuristic classification
const char* classify_link_type(const char* iface) {
    if (strncmp(iface, "wlan", 4) == 0) return "RF";
    if (strncmp(iface, "tun", 3) == 0) return "tunnel";
    if (strncmp(iface, "eth", 3) == 0) return "ethernet";
    if (strncmp(iface, "br-", 3) == 0) return "bridge";
    return "unknown";
}
```

**Option 3: Query AREDN Node Info (Most Reliable)**
```bash
# AREDN nodes expose /cgi-bin/sysinfo.json
curl http://neighbor-node.local.mesh/cgi-bin/sysinfo.json

# Contains interface details and tunnel status
{
  "interfaces": {
    "wlan0": {"type": "RF", "channel": 5, "bandwidth": 20},
    "tun50": {"type": "tunnel", "peer": "node-remote"}
  }
}
```

**Implementation Priority:**
1. Start with Option 2 (interface name matching) - simplest, no external dependencies
2. Add Option 1 (OLSR interface query) when routing adapter is implemented
3. Consider Option 3 (AREDN sysinfo) for maximum accuracy in Phase 3

**Enhanced hop_result schema with link type:**
```json
{
  "schema": "meshmon.v1",
  "type": "hop_result",
  "src": "node-A",
  "dst": "node-K",
  "sent_at": "2025-09-29T18:41:05Z",
  "hops": [
    {
      "seq": 0,
      "node": "node-A",
      "interface": "wlan0",
      "link_type": "RF",
      "to_next": {
        "ip": "10.0.0.4",
        "lq": 0.92,
        "nlq": 0.89,
        "etx": 1.19,
        "rtt_ms": 12.3
      }
    },
    {
      "seq": 1,
      "node": "node-D",
      "interface": "tun50",
      "link_type": "tunnel",
      "to_next": {
        "ip": "172.16.50.8",
        "lq": 1.0,
        "nlq": 1.0,
        "etx": 1.0,
        "rtt_ms": 45.2
      }
    },
    {
      "seq": 2,
      "node": "node-H",
      "interface": "wlan0",
      "link_type": "RF",
      "to_next": {
        "ip": "10.0.0.11",
        "lq": 0.67,
        "nlq": 0.72,
        "etx": 2.15,
        "rtt_ms": 28.7
      }
    }
  ]
}
```

**Bottleneck Analysis Benefits:**
- RF links with high ETX (>2.0) → Check antenna alignment, interference
- Tunnel links with high RTT → Check Internet backhaul quality
- Mixed path quality → Identify which technology is causing issues

### 7.4 Hop-by-Hop Bottleneck Identification

**Goal:** Isolate which specific hop in a multi-hop path is causing performance degradation.

**Data Collection:**

Each probe window collects:
1. **End-to-end metrics** → `path_result` message (RTT, jitter, loss for full path)
2. **Per-hop metrics** → `hop_result` message (RTT, LQ/NLQ/ETX for each hop)

**Analysis Approach:**

```c
// Agent calculates per-hop contribution to total RTT
typedef struct {
    int hop_index;
    char node[32];
    float rtt_ms;              // RTT to this hop
    float rtt_contribution_pct; // % of total path RTT
    float lq, nlq, etx;
    char link_type[16];
    bool is_bottleneck;        // Flagged if contribution > 40%
} hop_analysis_t;

// Example calculation:
// Path A→D→H→K: Total RTT = 86.2 ms
// Hop A→D: 12.3 ms (14% contribution)
// Hop D→H: 45.2 ms (52% contribution) ← BOTTLENECK
// Hop H→K: 28.7 ms (33% contribution)
```

**Bottleneck Detection Rules:**

1. **High RTT Contribution** - Hop contributes >40% of total path RTT
2. **High ETX** - ETX > 2.0 indicates poor link quality
3. **High Loss on Hop** - Requires ICMP probe to each hop (optional, expensive)
4. **Asymmetric Quality** - Large difference between LQ and NLQ

**Agent Data Collection:**

Agent collects raw per-hop metrics and stores in memory (circular buffer, last 10-20 probe windows):

```c
typedef struct {
    char dst_node[32];
    time_t timestamp;
    float end_to_end_rtt_ms;
    float end_to_end_jitter_ms;
    float end_to_end_loss_pct;
    int hop_count;
    struct {
        char node[32];
        char interface[16];
        char link_type[16];
        float rtt_ms;
        float lq, nlq, etx;
    } hops[MAX_HOPS];
} probe_result_t;
```

**Local CGI Access:**

`GET /cgi-bin/network` returns raw network performance data (same format sent to collector):

```json
{
  "schema": "meshmon.v1",
  "node": "node-A",
  "timestamp": "2025-09-29T18:45:00Z",
  "recent_probes": [
    {
      "dst": "node-K",
      "probed_at": "2025-09-29T18:44:00Z",
      "path_result": {
        "rtt_ms_avg": 86.2,
        "jitter_ms": 15.3,
        "loss_pct": 2.1
      },
      "hop_result": {
        "hops": [
          {
            "seq": 0,
            "node": "node-A",
            "interface": "wlan0",
            "link_type": "RF",
            "to_next": {"ip": "10.0.0.4", "lq": 0.92, "nlq": 0.89, "etx": 1.19, "rtt_ms": 12.3}
          },
          {
            "seq": 1,
            "node": "node-D",
            "interface": "tun50",
            "link_type": "tunnel",
            "to_next": {"ip": "172.16.50.8", "lq": 1.0, "nlq": 1.0, "etx": 1.0, "rtt_ms": 45.2}
          },
          {
            "seq": 2,
            "node": "node-H",
            "interface": "wlan0",
            "link_type": "RF",
            "to_next": {"ip": "10.0.0.11", "lq": 0.67, "nlq": 0.72, "etx": 2.15, "rtt_ms": 28.7}
          }
        ]
      }
    }
  ]
}
```

**Remote Reporting:**

Same data sent to collector as `path_result` and `hop_result` messages (meshmon.v1 schema).

**Analysis Done by Backend:**

Backend collector analyzes raw data to:
- Calculate RTT contribution percentages
- Flag bottleneck hops (>40% contribution, ETX >2.0)
- Aggregate patterns across multiple nodes
- Generate summaries and actionable alerts
- Provide user-facing interpretations with visual indicators

---

## 9) Implementation Phases

### Phase 1: Foundation
- [ ] Create mesh_monitor module structure
- [ ] Implement routing adapter (OLSR first)
- [ ] Basic probe engine (ping-like)
- [ ] Simple loss/RTT metrics

### Phase 2: Integration
- [ ] Wire into existing threads
- [ ] Shared configuration parsing
- [ ] Enhanced showphonebook endpoint
- [ ] Memory pool sharing

### Agent Status: Production Ready
- [x] RFC3550 jitter calculation
- [x] Hop-by-hop analysis
- [x] All agent features implemented

**Note:** Backend features like historical trending, quality correlation analysis, and web dashboards are handled by a separate backend project.

---

## 10) Backward Compatibility

### 10.1 Zero-Impact Default
- Monitoring disabled by default
- No configuration changes required
- Existing webhooks unchanged
- Same binary works on old/new nodes

### 10.2 Graceful Enhancement
- Old nodes ignore monitoring fields in JSON
- New features behind feature flags
- Automatic capability detection
- Progressive enhancement model

---

## 11) Emergency Operation Modes

### 11.1 Priority Hierarchy
1. **SIP Proxy** - Always highest priority
2. **Phonebook Fetch** - Required for directory
3. **Passive Safety** - Ensures reliability
4. **Mesh Monitor** - Lowest priority, first to disable

### 11.2 Resource Starvation Response
```
IF (CPU > 80%) THEN
    Pause monitoring for 5 minutes
IF (Memory < 10MB free) THEN
    Disable monitoring until reboot
IF (Flash writes > 100/hour) THEN
    Disable quality history
```

---

## 12) Testing Strategy

### 12.1 Unit Tests (Local CGI Scripts)

All monitoring functionality is testable locally via CGI endpoints without requiring a backend collector:

```bash
# Test health monitoring
curl http://node.local.mesh/cgi-bin/health

# Test network performance
curl http://node.local.mesh/cgi-bin/network

# Test crash reporting
curl http://node.local.mesh/cgi-bin/crash

# Test phonebook integration
curl http://node.local.mesh/cgi-bin/showphonebook
```

**Validation:**
- RFC3550 jitter calculator with test vectors
- JSON output schema validation
- Routing parser with OLSR/Babel fixtures
- Memory pool management
- Thread responsiveness checks
- Crash signal handler behavior

### 12.2 Remote Reporting Tests (No Backend Required)

Test remote reporting functionality without implementing the full collector backend:

**Option 1: Simple HTTP Echo Server**
```bash
# On test machine, run simple receiver
nc -l -p 5000 | tee received_messages.json
```

**Option 2: Minimal Python Test Collector**
```python
#!/usr/bin/env python3
# test_collector.py - Validates agent messages without storage
from http.server import HTTPServer, BaseHTTPRequestHandler
import json

class TestCollector(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers['Content-Length'])
        body = self.rfile.read(length)

        # Validate JSON schema
        try:
            data = json.loads(body)
            assert data.get('schema') == 'meshmon.v1'
            print(f"✓ Valid {data.get('type')} from {data.get('node', data.get('src'))}")
            self.send_response(200)
        except Exception as e:
            print(f"✗ Invalid message: {e}")
            self.send_response(400)

        self.end_headers()

HTTPServer(('0.0.0.0', 5000), TestCollector).serve_forever()
```

**Option 3: Log File Analysis**
```bash
# Configure agent to log POST bodies before sending
tail -f /tmp/meshmon_outgoing.log | jq .

# Validate message schemas
grep '"schema":"meshmon.v1"' /tmp/meshmon_outgoing.log | jq '.type' | sort | uniq -c
```

**Test scenarios:**
- Agent starts with `remote_enabled=1`, collector URL configured
- Verify agent_health messages sent at configured interval
- Verify path_result messages sent when probing enabled
- Verify crash_report messages after simulated crash
- Test exponential backoff when collector unreachable
- Verify message queue behavior during network partition

### 12.3 Field Validation
- Deploy to 3+ node test network
- Verify no impact on voice calls during active monitoring
- Measure actual CPU/memory resource usage
- Stress test with deliberately degraded links
- Validate local CGI access during mesh partition
- Test behavior when collector temporarily offline

---

## Appendix A: File Structure

```
Phonebook/
├── src/
│   ├── main.c                    (modified)
│   ├── common.h                   (modified)
│   ├── passive_safety/           (enhanced)
│   │   └── passive_safety.c      (monitoring checks added)
│   └── mesh_monitor/             (NEW)
│       ├── mesh_monitor.h
│       ├── mesh_monitor.c
│       ├── routing_adapter.c
│       ├── probe_engine.c
│       ├── metrics_calculator.c
│       ├── health_reporter.c
│       ├── software_health.c    (NEW)
│       └── monitor_config.c
├── files/
│   ├── etc/
│   │   ├── sipserver.conf        (enhanced)
│   │   └── config/
│   │       └── meshmon           (NEW - UCI config)
│   └── www/
│       └── cgi-bin/
│           ├── loadphonebook     (existing - trigger fetch)
│           ├── showphonebook     (existing - show entries)
│           ├── health            (NEW - phonebook health status)
│           ├── network           (NEW - network performance)
│           ├── crash             (NEW - crash reports)
│           └── connectioncheck   (NEW - on-demand probe trigger)
└── Makefile                       (updated dependencies)
```

---

## Appendix B: Memory Layout

```
[AREDN-Phonebook Memory Map with Monitoring]

0-4 MB:   Core SIP proxy + user database
4-6 MB:   Phonebook cache + XML buffer
6-8 MB:   Thread stacks + passive safety
8-10 MB:  [NEW] Probe engine buffers
10-12 MB: [NEW] Routing cache
12-14 MB: [NEW] Metrics history (optional)
14-16 MB: Peak temporary allocations
```

---

## Appendix B: Interface Specifications for Implementers

This appendix provides complete technical specifications for the two primary interfaces in the mesh monitoring system. Use these specifications to implement:
- **Lightweight monitoring agents** (alternative implementations)
- **Centralized collectors** (backend aggregation systems)
- **Integration tools** (dashboards, alerting systems)

### B.1 Agent-to-Agent Interface (UDP Probe Protocol)

#### B.1.1 Overview

**Purpose:** Network quality measurement between mesh nodes (RTT, jitter, packet loss)
**Transport:** UDP
**Port:** 40050 (configurable via `probe_port` in config)
**Protocol:** Echo request/response (similar to ICMP ping but over UDP)
**Bi-directional:** All agents act as both probe sender and responder

#### B.1.2 Probe Packet Format

```c
// Wire format (network byte order - big endian)
typedef struct {
    uint32_t sequence;           // Probe sequence number (0, 1, 2, ...)
    uint32_t timestamp_sec;      // Unix timestamp seconds (when probe sent)
    uint32_t timestamp_usec;     // Microseconds component (0-999999)
    char src_node[64];          // Source node identifier (null-terminated)
} probe_packet_t;

// Total packet size: 76 bytes
// - 4 bytes: sequence
// - 4 bytes: timestamp_sec
// - 4 bytes: timestamp_usec
// - 64 bytes: src_node (null-terminated string)
```

**Byte Order:** All integer fields use **network byte order (big-endian)**. Use `htonl()` to convert to network byte order before sending, `ntohl()` to convert from network byte order when receiving.

#### B.1.3 Protocol Flow

```
Node A (Probe Sender)                    Node B (Probe Responder)
================                          ===================

1. Get neighbor list from routing daemon
   (OLSR jsoninfo or Babel socket)

2. For each neighbor to probe:

   Create probe packet:
   - sequence = probe_number (0-9)
   - timestamp = gettimeofday()
   - src_node = hostname

   Convert to network byte order ────────>  3. Listen on UDP port 40050

   Send to neighbor:40050 ──────────────>  4. Receive probe packet

   Record send_time                         Convert from network byte order

                                           5. Echo packet back immediately
                                              (no modification needed)

   6. Receive echoed packet  <──────────   Send same packet back to source

   Record receive_time

   Calculate metrics:
   - RTT = receive_time - send_time
   - Store for jitter calculation

7. After probe window (5s default):

   Calculate final metrics:
   - RTT_avg = average of all RTTs
   - Jitter = average absolute difference between consecutive RTTs (RFC3550)
   - Loss% = (probes_sent - probes_received) / probes_sent * 100

8. Store results in probe_history[]
   Export to /tmp/meshmon_network.json
```

#### B.1.4 Probe Timing

**Default Configuration:**
```ini
network_status_interval_s = 40    # Probe cycle every 40 seconds
probe_window_s = 5                # Wait 5 seconds for responses
neighbor_targets = 2              # Probe 2 neighbors per cycle
```

**Typical Probe Sequence:**
```
T+0s:    Send 10 probes to Neighbor 1 (100ms apart = 1 second total)
T+5s:    Calculate metrics for Neighbor 1
T+6s:    Send 10 probes to Neighbor 2
T+11s:   Calculate metrics for Neighbor 2
T+12s:   Export network.json
T+40s:   Start next probe cycle
```

#### B.1.5 Metrics Calculation (RFC3550)

**RTT (Round-Trip Time):**
```c
// For each received probe response:
long sec_diff = recv_time.tv_sec - sent_time.tv_sec;
long usec_diff = recv_time.tv_usec - sent_time.tv_usec;
float rtt_ms = (sec_diff * 1000.0) + (usec_diff / 1000.0);

// Average RTT:
rtt_ms_avg = sum(all_rtts) / count(received_probes);
```

**Jitter (Inter-arrival Jitter per RFC3550 Section 6.4.1):**
```c
// Simplified implementation - mean absolute difference:
float jitter = 0.0;
for (int i = 1; i < rtt_count; i++) {
    float diff = rtt[i] - rtt[i-1];
    jitter += (diff < 0 ? -diff : diff);  // Absolute value
}
jitter_ms = jitter / (rtt_count - 1);
```

**Packet Loss:**
```c
float loss_pct = 100.0 * (1.0 - (received_count / sent_count));
```

#### B.1.6 Implementation Example (Lightweight Agent)

**Minimal UDP Probe Responder (Python):**
```python
#!/usr/bin/env python3
import socket

# Simple UDP echo responder for mesh monitoring
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('0.0.0.0', 40050))

print("Mesh probe responder listening on UDP port 40050...")

while True:
    data, addr = sock.recvfrom(1024)
    # Echo packet back unchanged
    sock.sendto(data, addr)
```

**Minimal Probe Sender (Python):**
```python
#!/usr/bin/env python3
import socket
import struct
import time

def send_probe(target_ip, target_port=40050):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(1.0)

    # Create probe packet
    sequence = 0
    now = time.time()
    timestamp_sec = int(now)
    timestamp_usec = int((now - timestamp_sec) * 1000000)
    src_node = socket.gethostname()[:63]  # Max 63 chars + null

    # Pack to network byte order
    packet = struct.pack('!III64s',
                        sequence,
                        timestamp_sec,
                        timestamp_usec,
                        src_node.encode('utf-8'))

    # Send probe
    send_time = time.time()
    sock.sendto(packet, (target_ip, target_port))

    # Wait for echo
    try:
        data, addr = sock.recvfrom(1024)
        recv_time = time.time()
        rtt_ms = (recv_time - send_time) * 1000
        print(f"RTT to {target_ip}: {rtt_ms:.2f}ms")
        return rtt_ms
    except socket.timeout:
        print(f"Probe to {target_ip} timed out")
        return None

# Example usage
send_probe("10.124.142.47")
```

---

### B.2 Agent-to-Collector Interface (HTTP JSON API)

> **⚠️ BACKEND IMPLEMENTATION:** This section documents the interface for backend implementers. The collector server, database, dashboards, and alerting are implemented in a separate backend project, not in this agent codebase.

#### B.2.1 Overview

**Purpose:** Centralized collection of health and network data from multiple agents
**Transport:** HTTP POST
**Port:** Configurable (default 5000 suggested)
**Endpoint:** `POST /ingest`
**Content-Type:** `application/json`
**Schema:** meshmon.v1 (see B.2.3)
**Authentication:** None (trusted mesh network) or implement custom auth in backend

#### B.2.2 Configuration

```ini
[mesh_monitor]
# Enable remote reporting
network_status_report_s = 40        # Report network status every 40s
collector_url = http://collector.local.mesh:5000/ingest

# (Future implementation - not in current code)
```

#### B.2.3 Message Schemas (meshmon.v1)

**Schema Version:** All messages include `"schema": "meshmon.v1"`

##### **Message Type 1: agent_health**

Sent periodically (every 60s default) or on significant changes:

```json
{
  "schema": "meshmon.v1",
  "type": "agent_health",
  "node": "HB9BLA-HAP-2",
  "sent_at": "2025-09-30T21:04:35Z",
  "routing_daemon": "olsr",
  "lat": "47.47497",
  "lon": "7.76720",
  "grid_square": "JN37vl",
  "hardware_model": "MikroTik RouterBOARD 952Ui-5ac2nD (hAP ac lite)",
  "firmware_version": "3.25.8.0",
  "cpu_pct": 2.5,
  "mem_mb": 12.3,
  "queue_len": 0,
  "uptime_seconds": 18245,
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
    "registered_users": 3
  },
  "monitoring": {
    "probe_queue_depth": 0,
    "last_probe_sent": "2025-09-30T21:04:30Z"
  }
}
```

##### **Message Type 2: network_status**

Sent after each probe cycle (every 40s default):

```json
{
  "schema": "meshmon.v1",
  "type": "network_status",
  "node": "HB9BLA-HAP-2",
  "sent_at": "2025-09-30T21:05:00Z",
  "routing_daemon": "olsr",
  "probe_count": 2,
  "probes": [
    {
      "dst_node": "HB9EDI-ROUTER",
      "dst_ip": "10.124.142.47",
      "timestamp": "2025-09-30T21:04:55Z",
      "routing_daemon": "olsr",
      "rtt_ms_avg": 12.34,
      "jitter_ms": 1.23,
      "loss_pct": 0.0,
      "hop_count": 2,
      "path": [
        {
          "node": "HB9ABC-RELAY",
          "interface": "wlan0",
          "link_type": "RF",
          "lq": 1.0,
          "nlq": 0.98,
          "etx": 1.02,
          "rtt_ms": 0.0
        },
        {
          "node": "HB9EDI-ROUTER",
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

##### **Message Type 3: crash_report**

Sent immediately when a crash is detected:

```json
{
  "schema": "meshmon.v1",
  "type": "crash_report",
  "node": "HB9BLA-HAP-2",
  "sent_at": "2025-09-30T21:10:00Z",
  "crash_time": "2025-09-30T21:09:58Z",
  "signal": 11,
  "signal_name": "SIGSEGV",
  "reason": "Segmentation fault at 0x00000000",
  "restart_count": 1,
  "uptime_before_crash": 18300
}
```

#### B.2.4 Collector Implementation Example

**Minimal Collector (Python Flask):**

```python
#!/usr/bin/env python3
from flask import Flask, request, jsonify
from datetime import datetime
import json

app = Flask(__name__)

@app.route('/ingest', methods=['POST'])
def ingest():
    data = request.get_json()

    # Validate schema
    if data.get('schema') != 'meshmon.v1':
        return jsonify({'error': 'Invalid schema'}), 400

    # Route by message type
    msg_type = data.get('type')
    node = data.get('node', 'unknown')

    print(f"[{datetime.now()}] Received {msg_type} from {node}")

    if msg_type == 'agent_health':
        handle_health(data)
    elif msg_type == 'network_status':
        handle_network(data)
    elif msg_type == 'crash_report':
        handle_crash(data)
    else:
        return jsonify({'error': 'Unknown message type'}), 400

    return jsonify({'status': 'accepted'}), 200

def handle_health(data):
    # Store in database, check health score, etc.
    print(f"  Health Score: {data.get('health_score')}")
    print(f"  Location: {data.get('lat')}, {data.get('lon')}")
    # TODO: Store in InfluxDB, Prometheus, etc.

def handle_network(data):
    # Process network probes
    for probe in data.get('probes', []):
        print(f"  Probe to {probe['dst_ip']}: RTT={probe['rtt_ms_avg']}ms, Loss={probe['loss_pct']}%")
    # TODO: Detect degraded paths, calculate network-wide metrics

def handle_crash(data):
    # Alert on crashes
    print(f"  CRASH: {data.get('signal_name')} - {data.get('reason')}")
    # TODO: Send alert, track crash patterns

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
```

**Run the collector:**
```bash
pip3 install flask
python3 collector.py
```

**Configure agents to report:**
```ini
# /etc/sipserver.conf
[mesh_monitor]
network_status_report_s = 40
collector_url = http://collector.local.mesh:5000/ingest
```

#### B.2.5 Collector Best Practices

**1. Message Deduplication:**
```python
# Track last message timestamp per node
last_seen = {}

def is_duplicate(node, sent_at):
    key = f"{node}:{sent_at}"
    if key in last_seen:
        return True
    last_seen[key] = datetime.now()
    return False
```

**2. Time-Series Storage:**
- **InfluxDB:** For metrics (RTT, jitter, health_score)
- **PostgreSQL/TimescaleDB:** For structured data and queries
- **Elasticsearch:** For log-style search and aggregation

**3. Alerting Rules:**
```python
def check_alerts(health_data):
    if health_data['health_score'] < 80:
        alert(f"Low health on {health_data['node']}")

    if health_data['checks']['memory_stable'] == False:
        alert(f"Memory leak detected on {health_data['node']}")

    for probe in network_data['probes']:
        if probe['loss_pct'] > 50:
            alert(f"High packet loss to {probe['dst_ip']}")
```

**4. Geographic Visualization:**
```python
# Generate GeoJSON for mapping
def to_geojson(health_messages):
    features = []
    for msg in health_messages:
        if msg['lat'] != 'unknown':
            features.append({
                'type': 'Feature',
                'geometry': {
                    'type': 'Point',
                    'coordinates': [float(msg['lon']), float(msg['lat'])]
                },
                'properties': {
                    'node': msg['node'],
                    'health_score': msg['health_score']
                }
            })
    return {'type': 'FeatureCollection', 'features': features}
```

#### B.2.6 Local CGI Access (Read-Only Query Interface)

Agents also expose data via HTTP GET for local access (always available, no configuration needed):

```bash
# Get agent health
curl http://node.local.mesh/cgi-bin/health

# Get network status
curl http://node.local.mesh/cgi-bin/network

# Get crash history
curl http://node.local.mesh/cgi-bin/crash
```

**Response Format:** Same JSON as sent to collector (meshmon.v1 schema)

**Use Cases:**
- Local troubleshooting without backend infrastructure
- Emergency access when collector is down
- Integration with other mesh services

---

### B.3 Implementation Checklist

#### B.3.1 For Lightweight Agent Implementers

**Minimum Requirements:**
- [ ] UDP listener on port 40050 that echoes packets back
- [ ] No parsing needed - just echo the exact bytes received
- [ ] Optional: Implement probe sender for testing
- [ ] Optional: Export metrics in meshmon.v1 JSON format

**Reference Implementation:** `Phonebook/src/mesh_monitor/probe_engine.c`

#### B.3.2 For Collector Implementers

**Minimum Requirements:**
- [ ] HTTP server accepting POST on `/ingest`
- [ ] Parse JSON with `schema: "meshmon.v1"`
- [ ] Handle three message types: `agent_health`, `network_status`, `crash_report`
- [ ] Store or forward data as needed

**Optional Enhancements:**
- [ ] Time-series database integration (InfluxDB, Prometheus)
- [ ] Geographic visualization (map with health scores)
- [ ] Alerting on health degradation or crashes
- [ ] Network topology graph generation
- [ ] Historical trending and analysis

**Reference:** Section 6 and AREDNmon-Architecture.md

---

### B.4 Testing Tools

**Test UDP Probe Protocol:**
```bash
# On responder node:
nc -ul 40050 | nc -u localhost 40050  # Echo server

# On sender node:
echo "test" | nc -u target-node.local.mesh 40050
```

**Test Collector API:**
```bash
# Send test health message
curl -X POST http://collector:5000/ingest \
  -H "Content-Type: application/json" \
  -d '{
    "schema": "meshmon.v1",
    "type": "agent_health",
    "node": "TEST-NODE",
    "sent_at": "2025-09-30T12:00:00Z",
    "health_score": 95.0
  }'
```

**Validate JSON Schema:**
```bash
# Install jq for JSON validation
cat /tmp/meshmon_health.json | jq .
cat /tmp/meshmon_network.json | jq .
```

---

**End of Appendix B**

This appendix provides complete specifications for implementing compatible agents and collectors. All implementations following these interfaces will be interoperable with the AREDN-Phonebook monitoring ecosystem.

---

**End of Specification v1.0**

*This enhanced specification maintains the emergency communication focus of AREDN-Phonebook while adding valuable network observability features. The modular design ensures monitoring never compromises core phonebook functionality.*