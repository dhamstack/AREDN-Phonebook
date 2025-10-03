# Software Health Monitoring Enhancement
## For AREDN-Phonebook-Monitoring-FSD v1.0

### Missing Software Health Features

#### 1. Enhanced Agent Health Reporting

**Current (Section 5.1):**
```json
"sip_status": {
  "registered_users": 12,
  "active_calls": 2,
  "uptime_seconds": 86400
}
```

**Enhanced:**
```json
"software_health": {
  "uptime_seconds": 86400,
  "restart_count": 2,
  "last_crash": "2025-09-29T12:30:00Z",
  "crash_count_24h": 0,
  "threads": {
    "main": {"status": "healthy", "cpu_pct": 1.2},
    "fetcher": {"status": "healthy", "last_heartbeat": "2025-09-29T18:45:00Z"},
    "monitor": {"status": "healthy", "probe_success_rate": 98.5},
    "safety": {"status": "healthy", "recoveries_performed": 3}
  },
  "memory": {
    "rss_mb": 8.2,
    "heap_mb": 6.1,
    "leak_detected": false
  },
  "file_descriptors": {
    "open": 12,
    "max": 1024,
    "leaked": 0
  },
  "error_rates": {
    "sip_errors_per_hour": 2,
    "probe_failures_per_hour": 1,
    "fetch_failures_per_hour": 0
  }
}
```

#### 2. Crash Detection & Recovery

**Add to passive_safety.c:**
```c
typedef struct {
    time_t last_crash_time;
    int crash_count_24h;
    int total_restart_count;
    char last_crash_reason[128];
} crash_tracker_t;

void setup_crash_handler(void) {
    signal(SIGSEGV, crash_signal_handler);
    signal(SIGBUS, crash_signal_handler);
    signal(SIGFPE, crash_signal_handler);
    signal(SIGABRT, crash_signal_handler);
}

void crash_signal_handler(int sig) {
    crash_tracker.last_crash_time = time(NULL);
    crash_tracker.crash_count_24h++;
    snprintf(crash_tracker.last_crash_reason, sizeof(crash_tracker.last_crash_reason),
             "Signal %d at %ld", sig, time(NULL));

    // Write crash info to persistent storage
    write_crash_info_to_flash();

    // Attempt graceful shutdown
    emergency_shutdown();

    // Let system restart us
    exit(1);
}
```

#### 3. Thread Health Monitoring

**Enhanced thread tracking:**
```c
typedef struct {
    pthread_t tid;
    time_t last_heartbeat;
    time_t start_time;
    int restart_count;
    float cpu_usage;
    size_t stack_usage;
    bool is_responsive;
} thread_health_t;

void monitor_thread_health(void) {
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_health_t *th = &thread_health[i];

        // Check heartbeat
        if (time(NULL) - th->last_heartbeat > THREAD_TIMEOUT) {
            LOG_ERROR("Thread %d unresponsive for %ld seconds",
                      i, time(NULL) - th->last_heartbeat);
            th->is_responsive = false;

            // Attempt recovery
            restart_unresponsive_thread(i);
        }

        // Check stack usage
        th->stack_usage = get_thread_stack_usage(th->tid);
        if (th->stack_usage > STACK_WARNING_THRESHOLD) {
            LOG_WARN("Thread %d high stack usage: %zu bytes", i, th->stack_usage);
        }
    }
}
```

#### 4. Memory Leak Detection

**Add to health monitoring:**
```c
typedef struct {
    size_t initial_rss;
    size_t current_rss;
    size_t peak_rss;
    time_t last_check;
    bool leak_suspected;
} memory_tracker_t;

void detect_memory_leaks(void) {
    size_t current_rss = get_process_rss();
    memory_tracker.current_rss = current_rss;

    if (current_rss > memory_tracker.peak_rss) {
        memory_tracker.peak_rss = current_rss;
    }

    // Check for steady growth
    if (current_rss > memory_tracker.initial_rss * 1.5) {
        memory_tracker.leak_suspected = true;
        LOG_WARN("Possible memory leak: RSS grew from %zu to %zu MB",
                 memory_tracker.initial_rss / 1024 / 1024,
                 current_rss / 1024 / 1024);
    }
}
```

#### 5. Health Status Endpoint

**New endpoint: `/cgi-bin/healthcheck`**
```json
{
  "status": "healthy",  // healthy | degraded | critical
  "timestamp": "2025-09-29T18:45:00Z",
  "uptime_seconds": 86400,
  "health_score": 95.5,  // 0-100 overall health
  "checks": {
    "threads_responsive": true,
    "memory_stable": true,
    "no_recent_crashes": true,
    "disk_space_ok": true,
    "network_reachable": true
  },
  "alerts": [
    {
      "level": "warning",
      "message": "High mesh jitter detected to node-K",
      "since": "2025-09-29T18:30:00Z"
    }
  ],
  "recommendations": [
    "Consider reducing probe frequency during peak hours"
  ]
}
```

#### 6. Configuration Additions

**Add to /etc/sipserver.conf:**
```ini
[software_health]
enabled = 1
crash_reporting = 1
thread_monitoring = 1
memory_leak_detection = 1
health_check_interval = 60     # seconds
crash_history_days = 7
max_restart_attempts = 3
health_endpoint = 1            # Enable /cgi-bin/healthcheck
```

### Implementation Priority

1. **Phase 1**: Basic crash detection and restart counting
2. **Phase 2**: Thread health monitoring and heartbeats
3. **Phase 3**: Memory leak detection and FD monitoring
4. **Phase 4**: Health status endpoint and alerting

### Integration Points

- **passive_safety.c**: Add software health checks to existing safety thread
- **health_reporter.c**: Extend to include software health metrics
- **showphonebook endpoint**: Add software_health section
- **New healthcheck endpoint**: Dedicated health status API