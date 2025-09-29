# Software Health Monitoring Implementation Plan
## For AREDN-Phonebook v1.5.0

### Overview
Implement comprehensive software health monitoring as foundation for mesh monitoring. Focus on emergency reliability and self-healing capabilities.

---

## Phase 1: Basic Health Infrastructure (Week 1)

### 1.1 Core Health Data Structures

**File:** `Phonebook/src/software_health/software_health.h`
```c
typedef struct {
    time_t process_start_time;
    time_t last_restart_time;
    int restart_count;
    int crash_count_24h;
    char last_crash_reason[128];
    time_t last_crash_time;
} process_health_t;

typedef struct {
    pthread_t tid;
    char name[32];
    time_t last_heartbeat;
    time_t start_time;
    int restart_count;
    bool is_responsive;
    float cpu_usage;
} thread_health_t;

typedef struct {
    size_t initial_rss;
    size_t current_rss;
    size_t peak_rss;
    float growth_rate_mb_per_hour;
    bool leak_suspected;
    time_t last_check;
} memory_health_t;

typedef struct {
    int sip_errors_per_hour;
    int fetch_failures_per_hour;
    int total_errors_24h;
    time_t error_tracking_start;
} error_tracker_t;
```

### 1.2 Health Manager Module

**File:** `Phonebook/src/software_health/software_health.c`
```c
// Global health state
static process_health_t g_process_health;
static thread_health_t g_thread_health[MAX_THREADS];
static memory_health_t g_memory_health;
static error_tracker_t g_error_tracker;
static pthread_mutex_t health_mutex = PTHREAD_MUTEX_INITIALIZER;

// Core functions
int software_health_init(void);
void software_health_shutdown(void);
void update_thread_heartbeat(int thread_id);
void record_error_event(const char* component, const char* error);
void record_crash_event(int signal, const char* reason);
bool is_system_healthy(void);
float calculate_health_score(void);
```

### 1.3 Configuration Integration

**File:** `Phonebook/src/config_loader/config_loader.h` (add to existing)
```c
typedef struct {
    int software_health_enabled;
    int crash_detection;
    int thread_monitoring;
    int memory_leak_detection;
    int health_check_interval;
    int restart_threshold;
    int uptime_reporting;
} software_health_config_t;

// Add to main config structure
extern software_health_config_t g_software_health_config;
```

### 1.4 Integration Points

**File:** `Phonebook/src/main.c` (modifications)
```c
#include "software_health/software_health.h"

int main() {
    // Initialize health monitoring early
    if (g_software_health_config.software_health_enabled) {
        software_health_init();
        setup_crash_handlers();
    }

    // ... existing code ...

    // Register thread health for main thread
    register_thread_health(pthread_self(), "main");

    // Main loop with health heartbeats
    while (keep_running) {
        update_thread_heartbeat(THREAD_MAIN);
        // ... existing SIP processing ...
    }
}
```

**Deliverables:**
- [ ] software_health.h header with all data structures
- [ ] software_health.c basic implementation
- [ ] Configuration loading for health settings
- [ ] Main.c integration points
- [ ] Makefile updates for new module

---

## Phase 2: Thread Monitoring & Crash Detection (Week 2)

### 2.1 Thread Health Monitoring

**File:** `Phonebook/src/software_health/thread_monitor.c`
```c
void register_thread_health(pthread_t tid, const char* name) {
    pthread_mutex_lock(&health_mutex);
    for (int i = 0; i < MAX_THREADS; i++) {
        if (g_thread_health[i].tid == 0) {
            g_thread_health[i].tid = tid;
            strncpy(g_thread_health[i].name, name, sizeof(g_thread_health[i].name));
            g_thread_health[i].start_time = time(NULL);
            g_thread_health[i].last_heartbeat = time(NULL);
            g_thread_health[i].is_responsive = true;
            break;
        }
    }
    pthread_mutex_unlock(&health_mutex);
}

void check_thread_responsiveness(void) {
    time_t now = time(NULL);
    for (int i = 0; i < MAX_THREADS; i++) {
        if (g_thread_health[i].tid != 0) {
            time_t silence = now - g_thread_health[i].last_heartbeat;
            if (silence > THREAD_TIMEOUT_SECONDS) {
                g_thread_health[i].is_responsive = false;
                LOG_ERROR("Thread %s unresponsive for %ld seconds",
                         g_thread_health[i].name, silence);
            }
        }
    }
}
```

