#ifndef ROUTING_ADAPTER_H
#define ROUTING_ADAPTER_H

#include "mesh_monitor.h"

// Neighbor information from routing daemon
typedef struct {
    char ip[16];
    char node[64];
    char interface[16];
    float lq;   // Link quality
    float nlq;  // Neighbor link quality
    float etx;  // Expected transmission count
} neighbor_info_t;

// Route information
typedef struct {
    char dst_ip[16];
    char dst_node[64];
    char next_hop_ip[16];
    int hop_count;
    float etx;
} route_info_t;

// Initialize routing adapter
int routing_adapter_init(routing_daemon_t daemon_type);

// Shutdown routing adapter
void routing_adapter_shutdown(void);

// Get list of neighbors
int get_neighbors(neighbor_info_t *neighbors, int max_neighbors);

// Get route to destination
int get_route(const char *dst_ip, route_info_t *route);

// Get hop-by-hop path information
int get_path_hops(const char *dst_ip, neighbor_info_t *hops, int max_hops);

// Detect link type from interface name
const char* classify_link_type(const char *interface);

// Get current routing daemon name
const char* get_routing_daemon_name(void);

// Get raw OLSR jsoninfo data (for agent discovery)
int http_get_olsr_jsoninfo(const char *endpoint, char *buffer, size_t buffer_size);

// Generic HTTP GET to localhost (for AREDN APIs)
int http_get_localhost(const char *host, int port, const char *path, char *buffer, size_t buffer_size);

#endif // ROUTING_ADAPTER_H
