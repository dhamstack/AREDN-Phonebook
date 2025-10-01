#define MODULE_NAME "ROUTING_ADAPTER"

#include "routing_adapter.h"
#include "../log_manager/log_manager.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

#define OLSR_JSONINFO_HOST "127.0.0.1"
#define OLSR_JSONINFO_PORT 9090
#define HTTP_TIMEOUT_SEC 5
#define HTTP_BUFFER_SIZE 65536

#define BABEL_SOCKET_PATH "/var/run/babeld.sock"
#define BABEL_BUFFER_SIZE 32768

static routing_daemon_t current_daemon = ROUTING_AUTO;
static bool adapter_initialized = false;

// Forward declarations for Babel functions
static int get_babel_neighbors(neighbor_info_t *neighbors, int max_neighbors);
static int get_babel_route(const char *dst_ip, route_info_t *route);
static int get_babel_path_hops(const char *dst_ip, neighbor_info_t *hops, int max_hops);

// Detect which routing daemon is running
static routing_daemon_t detect_routing_daemon(void) {
    // Check for OLSR jsoninfo plugin
    if (access("/var/run/olsrd.pid", F_OK) == 0) {
        LOG_INFO("Detected OLSR routing daemon");
        return ROUTING_OLSR;
    }

    // Check for Babel
    if (access("/var/run/babeld.pid", F_OK) == 0) {
        LOG_INFO("Detected Babel routing daemon");
        return ROUTING_BABEL;
    }

    LOG_WARN("No routing daemon detected");
    return ROUTING_AUTO;
}

const char* classify_link_type(const char *interface) {
    if (!interface) return "unknown";

    // FSD Section 7.3 - Interface name pattern matching
    if (strncmp(interface, "wlan", 4) == 0) return "RF";
    if (strncmp(interface, "tun", 3) == 0) return "tunnel";
    if (strncmp(interface, "eth", 3) == 0) return "ethernet";
    if (strncmp(interface, "br-", 3) == 0) return "bridge";

    return "unknown";
}

int routing_adapter_init(routing_daemon_t daemon_type) {
    LOG_INFO("Initializing routing adapter");

    if (daemon_type == ROUTING_AUTO) {
        current_daemon = detect_routing_daemon();
    } else {
        current_daemon = daemon_type;
    }

    if (current_daemon == ROUTING_AUTO) {
        LOG_ERROR("No routing daemon available");
        return -1;
    }

    adapter_initialized = true;
    LOG_INFO("Routing adapter initialized (daemon=%d)", current_daemon);
    return 0;
}

void routing_adapter_shutdown(void) {
    adapter_initialized = false;
    LOG_INFO("Routing adapter shutdown");
}

// Simple HTTP GET request to OLSR jsoninfo (exported for agent discovery)
// Generic HTTP GET to localhost
int http_get_localhost(const char *host, int port, const char *path, char *buffer, size_t buffer_size) {
    int sockfd;
    struct sockaddr_in serv_addr;
    struct timeval timeout;
    char request[512];
    ssize_t bytes_received = 0;

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        LOG_ERROR("Failed to create socket for HTTP GET: %s", strerror(errno));
        return -1;
    }

    // Set receive timeout
    timeout.tv_sec = HTTP_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    // Setup server address
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &serv_addr.sin_addr) <= 0) {
        LOG_ERROR("Invalid HTTP address: %s", host);
        close(sockfd);
        return -1;
    }

    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        LOG_DEBUG("Failed to connect to %s:%d: %s", host, port, strerror(errno));
        close(sockfd);
        return -1;
    }

    // Send HTTP GET request
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n\r\n",
             path, host);

    if (send(sockfd, request, strlen(request), 0) < 0) {
        LOG_ERROR("Failed to send HTTP request: %s", strerror(errno));
        close(sockfd);
        return -1;
    }

    // Read response
    memset(buffer, 0, buffer_size);
    bytes_received = recv(sockfd, buffer, buffer_size - 1, 0);

    close(sockfd);

    if (bytes_received < 0) {
        LOG_ERROR("Failed to receive HTTP response: %s", strerror(errno));
        return -1;
    }

    buffer[bytes_received] = '\0';

    // Find start of JSON (after HTTP headers)
    char *json_start = strstr(buffer, "\r\n\r\n");
    if (json_start) {
        json_start += 4;
        memmove(buffer, json_start, strlen(json_start) + 1);
    } else {
        json_start = strstr(buffer, "\n\n");
        if (json_start) {
            json_start += 2;
            memmove(buffer, json_start, strlen(json_start) + 1);
        }
    }

    return 0;
}

