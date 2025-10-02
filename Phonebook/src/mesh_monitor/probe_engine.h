#ifndef PROBE_ENGINE_H
#define PROBE_ENGINE_H

#include "mesh_monitor.h"
#include "routing_adapter.h"

// Probe packet structure with explicit return address
typedef struct {
    uint32_t sequence;
    uint32_t timestamp_sec;
    uint32_t timestamp_usec;
    char src_node[64];
    char return_ip[16];      // Explicit return IP address (e.g., "10.47.245.209")
    uint16_t return_port;    // Explicit return port (network byte order)
} __attribute__((packed)) probe_packet_t;

// Initialize probe engine
int probe_engine_init(mesh_monitor_config_t *config);

// Shutdown probe engine
void probe_engine_shutdown(void);

// Send probes to targets (uses hostname, resolves via DNS)
int send_probes(const char *dst_hostname, int count, int interval_ms);

// Probe responder (listens for incoming probes and echoes them back)
void* probe_responder_thread(void *arg);

// Calculate metrics from probe responses
int calculate_probe_metrics(const char *dst_ip, probe_result_t *result);

#endif // PROBE_ENGINE_H
