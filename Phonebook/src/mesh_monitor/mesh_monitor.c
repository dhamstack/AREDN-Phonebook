#define MODULE_NAME "MESH_MONITOR"

#include "mesh_monitor.h"
#include "monitor_config.h"
#include "routing_adapter.h"
#include "probe_engine.h"
#include "health_reporter.h"
#include "agent_discovery.h"
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

    // Initialize agent discovery
    if (agent_discovery_init() != 0) {
        LOG_ERROR("Failed to initialize agent discovery");
        probe_engine_shutdown();
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

    LOG_INFO("[TRACE-1] Mesh monitor thread started");
    monitor_running = true;

    // Start with timestamps in the past to trigger immediate first probe/discovery
    time_t now = time(NULL);
    time_t last_probe_time = now - g_monitor_config.network_status_interval_s - 1;  // Ensure immediate trigger
    time_t last_discovery_time = now;  // Skip discovery, use cached agents

    LOG_INFO("[TRACE-2] Initialized: will probe immediately (last_probe_time=%ld, now=%ld, interval=%d)",
             last_probe_time, now, g_monitor_config.network_status_interval_s);

    while (monitor_running) {
        time_t now = time(NULL);
        LOG_DEBUG("[TRACE-3] Loop iteration: now=%ld", now);

        // Check if it's time to run agent discovery scan (every 1 hour)
        // TEMPORARILY DISABLED for testing - using cached agents only
        if (0 && now - last_discovery_time >= 3600) {  // AGENT_DISCOVERY_INTERVAL_S
            LOG_INFO("Running periodic agent discovery scan");
            perform_agent_discovery_scan();
            last_discovery_time = now;
        }

        // Check if it's time to probe
        LOG_INFO("[TRACE-4] Probe check: now=%ld, last_probe=%ld, diff=%ld, interval=%d",
                  now, last_probe_time, now - last_probe_time, g_monitor_config.network_status_interval_s);
        if (now - last_probe_time >= g_monitor_config.network_status_interval_s) {
            LOG_INFO("[TRACE-5] Starting probe cycle");

            // Get discovered agents instead of neighbors
            discovered_agent_t agents[MAX_DISCOVERED_AGENTS];
            LOG_INFO("[TRACE-6] Calling get_discovered_agents()...");
            int agent_count = get_discovered_agents(agents, MAX_DISCOVERED_AGENTS);

            LOG_INFO("[TRACE-7] get_discovered_agents() returned %d agents", agent_count);

            if (agent_count > 0) {
                LOG_INFO("[TRACE-8] Probing %d discovered agents", agent_count);

                for (int i = 0; i < agent_count; i++) {
                    LOG_INFO("[TRACE-9] Probing agent %d: %s", i, agents[i].ip);

                    // Send probes (UDP echo test packets)
                    LOG_INFO("[TRACE-10] Calling send_probes() to send UDP packets to %s...", agents[i].ip);
                    int probes_sent = send_probes(agents[i].ip, 10, 100);  // 10 probes, 100ms apart
                    LOG_INFO("[TRACE-11] send_probes() sent %d UDP packets", probes_sent);

                    if (probes_sent > 0) {
                        LOG_INFO("[TRACE-12] Probes sent successfully, waiting %d seconds for responses...", g_monitor_config.probe_window_s);
                        // Wait for probe window to complete
                        sleep(g_monitor_config.probe_window_s);

                        LOG_INFO("[TRACE-13] Wait complete, calling calculate_probe_metrics()...");
                        // Calculate metrics
                        probe_result_t result;
                        int calc_result = calculate_probe_metrics(agents[i].ip, &result);
                        LOG_INFO("[TRACE-14] calculate_probe_metrics() returned %d (RTT=%.2f, loss=%.1f%%)",
                                 calc_result, result.rtt_ms_avg, result.loss_pct);
                        if (calc_result == 0) {
                            // Record routing daemon used for this probe
                            strncpy(result.routing_daemon, get_routing_daemon_name(), sizeof(result.routing_daemon) - 1);

                            // Get hop-by-hop path information (Phase 2)
                            neighbor_info_t path_hops[MAX_HOPS];
                            int hop_count = get_path_hops(agents[i].ip, path_hops, MAX_HOPS);

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
                                strncpy(result.dst_node, agents[i].node, sizeof(result.dst_node) - 1);
                                LOG_DEBUG("Path to %s: %d hops", agents[i].ip, hop_count);
                            }

                            // Store in history
                            pthread_mutex_lock(&history_mutex);
                            memcpy(&probe_history[probe_history_index], &result, sizeof(probe_result_t));
                            probe_history_index = (probe_history_index + 1) % PROBE_HISTORY_SIZE;
                            pthread_mutex_unlock(&history_mutex);

                            LOG_DEBUG("Probe metrics calculated for %s", agents[i].ip);
                        }
                    }
                }
            } else {
                LOG_DEBUG("No discovered agents to probe - run discovery scan");
                // Trigger immediate discovery if no agents found
                if (now - last_discovery_time > 60) {  // Don't spam discovery
                    perform_agent_discovery_scan();
                    last_discovery_time = now;
                }
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
