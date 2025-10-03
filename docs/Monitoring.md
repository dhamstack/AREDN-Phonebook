# Monitoring System - Functional Specification Document

## 1. Overview

The AREDN-Phonebook monitoring system provides comprehensive network and VoIP quality monitoring for AREDN mesh networks. It consists of two integrated subsystems:

1. **VoIP Quality Monitor** - Tests SIP phone reachability and call quality
2. **Mesh Network Monitor** - Measures mesh node connectivity and performance

Both systems run continuously in background threads and export metrics for health monitoring and network analysis.

---

## 2. VoIP Quality Monitoring

### 2.1 Purpose

Monitor registered SIP phones to ensure they are reachable and capable of handling quality voice calls. Tests both SIP signaling and RTP media quality.

### 2.2 Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Quality Monitor Thread                    │
│                  (phone_quality_monitor.c)                   │
└───────────────────────┬─────────────────────────────────────┘
                        │
        ┌───────────────┼───────────────┐
        ▼               ▼               ▼
   ┌─────────┐    ┌─────────┐    ┌─────────┐
   │ Phone 1 │    │ Phone 2 │    │ Phone N │
   │ Test    │    │ Test    │    │ Test    │
   └─────────┘    └─────────┘    └─────────┘
        │               │               │
        └───────────────┴───────────────┘
                        ▼
              ┌──────────────────┐
              │ Results Storage  │
              │ JSON Export      │
              └──────────────────┘
```

### 2.3 Test Phone Simulation

The quality monitor simulates a real SIP phone to make calls through the server:

**Test Phone Configuration:**
- IP Address: Configurable via `SIP_TEST_PHONE_IP` environment variable (default: server_ip + 1)
- Port: Random ephemeral port (not 5060)
- Phone Number: 999901 (reserved test number)

**Call Flow:**
```
1. Test Phone (10.184.177.227:random)
   ↓ INVITE sip:441530@localnode.local.mesh:5060
2. SIP Server (10.184.177.226:5060)
   ↓ DNS lookup: 441530.local.mesh → 10.197.143.20
   ↓ Creates CallSession
3. Real Phone (10.197.143.20:5060 via registered Contact)
   ↓ 100 Trying / 180 Ringing / 200 OK
4. SIP Server (forwards responses)
   ↓
