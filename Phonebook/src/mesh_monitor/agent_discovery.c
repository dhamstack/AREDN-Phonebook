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
#include <ctype.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>

// Global state (RAM only)
static discovered_agent_t agent_cache[MAX_DISCOVERED_AGENTS];
static int agent_count = 0;
static time_t last_discovery_scan = 0;
static bool discovery_initialized = false;
static pthread_mutex_t discovery_mutex = PTHREAD_MUTEX_INITIALIZER;

// Forward declarations
static int parse_hosts_ips(const char *json, char ips[][INET_ADDRSTRLEN], char names[][64], int max_ips);
static bool is_numeric_name(const char *name);
static bool is_lan_interface(const char *name);
static bool test_agent_http_ping(const char *nodename, char *resolved_ip, char *lan_ip);
static int add_agent_to_cache(const char *ip, const char *lan_ip, const char *node);

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

    // Query AREDN sysinfo hosts (all nodes in mesh)
    char sysinfo_json[65536];
    memset(sysinfo_json, 0, sizeof(sysinfo_json));

    // Use localnode.local.mesh (same as phonebook does)
    if (http_get_localhost("localnode.local.mesh", 8080, "/cgi-bin/sysinfo.json?hosts=1", sysinfo_json, sizeof(sysinfo_json)) != 0) {
        LOG_WARN("AREDN sysinfo API not available (older firmware?), skipping agent discovery on this node");
        return 0;
    }

    // Parse ALL hosts first (we need the full list to iterate through)
    // Use a larger limit to get all hosts
    #define PARSE_LIMIT 500
    char (*unique_ips)[INET_ADDRSTRLEN] = malloc(PARSE_LIMIT * sizeof(*unique_ips));
    char (*node_names)[64] = malloc(PARSE_LIMIT * sizeof(*node_names));

    if (!unique_ips || !node_names) {
        LOG_ERROR("Failed to allocate memory for host list parsing");
        free(unique_ips);
        free(node_names);
        return -1;
    }

    int ip_count = parse_hosts_ips(sysinfo_json, unique_ips, node_names, PARSE_LIMIT);

    if (ip_count == 0) {
        LOG_WARN("No IPs found in AREDN hosts");
        free(unique_ips);
        free(node_names);
        return 0;
    }

    LOG_INFO("Found %d hosts in mesh, will test one at a time to minimize memory", ip_count);

    // Get local hostname to skip testing ourselves
    char local_hostname[64] = {0};
    if (gethostname(local_hostname, sizeof(local_hostname) - 1) != 0) {
        LOG_WARN("Failed to get local hostname, will test all nodes");
    }

    pthread_mutex_lock(&discovery_mutex);
    int new_agents = 0;
    int existing_agents = 0;
    int routers_tested = 0;

    // Test each host one at a time (minimal memory footprint)
    for (int i = 0; i < ip_count; i++) {
        // Skip invalid entries
        if (node_names[i][0] == '\0' || unique_ips[i][0] == '\0') {
            continue;
        }

        // Skip telephones (numeric-only names)
        if (is_numeric_name(node_names[i])) {
            continue;
        }

        // Skip LAN interface entries (lan.*)
        if (is_lan_interface(node_names[i])) {
            continue;
        }

        // Skip ourselves (no need to test local node)
        if (local_hostname[0] != '\0' && strcmp(node_names[i], local_hostname) == 0) {
            LOG_DEBUG("Skipping local node: %s", local_hostname);
            continue;
        }

        routers_tested++;
        LOG_INFO("Testing %d/%d: %s for agent", routers_tested, ip_count, node_names[i]);

        // Test agent via HTTP ping
        char resolved_ip[INET_ADDRSTRLEN];
        char lan_ip[INET_ADDRSTRLEN] = {0};
        if (test_agent_http_ping(node_names[i], resolved_ip, lan_ip)) {
            // Check if already in cache
            bool already_cached = false;
            for (int j = 0; j < agent_count; j++) {
                if (strcmp(agent_cache[j].ip, resolved_ip) == 0) {
                    already_cached = true;
                    agent_cache[j].last_seen = time(NULL);
                    agent_cache[j].is_active = true;
                    // Update LAN IP in case it changed
                    strncpy(agent_cache[j].lan_ip, lan_ip, sizeof(agent_cache[j].lan_ip) - 1);
                    existing_agents++;
                    LOG_INFO("Agent %s (%s, LAN=%s) already in cache, refreshed", node_names[i], resolved_ip, lan_ip);
                    break;
                }
            }

            if (!already_cached) {
                if (add_agent_to_cache(resolved_ip, lan_ip, node_names[i]) == 0) {
                    new_agents++;
                    LOG_INFO("Discovered new agent at %s (mesh=%s, LAN=%s)", node_names[i], resolved_ip, lan_ip);
                }
            }
        }
    }

    LOG_INFO("Discovery loop completed: tested %d routers out of %d total hosts", routers_tested, ip_count);

    // Free the host list now that we're done
    free(unique_ips);
    free(node_names);

    last_discovery_scan = time(NULL);

    // Save updated cache
    save_agent_cache();

    pthread_mutex_unlock(&discovery_mutex);

    time_t scan_duration = time(NULL) - scan_start;
    LOG_INFO("Agent discovery complete: %d new, %d existing, %d total agents (scan took %ld seconds, tested %d routers)",
             new_agents, existing_agents, agent_count, scan_duration, routers_tested);

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
        // Parse: ip,lan_ip,node,timestamp
        char ip[INET_ADDRSTRLEN];
        char lan_ip[INET_ADDRSTRLEN];
        char node[64];
        long timestamp;

        // Try new format first (ip,lan_ip,node,timestamp)
        if (sscanf(line, "%[^,],%[^,],%[^,],%ld", ip, lan_ip, node, &timestamp) == 4) {
            strncpy(agent_cache[agent_count].ip, ip, sizeof(agent_cache[agent_count].ip) - 1);
            strncpy(agent_cache[agent_count].lan_ip, lan_ip, sizeof(agent_cache[agent_count].lan_ip) - 1);
            strncpy(agent_cache[agent_count].node, node, sizeof(agent_cache[agent_count].node) - 1);
            agent_cache[agent_count].last_seen = (time_t)timestamp;
            agent_cache[agent_count].is_active = true;
            agent_count++;
        }
        // Fall back to old format (ip,node,timestamp) for backward compatibility
        else if (sscanf(line, "%[^,],%[^,],%ld", ip, node, &timestamp) == 3) {
            strncpy(agent_cache[agent_count].ip, ip, sizeof(agent_cache[agent_count].ip) - 1);
            strncpy(agent_cache[agent_count].lan_ip, ip, sizeof(agent_cache[agent_count].lan_ip) - 1);  // Use mesh IP as fallback
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
        fprintf(fp, "%s,%s,%s,%ld\n",
                agent_cache[i].ip,
                agent_cache[i].lan_ip,
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

static bool is_numeric_name(const char *name) {
    if (!name || *name == '\0') {
        return false;
    }

    // Check if the name contains only digits
    for (const char *p = name; *p != '\0'; p++) {
        if (!isdigit((unsigned char)*p)) {
            return false;
        }
    }

    return true;
}

static bool is_lan_interface(const char *name) {
    if (!name) return false;

    // Check if name starts with "lan."
    return (strncmp(name, "lan.", 4) == 0);
}

// Test if node has an agent via HTTP ping endpoint
// Returns true if agent found, fills resolved_ip with mesh IP and lan_ip with LAN IP
static bool test_agent_http_ping(const char *nodename, char *resolved_ip, char *lan_ip) {
    if (!nodename || !resolved_ip || !lan_ip) {
        return false;
    }

    // Build FQDN: <nodename>.local.mesh
    char hostname[256];
    snprintf(hostname, sizeof(hostname), "%s.%s", nodename, AREDN_MESH_DOMAIN);

    // Resolve via DNS to get IP and check reachability
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    struct addrinfo *res = NULL;

    int status = getaddrinfo(hostname, NULL, &hints, &res);
    if (status != 0) {
        LOG_DEBUG("DNS resolution failed for %s", hostname);
        return false;
    }

    // Extract IP address
    struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
    inet_ntop(AF_INET, &addr->sin_addr, resolved_ip, INET_ADDRSTRLEN);
    freeaddrinfo(res);

    LOG_DEBUG("Resolved %s -> %s, testing for agent", nodename, resolved_ip);

    // Test HTTP hello endpoint: http://<ip>:8080/cgi-bin/hello
    char hello_path[256];
    snprintf(hello_path, sizeof(hello_path), "/cgi-bin/hello");

    char response[512];
    if (http_get_localhost(resolved_ip, 8080, hello_path, response, sizeof(response)) == 0) {
        // Response is just the LAN IP address (e.g., "10.51.55.233\n")
        // Strip whitespace and validate it's an IP
        char *start = response;
        while (*start && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')) start++;

        char *end = start;
        while (*end && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n') end++;
        *end = '\0';

        // Simple validation: check if it looks like an IP (contains dots and digits)
        if (strchr(start, '.') && strlen(start) >= 7 && strlen(start) <= 15) {
            strncpy(lan_ip, start, INET_ADDRSTRLEN - 1);
            lan_ip[INET_ADDRSTRLEN - 1] = '\0';
            LOG_DEBUG("Agent hello successful for %s (mesh=%s, LAN=%s)", nodename, resolved_ip, lan_ip);
            return true;
        } else {
            // Fallback: use mesh IP if response doesn't look like an IP
            strncpy(lan_ip, resolved_ip, INET_ADDRSTRLEN - 1);
            LOG_DEBUG("Agent hello returned invalid IP '%s', using mesh IP %s", start, resolved_ip);
        }
    }

    LOG_DEBUG("Agent hello failed for %s (%s)", nodename, resolved_ip);
    return false;
}

static int parse_hosts_ips(const char *json, char ips[][INET_ADDRSTRLEN], char names[][64], int max_ips) {
    if (!json || !ips || !names) {
        return 0;
    }

    int count = 0;
    const char *pos = json;

    // Look for "hosts" array in JSON
    const char *hosts_array = strstr(pos, "\"hosts\"");
    if (!hosts_array) {
        LOG_DEBUG("No hosts array found in AREDN sysinfo response");
        return 0;
    }

    // Find the opening bracket
    const char *array_start = strchr(hosts_array, '[');
    if (!array_start) {
        return 0;
    }

    pos = array_start + 1;

    // Parse all IPs and names from hosts array
    while (count < max_ips && pos && *pos) {
        // Look for the start of an object
        const char *obj_start = strchr(pos, '{');
        if (!obj_start) {
            break;
        }

        // Find the end of this object
        const char *obj_end = strchr(obj_start, '}');
        if (!obj_end) {
            break;
        }

        // Extract name field within this object
        const char *name_field = obj_start;
        while (name_field < obj_end) {
            name_field = strstr(name_field, "\"name\"");
            if (name_field && name_field < obj_end) {
                break;
            }
            name_field = NULL;
            break;
        }

        // Extract ip field within this object
        const char *ip_field = obj_start;
        while (ip_field < obj_end) {
            ip_field = strstr(ip_field, "\"ip\"");
            if (ip_field && ip_field < obj_end) {
                break;
            }
            ip_field = NULL;
            break;
        }

        if (name_field && ip_field) {
            // Extract name value
            const char *name_start = strchr(name_field, ':');
            if (name_start) {
                name_start = strchr(name_start, '"');
                if (name_start) {
                    name_start++;
                    const char *name_end = strchr(name_start, '"');
                    if (name_end && (name_end - name_start) < 64) {
                        size_t name_len = name_end - name_start;
                        memcpy(names[count], name_start, name_len);
                        names[count][name_len] = '\0';
                    }
                }
            }

            // Extract IP value
            const char *ip_start = strchr(ip_field, ':');
            if (ip_start) {
                ip_start = strchr(ip_start, '"');
                if (ip_start) {
                    ip_start++;
                    const char *ip_end = strchr(ip_start, '"');
                    if (ip_end && (ip_end - ip_start) < INET_ADDRSTRLEN) {
                        size_t ip_len = ip_end - ip_start;
                        memcpy(ips[count], ip_start, ip_len);
                        ips[count][ip_len] = '\0';

                        count++;
                    }
                }
            }
        }

        pos = obj_end + 1;
    }

    LOG_DEBUG("Parsed %d unique IPs and names from AREDN hosts", count);
    return count;
}


static int add_agent_to_cache(const char *ip, const char *lan_ip, const char *node) {
    if (!ip || agent_count >= MAX_DISCOVERED_AGENTS) {
        return -1;
    }

    strncpy(agent_cache[agent_count].ip, ip, sizeof(agent_cache[agent_count].ip) - 1);
    strncpy(agent_cache[agent_count].lan_ip, lan_ip ? lan_ip : ip, sizeof(agent_cache[agent_count].lan_ip) - 1);
    strncpy(agent_cache[agent_count].node, node ? node : ip, sizeof(agent_cache[agent_count].node) - 1);
    agent_cache[agent_count].last_seen = time(NULL);
    agent_cache[agent_count].is_active = true;

    agent_count++;
    return 0;
}
