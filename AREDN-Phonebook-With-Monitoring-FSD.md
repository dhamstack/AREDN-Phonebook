# AREDN Phonebook Enhanced — Functional Specification v1.0
## With Integrated Mesh Quality Monitoring

**Audience:** C developers and AREDN network operators
**Goal:** Enhance AREDN-Phonebook with lightweight mesh quality monitoring while maintaining its core emergency phonebook functionality
**Foundation:** Building upon AREDN-Phonebook v1.4.5 and mesh monitoring concepts v0.9

---

## Executive Summary

AREDN Phonebook Enhanced extends the existing emergency phonebook SIP server with integrated mesh network quality monitoring. This unified solution provides:

1. **Original Phonebook Features** - Emergency-resilient directory services for SIP phones
2. **Mesh Health Monitoring** - Real-time path quality metrics for network operators
3. **Unified Architecture** - Single daemon, shared resources, minimal overhead
4. **Emergency Focus** - Degradation modes ensure phonebook always works

The enhanced system maintains backward compatibility while adding optional monitoring that helps operators identify network issues before they affect emergency communications.

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
probe_interval_s = 40     # Seconds between probe cycles
probe_window_s = 5        # Duration of each probe burst
neighbor_targets = 2      # Neighbors to probe per cycle
rotating_peer = 1         # Additional non-neighbor target
max_probe_kbps = 80      # Bandwidth limit per probe
routing_daemon = auto     # auto | olsr | babel
routing_cache_s = 5       # Cache routing info for N seconds
health_report_interval = 300  # Health report interval
probe_port = 40050        # UDP port for probes
dscp_ef = 1              # Mark probes with DSCP EF (voice-like)
collector_url =          # Optional: external collector endpoint

# Software health monitoring
software_health_enabled = 1     # Enable software health tracking
crash_detection = 1             # Monitor and report crashes
thread_monitoring = 1           # Track thread health and responsiveness
memory_leak_detection = 1       # Detect memory growth patterns
health_check_interval = 60      # Software health check interval (seconds)
restart_threshold = 3           # Max restarts before alerting
uptime_reporting = 1            # Include uptime in health reports
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

### 5.2 New Mesh Quality Endpoint

**Endpoint:** `GET /cgi-bin/meshquality`

```json
{
  "node_id": "W6ABC-1",
  "timestamp": "2025-09-29T18:45:00Z",
  "paths": [
    {
      "dst": "W6XYZ-2",
      "state": "good",
      "metrics": {
        "rtt_ms": 12.3,
        "jitter_ms": 2.1,
        "loss_pct": 0.0
      },
      "route": ["W6ABC-1", "W6DEF-3", "W6XYZ-2"],
      "hops": [
        {"from": "W6ABC-1", "to": "W6DEF-3", "lq": 0.95, "nlq": 0.92},
        {"from": "W6DEF-3", "to": "W6XYZ-2", "lq": 0.88, "nlq": 0.90}
      ]
    }
  ],
  "summary": {
    "paths_monitored": 3,
    "paths_good": 2,
    "paths_degraded": 1,
    "paths_failed": 0
  }
}
```

### 5.3 Webhook for Quality Alerts

**Endpoint:** `POST /cgi-bin/qualityalert` (NEW)

Triggers immediate quality probe to specific destination:

```json
{
  "target": "W6XYZ-2",
  "duration_s": 10,
  "priority": "high"
}
```

### 5.4 Software Health Status Endpoint

**Endpoint:** `GET /cgi-bin/healthstatus` (NEW)

Returns comprehensive software and system health:

```json
{
  "overall_status": "healthy",
  "health_score": 95.5,
  "timestamp": "2025-09-29T18:45:00Z",
  "checks": {
    "threads_responsive": true,
    "memory_stable": true,
    "no_recent_crashes": true,
    "sip_service_ok": true,
    "phonebook_current": true,
    "mesh_reachable": true
  },
  "alerts": [
    {
      "level": "warning",
      "component": "mesh_monitor",
      "message": "High jitter detected to node-K: 45ms",
      "since": "2025-09-29T18:30:00Z",
      "action": "Check RF interference"
    }
  ],
  "recommendations": [
    "Consider reducing probe frequency during peak hours",
    "Monitor memory usage - approaching 75% of budget"
  ],
  "diagnostics": {
    "last_restart_reason": "Manual restart",
    "performance_trend": "stable",
    "error_trend": "decreasing"
  }
}
```

---

## 6) Network Behavior & Resource Budgets

### 6.1 Enhanced Resource Targets

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

---

## 8) Passive Safety Enhancements

### 8.1 Enhanced Self-Healing Features