5. Test Phone (measures quality)
```

### 2.4 Test Procedure

Each phone undergoes a multi-step quality test:

#### Step 1: DNS Resolution
- Resolve `{phone_number}.local.mesh` to IP address
- Skip phone if DNS fails (phone offline/unreachable)

#### Step 2: OPTIONS Ping
- Send `OPTIONS` request to verify SIP responsiveness
- Measure SIP RTT (Round-Trip Time)
- Fail if no 200 OK within timeout (default: 5s)

#### Step 3: INVITE Test Call
- Send `INVITE` through server with:
  - Request-URI: `sip:{phone_number}@localnode.local.mesh:5060`
  - From: `<sip:999901@localnode.local.mesh:5060>`
  - To: `<sip:{phone_number}@localnode.local.mesh:5060>`
  - Clean SDP offer (PCMU/8000, ptime:40)
  - No auto-answer headers (clean test)
- Wait for 100/180/200 responses
- Measure INVITE RTT

#### Step 4: RTP Media Test
- Establish RTP session on ephemeral port
- Send PCMU packets (40ms ptime) for duration
- Receive and analyze return RTP packets
- Calculate:
  - Media RTT (round-trip time)
  - Jitter (packet timing variation)
  - Packet loss percentage
  - MOS score (Mean Opinion Score, 1-5 scale)

#### Step 5: Call Teardown
- Send BYE request
- Clean up call session

### 2.5 Configuration

Configuration parameters in `quality_monitor_config_t`:

```c
typedef struct {
    int enabled;                    // Enable/disable monitoring
    int test_interval_sec;          // Interval between test cycles (default: 300s)
    int cycle_delay_sec;            // Delay between testing phones (default: 1s)
    voip_probe_config_t probe_config;  // Timeouts and test duration
} quality_monitor_config_t;
```

Probe configuration in `voip_probe_config_t`:

```c
typedef struct {
    int options_timeout_ms;    // OPTIONS timeout (default: 5000ms)
    int invite_timeout_ms;     // INVITE timeout (default: 5000ms)
    int rtp_duration_ms;       // RTP test duration (default: 3000ms)
    int rtp_packet_interval_ms; // RTP packet interval (default: 40ms)
} voip_probe_config_t;
```

### 2.6 Test Results

Results are stored in `voip_probe_result_t`:

```c
typedef struct {
    voip_probe_status_t status;     // VOIP_PROBE_SUCCESS or error code
    char status_reason[128];         // Human-readable status

    // SIP Metrics
    long options_rtt_ms;             // OPTIONS round-trip time
    long sip_rtt_ms;                 // INVITE round-trip time

    // RTP Metrics
    long media_rtt_ms;               // RTP round-trip time
    double jitter_ms;                // Jitter in milliseconds
    double loss_fraction;            // Packet loss (0.0 - 1.0)
    uint32_t packets_sent;           // Total RTP packets sent
    uint32_t packets_lost;           // Packets lost

    // Quality Score
    double mos_score;                // MOS 1.0-5.0 (calculated)
} voip_probe_result_t;
```

### 2.7 Results Export

Quality results are exported to `/tmp/phone_quality.json`:

```json
{
  "timestamp": 1759491234,
  "phones": [
    {
      "phone_number": "441530",
      "phone_ip": "10.197.143.20",
      "status": "SUCCESS",
      "options_rtt_ms": 12,
      "sip_rtt_ms": 45,
      "media_rtt_ms": 89,
      "jitter_ms": 2.3,
      "loss_percent": 0.5,
      "packets_sent": 75,
      "packets_lost": 0,
      "mos_score": 4.2
    }
  ]
}
```

### 2.8 Response Queue Architecture

To prevent race conditions on the shared SIP socket, quality tests use a response queue:

- **Main SIP Thread**: Routes 200 OK responses to quality monitor queue
- **Quality Monitor Thread**: Dequeues responses for ongoing tests
- **Queue Size**: 10 slots
- **Thread-Safe**: Mutex-protected access

```
SIP Socket → Main Thread → Triage Logic
                              ├─→ Regular Call → CallSession
                              └─→ Quality Test Response → Response Queue
                                                              ↓
                                    Quality Monitor Thread ←──┘
```

### 2.9 Status Values

```c
typedef enum {
    VOIP_PROBE_SUCCESS = 0,      // Test completed successfully
    VOIP_PROBE_OPTIONS_TIMEOUT,  // No OPTIONS response
    VOIP_PROBE_SIP_TIMEOUT,      // No INVITE response
    VOIP_PROBE_MEDIA_TIMEOUT,    // No RTP packets received
    VOIP_PROBE_SIP_ERROR,        // SIP protocol error
    VOIP_PROBE_MEDIA_ERROR       // RTP/media error
} voip_probe_status_t;
```

---

## 3. Mesh Network Monitoring

### 3.1 Purpose

Monitor connectivity and performance between AREDN mesh nodes to detect network issues, measure latency, and track mesh health.

### 3.2 Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                  Mesh Monitor Thread                         │
│                   (mesh_monitor.c)                           │
└───────────────────┬─────────────────────────────────────────┘
                    │
        ┌───────────┼───────────┐
        ▼           ▼           ▼
   ┌─────────┐ ┌─────────┐ ┌─────────┐
   │ Agent 1 │ │ Agent 2 │ │ Agent N │
   │ Probe   │ │ Probe   │ │ Probe   │
   └─────────┘ └─────────┘ └─────────┘
        │           │           │
        └───────────┴───────────┘
                    ▼
          ┌──────────────────┐
          │ Health Metrics   │
          │ JSON Export      │
          └──────────────────┘
```

### 3.3 Agent Discovery

Mesh agents are discovered via:

1. **Static Configuration** (`/etc/meshmon_agents.conf`)
2. **Dynamic Discovery** (optional future feature)

Agent configuration format:
```
# Format: node_name mesh_ip lan_ip
HB9BLA-HAP-2 10.198.102.253 10.51.55.233
HB9ABC-ROUTER 10.45.12.45 10.45.12.1
```

### 3.4 Probe Protocol

#### UDP Echo Probe Packet

