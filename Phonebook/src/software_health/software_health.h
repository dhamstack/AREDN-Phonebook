#ifndef SOFTWARE_HEALTH_H
#define SOFTWARE_HEALTH_H

#include "../common.h"

// Thread identifiers for health tracking
#define THREAD_MAIN     0
#define THREAD_FETCHER  1
#define THREAD_UPDATER  2
#define THREAD_SAFETY   3
#define MAX_THREADS     4

// Health check intervals and thresholds
#define THREAD_TIMEOUT_SECONDS      30    // Thread considered hung after 30s
#define THREAD_RESTART_THRESHOLD    60    // Auto-restart after 60s silence
#define MEMORY_CHECK_INTERVAL       300   // Check memory every 5 minutes
#define HEALTH_REPORT_INTERVAL      60    // Report health every minute
#define ERROR_RATE_WINDOW          3600   // Track errors per hour

// Process health tracking
typedef struct {
    time_t process_start_time;       // When process started
    time_t last_restart_time;        // When last restarted
    int restart_count;               // Number of restarts
    int crash_count_24h;            // Crashes in last 24 hours
    char last_crash_reason[128];     // Description of last crash
    time_t last_crash_time;          // When last crash occurred
    pid_t process_pid;               // Current process ID
} process_health_t;

// Thread health tracking
typedef struct {
    pthread_t tid;                   // Thread ID
    char name[32];                   // Thread name (main, fetcher, etc.)
    time_t last_heartbeat;           // Last heartbeat timestamp
    time_t start_time;               // When thread started
    int restart_count;               // Number of restarts for this thread
    bool is_responsive;              // Currently responsive flag
    float cpu_usage;                 // CPU usage percentage (future)
} thread_health_t;

// Memory health tracking
typedef struct {
    size_t initial_rss;              // RSS at startup (bytes)
    size_t current_rss;              // Current RSS (bytes)
    size_t peak_rss;                 // Peak RSS seen (bytes)
    float growth_rate_mb_per_hour;   // Memory growth rate
    bool leak_suspected;             // Memory leak detected flag
    time_t last_check;               // Last memory check time
    size_t last_rss;                 // RSS at last check
} memory_health_t;

// Error rate tracking
typedef struct {
    int sip_errors_per_hour;         // SIP processing errors
    int fetch_failures_per_hour;     // Phonebook fetch failures
    int probe_failures_per_hour;     // Network probe failures (future)
    int total_errors_24h;            // Total errors in 24 hours
    time_t error_tracking_start;     // When error tracking started
    int error_counts[24];            // Hourly error buckets
} error_tracker_t;

// Health summary for reporting (extended for monitoring)
typedef struct {
    bool is_healthy;                 // Overall health status
    float health_score;              // Health score 0-100
    time_t uptime_seconds;           // Process uptime
    int restart_count;               // Total restart count
    bool threads_responsive;         // All threads responding
    bool memory_stable;              // Memory usage stable
    bool no_recent_crashes;          // No crashes in 24h
    bool sip_service_ok;            // SIP service functioning
    bool phonebook_current;         // Phonebook data current
} software_health_summary_t;

// Unified agent health for meshmon.v1 schema
typedef struct {
    char schema[16];                 // "meshmon.v1"
    char type[32];                   // "agent_health"
    char node[64];                   // Node identifier (e.g., "W6ABC-1")
    char sent_at[32];                // ISO 8601 timestamp
    float cpu_pct;                   // CPU usage percentage
    float mem_mb;                    // Memory usage in MB
    int queue_len;                   // Network probe queue length
    time_t uptime_seconds;           // Process uptime
    int restart_count;               // Total restart count
    bool threads_responsive;         // All threads responding
    float health_score;              // Health score 0-100

    // Checks object
    struct {
        bool memory_stable;          // Memory usage stable
        bool no_recent_crashes;      // No crashes in 24h
        bool sip_service_ok;         // SIP service functioning
        bool phonebook_current;      // Phonebook data current
    } checks;

    // SIP service object
    struct {
        int active_calls;            // Number of active calls
        int registered_users;        // Number of registered users
    } sip_service;

    // Monitoring object
    struct {
        int probe_queue_depth;       // Probe queue depth
        char last_probe_sent[32];    // Last probe sent timestamp
    } monitoring;
} agent_health_t;

// Crash report structure for meshmon.v1 schema
typedef struct {
    char schema[16];                 // "meshmon.v1"
    char type[32];                   // "crash_report"
    char node[64];                   // Node identifier
    char sent_at[32];                // ISO 8601 timestamp
    char crash_time[32];             // When crash occurred
    int signal;                      // Signal number
    char signal_name[32];            // Signal name (SIGSEGV, etc.)
    char reason[256];                // Crash reason/description
    int restart_count;               // Restart count at time of crash
    time_t uptime_before_crash;      // Uptime before crash
} crash_report_t;

// Crash history tracking
#define MAX_CRASH_HISTORY 5

// Global health state (declared here, defined in .c file)
extern process_health_t g_process_health;
extern thread_health_t g_thread_health[MAX_THREADS];
extern memory_health_t g_memory_health;
extern error_tracker_t g_error_tracker;
extern pthread_mutex_t g_health_mutex;
extern crash_report_t g_crash_history[MAX_CRASH_HISTORY];
extern int g_crash_history_count;

// Core health management functions
int software_health_init(void);
void software_health_shutdown(void);
bool is_software_health_enabled(void);

// Thread health functions
void register_thread_health(pthread_t tid, const char* name);
void update_thread_heartbeat(int thread_id);
void check_thread_responsiveness(void);
bool is_thread_responsive(int thread_id);
time_t get_thread_silence_duration(int thread_id);

// Process health functions
void record_process_start(void);
void record_process_restart(const char* reason);
void increment_restart_counter(const char* component);

// Memory health functions
void monitor_memory_usage(void);
size_t get_process_rss(void);
bool detect_memory_leak(size_t current_rss);

// Error tracking functions
void record_error_event(const char* component, const char* error);
void record_sip_error(const char* error_details);
void record_fetch_error(const char* error_details);
int get_hourly_error_rate(void);
void cleanup_old_errors(void);

// Health summary and scoring
void get_health_summary(software_health_summary_t* summary);
float calculate_health_score(void);
bool is_system_healthy(void);

// Health reporting
void log_health_status(void);
void write_health_to_file(const char* filepath);

// Unified health monitoring (meshmon.v1 schema)
void populate_agent_health(agent_health_t* health);
void export_health_to_json(const char* filepath);
void populate_crash_report(crash_report_t* report, int signal);
void export_crash_to_json(const char* filepath);
char* agent_health_to_json_string(const agent_health_t* health);
char* crash_report_to_json_string(const crash_report_t* report);

// Periodic health monitoring
void periodic_health_check(void);

// Emergency functions
void emergency_save_health_state(void);
void load_health_state_from_storage(void);

// Utility functions
const char* get_thread_name(int thread_id);
int get_thread_id_by_name(const char* name);
void format_uptime(time_t uptime, char* buffer, size_t buffer_size);

#endif // SOFTWARE_HEALTH_H