int http_get_olsr_jsoninfo(const char *endpoint, char *buffer, size_t buffer_size) {
    char path[256];
    snprintf(path, sizeof(path), "/%s", endpoint);
    return http_get_localhost(OLSR_JSONINFO_HOST, OLSR_JSONINFO_PORT, path, buffer, buffer_size);
}

// Simple JSON parser for OLSR neighbors (no external JSON library)
static int parse_olsr_neighbors_json(const char *json, neighbor_info_t *neighbors, int max_neighbors) {
    int count = 0;
    const char *pos = json;

    // Look for "neighbors" array in JSON
    const char *neighbors_array = strstr(pos, "\"neighbors\"");
    if (!neighbors_array) {
        LOG_DEBUG("No neighbors array found in OLSR response");
        return 0;
    }

    // Find the opening bracket of the array
    const char *array_start = strchr(neighbors_array, '[');
    if (!array_start) {
        LOG_DEBUG("Malformed neighbors array");
        return 0;
    }

    pos = array_start + 1;

    // Parse each neighbor object
    while (count < max_neighbors && pos && *pos) {
        // Look for IP address field
        const char *ip_field = strstr(pos, "\"ipAddress\"");
        if (!ip_field) {
            ip_field = strstr(pos, "\"neighborIP\"");
        }
        if (!ip_field) break;

        // Find the value after the colon
        const char *value_start = strchr(ip_field, ':');
        if (!value_start) break;
        value_start++;

        // Skip whitespace and quote
        while (*value_start == ' ' || *value_start == '\t' || *value_start == '"') {
            value_start++;
        }

        // Extract IP address
        char ip_buffer[16] = {0};
        int i = 0;
        while (i < 15 && *value_start && *value_start != '"' && *value_start != ',' && *value_start != '}') {
            ip_buffer[i++] = *value_start++;
        }
        ip_buffer[i] = '\0';

        // Validate and store IP
        struct in_addr addr;
        if (inet_pton(AF_INET, ip_buffer, &addr) == 1) {
            strncpy(neighbors[count].ip, ip_buffer, sizeof(neighbors[count].ip) - 1);
            neighbors[count].ip[sizeof(neighbors[count].ip) - 1] = '\0';

            // Try to extract node name (hostname) if available
            const char *hostname_field = strstr(pos, "\"hostname\"");
            if (hostname_field && hostname_field < pos + 500) {  // Within same object
                const char *name_start = strchr(hostname_field, ':');
                if (name_start) {
                    name_start++;
                    while (*name_start == ' ' || *name_start == '\t' || *name_start == '"') {
                        name_start++;
                    }
                    int j = 0;
                    while (j < 63 && *name_start && *name_start != '"' && *name_start != ',') {
                        neighbors[count].node[j++] = *name_start++;
                    }
                    neighbors[count].node[j] = '\0';
                }
            }

            // If no hostname, use IP as node name
            if (neighbors[count].node[0] == '\0') {
                strncpy(neighbors[count].node, ip_buffer, sizeof(neighbors[count].node) - 1);
            }

            // Default interface (OLSR doesn't always provide this easily)
            strncpy(neighbors[count].interface, "unknown", sizeof(neighbors[count].interface) - 1);

            count++;
        }

        // Move to next object
        pos = strchr(pos, '}');
        if (pos) pos++;
    }

    LOG_DEBUG("Parsed %d neighbors from OLSR jsoninfo", count);
    return count;
}