### 2.2 Crash Detection System

**File:** `Phonebook/src/software_health/crash_handler.c`
```c
void setup_crash_handlers(void) {
    signal(SIGSEGV, crash_signal_handler);
    signal(SIGBUS, crash_signal_handler);
    signal(SIGFPE, crash_signal_handler);
    signal(SIGABRT, crash_signal_handler);

    // Load crash history from persistent storage
    load_crash_history();
}

void crash_signal_handler(int sig) {
    // Record crash info immediately
    record_crash_event(sig, "Signal received");

    // Emergency save of critical data
    emergency_save_phonebook_state();
    emergency_save_health_state();

    LOG_CRITICAL("CRASH: Signal %d received - emergency shutdown", sig);

    // Clean exit for systemd/procd restart
    emergency_shutdown();
    exit(1);
}

void record_crash_event(int signal, const char* reason) {
    pthread_mutex_lock(&health_mutex);
    g_process_health.last_crash_time = time(NULL);
    g_process_health.crash_count_24h++;
    snprintf(g_process_health.last_crash_reason,
             sizeof(g_process_health.last_crash_reason),
             "Signal %d: %s", signal, reason);

    // Write to persistent storage
    write_crash_info_to_flash();
    pthread_mutex_unlock(&health_mutex);
}
```

### 2.3 Memory Leak Detection

**File:** `Phonebook/src/software_health/memory_monitor.c`
```c
void monitor_memory_usage(void) {
    size_t current_rss = get_process_rss();
    time_t now = time(NULL);

    pthread_mutex_lock(&health_mutex);

    if (g_memory_health.initial_rss == 0) {
        g_memory_health.initial_rss = current_rss;
    }

    g_memory_health.current_rss = current_rss;
    if (current_rss > g_memory_health.peak_rss) {
        g_memory_health.peak_rss = current_rss;
    }

    // Calculate growth rate
    if (g_memory_health.last_check > 0) {
        time_t elapsed = now - g_memory_health.last_check;
        if (elapsed > 0) {
            float growth = (float)(current_rss - g_memory_health.initial_rss);
            g_memory_health.growth_rate_mb_per_hour =
                (growth / 1024 / 1024) * (3600.0 / elapsed);
        }
    }

    // Detect potential leaks
    if (current_rss > g_memory_health.initial_rss * 1.5) {
        g_memory_health.leak_suspected = true;
        LOG_WARN("Memory leak suspected: RSS %zu MB (started at %zu MB)",
                 current_rss / 1024 / 1024,
                 g_memory_health.initial_rss / 1024 / 1024);
    }

    g_memory_health.last_check = now;
    pthread_mutex_unlock(&health_mutex);
}
```

### 2.4 Thread Integration

**Modify existing threads to include health heartbeats:**

**File:** `Phonebook/src/phonebook_fetcher/phonebook_fetcher.c`
```c
void *phonebook_fetcher_thread(void *arg) {
    register_thread_health(pthread_self(), "fetcher");

    while (1) {
        update_thread_heartbeat(THREAD_FETCHER);

        // ... existing fetcher logic ...

        sleep(1); // Health heartbeat every second during sleep
    }
}
```

**Deliverables:**
- [ ] Thread monitoring with heartbeat system
- [ ] Crash signal handlers with emergency save
- [ ] Memory leak detection algorithm
- [ ] Integration with all existing threads
- [ ] Persistent storage for crash history

---

## Phase 3: Health Endpoints & Reporting (Week 3)

### 3.1 Enhanced showphonebook Endpoint

**File:** `Phonebook/files/www/cgi-bin/showphonebook` (modify existing)
```bash
#!/bin/sh
echo "Content-Type: application/json"
echo ""

# Get existing phonebook data
PHONEBOOK_JSON=$(get_phonebook_status)

# Get software health data
HEALTH_JSON=$(get_software_health_status)

# Combine into enhanced response
cat << EOF
{
  "phonebook": $PHONEBOOK_JSON,
  "software_health": $HEALTH_JSON,
  "timestamp": "$(date -Iseconds)"
}
EOF
```

### 3.2 New Health Status CGI Script

**File:** `Phonebook/files/www/cgi-bin/healthstatus` (new)
```bash
#!/bin/sh
echo "Content-Type: application/json"
echo ""

# Get comprehensive health status
/usr/bin/aredn-phonebook --health-status
```

