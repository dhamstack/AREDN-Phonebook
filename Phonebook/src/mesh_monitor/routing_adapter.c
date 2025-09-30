#define MODULE_NAME "ROUTING_ADAPTER"

#include "routing_adapter.h"
#include "../log_manager/log_manager.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

#define OLSR_JSONINFO_HOST "127.0.0.1"
#define OLSR_JSONINFO_PORT 9090
#define HTTP_TIMEOUT_SEC 5
#define HTTP_BUFFER_SIZE 65536

static routing_daemon_t current_daemon = ROUTING_AUTO;
static bool adapter_initialized = false;

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

// Simple HTTP GET request to OLSR jsoninfo
static int http_get_olsr_jsoninfo(const char *endpoint, char *buffer, size_t buffer_size) {
    int sockfd;
    struct sockaddr_in serv_addr;
    struct timeval timeout;
    char request[512];
    ssize_t bytes_received = 0;

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        LOG_ERROR("Failed to create socket for OLSR jsoninfo: %s", strerror(errno));
        return -1;
    }

    // Set receive timeout
    timeout.tv_sec = HTTP_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    // Setup server address
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(OLSR_JSONINFO_PORT);

    if (inet_pton(AF_INET, OLSR_JSONINFO_HOST, &serv_addr.sin_addr) <= 0) {
        LOG_ERROR("Invalid OLSR jsoninfo address");
        close(sockfd);
        return -1;
    }

    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        LOG_DEBUG("Failed to connect to OLSR jsoninfo (daemon may not be running): %s", strerror(errno));
        close(sockfd);
        return -1;
    }

    // Send HTTP GET request
    snprintf(request, sizeof(request),
             "GET /%s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n\r\n",
             endpoint, OLSR_JSONINFO_HOST);

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

// Parse Babel control socket
static int get_babel_neighbors(neighbor_info_t *neighbors, int max_neighbors) {
    // TODO: Implement Babel control socket query
    LOG_DEBUG("Babel neighbor query (stub implementation)");
    return 0;
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

int get_route(const char *dst_ip, route_info_t *route) {
    if (!adapter_initialized || !dst_ip || !route) {
        return -1;
    }

    // TODO: Query routing daemon for specific route
    LOG_DEBUG("Route query for %s (stub implementation)", dst_ip);
    return -1;  // Not found
}

int get_path_hops(const char *dst_ip, neighbor_info_t *hops, int max_hops) {
    if (!adapter_initialized || !dst_ip || !hops || max_hops <= 0) {
        return -1;
    }

    // TODO: Query routing daemon for hop-by-hop path
    LOG_DEBUG("Path hops query for %s (stub implementation)", dst_ip);
    return 0;  // No hops found
}