// Parse OLSR jsoninfo neighbors endpoint
static int get_olsr_neighbors(neighbor_info_t *neighbors, int max_neighbors) {
    static char http_buffer[HTTP_BUFFER_SIZE];

    // Query OLSR jsoninfo neighbors endpoint
    if (http_get_olsr_jsoninfo("neighbors", http_buffer, sizeof(http_buffer)) != 0) {
        LOG_DEBUG("Failed to query OLSR neighbors");
        return 0;
    }

    // Parse JSON response
    return parse_olsr_neighbors_json(http_buffer, neighbors, max_neighbors);
}

// Send command to Babel control socket and read response
static int babel_control_command(const char *command, char *buffer, size_t buffer_size) {
    int sockfd;
    struct sockaddr_un addr;
    ssize_t bytes_received = 0;

    // Create Unix domain socket
    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) {
        LOG_ERROR("Failed to create socket for Babel: %s", strerror(errno));
        return -1;
    }

    // Set receive timeout
    struct timeval timeout;
    timeout.tv_sec = HTTP_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    // Setup socket address
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, BABEL_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    // Connect to Babel control socket
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_DEBUG("Failed to connect to Babel control socket (daemon may not be running): %s", strerror(errno));
        close(sockfd);
        return -1;
    }

    // Send command
    size_t cmd_len = strlen(command);
    if (send(sockfd, command, cmd_len, 0) < 0) {
        LOG_ERROR("Failed to send Babel command: %s", strerror(errno));
        close(sockfd);
        return -1;
    }

    // Read response
    memset(buffer, 0, buffer_size);
    bytes_received = recv(sockfd, buffer, buffer_size - 1, 0);

    close(sockfd);

    if (bytes_received < 0) {
        LOG_ERROR("Failed to receive Babel response: %s", strerror(errno));
        return -1;
    }

    buffer[bytes_received] = '\0';
    return 0;
}

// Parse Babel "dump" output for neighbors
static int get_babel_neighbors(neighbor_info_t *neighbors, int max_neighbors) {
    static char babel_buffer[BABEL_BUFFER_SIZE];

    // Query Babel for neighbor table using "dump" command
    if (babel_control_command("dump\n", babel_buffer, sizeof(babel_buffer)) != 0) {
        LOG_DEBUG("Failed to query Babel neighbors");
        return 0;
    }

    int count = 0;
    char *line = babel_buffer;
    char *next_line;

    // Parse Babel dump output line by line
    // Format: "neighbour <id> address <ip> if <interface> reach <reach> rxcost <cost> ..."
    while (line && *line && count < max_neighbors) {
        next_line = strchr(line, '\n');
        if (next_line) *next_line++ = '\0';

        // Look for neighbor lines
        if (strncmp(line, "neighbour ", 10) == 0) {
            char *address_field = strstr(line, "address ");
            if (address_field) {
                address_field += 8;  // Skip "address "

                // Extract IP address
                char ip_buffer[16] = {0};
                int i = 0;
                while (i < 15 && address_field[i] && address_field[i] != ' ' && address_field[i] != '\t') {
                    ip_buffer[i] = address_field[i];
                    i++;
                }
                ip_buffer[i] = '\0';

                // Validate IP
                struct in_addr addr;
                if (inet_pton(AF_INET, ip_buffer, &addr) == 1) {
                    strncpy(neighbors[count].ip, ip_buffer, sizeof(neighbors[count].ip) - 1);
                    strncpy(neighbors[count].node, ip_buffer, sizeof(neighbors[count].node) - 1);

                    // Extract interface
                    char *if_field = strstr(line, "if ");
                    if (if_field) {
                        if_field += 3;
                        int j = 0;
                        while (j < 15 && if_field[j] && if_field[j] != ' ' && if_field[j] != '\t') {
                            neighbors[count].interface[j] = if_field[j];
                            j++;
                        }
                        neighbors[count].interface[j] = '\0';
                    }

                    // Extract rxcost (use as rough ETX estimate)
                    char *rxcost_field = strstr(line, "rxcost ");
                    if (rxcost_field) {
                        rxcost_field += 7;
                        int cost = atoi(rxcost_field);
                        neighbors[count].etx = cost / 256.0;  // Babel cost is 256 * metric
                    }

                    count++;
                }
            }
        }

        line = next_line;
    }

    LOG_DEBUG("Parsed %d neighbors from Babel", count);
    return count;
}

