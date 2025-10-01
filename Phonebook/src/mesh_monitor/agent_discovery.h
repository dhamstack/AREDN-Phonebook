// agent_discovery.h - Agent Discovery System for Mesh Monitoring
#ifndef AGENT_DISCOVERY_H
#define AGENT_DISCOVERY_H

#include "../common.h"

#define MAX_DISCOVERED_AGENTS 100
#define AGENT_CACHE_FILE "/tmp/aredn_agent_cache.txt"
#define DISCOVERY_SCAN_INTERVAL_S 3600  // 1 hour

typedef struct {
    char ip[INET_ADDRSTRLEN];
    char node[MAX_HOSTNAME_LEN];
    time_t last_seen;
    bool is_active;
} discovered_agent_t;

// Initialize agent discovery system
int agent_discovery_init(void);

// Shutdown agent discovery
void agent_discovery_shutdown(void);

// Perform discovery scan (queries topology, probes all nodes, caches responders)
int perform_agent_discovery_scan(void);

// Get list of discovered agents for probing
int get_discovered_agents(discovered_agent_t *agents, int max_agents);

// Load cached agent list from disk
int load_agent_cache(void);

// Save agent list to disk
int save_agent_cache(void);

#endif // AGENT_DISCOVERY_H
