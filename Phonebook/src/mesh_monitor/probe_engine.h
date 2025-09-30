#ifndef PROBE_ENGINE_H
#define PROBE_ENGINE_H

#include "mesh_monitor.h"
#include "routing_adapter.h"

// Probe packet structure (simple UDP echo)
typedef struct {
    uint32_t sequence;
    uint32_t timestamp_sec;
    uint32_t timestamp_usec;
    char src_node[64];
} __attribute__((packed)) probe_packet_t;

// Initialize probe engine
int probe_engine_init(mesh_monitor_config_t *config);

// Shutdown probe engine
void probe_engine_shutdown(void);

// Send probes to targets
int send_probes(const char *dst_ip, int count, int interval_ms);

// Probe responder (listens for incoming probes and echoes them back)
void* probe_responder_thread(void *arg);

// Calculate metrics from probe responses
int calculate_probe_metrics(const char *dst_ip, probe_result_t *result);

#endif // PROBE_ENGINE_H