int get_neighbors(neighbor_info_t *neighbors, int max_neighbors) {
    if (!adapter_initialized || !neighbors || max_neighbors <= 0) {
        return -1;
    }

    switch (current_daemon) {
        case ROUTING_OLSR:
            return get_olsr_neighbors(neighbors, max_neighbors);
        case ROUTING_BABEL:
            return get_babel_neighbors(neighbors, max_neighbors);
        default:
            LOG_ERROR("Invalid routing daemon type");
            return -1;
    }
}

// Parse OLSR jsoninfo routes endpoint
static int get_olsr_route(const char *dst_ip, route_info_t *route) {
    static char http_buffer[HTTP_BUFFER_SIZE];

    // Query OLSR jsoninfo routes endpoint
    if (http_get_olsr_jsoninfo("routes", http_buffer, sizeof(http_buffer)) != 0) {
        LOG_DEBUG("Failed to query OLSR routes");
        return -1;
    }

    // Find the route to dst_ip in JSON
    char search_pattern[64];
    snprintf(search_pattern, sizeof(search_pattern), "\"destination\":\"%s\"", dst_ip);

    const char *route_entry = strstr(http_buffer, search_pattern);
    if (!route_entry) {
        // Try with CIDR notation (destination might be 10.x.x.x/32)
        snprintf(search_pattern, sizeof(search_pattern), "\"destination\":\"%s/", dst_ip);
        route_entry = strstr(http_buffer, search_pattern);
        if (!route_entry) {
            LOG_DEBUG("No route found for %s", dst_ip);
            return -1;
        }
    }

    // Extract gateway IP
    const char *gateway_field = strstr(route_entry, "\"gateway\"");
    if (gateway_field && gateway_field < route_entry + 500) {
        const char *ip_start = strchr(gateway_field, ':');
        if (ip_start) {
            ip_start++;
            while (*ip_start == ' ' || *ip_start == '\t' || *ip_start == '"') ip_start++;

            int i = 0;
            while (i < 15 && *ip_start && *ip_start != '"' && *ip_start != ',') {
                route->next_hop_ip[i++] = *ip_start++;
            }
            route->next_hop_ip[i] = '\0';
        }
    }

    // Extract metric (ETX)
    const char *metric_field = strstr(route_entry, "\"metric\"");
    if (metric_field && metric_field < route_entry + 500) {
        const char *value_start = strchr(metric_field, ':');
        if (value_start) {
            route->etx = atof(value_start + 1);
        }
    }

    // Extract hop count
    const char *hops_field = strstr(route_entry, "\"hops\"");
    if (hops_field && hops_field < route_entry + 500) {
        const char *value_start = strchr(hops_field, ':');
        if (value_start) {
            route->hop_count = atoi(value_start + 1);
        }
    }

    strncpy(route->dst_ip, dst_ip, sizeof(route->dst_ip) - 1);

    LOG_DEBUG("Route to %s: next_hop=%s, hops=%d, etx=%.2f",
              dst_ip, route->next_hop_ip, route->hop_count, route->etx);

    return 0;
}

int get_route(const char *dst_ip, route_info_t *route) {
    if (!adapter_initialized || !dst_ip || !route) {
        return -1;
    }

    memset(route, 0, sizeof(route_info_t));

    switch (current_daemon) {
        case ROUTING_OLSR:
            return get_olsr_route(dst_ip, route);
        case ROUTING_BABEL:
            return get_babel_route(dst_ip, route);
        default:
            LOG_ERROR("Invalid routing daemon type");
            return -1;
    }
}

