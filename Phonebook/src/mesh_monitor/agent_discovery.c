// agent_discovery.c - Agent Discovery System for Mesh Monitoring
#define MODULE_NAME "AGENT_DISCOVERY"

#include "agent_discovery.h"
#include "routing_adapter.h"
#include "probe_engine.h"
#include "../log_manager/log_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

// Global state (RAM only)
static discovered_agent_t agent_cache[MAX_DISCOVERED_AGENTS];
static int agent_count = 0;
static time_t last_discovery_scan = 0;
static bool discovery_initialized = false;
static pthread_mutex_t discovery_mutex = PTHREAD_MUTEX_INITIALIZER;

// Forward declarations
static int parse_neighbors_ips(const char *json, char ips[][INET_ADDRSTRLEN], int max_ips);
static bool test_agent_probe(const char *ip);
static int add_agent_to_cache(const char *ip, const char *node);

//=============================================================================
// Initialization and Shutdown
//=============================================================================

int agent_discovery_init(void) {
    LOG_INFO("Initializing agent discovery system");

    pthread_mutex_lock(&discovery_mutex);

    // Clear cache
    memset(agent_cache, 0, sizeof(agent_cache));
    agent_count = 0;
    last_discovery_scan = 0;

    // Try to load cached agents from /tmp/
    load_agent_cache();

    discovery_initialized = true;
    pthread_mutex_unlock(&discovery_mutex);

    LOG_INFO("Agent discovery initialized with %d cached agents", agent_count);
    return 0;
}

void agent_discovery_shutdown(void) {
    LOG_INFO("Shutting down agent discovery");

    pthread_mutex_lock(&discovery_mutex);
    discovery_initialized = false;

    // Save cache before shutdown
    save_agent_cache();

    pthread_mutex_unlock(&discovery_mutex);
}

//=============================================================================
// Discovery Scan
//=============================================================================

int perform_agent_discovery_scan(void) {
    if (!discovery_initialized) {
        LOG_ERROR("Agent discovery not initialized");
        return -1;
    }

    LOG_INFO("Starting agent discovery scan");
    time_t scan_start = time(NULL);

    // Query OLSR neighbors (active nodes only)
    char neighbors_json[65536];
    memset(neighbors_json, 0, sizeof(neighbors_json));

    // Use routing_adapter http_get function (now public)
    if (http_get_olsr_jsoninfo("neighbors", neighbors_json, sizeof(neighbors_json)) != 0) {
        LOG_ERROR("Failed to query OLSR neighbors for agent discovery");
        return -1;
    }

    // Parse unique IPs from neighbors
    char unique_ips[MAX_DISCOVERED_AGENTS][INET_ADDRSTRLEN];
    int ip_count = parse_neighbors_ips(neighbors_json, unique_ips, MAX_DISCOVERED_AGENTS);

    if (ip_count == 0) {
        LOG_WARN("No IPs found in OLSR neighbors");
        return 0;
    }

    LOG_INFO("Found %d neighbor nodes, testing for agents", ip_count);

    // Test each IP for agent response
    pthread_mutex_lock(&discovery_mutex);
    int new_agents = 0;
    int existing_agents = 0;

    for (int i = 0; i < ip_count; i++) {
        // Check if already in cache
        bool already_cached = false;
        for (int j = 0; j < agent_count; j++) {
            if (strcmp(agent_cache[j].ip, unique_ips[i]) == 0) {
                already_cached = true;
                agent_cache[j].last_seen = time(NULL);
                agent_cache[j].is_active = true;
                existing_agents++;
                LOG_INFO("Agent discovery progress: %d/%d - %s (cached, refreshed)", i+1, ip_count, unique_ips[i]);
                break;
            }
        }

        if (!already_cached) {
            // Test if this node has an agent
            LOG_INFO("Agent discovery progress: %d/%d - testing %s", i+1, ip_count, unique_ips[i]);
            if (test_agent_probe(unique_ips[i])) {
                if (add_agent_to_cache(unique_ips[i], unique_ips[i]) == 0) {
                    new_agents++;
                    LOG_INFO("Discovered new agent at %s", unique_ips[i]);
                }
            } else {
                LOG_DEBUG("No agent response from %s", unique_ips[i]);
            }
        }
    }

    last_discovery_scan = time(NULL);

    // Save updated cache
    save_agent_cache();

    pthread_mutex_unlock(&discovery_mutex);

    time_t scan_duration = time(NULL) - scan_start;
    LOG_INFO("Agent discovery complete: %d new, %d existing, %d total agents (scan took %ld seconds)",
             new_agents, existing_agents, agent_count, scan_duration);

    return agent_count;
}

//=============================================================================
// Get Discovered Agents
//=============================================================================

int get_discovered_agents(discovered_agent_t *agents, int max_agents) {
    if (!discovery_initialized || !agents || max_agents <= 0) {
        return -1;
    }

    pthread_mutex_lock(&discovery_mutex);

    int count = (agent_count < max_agents) ? agent_count : max_agents;
    memcpy(agents, agent_cache, count * sizeof(discovered_agent_t));

    pthread_mutex_unlock(&discovery_mutex);

    return count;
}

//=============================================================================
// Cache Management
//=============================================================================