```c
// In passive_safety.c - Enhanced with software health monitoring

void passive_monitor_health_check(void) {
    // Kill stuck probe threads
    if (probe_thread_hung()) {
        LOG_WARN("Monitor probe thread hung - restarting");
        pthread_cancel(probe_tid);
        restart_probe_engine();
        increment_restart_counter("probe_thread");
    }

    // Clear stale probe results > 1 hour
    cleanup_old_probe_data();

    // Reset if consuming too much memory
    if (monitor_mem_usage() > MAX_MONITOR_MEM) {
        LOG_ERROR("Monitor memory usage exceeded %d MB - resetting", MAX_MONITOR_MEM);
        reset_monitor_module();
        increment_restart_counter("memory_reset");
    }
}

void passive_software_health_check(void) {
    // Check thread responsiveness
    for (int i = 0; i < NUM_THREADS; i++) {
        if (!thread_responsive(i)) {
            LOG_ERROR("Thread %s unresponsive for %d seconds",
                      thread_names[i], get_thread_silence_duration(i));
            if (get_thread_silence_duration(i) > THREAD_RESTART_THRESHOLD) {
                restart_unresponsive_thread(i);
            }
        }
    }

    // Monitor memory growth
    size_t current_rss = get_process_memory_usage();
    if (detect_memory_leak(current_rss)) {
        LOG_WARN("Memory leak suspected: RSS grown to %zu MB", current_rss / 1024 / 1024);
    }

    // Check error rates
    if (get_hourly_error_rate() > ERROR_RATE_THRESHOLD) {
        LOG_WARN("High error rate detected: %d errors/hour", get_hourly_error_rate());
    }
}

void correlate_failures_with_mesh(void) {
    // Log correlation between SIP failures and mesh issues
    if (sip_registration_failures > threshold) {
        mesh_quality_t quality = get_path_quality(peer);
        if (quality.loss_pct > 5.0) {
            LOG_WARN("SIP failures correlate with %.1f%% packet loss to %s",
                     quality.loss_pct, peer);
            update_health_alert("sip_mesh_correlation",
                               "SIP registration failures due to mesh quality");
        }
    }
}

void setup_crash_recovery(void) {
    // Install signal handlers for crash detection
    signal(SIGSEGV, crash_signal_handler);
    signal(SIGBUS, crash_signal_handler);
    signal(SIGFPE, crash_signal_handler);

    // Set up crash info persistence
    load_crash_history();
}

void crash_signal_handler(int sig) {
    // Record crash information
    record_crash_event(sig, "Signal received", time(NULL));

    // Attempt emergency data save
    emergency_save_phonebook_state();

    LOG_CRITICAL("Process crashed with signal %d - emergency shutdown initiated", sig);

    // Clean shutdown and let system restart us
    emergency_shutdown();
    exit(1);
}
```

---

## 9) Implementation Phases

### Phase 1: Foundation (Week 1-2)
- [ ] Create mesh_monitor module structure
- [ ] Implement routing adapter (OLSR first)
- [ ] Basic probe engine (ping-like)
- [ ] Simple loss/RTT metrics

### Phase 2: Integration (Week 3)
- [ ] Wire into existing threads
- [ ] Shared configuration parsing
- [ ] Enhanced showphonebook endpoint
- [ ] Memory pool sharing

### Phase 3: Advanced Features (Week 4)
- [ ] RFC3550 jitter calculation
- [ ] Hop-by-hop analysis
- [ ] Quality correlation with SIP
- [ ] Historical trending

### Phase 4: Testing & Optimization (Week 5)
- [ ] Field testing on real AREDN network
- [ ] Memory optimization
- [ ] Flash write minimization
- [ ] Documentation

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

### 12.1 Unit Tests
- RFC3550 jitter calculator with test vectors
- JSON marshalling/unmarshalling
- Routing parser with OLSR/Babel fixtures
- Memory pool management

### 12.2 Integration Tests
- Combined phonebook + monitoring operation
- Resource limit enforcement
- Degradation mode transitions
- Flash write budgeting

### 12.3 Field Validation
- Deploy to 3+ node test network
- Verify no impact on voice calls
- Measure actual resource usage
- Stress test with poor links

---

## 13) Documentation Updates

### 13.1 User Documentation
- New configuration options in README
- Mesh quality endpoint usage
- Troubleshooting guide updates
- Performance tuning guide

### 13.2 Developer Documentation
- Module API reference
- Integration guide for custom collectors
- Protocol specifications
- Build instructions for monitoring

---

## 14) Success Metrics

### 14.1 Functional Success
- ✅ Phonebook operation unaffected
- ✅ <5% CPU overhead with monitoring
- ✅ Quality metrics match iperf3/ping
- ✅ Correlation identifies real issues

### 14.2 Emergency Resilience
- ✅ Degrades gracefully under load
- ✅ Survives routing daemon failures
- ✅ Self-heals from probe hangs
- ✅ Maintains backward compatibility

---

## 15) Future Roadmap

### v1.1 Features
- mDNS peer discovery
- Grafana dashboard templates
- SNMP export option
- Mesh topology visualization

### v1.2 Features
- Machine learning anomaly detection
- Predictive failure warnings
- Automatic rerouting suggestions
- Band steering optimization

### Long-term Vision
- Full network observability platform
- Integration with Winlink gateways
- Federal emergency system compatibility
- Satellite backup coordination

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
│   │   └── sipserver.conf        (enhanced)
│   └── www/
│       └── cgi-bin/
│           ├── meshquality       (NEW CGI script)
│           └── healthstatus      (NEW CGI script)
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

**End of Specification v1.0**

*This enhanced specification maintains the emergency communication focus of AREDN-Phonebook while adding valuable network observability features. The modular design ensures monitoring never compromises core phonebook functionality.*