### 3.3 Health Status Command

**File:** `Phonebook/src/main.c` (add command line option)
```c
void print_health_status_json(void) {
    software_health_summary_t summary;
    get_health_summary(&summary);

    printf("{\n");
    printf("  \"overall_status\": \"%s\",\n",
           summary.is_healthy ? "healthy" : "degraded");
    printf("  \"health_score\": %.1f,\n", summary.health_score);
    printf("  \"uptime_seconds\": %ld,\n", summary.uptime_seconds);
    printf("  \"restart_count\": %d,\n", summary.restart_count);
    printf("  \"threads\": {\n");

    for (int i = 0; i < MAX_THREADS; i++) {
        if (g_thread_health[i].tid != 0) {
            printf("    \"%s\": {\"responsive\": %s},\n",
                   g_thread_health[i].name,
                   g_thread_health[i].is_responsive ? "true" : "false");
        }
    }

    printf("  },\n");
    printf("  \"memory\": {\n");
    printf("    \"rss_mb\": %.1f,\n",
           g_memory_health.current_rss / 1024.0 / 1024.0);
    printf("    \"growth_rate_mb_per_hour\": %.2f,\n",
           g_memory_health.growth_rate_mb_per_hour);
    printf("    \"leak_suspected\": %s\n",
           g_memory_health.leak_suspected ? "true" : "false");
    printf("  }\n");
    printf("}\n");
}
```

### 3.4 Health Summary Functions

**File:** `Phonebook/src/software_health/health_summary.c`
```c
void get_health_summary(software_health_summary_t* summary) {
    summary->uptime_seconds = time(NULL) - g_process_health.process_start_time;
    summary->restart_count = g_process_health.restart_count;
    summary->is_healthy = is_system_healthy();
    summary->health_score = calculate_health_score();

    // Check all components
    summary->threads_responsive = true;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (g_thread_health[i].tid != 0 && !g_thread_health[i].is_responsive) {
            summary->threads_responsive = false;
            break;
        }
    }

    summary->memory_stable = !g_memory_health.leak_suspected;
    summary->no_recent_crashes = (g_process_health.crash_count_24h == 0);
}

float calculate_health_score(void) {
    float score = 100.0;

    // Deduct for unresponsive threads
    for (int i = 0; i < MAX_THREADS; i++) {
        if (g_thread_health[i].tid != 0 && !g_thread_health[i].is_responsive) {
            score -= 20.0;
        }
    }

    // Deduct for memory issues
    if (g_memory_health.leak_suspected) score -= 15.0;

    // Deduct for recent crashes
    score -= (g_process_health.crash_count_24h * 10.0);

    // Deduct for frequent restarts
    if (g_process_health.restart_count > 5) score -= 10.0;

    return fmax(0.0, score);
}
```

**Deliverables:**
- [ ] Enhanced showphonebook with health data
- [ ] New healthstatus CGI endpoint
- [ ] Command-line health status option
- [ ] Health scoring algorithm
- [ ] JSON output formatting

---

## Phase 4: Integration & Testing (Week 4)

### 4.1 Passive Safety Integration

**File:** `Phonebook/src/passive_safety/passive_safety.c` (enhance existing)
```c
void *passive_safety_thread(void *arg) {
    register_thread_health(pthread_self(), "safety");

    while (1) {
        update_thread_heartbeat(THREAD_SAFETY);

        // Existing passive safety checks
        passive_cleanup_stale_call_sessions();
        validate_and_correct_config();

        // NEW: Software health checks
        if (g_software_health_config.software_health_enabled) {
            check_thread_responsiveness();
            monitor_memory_usage();

            // Auto-recovery for unresponsive threads
            attempt_thread_recovery();
        }

        sleep(PASSIVE_SAFETY_INTERVAL);
    }
}

void attempt_thread_recovery(void) {
    for (int i = 0; i < MAX_THREADS; i++) {
        if (g_thread_health[i].tid != 0 && !g_thread_health[i].is_responsive) {
            time_t silence = time(NULL) - g_thread_health[i].last_heartbeat;

            if (silence > THREAD_RESTART_THRESHOLD) {
                LOG_WARN("Attempting recovery of unresponsive thread: %s",
                         g_thread_health[i].name);

                if (strcmp(g_thread_health[i].name, "fetcher") == 0) {
                    restart_fetcher_thread();
                } else if (strcmp(g_thread_health[i].name, "updater") == 0) {
                    restart_updater_thread();
                }

                g_thread_health[i].restart_count++;
            }
        }
    }
}
```

