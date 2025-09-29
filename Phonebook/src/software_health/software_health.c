#define MODULE_NAME "SOFTWARE_HEALTH"

#include "software_health.h"
#include "../config_loader/config_loader.h"
#include "../log_manager/log_manager.h"
#include <math.h>

// Global health state definitions
process_health_t g_process_health = {0};
thread_health_t g_thread_health[MAX_THREADS] = {0};
memory_health_t g_memory_health = {0};
error_tracker_t g_error_tracker = {0};
pthread_mutex_t g_health_mutex = PTHREAD_MUTEX_INITIALIZER;

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
// Core Health Management Functions
//=============================================================================

int software_health_init(void) {
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
                    LOG_ERROR("Thread %s unresponsive for %ld seconds",
                             g_thread_health[i].name, silence);
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

    return fmax(0.0, score);
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

void write_health_to_file(const char* filepath) {
    if (!health_enabled || !filepath) return;

    // TODO: Implement health data export to file
    LOG_DEBUG("Health data export to %s requested", filepath);
}