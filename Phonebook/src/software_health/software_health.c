// Software Health Monitoring - Built 2025-09-30
#define MODULE_NAME "SOFTWARE_HEALTH"

#include "software_health.h"
#include "../config_loader/config_loader.h"
#include "../log_manager/log_manager.h"
#include <signal.h>

// Simple max function to avoid math library dependency
static inline float fmax_simple(float a, float b) {
    return (a > b) ? a : b;
}

// Forward declarations for static functions
static void add_crash_to_history(const crash_report_t* report);

// Global health state definitions
process_health_t g_process_health = {0};
thread_health_t g_thread_health[MAX_THREADS] = {0};
memory_health_t g_memory_health = {0};
error_tracker_t g_error_tracker = {0};
pthread_mutex_t g_health_mutex = PTHREAD_MUTEX_INITIALIZER;
crash_report_t g_crash_history[MAX_CRASH_HISTORY] = {0};
int g_crash_history_count = 0;

// Thread names for easy identification
static const char* thread_names[MAX_THREADS] = {
    "main",
    "fetcher",
    "updater",
    "safety"
};

// Health monitoring enabled flag
static bool health_enabled = false;

//=============================================================================
// Crash Detection and Signal Handling
//=============================================================================

static void crash_signal_handler(int sig) {
    if (!health_enabled) return;

    time_t now = time(NULL);

    pthread_mutex_lock(&g_health_mutex);

    // Record crash information
    g_process_health.last_crash_time = now;
    g_process_health.crash_count_24h++;

    snprintf(g_process_health.last_crash_reason, sizeof(g_process_health.last_crash_reason),
             "Signal %d at %lld", sig, (long long)now);

    pthread_mutex_unlock(&g_health_mutex);

    // Log the crash
    LOG_ERROR("CRASH DETECTED: Signal %d (%s) at %ld", sig,
              (sig == SIGSEGV ? "Segmentation fault" :
               sig == SIGBUS ? "Bus error" :
               sig == SIGFPE ? "Floating point exception" :
               sig == SIGABRT ? "Abort" : "Unknown"), now);

    // Create crash report and add to history
    crash_report_t crash_report;
    populate_crash_report(&crash_report, sig);
    add_crash_to_history(&crash_report);

    // Export crash history to JSON
    export_crash_to_json("/tmp/meshmon_crashes.json");

    // Save crash state for later analysis
    emergency_save_health_state();

    // Allow default signal handling to proceed
    signal(sig, SIG_DFL);
    raise(sig);
}

static int setup_crash_handlers(void) {
    if (!g_health_config.crash_reporting) {
        LOG_DEBUG("Crash reporting disabled by configuration");
        return 0;
    }

    // Install signal handlers for major crash signals
    if (signal(SIGSEGV, crash_signal_handler) == SIG_ERR) {
        LOG_ERROR("Failed to install SIGSEGV handler");
        return -1;
    }

    if (signal(SIGBUS, crash_signal_handler) == SIG_ERR) {
        LOG_ERROR("Failed to install SIGBUS handler");
        return -1;
    }

    if (signal(SIGFPE, crash_signal_handler) == SIG_ERR) {
        LOG_ERROR("Failed to install SIGFPE handler");
        return -1;
    }

    if (signal(SIGABRT, crash_signal_handler) == SIG_ERR) {
        LOG_ERROR("Failed to install SIGABRT handler");
        return -1;
    }

    LOG_INFO("Crash detection signal handlers installed");
    return 0;
}

//=============================================================================
// Core Health Management Functions
//=============================================================================

