#define MODULE_NAME "ROUTING_ADAPTER"

#include "routing_adapter.h"
#include "../log_manager/log_manager.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

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

// Parse OLSR jsoninfo neighbors endpoint
static int get_olsr_neighbors(neighbor_info_t *neighbors, int max_neighbors) {
    // TODO: Implement actual HTTP query to http://127.0.0.1:9090/neighbors
    // For now, return 0 neighbors (stub implementation)
    LOG_DEBUG("OLSR neighbor query (stub implementation)");
    return 0;
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
