# Remote Collector Implementation Plan

## Overview

This document outlines the implementation plan for **Phase 3: Remote Collector Reporting** - enabling agents to send health and network monitoring data to a centralized collector via HTTP POST.

**Status:** ❌ Not Yet Implemented
**Priority:** Low (Optional Feature)
**Dependencies:** Phases 0, 1, 2 (all complete ✅)

---

## Current State Analysis

### What Exists Already

**Configuration Support:**
```c
// mesh_monitor_config_t already has these fields:
int network_status_report_s;    // Report interval (0 = disabled)
char collector_url[256];         // Collector endpoint URL
```

**Data Export Functions:**
- ✅ `agent_health_to_json_string()` - Generates health JSON
- ✅ `export_health_to_json()` - Writes to file
- ✅ `export_network_to_json()` - Writes network JSON to file
- ✅ `export_crash_to_json()` - Writes crash JSON to file

**JSON Schema:**
- ✅ meshmon.v1 format fully implemented
- ✅ All three message types defined (agent_health, network_status, crash_report)

### What's Missing

**HTTP Client:**
- ❌ No HTTP POST implementation
- ❌ No libcurl or HTTP client library integrated

**Reporter Thread:**
- ❌ No remote_reporter_thread implementation
- ❌ No periodic sending logic
- ❌ No error handling/retry logic

**Configuration Parsing:**
- ❌ `network_status_report_s` and `collector_url` not parsed from config file

---

## Implementation Plan

### Phase 3.1: HTTP Client Integration

#### Option A: Use libcurl (Recommended)
**Pros:**
- Full-featured HTTP client
- Handles timeouts, retries, redirects
- Well-tested on OpenWrt

**Cons:**
- Adds ~200KB to binary size
- External dependency

**Implementation:**
```c
// Phonebook/src/mesh_monitor/http_client.c

#include <curl/curl.h>

int http_post_json(const char *url, const char *json_data) {
    CURL *curl;
    CURLcode res;

    curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("Failed to initialize curl");
        return -1;
    }

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        LOG_ERROR("HTTP POST failed: %s", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        return -1;
    }

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    return 0;
}
```

**Makefile Changes:**
```makefile
# Add libcurl dependency
LDFLAGS += -lcurl
```

#### Option B: Use wget/curl command (Simpler)
**Pros:**
- No code changes needed
- Already available on OpenWrt
- Zero binary size increase

**Cons:**
- Less control over errors
- Spawns external process

**Implementation:**
```c
// Phonebook/src/mesh_monitor/http_client.c

int http_post_json(const char *url, const char *json_data) {
    char cmd[4096];

    // Write JSON to temp file
    FILE *fp = fopen("/tmp/meshmon_post.json", "w");
    if (!fp) return -1;
    fprintf(fp, "%s", json_data);
    fclose(fp);

    // Use wget to POST
    snprintf(cmd, sizeof(cmd),
             "wget -qO- --post-file=/tmp/meshmon_post.json "
             "--header='Content-Type: application/json' "
             "'%s' >/dev/null 2>&1",
             url);

    int ret = system(cmd);
    unlink("/tmp/meshmon_post.json");

    return (ret == 0) ? 0 : -1;
}
```

**Recommendation:** Start with **Option B (wget)**, upgrade to Option A (libcurl) if needed.

---

### Phase 3.2: Configuration Parsing

**Update monitor_config.c:**

```c
// In parse_mesh_monitor_config():

// Parse remote reporting interval
const char *report_interval = uci_lookup_option_string(ctx, section, "network_status_report_s");
if (report_interval) {
    config->network_status_report_s = atoi(report_interval);
} else {
    config->network_status_report_s = 0; // Disabled by default
}

// Parse collector URL
const char *collector = uci_lookup_option_string(ctx, section, "collector_url");
if (collector) {
    strncpy(config->collector_url, collector, sizeof(config->collector_url) - 1);
} else {
    config->collector_url[0] = '\0';
}
```

**Configuration File (/etc/sipserver.conf):**
```ini
[mesh_monitor]
# Remote reporting (optional - disabled by default)
network_status_report_s=40
collector_url=http://collector.local.mesh:5000/ingest
```

---

### Phase 3.3: Remote Reporter Thread

**Create new file: Phonebook/src/mesh_monitor/remote_reporter.c**