int software_health_init(void) {
    printf("GEMINI_DEBUG: software_health_init called\n");
    LOG_INFO("Initializing software health monitoring");

    pthread_mutex_lock(&g_health_mutex);

    // Initialize process health
    g_process_health.process_start_time = time(NULL);
    g_process_health.process_pid = getpid();
    g_process_health.restart_count = 0;
    g_process_health.crash_count_24h = 0;
    g_process_health.last_crash_time = 0;
    memset(g_process_health.last_crash_reason, 0, sizeof(g_process_health.last_crash_reason));

    // Initialize thread health array
    memset(g_thread_health, 0, sizeof(g_thread_health));

    // Initialize memory health
    g_memory_health.initial_rss = get_process_rss();
    g_memory_health.current_rss = g_memory_health.initial_rss;
    g_memory_health.peak_rss = g_memory_health.initial_rss;
    g_memory_health.growth_rate_mb_per_hour = 0.0;
    g_memory_health.leak_suspected = false;
    g_memory_health.last_check = time(NULL);
    g_memory_health.last_rss = g_memory_health.initial_rss;

    // Initialize error tracking
    memset(&g_error_tracker, 0, sizeof(g_error_tracker));
    g_error_tracker.error_tracking_start = time(NULL);

    health_enabled = true;

    pthread_mutex_unlock(&g_health_mutex);

    // Setup crash detection handlers
    if (setup_crash_handlers() != 0) {
        LOG_WARN("Failed to setup crash detection handlers");
    }

    // Try to load previous health state if available
    load_health_state_from_storage();

    LOG_INFO("Software health monitoring initialized (PID: %d, Initial RSS: %zu MB)",
             g_process_health.process_pid,
             g_memory_health.initial_rss / 1024 / 1024);

    return 0;
}

void software_health_shutdown(void) {
    if (!health_enabled) return;

    LOG_INFO("Shutting down software health monitoring");

    pthread_mutex_lock(&g_health_mutex);

    // Save health state before shutdown
    emergency_save_health_state();

    health_enabled = false;

    pthread_mutex_unlock(&g_health_mutex);

    LOG_INFO("Software health monitoring shutdown complete");
}

bool is_software_health_enabled(void) {
    return health_enabled;
}

//=============================================================================
// Thread Health Functions
//=============================================================================

void register_thread_health(pthread_t tid, const char* name) {
    if (!health_enabled) return;

    pthread_mutex_lock(&g_health_mutex);

    // Find available slot
    for (int i = 0; i < MAX_THREADS; i++) {
        if (g_thread_health[i].tid == 0) {
            g_thread_health[i].tid = tid;
            strncpy(g_thread_health[i].name, name, sizeof(g_thread_health[i].name) - 1);
            g_thread_health[i].name[sizeof(g_thread_health[i].name) - 1] = '\0';
            g_thread_health[i].start_time = time(NULL);
            g_thread_health[i].last_heartbeat = time(NULL);
            g_thread_health[i].restart_count = 0;
            g_thread_health[i].is_responsive = true;
            g_thread_health[i].cpu_usage = 0.0;

            LOG_DEBUG("Registered thread health for %s (slot %d)", name, i);
            break;
        }
    }

    pthread_mutex_unlock(&g_health_mutex);
}

void update_thread_heartbeat(int thread_id) {
    if (!health_enabled || thread_id < 0 || thread_id >= MAX_THREADS) return;

    pthread_mutex_lock(&g_health_mutex);

    if (g_thread_health[thread_id].tid != 0) {
        g_thread_health[thread_id].last_heartbeat = time(NULL);
        g_thread_health[thread_id].is_responsive = true;
    }

    pthread_mutex_unlock(&g_health_mutex);
}

void check_thread_responsiveness(void) {
    if (!health_enabled) return;

    time_t now = time(NULL);

    pthread_mutex_lock(&g_health_mutex);

    for (int i = 0; i < MAX_THREADS; i++) {
        if (g_thread_health[i].tid != 0) {
            time_t silence = now - g_thread_health[i].last_heartbeat;

            if (silence > THREAD_TIMEOUT_SECONDS) {
                if (g_thread_health[i].is_responsive) {
                    // First time detecting unresponsiveness
                    g_thread_health[i].is_responsive = false;
                    LOG_ERROR("Thread %s unresponsive for %lld seconds",
                             g_thread_health[i].name, (long long)silence);
                }
            } else {
                g_thread_health[i].is_responsive = true;
            }
        }
    }

    pthread_mutex_unlock(&g_health_mutex);
}