### 4.2 Configuration File Updates

**File:** `Phonebook/files/etc/sipserver.conf` (add section)
```ini
# Software Health Monitoring
[software_health]
enabled = 1
crash_detection = 1
thread_monitoring = 1
memory_leak_detection = 1
health_check_interval = 60
restart_threshold = 3
uptime_reporting = 1
```

### 4.3 Makefile Updates

**File:** `Phonebook/Makefile` (add new modules)
```makefile
define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-static \
		-I$(PKG_BUILD_DIR) \
		-o $(PKG_BUILD_DIR)/aredn-phonebook \
		$(PKG_BUILD_DIR)/main.c \
		$(PKG_BUILD_DIR)/call-sessions/call_sessions.c \
		$(PKG_BUILD_DIR)/user_manager/user_manager.c \
		$(PKG_BUILD_DIR)/phonebook_fetcher/phonebook_fetcher.c \
		$(PKG_BUILD_DIR)/sip_core/sip_core.c \
		$(PKG_BUILD_DIR)/status_updater/status_updater.c \
		$(PKG_BUILD_DIR)/file_utils/file_utils.c \
		$(PKG_BUILD_DIR)/csv_processor/csv_processor.c \
		$(PKG_BUILD_DIR)/log_manager/log_manager.c \
		$(PKG_BUILD_DIR)/config_loader/config_loader.c \
		$(PKG_BUILD_DIR)/passive_safety/passive_safety.c \
		$(PKG_BUILD_DIR)/software_health/software_health.c \
		$(PKG_BUILD_DIR)/software_health/thread_monitor.c \
		$(PKG_BUILD_DIR)/software_health/crash_handler.c \
		$(PKG_BUILD_DIR)/software_health/memory_monitor.c \
		$(PKG_BUILD_DIR)/software_health/health_summary.c \
		-lpthread
endef

define Package/aredn-phonebook/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/aredn-phonebook $(1)/usr/bin/
	$(INSTALL_DIR) $(1)/etc
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/files/etc/sipserver.conf $(1)/etc/
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/AREDN-Phonebook $(1)/etc/init.d/
	$(INSTALL_DIR) $(1)/www/cgi-bin
	$(INSTALL_BIN) ./files/www/cgi-bin/loadphonebook $(1)/www/cgi-bin/
	$(INSTALL_BIN) ./files/www/cgi-bin/showphonebook $(1)/www/cgi-bin/
	$(INSTALL_BIN) ./files/www/cgi-bin/healthstatus $(1)/www/cgi-bin/
endef
```

### 4.4 Testing Strategy

**Unit Tests:**
- Health score calculation with various failure scenarios
- Thread responsiveness detection
- Memory leak detection algorithm
- Crash recovery mechanisms

**Integration Tests:**
- Simulate thread hangs and verify recovery
- Test crash scenarios and emergency save
- Verify health endpoints return correct JSON
- Load testing with health monitoring enabled

**Field Tests:**
- Deploy on test AREDN node
- Monitor health metrics over 24 hours
- Induce controlled failures
- Verify automatic recovery

**Deliverables:**
- [ ] Complete passive safety integration
- [ ] Updated configuration files
- [ ] Enhanced Makefile with all modules
- [ ] Test suite for health monitoring
- [ ] Documentation updates

---

## Implementation Priority

### Week 1: Foundation
- Core data structures and basic module
- Configuration integration
- Main.c integration points

### Week 2: Core Monitoring
- Thread health with heartbeats
- Crash detection and handlers
- Memory monitoring

### Week 3: Reporting
- Health endpoints and JSON output
- Enhanced showphonebook
- Command-line health status

### Week 4: Integration
- Passive safety integration
- Testing and validation
- Documentation

## Success Metrics

- ✅ All threads report health heartbeats
- ✅ Crash detection saves emergency data
- ✅ Memory leak detection works accurately
- ✅ Health endpoints return valid JSON
- ✅ System auto-recovers from thread failures
- ✅ Health score accurately reflects system status
- ✅ <1% CPU overhead for health monitoring
- ✅ Emergency communications never interrupted

This foundation will be perfect for adding mesh monitoring in Phase 2!