```c
#define MODULE_NAME "REMOTE_REPORTER"

#include "remote_reporter.h"
#include "http_client.h"
#include "../software_health/software_health.h"
#include "health_reporter.h"
#include "../log_manager/log_manager.h"
#include <pthread.h>
#include <unistd.h>

static bool reporter_running = false;
static mesh_monitor_config_t reporter_config;

void* remote_reporter_thread(void *arg) {
    mesh_monitor_config_t *config = (mesh_monitor_config_t *)arg;
    memcpy(&reporter_config, config, sizeof(mesh_monitor_config_t));

    LOG_INFO("Remote reporter thread started (interval=%ds, url=%s)",
             reporter_config.network_status_report_s,
             reporter_config.collector_url);

    reporter_running = true;
    time_t last_health_report = 0;
    time_t last_network_report = 0;

    while (reporter_running) {
        time_t now = time(NULL);

        // Send health report (every 60s or as configured)
        if (now - last_health_report >= 60) {
            send_health_report();
            last_health_report = now;
        }

        // Send network status (at configured interval)
        if (reporter_config.network_status_report_s > 0 &&
            now - last_network_report >= reporter_config.network_status_report_s) {
            send_network_report();
            last_network_report = now;
        }

        sleep(5); // Check every 5 seconds
    }

    LOG_INFO("Remote reporter thread stopped");
    return NULL;
}

static void send_health_report(void) {
    agent_health_t health;
    populate_agent_health(&health);

    char *json = agent_health_to_json_string(&health);
    if (!json) {
        LOG_ERROR("Failed to generate health JSON");
        return;
    }

    int ret = http_post_json(reporter_config.collector_url, json);
    if (ret != 0) {
        LOG_WARN("Failed to send health report to collector");
    } else {
        LOG_DEBUG("Health report sent to collector");
    }

    free(json);
}

static void send_network_report(void) {
    // Read network JSON from file
    FILE *fp = fopen("/tmp/meshmon_network.json", "r");
    if (!fp) {
        LOG_WARN("No network data to report");
        return;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *json = malloc(size + 1);
    if (!json) {
        fclose(fp);
        return;
    }

    fread(json, 1, size, fp);
    json[size] = '\0';
    fclose(fp);

    int ret = http_post_json(reporter_config.collector_url, json);
    if (ret != 0) {
        LOG_WARN("Failed to send network report to collector");
    } else {
        LOG_DEBUG("Network report sent to collector");
    }

    free(json);
}

void remote_reporter_shutdown(void) {
    reporter_running = false;
}
```

**Header file: Phonebook/src/mesh_monitor/remote_reporter.h**

```c
#ifndef REMOTE_REPORTER_H
#define REMOTE_REPORTER_H

#include "mesh_monitor.h"

// Initialize and start remote reporter thread
void* remote_reporter_thread(void *arg);

// Shutdown remote reporter
void remote_reporter_shutdown(void);

#endif // REMOTE_REPORTER_H
```

---

### Phase 3.4: Integration into main.c

```c
// In main.c, after mesh_monitor_init():

static pthread_t remote_reporter_tid = 0;

// Start remote reporter if configured
if (g_monitor_config.network_status_report_s > 0 &&
    strlen(g_monitor_config.collector_url) > 0) {

    LOG_INFO("Starting remote reporter thread...");

    if (pthread_create(&remote_reporter_tid, NULL,
                      remote_reporter_thread,
                      &g_monitor_config) != 0) {
        LOG_ERROR("Failed to create remote reporter thread");
    } else {
        LOG_INFO("Remote reporter thread started");
    }
}

// In shutdown sequence:
if (remote_reporter_tid != 0) {
    remote_reporter_shutdown();
    pthread_join(remote_reporter_tid, NULL);
}
```

---

### Phase 3.5: Crash Report Integration

**Update software_health.c:**

```c
// In crash_signal_handler(), after writing to file:

if (strlen(g_monitor_config.collector_url) > 0) {
    // Send crash report immediately
    crash_report_t report;
    populate_crash_report(&report, sig);

    char *json = crash_report_to_json_string(&report);
    if (json) {
        http_post_json(g_monitor_config.collector_url, json);
        free(json);
    }
}
```

---

## File Structure

```
Phonebook/src/mesh_monitor/
├── http_client.c           (NEW) - HTTP POST implementation
├── http_client.h           (NEW) - HTTP client API
├── remote_reporter.c       (NEW) - Reporter thread
├── remote_reporter.h       (NEW) - Reporter API
├── monitor_config.c        (UPDATE) - Parse collector config
└── mesh_monitor.c          (UPDATE) - Start reporter thread

Phonebook/src/
└── main.c                  (UPDATE) - Initialize reporter

Phonebook/files/etc/
└── sipserver.conf          (UPDATE) - Add collector config
```

---

## Testing Plan

### Unit Testing