```c
typedef struct {
    uint32_t magic;           // 0x4D455348 ("MESH")
    uint32_t version;         // Protocol version (1)
    uint32_t seq;             // Sequence number (0-9)
    char src_node[64];        // Source node name
    char return_ip[64];       // Return IP address
    uint16_t return_port;     // Return UDP port
    uint64_t send_timestamp;  // Send time (microseconds)
} probe_packet_t;
```

#### Probe Process

1. **Send Phase**: Send 10 UDP packets (seq 0-9) to agent's LAN IP:40050
2. **Echo Phase**: Agent echoes packets back to return IP:port
3. **Receive Phase**: Calculate RTT and loss for each received packet
4. **Metrics Phase**: Aggregate results (avg RTT, loss %)

### 3.5 Probe Responder

Each node also runs a **probe responder** thread:

- Listens on UDP port 40050
- Receives probe packets from other agents
- Echoes packets back to `return_ip:return_port`
- Validates packet structure (magic, size)

This allows **bidirectional probing** between mesh nodes.

### 3.6 Health Metrics

Mesh monitoring exports metrics to `/tmp/meshmon_health.json`:

```json
{
  "timestamp": 1759491234,
  "local_node": "HB9BLA-VM-1",
  "local_ip": "10.197.143.17",
  "agents": [
    {
      "node_name": "HB9BLA-HAP-2",
      "mesh_ip": "10.198.102.253",
      "lan_ip": "10.51.55.233",
      "status": "reachable",
      "rtt_ms": 12.5,
      "loss_percent": 0.0,
      "last_probe": 1759491230
    }
  ],
  "software_health": {
    "uptime_seconds": 12345,
    "thread_count": 8,
    "memory_kb": 45678
  }
}
```

### 3.7 Configuration

Mesh monitor configuration in `mesh_monitor_config_t`:

```c
typedef struct {
    int enabled;                      // Enable monitoring
    int probe_interval_s;             // Probe interval (default: 40s)
    int probe_timeout_ms;             // Probe timeout (default: 5000ms)
    int probe_count;                  // Packets per probe (default: 10)
    int responder_port;               // Listen port (default: 40050)
    char collector_url[256];          // Remote collector URL
    int network_status_report_s;      // Report interval (0=disabled)
} mesh_monitor_config_t;
```

### 3.8 Remote Reporting (Optional)

Mesh monitors can report to a central collector:

- HTTP POST to `collector_url`
- JSON payload with full health metrics
- Interval: `network_status_report_s`
- Authentication: Bearer token (if configured)

---

## 4. Integration with Main Server

### 4.1 Thread Architecture

```
Main Process
  ├─ Main Thread (SIP message handling)
  ├─ Phonebook Fetcher Thread
  ├─ Status Updater Thread
  ├─ Quality Monitor Thread ← VoIP monitoring
  ├─ Mesh Monitor Thread    ← Network monitoring
  ├─ Mesh Responder Thread  ← Echo responder
  └─ Remote Reporter Thread ← Optional reporting
```

### 4.2 Initialization Sequence

```c
// 1. Create SIP socket and bind
int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
bind(sockfd, ...);

// 2. Initialize quality monitor
char server_ip[64];
get_server_ip(server_ip, sizeof(server_ip));
quality_monitor_init(sockfd, server_ip);
quality_monitor_start();

// 3. Initialize mesh monitor
mesh_monitor_init(NULL);  // NULL = use default config
pthread_create(&mesh_monitor_tid, NULL, mesh_monitor_thread, NULL);

// 4. Main SIP processing loop
while (1) {
    recvfrom(sockfd, buffer, ...);
    process_incoming_sip_message(...);
}
```

### 4.3 SIP Message Triage

The main SIP processing thread routes messages:

```c
void process_incoming_sip_message(...) {
    // Check if message is for quality monitor
    if (is_quality_monitor_response(call_id_hdr)) {
        route_to_quality_monitor(buffer);
        return;
    }

    // Normal SIP call handling
    if (strcmp(method, "INVITE") == 0) {
        handle_invite(...);
    }
    // ...
}
```

---

## 5. Configuration Files

### 5.1 SIP Server Configuration

File: `/etc/aredn-phonebook/sipserver.conf`