int load_agent_cache(void) {
    FILE *fp = fopen(AGENT_CACHE_FILE, "r");
    if (!fp) {
        LOG_DEBUG("No agent cache found at %s, will perform fresh discovery", AGENT_CACHE_FILE);
        return 0;  // Not an error
    }

    char line[256];
    agent_count = 0;

    while (fgets(line, sizeof(line), fp) && agent_count < MAX_DISCOVERED_AGENTS) {
        // Parse: ip,node,timestamp
        char ip[INET_ADDRSTRLEN];
        char node[64];
        long timestamp;

        if (sscanf(line, "%[^,],%[^,],%ld", ip, node, &timestamp) == 3) {
            strncpy(agent_cache[agent_count].ip, ip, sizeof(agent_cache[agent_count].ip) - 1);
            strncpy(agent_cache[agent_count].node, node, sizeof(agent_cache[agent_count].node) - 1);
            agent_cache[agent_count].last_seen = (time_t)timestamp;
            agent_cache[agent_count].is_active = true;
            agent_count++;
        }
    }

    fclose(fp);
    LOG_INFO("Loaded %d agents from cache", agent_count);
    return agent_count;
}

int save_agent_cache(void) {
    FILE *fp = fopen(AGENT_CACHE_FILE, "w");
    if (!fp) {
        LOG_ERROR("Failed to write agent cache to %s: %s", AGENT_CACHE_FILE, strerror(errno));
        return -1;
    }

    for (int i = 0; i < agent_count; i++) {
        fprintf(fp, "%s,%s,%ld\n",
                agent_cache[i].ip,
                agent_cache[i].node,
                (long)agent_cache[i].last_seen);
    }

    fclose(fp);
    LOG_DEBUG("Saved %d agents to cache", agent_count);
    return 0;
}

//=============================================================================
// Helper Functions
//=============================================================================

static int parse_neighbors_ips(const char *json, char ips[][INET_ADDRSTRLEN], int max_ips) {
    if (!json || !ips) {
        return 0;
    }

    int count = 0;
    const char *pos = json;

    // Look for "neighbors" array in JSON
    const char *neighbors_array = strstr(pos, "\"neighbors\"");
    if (!neighbors_array) {
        LOG_DEBUG("No neighbors array found in OLSR response");
        return 0;
    }

    // Find the opening bracket
    const char *array_start = strchr(neighbors_array, '[');
    if (!array_start) {
        return 0;
    }

    pos = array_start + 1;

    // Parse all unique IPs from neighbors (only ipv4Address field)
    while (count < max_ips && pos && *pos) {
        // Look for ipv4Address field
        const char *ip_field = strstr(pos, "\"ipv4Address\"");

        if (!ip_field || ip_field > pos + 1000) {
            break;
        }

        // Extract IP
        const char *ip_start = strchr(ip_field, ':');
        if (ip_start) {
            ip_start = strchr(ip_start, '"');
            if (ip_start) {
                ip_start++;
                const char *ip_end = strchr(ip_start, '"');
                if (ip_end && (ip_end - ip_start) < INET_ADDRSTRLEN) {
                    char ip_buffer[INET_ADDRSTRLEN];
                    size_t ip_len = ip_end - ip_start;
                    memcpy(ip_buffer, ip_start, ip_len);
                    ip_buffer[ip_len] = '\0';

                    // Check if IP is unique
                    bool is_unique = true;
                    for (int i = 0; i < count; i++) {
                        if (strcmp(ips[i], ip_buffer) == 0) {
                            is_unique = false;
                            break;
                        }
                    }

                    if (is_unique) {
                        strncpy(ips[count], ip_buffer, INET_ADDRSTRLEN - 1);
                        ips[count][INET_ADDRSTRLEN - 1] = '\0';
                        count++;
                    }
                }
            }
        }

        pos = ip_field + 1;
    }

    LOG_DEBUG("Parsed %d unique IPs from OLSR neighbors", count);
    return count;
}

static bool test_agent_probe(const char *ip) {
    if (!ip) {
        return false;
    }

    // Send ONE probe
    LOG_DEBUG("Sending probe to %s", ip);
    int sent = send_probes(ip, 1, 0);
    if (sent <= 0) {
        LOG_DEBUG("Failed to send probe to %s", ip);
        return false;
    }

    // Wait up to 10 seconds for response (as per updated FSD)
    sleep(10);

    // Check if we got a response by trying to calculate metrics
    probe_result_t result;
    memset(&result, 0, sizeof(result));

    if (calculate_probe_metrics(ip, &result) == 0) {
        // Check if we actually got responses (loss < 100%)
        if (result.loss_pct < 100.0) {
            LOG_INFO("Agent test successful for %s (loss: %.1f%%, rtt: %.1fms)", ip, result.loss_pct, result.rtt_ms_avg);
            return true;
        } else {
            LOG_DEBUG("Agent test failed for %s (100%% packet loss)", ip);
        }
    } else {
        LOG_DEBUG("Failed to calculate metrics for %s", ip);
    }

    return false;
}

static int add_agent_to_cache(const char *ip, const char *node) {
    if (!ip || agent_count >= MAX_DISCOVERED_AGENTS) {
        return -1;
    }

    strncpy(agent_cache[agent_count].ip, ip, sizeof(agent_cache[agent_count].ip) - 1);
    strncpy(agent_cache[agent_count].node, node ? node : ip, sizeof(agent_cache[agent_count].node) - 1);
    agent_cache[agent_count].last_seen = time(NULL);
    agent_cache[agent_count].is_active = true;

    agent_count++;
    return 0;
}