bool is_thread_responsive(int thread_id) {
    if (!health_enabled || thread_id < 0 || thread_id >= MAX_THREADS) return true;

    pthread_mutex_lock(&g_health_mutex);
    bool responsive = g_thread_health[thread_id].is_responsive;
    pthread_mutex_unlock(&g_health_mutex);

    return responsive;
}

time_t get_thread_silence_duration(int thread_id) {
    if (!health_enabled || thread_id < 0 || thread_id >= MAX_THREADS) return 0;

    pthread_mutex_lock(&g_health_mutex);
    time_t silence = time(NULL) - g_thread_health[thread_id].last_heartbeat;
    pthread_mutex_unlock(&g_health_mutex);

    return silence;
}

//=============================================================================
// Process Health Functions
//=============================================================================

void record_process_start(void) {
    if (!health_enabled) return;

    pthread_mutex_lock(&g_health_mutex);
    g_process_health.process_start_time = time(NULL);
    g_process_health.process_pid = getpid();
    pthread_mutex_unlock(&g_health_mutex);

    LOG_INFO("Process start recorded (PID: %d)", g_process_health.process_pid);
}

void record_process_restart(const char* reason) {
    if (!health_enabled) return;

    pthread_mutex_lock(&g_health_mutex);
    g_process_health.last_restart_time = time(NULL);
    g_process_health.restart_count++;
    pthread_mutex_unlock(&g_health_mutex);

    LOG_WARN("Process restart recorded: %s (restart count: %d)",
             reason, g_process_health.restart_count);
}

void increment_restart_counter(const char* component) {
    if (!health_enabled) return;

    pthread_mutex_lock(&g_health_mutex);
    g_process_health.restart_count++;
    pthread_mutex_unlock(&g_health_mutex);

    LOG_WARN("Restart counter incremented for %s (total: %d)",
             component, g_process_health.restart_count);
}

//=============================================================================
// Memory Health Functions
//=============================================================================

void monitor_memory_usage(void) {
    if (!health_enabled) return;

    size_t current_rss = get_process_rss();
    time_t now = time(NULL);

    pthread_mutex_lock(&g_health_mutex);

    g_memory_health.current_rss = current_rss;
    if (current_rss > g_memory_health.peak_rss) {
        g_memory_health.peak_rss = current_rss;
    }

    // Calculate growth rate if we have previous data
    if (g_memory_health.last_check > 0) {
        time_t elapsed = now - g_memory_health.last_check;
        if (elapsed > 0) {
            float growth_bytes = (float)(current_rss - g_memory_health.last_rss);
            g_memory_health.growth_rate_mb_per_hour =
                (growth_bytes / 1024.0 / 1024.0) * (3600.0 / elapsed);
        }
    }

    // Check for memory leak
    if (detect_memory_leak(current_rss)) {
        if (!g_memory_health.leak_suspected) {
            g_memory_health.leak_suspected = true;
            LOG_WARN("Memory leak suspected: RSS %zu MB (started at %zu MB, growth rate: %.2f MB/h)",
                     current_rss / 1024 / 1024,
                     g_memory_health.initial_rss / 1024 / 1024,
                     g_memory_health.growth_rate_mb_per_hour);
        }
    } else {
        g_memory_health.leak_suspected = false;
    }

    g_memory_health.last_check = now;
    g_memory_health.last_rss = current_rss;

    pthread_mutex_unlock(&g_health_mutex);
}

size_t get_process_rss(void) {
    FILE* status_file = fopen("/proc/self/status", "r");
    if (!status_file) return 0;

    char line[256];
    size_t rss_kb = 0;

    while (fgets(line, sizeof(line), status_file)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line, "VmRSS: %zu kB", &rss_kb);
            break;
        }
    }

    fclose(status_file);
    return rss_kb * 1024; // Convert to bytes
}

bool detect_memory_leak(size_t current_rss) {
    if (!health_enabled) return false;

    // Consider it a leak if memory has grown by more than 50% from initial
    // and growth rate is positive
    return (current_rss > g_memory_health.initial_rss * 1.5) &&
           (g_memory_health.growth_rate_mb_per_hour > 0.1);
}