// Parse Babel "dump" output for route to specific destination
static int get_babel_route(const char *dst_ip, route_info_t *route) {
    static char babel_buffer[BABEL_BUFFER_SIZE];

    // Query Babel routing table
    if (babel_control_command("dump\n", babel_buffer, sizeof(babel_buffer)) != 0) {
        LOG_DEBUG("Failed to query Babel routes");
        return -1;
    }

    char *line = babel_buffer;
    char *next_line;

    // Parse Babel dump output for route entries
    // Format: "route <prefix> via <nexthop> if <interface> metric <metric> ..."
    while (line && *line) {
        next_line = strchr(line, '\n');
        if (next_line) *next_line++ = '\0';

        if (strncmp(line, "route ", 6) == 0) {
            // Check if this route matches our destination
            char route_prefix[64];
            sscanf(line + 6, "%63s", route_prefix);

            // Simple match - check if dst_ip starts with prefix or is exact match
            if (strstr(route_prefix, dst_ip) == route_prefix || strstr(dst_ip, route_prefix) == dst_ip) {
                // Extract next hop
                char *via_field = strstr(line, "via ");
                if (via_field) {
                    via_field += 4;
                    int i = 0;
                    while (i < 15 && via_field[i] && via_field[i] != ' ' && via_field[i] != '\t') {
                        route->next_hop_ip[i] = via_field[i];
                        i++;
                    }
                    route->next_hop_ip[i] = '\0';
                }

                // Extract metric (convert to ETX-like value)
                char *metric_field = strstr(line, "metric ");
                if (metric_field) {
                    metric_field += 7;
                    int metric = atoi(metric_field);
                    route->etx = metric / 256.0;
                    // Rough hop count estimate from metric (Babel typically uses 256 per hop)
                    route->hop_count = (metric + 128) / 256;
                }

                strncpy(route->dst_ip, dst_ip, sizeof(route->dst_ip) - 1);

                LOG_DEBUG("Babel route to %s: next_hop=%s, hops=%d, etx=%.2f",
                          dst_ip, route->next_hop_ip, route->hop_count, route->etx);

                return 0;
            }
        }

        line = next_line;
    }

    LOG_DEBUG("No Babel route found for %s", dst_ip);
    return -1;
}

// Get hop-by-hop path using OLSR topology
static int get_olsr_path_hops(const char *dst_ip, neighbor_info_t *hops, int max_hops) {
    // Get the route first to know the path
    route_info_t route;
    if (get_olsr_route(dst_ip, &route) != 0) {
        LOG_DEBUG("No route found for path analysis to %s", dst_ip);
        return 0;
    }

    // If it's a direct neighbor (1 hop), return it
    if (route.hop_count <= 1) {
        memset(&hops[0], 0, sizeof(neighbor_info_t));
        strncpy(hops[0].ip, dst_ip, sizeof(hops[0].ip) - 1);
        strncpy(hops[0].node, dst_ip, sizeof(hops[0].node) - 1);
        hops[0].etx = route.etx;
        LOG_DEBUG("Direct neighbor path: %s (1 hop)", dst_ip);
        return 1;
    }

    // For multi-hop paths, query OLSR topology
    static char http_buffer[HTTP_BUFFER_SIZE];
    if (http_get_olsr_jsoninfo("topology", http_buffer, sizeof(http_buffer)) != 0) {
        LOG_DEBUG("Failed to query OLSR topology");
        return 0;
    }

    // Build path from topology data
    // Start from our next hop (gateway) and follow the path
    int hop_count = 0;
    char current_node[16];
    strncpy(current_node, route.next_hop_ip, sizeof(current_node) - 1);

    // Add first hop (next_hop/gateway)
    memset(&hops[hop_count], 0, sizeof(neighbor_info_t));
    strncpy(hops[hop_count].ip, current_node, sizeof(hops[hop_count].ip) - 1);
    strncpy(hops[hop_count].node, current_node, sizeof(hops[hop_count].node) - 1);
    strncpy(hops[hop_count].interface, "unknown", sizeof(hops[hop_count].interface) - 1);
    hop_count++;

    // Follow the path through topology until we reach destination
    while (hop_count < max_hops && strcmp(current_node, dst_ip) != 0) {
        // Find topology entry where lastHopIP == current_node and destinationIP moves closer to dst_ip
        // This is a simplified approach - full implementation would need dijkstra or similar

        // For now, just add the final destination
        if (hop_count == 1) {
            memset(&hops[hop_count], 0, sizeof(neighbor_info_t));
            strncpy(hops[hop_count].ip, dst_ip, sizeof(hops[hop_count].ip) - 1);
            strncpy(hops[hop_count].node, dst_ip, sizeof(hops[hop_count].node) - 1);
            hops[hop_count].etx = route.etx;
            hop_count++;
        }
        break;
    }

    LOG_DEBUG("Path to %s: %d hops", dst_ip, hop_count);
    return hop_count;
}