```ini
[general]
pb_interval_seconds=3600
status_update_interval_seconds=300

[quality_monitor]
enabled=1
test_interval_sec=300
cycle_delay_sec=1
options_timeout_ms=5000
invite_timeout_ms=5000
rtp_duration_ms=3000

[mesh_monitor]
enabled=1
probe_interval_s=40
probe_timeout_ms=5000
collector_url=http://collector.local.mesh/api/health
network_status_report_s=300
```

### 5.2 Mesh Agents Configuration

File: `/etc/meshmon_agents.conf`

```
# Mesh monitoring agents
# Format: node_name mesh_ip lan_ip
HB9BLA-HAP-2 10.198.102.253 10.51.55.233
HB9ABC-NODE 10.45.12.45 10.45.12.1
```

---

## 6. API and Data Access

### 6.1 Local JSON Files

Monitoring data is exported to local files:

- **VoIP Quality**: `/tmp/phone_quality.json`
- **Mesh Health**: `/tmp/meshmon_health.json`

These files are updated after each monitoring cycle.

### 6.2 Web Access (via CGI)

The exported JSON files can be served via the built-in web server:

```
http://node.local.mesh/cgi-bin/phone_quality
http://node.local.mesh/cgi-bin/meshmon_health
```

### 6.3 Remote Collector API

Mesh monitors can POST to a remote collector:

```
POST /api/health HTTP/1.1
Host: collector.local.mesh
Content-Type: application/json
Authorization: Bearer {token}

{JSON health data}
```

---

## 7. Error Handling and Logging

### 7.1 Log Levels

All monitoring components use the standard logging framework:

```c
LOG_ERROR()   // Critical errors
LOG_WARN()    // Warnings (timeouts, failures)
LOG_INFO()    // Normal operations, test results
LOG_DEBUG()   // Detailed debugging (disabled in production)
```

### 7.2 Common Error Scenarios

**VoIP Quality Monitoring:**
- Phone offline → Skip (DNS failure)
- OPTIONS timeout → Log warning, continue
- INVITE timeout → Mark as failed, continue
- Bind failure (test phone IP) → Fall back to direct test

**Mesh Monitoring:**
- Agent unreachable → Mark as down, report 100% loss
- Responder not running → No echo packets received
- Network partition → Multiple agents down

### 7.3 Health Monitoring

Software health is tracked for all threads:

- Thread aliveness check (30s timeout)
- Deadlock detection
- Memory usage monitoring
- Exported in `meshmon_health.json`

---

## 8. Performance Considerations

### 8.1 Resource Usage

**VoIP Quality Monitor:**
- CPU: Low (background tests every 5 minutes)
- Memory: ~50KB per test (RTP buffers, packet storage)
- Network: ~100KB per phone test (SIP + RTP traffic)

**Mesh Monitor:**
- CPU: Very low (simple UDP echo probes)
- Memory: ~10KB per agent
- Network: ~1KB per probe cycle (10 packets × 100 bytes)

### 8.2 Scalability

**Phone Monitoring:**
- Tested with: 200+ phones
- Cycle time: ~200s for 200 phones (1s delay between)
- Bottleneck: Sequential testing (one phone at a time)

**Mesh Monitoring:**
- Tested with: 50+ agents
- Cycle time: ~2s per agent (10 probes + processing)
- Can be parallelized for faster cycles

### 8.3 Network Impact

- VoIP tests generate ~3s of PCMU call traffic per phone
- Mesh probes are lightweight (10 × 100-byte UDP packets)
- All tests run on background threads (non-blocking)

---

## 9. Future Enhancements

### 9.1 Planned Features

1. **Parallel Phone Testing**
   - Test multiple phones simultaneously
   - Reduce cycle time for large deployments

2. **Test Phone Auto-Answer**
   - Add `Call-Info: ;answer-after=0` for silent tests
   - Requires phone policy gate validation first

3. **Mesh Agent Auto-Discovery**
   - Discover agents via OLSR/Batman routing tables
   - No manual configuration needed

4. **Historical Metrics**
   - Store time-series data (SQLite or InfluxDB)
   - Trend analysis and alerting

5. **Web Dashboard**
   - Real-time monitoring UI
   - Alert notifications
   - Historical graphs

### 9.2 Known Limitations

1. **Test Phone IP Binding**
   - Requires IP to exist on system interface
   - Use `SIP_TEST_PHONE_IP` env var to override

2. **Multi-Homed Hosts**
   - Server bound to INADDR_ANY may cause source IP mismatch
   - Future: Bind to specific trusted IP