//=============================================================================
// Error Tracking Functions
//=============================================================================

void record_error_event(const char* component, const char* error) {
    if (!health_enabled) return;

    pthread_mutex_lock(&g_health_mutex);

    // Update appropriate error counter
    if (strstr(component, "sip") || strstr(component, "SIP")) {
        g_error_tracker.sip_errors_per_hour++;
    } else if (strstr(component, "fetch") || strstr(component, "phonebook")) {
        g_error_tracker.fetch_failures_per_hour++;
    }

    g_error_tracker.total_errors_24h++;

    pthread_mutex_unlock(&g_health_mutex);

    LOG_ERROR("Error recorded for %s: %s", component, error);
}

void record_sip_error(const char* error_details) {
    record_error_event("sip", error_details);
}

void record_fetch_error(const char* error_details) {
    record_error_event("fetch", error_details);
}

int get_hourly_error_rate(void) {
    if (!health_enabled) return 0;

    pthread_mutex_lock(&g_health_mutex);
    int total_errors = g_error_tracker.sip_errors_per_hour +
                      g_error_tracker.fetch_failures_per_hour +
                      g_error_tracker.probe_failures_per_hour;
    pthread_mutex_unlock(&g_health_mutex);

    return total_errors;
}

//=============================================================================
// Health Summary and Scoring
//=============================================================================

void get_health_summary(software_health_summary_t* summary) {
    if (!summary || !health_enabled) return;

    pthread_mutex_lock(&g_health_mutex);

    summary->uptime_seconds = time(NULL) - g_process_health.process_start_time;
    summary->restart_count = g_process_health.restart_count;
    summary->no_recent_crashes = (g_process_health.crash_count_24h == 0);

    // Check if all threads are responsive
    summary->threads_responsive = true;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (g_thread_health[i].tid != 0 && !g_thread_health[i].is_responsive) {
            summary->threads_responsive = false;
            break;
        }
    }

    summary->memory_stable = !g_memory_health.leak_suspected;
    summary->health_score = calculate_health_score();
    summary->is_healthy = is_system_healthy();

    // Basic service checks (can be enhanced later)
    summary->sip_service_ok = true;      // TODO: Add actual SIP health check
    summary->phonebook_current = true;   // TODO: Add phonebook freshness check

    pthread_mutex_unlock(&g_health_mutex);
}

float calculate_health_score(void) {
    if (!health_enabled) return 100.0;

    float score = 100.0;

    // Deduct for unresponsive threads (20 points each)
    for (int i = 0; i < MAX_THREADS; i++) {
        if (g_thread_health[i].tid != 0 && !g_thread_health[i].is_responsive) {
            score -= 20.0;
        }
    }

    // Deduct for memory issues (15 points)
    if (g_memory_health.leak_suspected) {
        score -= 15.0;
    }

    // Deduct for recent crashes (10 points each)
    score -= (g_process_health.crash_count_24h * 10.0);

    // Deduct for frequent restarts (5 points if > 5 restarts)
    if (g_process_health.restart_count > 5) {
        score -= 10.0;
    }

    // Deduct for high error rates (1 point per error per hour)
    score -= get_hourly_error_rate();

    return fmax_simple(0.0, score);
}

bool is_system_healthy(void) {
    if (!health_enabled) return true;

    return calculate_health_score() >= 80.0;
}

//=============================================================================
// Utility Functions
//=============================================================================

const char* get_thread_name(int thread_id) {
    if (thread_id < 0 || thread_id >= MAX_THREADS) return "unknown";
    return thread_names[thread_id];
}

