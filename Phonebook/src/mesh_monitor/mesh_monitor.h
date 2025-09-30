#ifndef MESH_MONITOR_H
#define MESH_MONITOR_H

#include "../common.h"
#include <stdbool.h>
#include <time.h>

// Maximum limits
#define MAX_NEIGHBORS 32
#define MAX_PROBE_TARGETS 10
#define MAX_HOPS 10
#define PROBE_HISTORY_SIZE 20

// Monitoring modes
typedef enum {
    MONITOR_MODE_DISABLED = 0,
    MONITOR_MODE_LIGHTWEIGHT = 1,
    MONITOR_MODE_FULL = 2
} monitor_mode_t;

// Routing daemon types
typedef enum {
    ROUTING_AUTO = 0,
    ROUTING_OLSR = 1,
    ROUTING_BABEL = 2
} routing_daemon_t;

// Configuration structure
typedef struct {
    bool enabled;
    monitor_mode_t mode;

    // Network status measurement
    int network_status_interval_s;
    int probe_window_s;
    int neighbor_targets;
    int rotating_peer;
    int max_probe_kbps;
    int probe_port;
    bool dscp_ef;

    // Routing daemon integration
    routing_daemon_t routing_daemon;
    int routing_cache_s;

    // Remote reporting (optional)
    int network_status_report_s;
    char collector_url[256];
} mesh_monitor_config_t;

// Probe result structure
typedef struct {
    char dst_node[64];
    char dst_ip[16];
    time_t timestamp;

    // End-to-end metrics
    float rtt_ms_avg;
    float rtt_ms_min;
    float rtt_ms_max;
    float jitter_ms;
    float loss_pct;

    // Hop-by-hop data
    int hop_count;
    struct {
        char node[64];
        char ip[16];
        char interface[16];
        char link_type[16];  // "RF", "tunnel", "ethernet"
        float lq;            // Link quality
        float nlq;           // Neighbor link quality
        float etx;           // Expected transmission count
        float rtt_ms;        // RTT to this hop
    } hops[MAX_HOPS];
} probe_result_t;

// Public API functions

// Initialize mesh monitoring
int mesh_monitor_init(mesh_monitor_config_t *config);

// Shutdown mesh monitoring
void mesh_monitor_shutdown(void);

// Thread entry point
void* mesh_monitor_thread(void *arg);

// Get recent probe results (for CGI access)
int get_recent_probes(probe_result_t *results, int max_results);

// Check if monitoring is enabled
bool is_mesh_monitor_enabled(void);

#endif // MESH_MONITOR_H