3. **Sequential Phone Testing**
   - Tests one phone at a time (slow for large networks)
   - Future: Parallel testing

4. **No Authentication**
   - Quality tests use reserved number (999901)
   - No SIP authentication required
   - Future: Support authenticated calls

---

## 10. Troubleshooting

### 10.1 VoIP Quality Tests Failing

**Symptom**: All phones timeout or fail SIP_ERROR

**Possible Causes:**
1. Test phone IP binding fails
   - Solution: Set `SIP_TEST_PHONE_IP` env var to valid IP

2. Phones reject INVITE from untrusted source
   - Solution: Verify packet source IP matches trusted server IP

3. Server not routing calls properly
   - Solution: Check DNS resolution, CallSession creation

**Debug Commands:**
```bash
export SIP_DEBUG=1
/etc/init.d/AREDN-Phonebook restart
logread -f | grep QUALITY
```

### 10.2 Mesh Probes Not Working

**Symptom**: All agents show 100% packet loss

**Possible Causes:**
1. Responder not running on remote agent
   - Solution: Ensure AREDN-Phonebook running on all agents

2. Firewall blocking UDP port 40050
   - Solution: Check iptables, allow UDP 40050

3. Incorrect agent IPs in config
   - Solution: Verify `/etc/meshmon_agents.conf`

**Debug Commands:**
```bash
logread -f | grep PROBE_ENGINE
cat /tmp/meshmon_health.json | jq .
```

### 10.3 Logs Not Showing Tests

**Symptom**: No QUALITY or PROBE logs appear

**Possible Causes:**
1. Monitoring disabled in config
   - Solution: Check `sipserver.conf`, set `enabled=1`

2. Server not starting (old version)
   - Solution: Check `--version`, reinstall latest `.ipk`

3. Thread crashed
   - Solution: Check for ERROR logs, core dumps

---

## 11. Source Code Reference

### 11.1 VoIP Quality Monitoring

| File | Purpose |
|------|---------|
| `phone_quality_monitor/phone_quality_monitor.c` | Main quality monitor thread |
| `phone_quality_monitor/phone_quality_monitor.h` | Configuration and API |
| `sip_quality_lib.c` | SIP/RTP test implementation |
| `sip_quality_lib.h` | Test API and data structures |

### 11.2 Mesh Network Monitoring

| File | Purpose |
|------|---------|
| `mesh_monitor/mesh_monitor.c` | Main monitor thread |
| `mesh_monitor/probe_engine.c` | UDP probe send/receive |
| `mesh_monitor/agent_discovery.c` | Agent config parsing |
| `mesh_monitor/health_reporter.c` | Metrics export |
| `mesh_monitor/remote_reporter.c` | Remote collector POST |

### 11.3 Integration Points

| File | Integration |
|------|-------------|
| `main.c` | Initialize monitors, start threads |
| `sip_core/sip_core.c` | Triage SIP responses to quality monitor |
| `common.h` | Shared data structures |

---

## 12. Version History

| Version | Date | Changes |
|---------|------|---------|
| 2.1.0 | 2025-10-03 | Initial monitoring system implementation |
| | | - VoIP quality monitoring with test phone simulation |
| | | - Mesh network monitoring with UDP probes |
| | | - Response queue for race condition prevention |
| | | - JSON metrics export |

---

## Appendix A: Configuration Examples

### Full sipserver.conf Example

```ini
[general]
pb_interval_seconds=3600
status_update_interval_seconds=300

[phonebook_servers]
server1_host=localnode.local.mesh
server1_port=80
server1_path=/phonebook.csv

[quality_monitor]
enabled=1
test_interval_sec=300
cycle_delay_sec=1
options_timeout_ms=5000
invite_timeout_ms=5000
rtp_duration_ms=3000
rtp_packet_interval_ms=40

[mesh_monitor]
enabled=1
probe_interval_s=40
probe_timeout_ms=5000
probe_count=10
responder_port=40050
network_status_report_s=300
collector_url=http://collector.local.mesh/api/health
```

### Test Phone IP Override

```bash
# /etc/init.d/AREDN-Phonebook
export SIP_TEST_PHONE_IP=10.228.113.126
/usr/bin/AREDN-Phonebook
```

---

*End of Monitoring System FSD*