**1. Test HTTP Client:**
```bash
# Start test collector
python3 -c "
from http.server import HTTPServer, BaseHTTPRequestHandler
class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        print(f'Received POST: {self.path}')
        content_len = int(self.headers['Content-Length'])
        body = self.rfile.read(content_len)
        print(f'Body: {body.decode()}')
        self.send_response(200)
        self.end_headers()
HTTPServer(('0.0.0.0', 5000), Handler).serve_forever()
"

# Test from agent
curl -X POST http://localhost:5000/ingest \
  -H "Content-Type: application/json" \
  -d @/tmp/meshmon_health.json
```

**2. Test Reporter Thread:**
```bash
# Configure collector
cat >> /etc/sipserver.conf << 'EOF'
network_status_report_s=10
collector_url=http://collector.local.mesh:5000/ingest
EOF

# Restart service
/etc/init.d/AREDN-Phonebook restart

# Watch logs
logread -f | grep REMOTE_REPORTER
```

### Integration Testing

**1. Multi-Node Setup:**
- Install on 3+ nodes
- Configure all to report to same collector
- Verify all nodes appear in collector

**2. Network Partition Test:**
- Disconnect collector
- Verify agents continue local operation
- Reconnect collector
- Verify agents resume reporting

**3. Load Test:**
- 10+ agents reporting every 10s
- Verify collector handles load
- Check for message drops

---

## Performance Impact

**Memory:**
- Reporter thread: ~100KB stack
- HTTP client buffers: ~50KB
- Total: ~150KB additional RAM

**CPU:**
- HTTP POST: <1% CPU per report
- Negligible impact at 40s interval

**Network:**
- Health report: ~1KB every 60s
- Network report: ~5KB every 40s
- Total: ~7KB/min (~10KB/node/min with 10 nodes = 100KB/min total)

---

## Configuration Examples

### Minimal (Remote Disabled)
```ini
[mesh_monitor]
enabled=1
# network_status_report_s not set = disabled
```

### Health Only
```ini
[mesh_monitor]
enabled=1
network_status_report_s=0  # Disabled
collector_url=http://collector:5000/ingest
# Will only send crash reports
```

### Full Reporting
```ini
[mesh_monitor]
enabled=1
network_status_report_s=40
collector_url=http://collector.local.mesh:5000/ingest
```

---

## Rollout Strategy

### Phase 1: Development (Week 1)
- [ ] Implement http_client.c (wget-based)
- [ ] Implement remote_reporter.c
- [ ] Update configuration parsing
- [ ] Unit tests

### Phase 2: Testing (Week 2)
- [ ] Integration into main.c
- [ ] Build and test on dev node
- [ ] Multi-node testing
- [ ] Performance validation

### Phase 3: Beta (Week 3)
- [ ] Deploy to 3-5 beta nodes
- [ ] Run collector for 1 week
- [ ] Monitor for issues
- [ ] Optimize based on feedback

### Phase 4: Production (Week 4+)
- [ ] Document deployment guide
- [ ] Release collector reference implementation
- [ ] Add to main branch
- [ ] Announce to AREDN community

---

## Risk Mitigation

**Risk 1: Collector Unavailable**
- **Impact:** Reports fail, logs fill with errors
- **Mitigation:** Exponential backoff, max retry limit, suppress repeated errors

**Risk 2: Memory Leak in HTTP Client**
- **Impact:** Agent OOM crash
- **Mitigation:** Extensive testing, memory profiling, watchdog monitoring

**Risk 3: JSON Too Large**
- **Impact:** HTTP POST fails or times out
- **Mitigation:** Limit probe_count in network_status, chunk large reports

**Risk 4: Collector Creates Bottleneck**
- **Impact:** Can't scale beyond X nodes
- **Mitigation:** Load balancing, queue-based architecture, horizontal scaling

---

## Success Criteria

- [ ] Agents successfully report to collector
- [ ] No impact on core phonebook functionality
- [ ] Memory usage < 200KB additional
- [ ] CPU impact < 2% average
- [ ] Handles collector downtime gracefully
- [ ] Scales to 50+ nodes reporting to one collector
- [ ] Zero crashes in 1-week beta test

---

## Future Enhancements

**Phase 3.2: Advanced Features**
- Compression (gzip) for large payloads
- HTTPS/TLS support
- Authentication tokens
- Message queuing for offline operation
- Multiple collector support (failover)

**Phase 3.3: Optimization**
- Delta compression (only send changes)
- Batch multiple messages
- Adaptive reporting intervals based on network health
- Priority queuing (crashes first, health last)

---

## References

- **FSD Appendix B:** Complete interface specification
- **probe_engine.c:** Reference for thread implementation
- **software_health.c:** Reference for JSON export
- **AREDNmon-Architecture.md:** Collector design

---

**Implementation Status:** 📋 **PLANNED** (Not Started)

This plan provides a complete roadmap for implementing remote collector reporting. Start with Phase 3.1 (HTTP Client) using the wget-based approach for fastest time to value.