int get_path_hops(const char *dst_ip, neighbor_info_t *hops, int max_hops) {
    if (!adapter_initialized || !dst_ip || !hops || max_hops <= 0) {
        return -1;
    }

    memset(hops, 0, sizeof(neighbor_info_t) * max_hops);

    switch (current_daemon) {
        case ROUTING_OLSR:
            return get_olsr_path_hops(dst_ip, hops, max_hops);
        case ROUTING_BABEL:
            return get_babel_path_hops(dst_ip, hops, max_hops);
        default:
            LOG_ERROR("Invalid routing daemon type");
            return -1;
    }
}

// Get hop-by-hop path for Babel
static int get_babel_path_hops(const char *dst_ip, neighbor_info_t *hops, int max_hops) {
    // Get the route first to know the path
    route_info_t route;
    if (get_babel_route(dst_ip, &route) != 0) {
        LOG_DEBUG("No route found for Babel path analysis to %s", dst_ip);
        return 0;
    }

    // For Babel, we construct a simplified path based on the route
    int hop_count = 0;

    // If it's a direct neighbor (1 hop)
    if (route.hop_count <= 1) {
        memset(&hops[0], 0, sizeof(neighbor_info_t));
        strncpy(hops[0].ip, dst_ip, sizeof(hops[0].ip) - 1);
        strncpy(hops[0].node, dst_ip, sizeof(hops[0].node) - 1);
        hops[0].etx = route.etx;
        LOG_DEBUG("Direct Babel neighbor path: %s (1 hop)", dst_ip);
        return 1;
    }

    // For multi-hop paths, add first hop (next hop)
    if (route.next_hop_ip[0] != '\0' && max_hops > 0) {
        memset(&hops[hop_count], 0, sizeof(neighbor_info_t));
        strncpy(hops[hop_count].ip, route.next_hop_ip, sizeof(hops[hop_count].ip) - 1);
        strncpy(hops[hop_count].node, route.next_hop_ip, sizeof(hops[hop_count].node) - 1);
        strncpy(hops[hop_count].interface, "unknown", sizeof(hops[hop_count].interface) - 1);
        hop_count++;
    }

    // Add destination as final hop if room
    if (hop_count < max_hops && strcmp(route.next_hop_ip, dst_ip) != 0) {
        memset(&hops[hop_count], 0, sizeof(neighbor_info_t));
        strncpy(hops[hop_count].ip, dst_ip, sizeof(hops[hop_count].ip) - 1);
        strncpy(hops[hop_count].node, dst_ip, sizeof(hops[hop_count].node) - 1);
        hops[hop_count].etx = route.etx;
        hop_count++;
    }

    LOG_DEBUG("Babel path to %s: %d hops", dst_ip, hop_count);
    return hop_count;
}

// Get current routing daemon name
const char* get_routing_daemon_name(void) {
    if (!adapter_initialized) {
        return "none";
    }

    switch (current_daemon) {
        case ROUTING_OLSR:
            return "olsr";
        case ROUTING_BABEL:
            return "babel";
        case ROUTING_AUTO:
        default:
            return "unknown";
    }
}
