#define MODULE_NAME "MESH_MONITOR"

#include "mesh_monitor.h"
#include "monitor_config.h"
#include "routing_adapter.h"
#include "probe_engine.h"
#include "health_reporter.h"
#include "../log_manager/log_manager.h"
#include <string.h>
#include <unistd.h>

// Global state
mesh_monitor_config_t g_monitor_config;  // Exposed for remote reporter
static bool monitor_enabled = false;
static bool monitor_running = false;
static pthread_t responder_tid = 0;

// Probe result storage (circular buffer)
static probe_result_t probe_history[PROBE_HISTORY_SIZE];
static int probe_history_index = 0;
static pthread_mutex_t history_mutex = PTHREAD_MUTEX_INITIALIZER;

int mesh_monitor_init(mesh_monitor_config_t *config) {
    LOG_INFO("Initializing mesh monitor");

    if (!config) {
        // Load configuration from file
        if (load_mesh_monitor_config(&g_monitor_config) != 0) {
            LOG_ERROR("Failed to load mesh monitor configuration");
            return -1;
        }
    } else {
        memcpy(&g_monitor_config, config, sizeof(mesh_monitor_config_t));
    }

    if (!g_monitor_config.enabled) {
        LOG_INFO("Mesh monitoring disabled by configuration");
        return 0;
    }

    if (g_monitor_config.mode == MONITOR_MODE_DISABLED) {
        LOG_INFO("Mesh monitoring mode set to disabled");
        return 0;
    }

    // Initialize routing adapter
    if (routing_adapter_init(g_monitor_config.routing_daemon) != 0) {
        LOG_ERROR("Failed to initialize routing adapter");
        return -1;
    }

    // Initialize probe engine
    if (probe_engine_init(&g_monitor_config) != 0) {
        LOG_ERROR("Failed to initialize probe engine");
        routing_adapter_shutdown();
        return -1;
    }

    // Start probe responder thread
    if (pthread_create(&responder_tid, NULL, probe_responder_thread, NULL) != 0) {
        LOG_ERROR("Failed to create probe responder thread");
        probe_engine_shutdown();
        routing_adapter_shutdown();
        return -1;
    }

    monitor_enabled = true;
    LOG_INFO("Mesh monitor initialized successfully");
    return 0;
}

void mesh_monitor_shutdown(void) {
    if (!monitor_enabled) return;

    LOG_INFO("Shutting down mesh monitor");

    monitor_running = false;

    // Stop probe engine
    probe_engine_shutdown();

    // Wait for responder thread
    if (responder_tid != 0) {
        pthread_join(responder_tid, NULL);
        responder_tid = 0;
    }

    // Shutdown routing adapter
    routing_adapter_shutdown();

    monitor_enabled = false;
    LOG_INFO("Mesh monitor shutdown complete");
}

void* mesh_monitor_thread(void *arg) {
    if (!monitor_enabled) {
        LOG_WARN("Mesh monitor thread started but monitoring not enabled");
        return NULL;
    }

    LOG_INFO("Mesh monitor thread started");
    monitor_running = true;

    time_t last_probe_time = 0;

    while (monitor_running) {
        time_t now = time(NULL);

        // Check if it's time to probe
        if (now - last_probe_time >= g_monitor_config.network_status_interval_s) {
            LOG_DEBUG("Starting probe cycle");

            // Get neighbors from routing daemon
            neighbor_info_t neighbors[MAX_NEIGHBORS];
            int neighbor_count = get_neighbors(neighbors, MAX_NEIGHBORS);

            if (neighbor_count > 0) {
                // Probe a subset of neighbors
                int targets = (neighbor_count < g_monitor_config.neighbor_targets)
                              ? neighbor_count
                              : g_monitor_config.neighbor_targets;

                for (int i = 0; i < targets; i++) {
                    LOG_DEBUG("Probing neighbor %s", neighbors[i].ip);

                    // Send probes
                    int probes_sent = send_probes(neighbors[i].ip, 10, 100);  // 10 probes, 100ms apart

                    if (probes_sent > 0) {
                        // Wait for probe window to complete
                        sleep(g_monitor_config.probe_window_s);

                        // Calculate metrics
                        probe_result_t result;
                        if (calculate_probe_metrics(neighbors[i].ip, &result) == 0) {
                            // Record routing daemon used for this probe
                            strncpy(result.routing_daemon, get_routing_daemon_name(), sizeof(result.routing_daemon) - 1);

                            // Get hop-by-hop path information (Phase 2)
                            neighbor_info_t path_hops[MAX_HOPS];
                            int hop_count = get_path_hops(neighbors[i].ip, path_hops, MAX_HOPS);

                            if (hop_count > 0) {
                                result.hop_count = hop_count;
                                // Copy hop information into result
                                for (int h = 0; h < hop_count && h < MAX_HOPS; h++) {
                                    strncpy(result.hops[h].node, path_hops[h].node, sizeof(result.hops[h].node) - 1);
                                    strncpy(result.hops[h].interface, path_hops[h].interface, sizeof(result.hops[h].interface) - 1);
                                    strncpy(result.hops[h].link_type, classify_link_type(path_hops[h].interface),
                                           sizeof(result.hops[h].link_type) - 1);
                                    result.hops[h].lq = path_hops[h].lq;
                                    result.hops[h].nlq = path_hops[h].nlq;
                                    result.hops[h].etx = path_hops[h].etx;
                                    result.hops[h].rtt_ms = 0.0;  // Per-hop RTT not available without per-hop probing
                                }
                                // Copy destination node name
                                strncpy(result.dst_node, neighbors[i].node, sizeof(result.dst_node) - 1);
                                LOG_DEBUG("Path to %s: %d hops", neighbors[i].ip, hop_count);
                            }

                            // Store in history
                            pthread_mutex_lock(&history_mutex);
                            memcpy(&probe_history[probe_history_index], &result, sizeof(probe_result_t));
                            probe_history_index = (probe_history_index + 1) % PROBE_HISTORY_SIZE;
                            pthread_mutex_unlock(&history_mutex);

                            LOG_DEBUG("Probe metrics calculated for %s", neighbors[i].ip);
                        }
                    }
                }
            } else {
                LOG_DEBUG("No neighbors found to probe");
            }

            // Export network status to JSON
            export_network_to_json("/tmp/meshmon_network.json");

            last_probe_time = now;
        }

        // Sleep for a bit before checking again
        sleep(5);
    }

    LOG_INFO("Mesh monitor thread stopped");
    return NULL;
}

int get_recent_probes(probe_result_t *results, int max_results) {
    if (!results || max_results <= 0) return 0;

    pthread_mutex_lock(&history_mutex);

    int count = 0;
    int total_probes = PROBE_HISTORY_SIZE;

    // Copy most recent probes
    for (int i = 0; i < total_probes && count < max_results; i++) {
        int index = (probe_history_index - 1 - i + PROBE_HISTORY_SIZE) % PROBE_HISTORY_SIZE;

        // Check if this slot has data (timestamp != 0)
        if (probe_history[index].timestamp != 0) {
            memcpy(&results[count], &probe_history[index], sizeof(probe_result_t));
            count++;
        }
    }

    pthread_mutex_unlock(&history_mutex);

    return count;
}

bool is_mesh_monitor_enabled(void) {
    return monitor_enabled;
}