int get_thread_id_by_name(const char* name) {
    if (!name) return -1;

    for (int i = 0; i < MAX_THREADS; i++) {
        if (strcmp(thread_names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

void format_uptime(time_t uptime, char* buffer, size_t buffer_size) {
    if (!buffer) return;

    int days = uptime / 86400;
    int hours = (uptime % 86400) / 3600;
    int minutes = (uptime % 3600) / 60;
    int seconds = uptime % 60;

    if (days > 0) {
        snprintf(buffer, buffer_size, "%dd %dh %dm %ds", days, hours, minutes, seconds);
    } else if (hours > 0) {
        snprintf(buffer, buffer_size, "%dh %dm %ds", hours, minutes, seconds);
    } else if (minutes > 0) {
        snprintf(buffer, buffer_size, "%dm %ds", minutes, seconds);
    } else {
        snprintf(buffer, buffer_size, "%ds", seconds);
    }
}

//=============================================================================
// Emergency Functions (Basic implementations - can be enhanced)
//=============================================================================

void emergency_save_health_state(void) {
    if (!health_enabled) return;

    // TODO: Implement persistent storage of health state
    LOG_INFO("Emergency health state save requested");
}

void load_health_state_from_storage(void) {
    if (!health_enabled) return;

    // TODO: Implement loading of previous health state
    LOG_DEBUG("Health state load from storage requested");
}

void log_health_status(void) {
    if (!health_enabled) return;

    software_health_summary_t summary;
    get_health_summary(&summary);

    char uptime_str[64];
    format_uptime(summary.uptime_seconds, uptime_str, sizeof(uptime_str));

    LOG_INFO("Health Status: %s (Score: %.1f, Uptime: %s, Restarts: %d)",
             summary.is_healthy ? "HEALTHY" : "DEGRADED",
             summary.health_score,
             uptime_str,
             summary.restart_count);
}

void cleanup_old_errors(void) {
    if (!health_enabled) return;

    // Reset hourly counters (called by periodic cleanup)
    pthread_mutex_lock(&g_health_mutex);
    g_error_tracker.sip_errors_per_hour = 0;
    g_error_tracker.fetch_failures_per_hour = 0;
    g_error_tracker.probe_failures_per_hour = 0;
    pthread_mutex_unlock(&g_health_mutex);
}

//=============================================================================
// Periodic Health Monitoring
//=============================================================================

void periodic_health_check(void) {
    if (!health_enabled) return;

    printf("GEMINI_DEBUG: periodic_health_check called\n");
    LOG_DEBUG("Periodic health check running.");

    // Update memory monitoring
    monitor_memory_usage();

    // Check thread responsiveness
    check_thread_responsiveness();

    // Log health status if configured
    static time_t last_health_log = 0;
    static time_t last_health_export = 0;
    time_t now = time(NULL);

    if (now - last_health_log >= g_health_config.health_check_interval) {
        log_health_status();
        last_health_log = now;
    }

    // Export health JSON every 60 seconds
    if (now - last_health_export >= 60) {
        export_health_to_json("/tmp/meshmon_health.json");
        last_health_export = now;
    }

    // Clean up old error counts periodically (every hour)
    static time_t last_cleanup = 0;
    if (now - last_cleanup >= 3600) {
        cleanup_old_errors();
        last_cleanup = now;
    }
}

void write_health_to_file(const char* filepath) {
    if (!health_enabled || !filepath) return;

    // TODO: Implement health data export to file
    LOG_DEBUG("Health data export to %s requested", filepath);
}

//=============================================================================
// ISO 8601 Timestamp Helper
//=============================================================================

static void get_iso8601_timestamp(char* buffer, size_t buffer_size) {
    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    strftime(buffer, buffer_size, "%Y-%m-%dT%H:%M:%SZ", tm_info);
}

//=============================================================================
// CPU Usage Calculation
//=============================================================================

static float get_cpu_usage_percent(void) {
    static unsigned long last_total_time = 0;
    static unsigned long last_process_time = 0;
    static bool first_call = true;

    // Read /proc/self/stat for process CPU time
    FILE* stat_file = fopen("/proc/self/stat", "r");
    if (!stat_file) return 0.0;

    unsigned long utime, stime;
    char buffer[512];
    if (!fgets(buffer, sizeof(buffer), stat_file)) {
        fclose(stat_file);
        return 0.0;
    }
    fclose(stat_file);

    // Parse utime and stime (fields 14 and 15)
    char *ptr = buffer;
    for (int i = 0; i < 13; i++) {
        ptr = strchr(ptr, ' ');
        if (!ptr) return 0.0;
        ptr++;
    }
    if (sscanf(ptr, "%lu %lu", &utime, &stime) != 2) {
        return 0.0;
    }

    unsigned long process_time = utime + stime;

    // Read /proc/stat for total CPU time
    stat_file = fopen("/proc/stat", "r");
    if (!stat_file) return 0.0;

    if (!fgets(buffer, sizeof(buffer), stat_file)) {
        fclose(stat_file);
        return 0.0;
    }
    fclose(stat_file);

    // Parse total CPU time from first line
    unsigned long user, nice, system, idle;
    if (sscanf(buffer, "cpu %lu %lu %lu %lu", &user, &nice, &system, &idle) != 4) {
        return 0.0;
    }

    unsigned long total_time = user + nice + system + idle;

    if (first_call) {
        first_call = false;
        last_total_time = total_time;
        last_process_time = process_time;
        return 0.0;
    }

    unsigned long total_delta = total_time - last_total_time;
    unsigned long process_delta = process_time - last_process_time;

    last_total_time = total_time;
    last_process_time = process_time;

    if (total_delta == 0) return 0.0;

    return (100.0 * process_delta) / total_delta;
}

//=============================================================================
// Unified Agent Health Population (meshmon.v1)
//=============================================================================

void populate_agent_health(agent_health_t* health) {
    if (!health || !health_enabled) return;

    memset(health, 0, sizeof(agent_health_t));

    pthread_mutex_lock(&g_health_mutex);

    // Schema and type
    strncpy(health->schema, "meshmon.v1", sizeof(health->schema) - 1);
    strncpy(health->type, "agent_health", sizeof(health->type) - 1);

    // Node identifier (get from hostname or config)
    char hostname[64] = {0};
    if (gethostname(hostname, sizeof(hostname) - 1) == 0) {
        strncpy(health->node, hostname, sizeof(health->node) - 1);
    } else {
        strncpy(health->node, "unknown", sizeof(health->node) - 1);
    }

    // Timestamp
    get_iso8601_timestamp(health->sent_at, sizeof(health->sent_at));

    // System metrics
    health->cpu_pct = get_cpu_usage_percent();
    health->mem_mb = (float)(g_memory_health.current_rss) / 1024.0 / 1024.0;
    health->queue_len = 0; // TODO: Implement network probe queue tracking
    health->uptime_seconds = time(NULL) - g_process_health.process_start_time;
    health->restart_count = g_process_health.restart_count;

    // Thread responsiveness check
    health->threads_responsive = true;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (g_thread_health[i].tid != 0 && !g_thread_health[i].is_responsive) {
            health->threads_responsive = false;
            break;
        }
    }

    health->health_score = calculate_health_score();

    // Checks object
    health->checks.memory_stable = !g_memory_health.leak_suspected;
    health->checks.no_recent_crashes = (g_process_health.crash_count_24h == 0);
    health->checks.sip_service_ok = true; // TODO: Add actual SIP health check
    health->checks.phonebook_current = true; // TODO: Add phonebook freshness check

    // SIP service object
    health->sip_service.active_calls = 0; // TODO: Get from call_sessions
    health->sip_service.registered_users = num_registered_users;

    // Monitoring object
    health->monitoring.probe_queue_depth = 0; // TODO: Implement probe queue tracking
    strncpy(health->monitoring.last_probe_sent, "N/A", sizeof(health->monitoring.last_probe_sent) - 1);

    pthread_mutex_unlock(&g_health_mutex);
}

//=============================================================================
// JSON Serialization for Agent Health
//=============================================================================

char* agent_health_to_json_string(const agent_health_t* health) {
    if (!health) return NULL;

    // Allocate buffer for JSON (2KB should be sufficient)
    char* json = (char*)malloc(2048);
    if (!json) return NULL;

    snprintf(json, 2048,
        "{\n"
        "  \"schema\": \"%s\",\n"
        "  \"type\": \"%s\",\n"
        "  \"node\": \"%s\",\n"
        "  \"sent_at\": \"%s\",\n"
        "  \"cpu_pct\": %.1f,\n"
        "  \"mem_mb\": %.1f,\n"
        "  \"queue_len\": %d,\n"
        "  \"uptime_seconds\": %lld,\n"
        "  \"restart_count\": %d,\n"
        "  \"threads_responsive\": %s,\n"
        "  \"health_score\": %.1f,\n"
        "  \"checks\": {\n"
        "    \"memory_stable\": %s,\n"
        "    \"no_recent_crashes\": %s,\n"
        "    \"sip_service_ok\": %s,\n"
        "    \"phonebook_current\": %s\n"
        "  },\n"
        "  \"sip_service\": {\n"
        "    \"active_calls\": %d,\n"
        "    \"registered_users\": %d\n"
        "  },\n"
        "  \"monitoring\": {\n"
        "    \"probe_queue_depth\": %d,\n"
        "    \"last_probe_sent\": \"%s\"\n"
        "  }\n"
        "}",
        health->schema,
        health->type,
        health->node,
        health->sent_at,
        health->cpu_pct,
        health->mem_mb,
        health->queue_len,
        (long long)health->uptime_seconds,
        health->restart_count,
        health->threads_responsive ? "true" : "false",
        health->health_score,
        health->checks.memory_stable ? "true" : "false",
        health->checks.no_recent_crashes ? "true" : "false",
        health->checks.sip_service_ok ? "true" : "false",
        health->checks.phonebook_current ? "true" : "false",
        health->sip_service.active_calls,
        health->sip_service.registered_users,
        health->monitoring.probe_queue_depth,
        health->monitoring.last_probe_sent
    );

    return json;
}

void export_health_to_json(const char* filepath) {
    printf("GEMINI_DEBUG: export_health_to_json called with filepath: %s\n", filepath);
    if (!health_enabled || !filepath) return;

    agent_health_t health;
    populate_agent_health(&health);

    char* json = agent_health_to_json_string(&health);
    if (!json) {
        LOG_ERROR("Failed to allocate memory for health JSON");
        return;
    }

    // Write to temp file first, then rename atomically
    char temp_path[512];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", filepath);

    FILE* f = fopen(temp_path, "w");
    if (!f) {
        printf("GEMINI_DEBUG: Failed to open %s for writing\n", temp_path);
        LOG_ERROR("Failed to open %s for writing: %s", temp_path, strerror(errno));
        free(json);
        return;
    }

    printf("GEMINI_DEBUG: Writing health JSON to %s\n", temp_path);
    fprintf(f, "%s\n", json);
    fclose(f);
    free(json);

    // Atomic rename
    if (rename(temp_path, filepath) != 0) {
        printf("GEMINI_DEBUG: Failed to rename %s to %s\n", temp_path, filepath);
        LOG_ERROR("Failed to rename %s to %s: %s", temp_path, filepath, strerror(errno));
        unlink(temp_path);
        return;
    }

    printf("GEMINI_DEBUG: Health data exported to %s successfully\n", filepath);
    LOG_DEBUG("Health data exported to %s", filepath);
}

//=============================================================================
// Crash Report Population and JSON Export
//=============================================================================

static const char* signal_to_name(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV";
        case SIGBUS: return "SIGBUS";
        case SIGFPE: return "SIGFPE";
        case SIGABRT: return "SIGABRT";
        default: return "UNKNOWN";
    }
}

void populate_crash_report(crash_report_t* report, int signal) {
    if (!report || !health_enabled) return;

    memset(report, 0, sizeof(crash_report_t));

    // Schema and type
    strncpy(report->schema, "meshmon.v1", sizeof(report->schema) - 1);
    strncpy(report->type, "crash_report", sizeof(report->type) - 1);

    // Node identifier
    char hostname[64] = {0};
    if (gethostname(hostname, sizeof(hostname) - 1) == 0) {
        strncpy(report->node, hostname, sizeof(report->node) - 1);
    } else {
        strncpy(report->node, "unknown", sizeof(report->node) - 1);
    }

    // Timestamps
    get_iso8601_timestamp(report->sent_at, sizeof(report->sent_at));
    get_iso8601_timestamp(report->crash_time, sizeof(report->crash_time));

    // Signal information
    report->signal = signal;
    strncpy(report->signal_name, signal_to_name(signal), sizeof(report->signal_name) - 1);

    // Crash context
    pthread_mutex_lock(&g_health_mutex);
    strncpy(report->reason, g_process_health.last_crash_reason, sizeof(report->reason) - 1);
    report->restart_count = g_process_health.restart_count;
    report->uptime_before_crash = time(NULL) - g_process_health.process_start_time;
    pthread_mutex_unlock(&g_health_mutex);
}

char* crash_report_to_json_string(const crash_report_t* report) {
    if (!report) return NULL;

    // Allocate buffer for JSON (1KB should be sufficient)
    char* json = (char*)malloc(1024);
    if (!json) return NULL;

    snprintf(json, 1024,
        "{\n"
        "  \"schema\": \"%s\",\n"
        "  \"type\": \"%s\",\n"
        "  \"node\": \"%s\",\n"
        "  \"sent_at\": \"%s\",\n"
        "  \"crash_time\": \"%s\",\n"
        "  \"signal\": %d,\n"
        "  \"signal_name\": \"%s\",\n"
        "  \"reason\": \"%s\",\n"
        "  \"restart_count\": %d,\n"
        "  \"uptime_before_crash\": %lld\n"
        "}",
        report->schema,
        report->type,
        report->node,
        report->sent_at,
        report->crash_time,
        report->signal,
        report->signal_name,
        report->reason,
        report->restart_count,
        (long long)report->uptime_before_crash
    );

    return json;
}

static void add_crash_to_history(const crash_report_t* report) {
    if (!report) return;

    pthread_mutex_lock(&g_health_mutex);

    // Shift history if full (circular buffer behavior)
    if (g_crash_history_count >= MAX_CRASH_HISTORY) {
        memmove(&g_crash_history[0], &g_crash_history[1],
                sizeof(crash_report_t) * (MAX_CRASH_HISTORY - 1));
        g_crash_history_count = MAX_CRASH_HISTORY - 1;
    }

    // Add new crash to end
    memcpy(&g_crash_history[g_crash_history_count], report, sizeof(crash_report_t));
    g_crash_history_count++;

    pthread_mutex_unlock(&g_health_mutex);
}

void export_crash_to_json(const char* filepath) {
    if (!health_enabled || !filepath) return;

    pthread_mutex_lock(&g_health_mutex);

    if (g_crash_history_count == 0) {
        pthread_mutex_unlock(&g_health_mutex);

        // Write empty array
        FILE* f = fopen(filepath, "w");
        if (f) {
            fprintf(f, "[]\n");
            fclose(f);
        }
        return;
    }

    // Build JSON array of crashes
    char* json = (char*)malloc(MAX_CRASH_HISTORY * 1024 + 256);
    if (!json) {
        pthread_mutex_unlock(&g_health_mutex);
        LOG_ERROR("Failed to allocate memory for crash history JSON");
        return;
    }

    strcpy(json, "[\n");

    for (int i = 0; i < g_crash_history_count; i++) {
        char* crash_json = crash_report_to_json_string(&g_crash_history[i]);
        if (crash_json) {
            if (i > 0) strcat(json, ",\n");
            strcat(json, crash_json);
            free(crash_json);
        }
    }

    strcat(json, "\n]");

    pthread_mutex_unlock(&g_health_mutex);

    // Write to temp file first, then rename atomically
    char temp_path[512];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", filepath);

    FILE* f = fopen(temp_path, "w");
    if (!f) {
        LOG_ERROR("Failed to open %s for writing: %s", temp_path, strerror(errno));
        free(json);
        return;
    }

    fprintf(f, "%s\n", json);
    fclose(f);
    free(json);

    // Atomic rename
    if (rename(temp_path, filepath) != 0) {
        LOG_ERROR("Failed to rename %s to %s: %s", temp_path, filepath, strerror(errno));
        unlink(temp_path);
        return;
    }

    LOG_DEBUG("Crash history exported to %s", filepath);
}