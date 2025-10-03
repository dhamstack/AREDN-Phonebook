# Mesh Monitoring System — Architecture Specification

**Purpose:** High-level architecture for a **lightweight mesh monitoring system** consisting of C-based agents on AREDN nodes and a Python-based collector on Raspberry Pi. Focus is on **actionable issues**, not raw metrics.

**Document Relationship:**
- **This document (AREDNmon-Architecture.md):** System architecture, design decisions, data flows, collector/UI design
- **AREDN-Phonebook-With-Monitoring-FSD.md:** Detailed agent implementation specification (AREDN-Phonebook integration, message schemas, CGI endpoints, configuration)

**Note:** For agent implementation details, refer to `AREDN-Phonebook-With-Monitoring-FSD.md`

---

## 1) Objectives & Scope
- **Primary goal:** Detect and present **network issues** (what/where/how-bad/likely-cause/next-step) for an AREDN mesh running mainly VoIP.
- **Scope:**  
  - Router **Agent** (C, OpenWrt): probing + local route/link introspection + JSON reporting.  
  - **Collector/Analyzer** (Python, Raspberry Pi): ingestion, storage, issue detection, API for UI.  
  - **UI**: Render within **OpenWISP** via a lightweight “Issues” panel that consumes the Pi API.  
  - **Database:** **PostgreSQL + TimescaleDB** (single DB for time-series + metadata).  
- **Out of scope (for now):** security/auth (closed network), long-term historical analytics beyond retention policy, automated remediation.

---

## 2) Success Criteria (Acceptance)
- Within **5 minutes** of deployment on ≥5 nodes, the UI shows **Overall Health** and any **open issues** (if present).
- For a deliberately degraded link, an **issue** is raised identifying **link scope**, **impact**, **likely cause**, and **next steps** within **2 probe cycles**.
- Added probe traffic remains **negligible** and does **not** degrade the mesh (see §8).

---

## 3) System Overview

### 3.1 Agent Responsibilities (Router)

**Agents** run on participating routers. They are **stateless data producers**:
- Autodetect routing daemon (**OLSR** or **Babel**)
- Read **neighbors** (LQ, NLQ, ETX) and **current route/hops** locally (localhost; no mesh chatter)
- Send lightweight **UDP probe windows** to selected peers (auto-discovered participants)
- Compute **per-window** metrics: **RTT, jitter (RFC3550), loss**
- Package with **hop list** + **link qualities** in self-contained messages
- POST results as **JSON** to the Pi collector
- **No retention:** Agent does not store history, compute baselines, or detect issues

**Agent limitations (by design):**
- No statistical analysis
- No trend tracking
- No issue detection
- No historical data storage
- Constant RAM usage regardless of uptime

### 3.2 Collector Responsibilities (Raspberry Pi)

**Collector/Analyzer** is the **stateful intelligence layer**:
- Receives JSON via **HTTP POST** (no TLS/auth required)
- Stores into **TimescaleDB** hypertables + relational tables (all history)
- Computes **baselines, trends, and statistical aggregates**
- Runs **Issue Engine** (rule-based) that opens/closes incidents
- Performs **pattern analysis** (crash patterns, bottleneck detection, etc.)
- Manages **node lifecycle** (last_seen, active/inactive/dead states)
- Exposes **REST API** for issues, nodes, crashes, and statistics
- Embedded into **OpenWISP** UI via a small panel (no separate portal)

**Separation of concerns:**
- **Agent:** Observe and report current state (lightweight, stateless)
- **Collector:** Analyze, store, and detect issues (heavyweight, stateful)

---

## 4) Functional Requirements

### 4.1 Issues (what the user sees)
- **Overall Health**: Green / Yellow / Red (computed from open incidents’ severities/impacts).
- **Issues list** (default view): cards with  
  - Title, Scope (link/node/route), Severity, Impact (e.g., routes affected), Start time, **Likely cause**, **Next steps**, Evidence summary.  
- **Details drawer** (per issue): timeline, affected routes, pattern (burst vs steady), references to involved nodes/links, and a short playbook.
- **Route highlight**: “Show routes” opens topology view (OpenWISP) with affected path highlighted.

### 4.2 Agent (Router)

**Design principle:** Agent is **stateless** for data retention. All historical data and statistics are managed by the collector. Agent only retains minimal operational state needed to generate current reports.

- **Auto-discovery of participants**:
  - Build peer set from routing daemon tables and optional Pi **registry** endpoint.
  - Only probe **participating** nodes (those running the agent), to ensure responses and minimize noise.
- **Routing data introspection**:
  - **OLSR**: query local jsoninfo (e.g., `127.0.0.1:9090`) for **neighbors LQ/NLQ/ETX** and **current route** to targets.
  - **Babel**: read local babeld socket (e.g., `/var/run/babeld.sock`) for neighbor metrics and selected routes.
  - **Tie every probe** to the **actual hop list** at send time.
- **Probing**:
  - **Window**: default **5 s** of UDP packets at ~20 pps (voice-like timing).
  - **Interval**: default **40 s** between windows (staggered per node).
  - **Target set per window**: **2 neighbors + 1 rotating peer** from participant set.
  - **Compute**: per-target **loss %, avg RTT (if echoed), RFC3550 jitter**, reorder % (optional).
  - **Per-window calculation only** - no historical tracking, no baselines, no trend analysis.
- **Reports**:
  - POST **JSON messages** to Pi `/ingest` immediately after computation.
  - Each message is **self-contained** with all context needed by collector.
  - Include **agent health** (phonebook health status) every 4 hours OR on significant change (event-driven).
- **No data retention**:
  - Agent does **not** store probe history, statistics, or trends.
  - Agent does **not** perform issue detection or analysis.
  - Agent does **not** track baselines or compute deltas over time.
  - **Only exception:** Minimal buffering (3-5 messages) when collector unreachable.

### 4.3 Collector/Analyzer (Pi)
- **Ingestion**: Accept JSON (`path_result`, `hop_result`, `agent_health`, `crash_report`). Validate schema and timestamps, insert into DB.
- **Node lifecycle management**:
  - Update `last_seen` timestamp on any message from node
  - Mark nodes inactive after 30 minutes of silence
  - Remove from UI after 24 hours (data retained in DB per retention policy)
- **Issue Engine** (rules below) evaluates sliding windows to **open**, **update**, and **close** incidents.
- **APIs**:
  - `GET /issues?status=open&since=X` — list (for UI panel)
  - `GET /issues/{id}` — details
  - `GET /issues/by-object?link=A--B | node=N | route=src,dst`
  - `GET /nodes?status=active` — list of active nodes (last_seen < 30 min)
  - `GET /nodes/{id}` — node details (location, version, health, crash history)
  - `GET /crashes?node={id}&since=X` — crash reports for node
  - `GET /crashes/patterns?timeframe=7d` — crash pattern analysis
- **OpenWISP UI**: simple panel that calls `/issues` and `/nodes` APIs, integrates with topology view for highlighting.

---

## 5) Data Model

### 5.1 Message Architecture

**Design principle:** Separate **network monitoring** data from **agent health** data due to different update frequencies and purposes.

- **Network messages** (`path_result`, `hop_result`): Report network status, sent every 40s during active probing
- **Phonebook health messages** (`agent_health`): Report phonebook software status, sent every 4 hours OR on significant change (event-driven)

**Rationale for separation:**
- Network conditions change rapidly (40s interval) while phonebook health changes slowly (4h baseline + events)
- Event-driven health reporting: ~97% bandwidth reduction vs. fixed intervals while maintaining responsiveness
- Avoids sending redundant health data with every network probe
- Allows independent control of reporting frequencies
- Enables graceful degradation (e.g., disable probing but keep health reporting)

### 5.2 JSON from Agent → Pi (wire contract)

**Path result (one per dst per window)**
```json
{
  "schema": "meshmon.v1",
  "type": "path_result",
  "src": "node-A",
  "dst": "node-K",
  "sent_at": "2025-09-29T18:41:05Z",
  "window_s": 5,
  "route": ["node-A","node-D","node-H","node-K"],
  "metrics": { "rtt_ms_avg": 72.3, "jitter_ms_rfc3550": 11.8, "loss_pct": 0.9 },
  "neighbors_seen": 5
}
```

**Hop quality (per hop observed for that path/window)**

**Core fields:** `hop` object (from, to), `l2` object (lq, nlq, etx)

**Link technology detection:** `interface` (e.g., wlan0, tun50), `link_type` (RF, tunnel, ethernet) for bottleneck analysis

**Full schema:** See `AREDN-Phonebook-With-Monitoring-FSD.md` Section 7.3-7.4

**Agent health (event-driven + 4-hour baseline)**

**Note:** "agent_health" represents **Phonebook Health Status** - health of the AREDN-Phonebook SIP proxy application.

**Reporting:** Every 4 hours baseline OR immediately on significant change (CPU >20%, memory >10MB, thread unresponsive, restart)

**Core fields:** `cpu_pct`, `mem_mb`, `queue_len`, `uptime_seconds`, `restart_count`, `threads_responsive`, `health_score`

**Extended fields:** `checks` object (memory_stable, no_recent_crashes, sip_service_ok, phonebook_current), `sip_service` object (active_calls, registered_users), `monitoring` object (probe_queue_depth, last_probe_sent)

**Full schema:** See `AREDN-Phonebook-With-Monitoring-FSD.md` Section 6.4.1

**Crash report (sent immediately on restart after crash)**
```json
{
  "schema": "meshmon.v1",
  "type": "crash_report",
  "node": "node-A",
  "sent_at": "2025-09-29T18:45:00Z",
  "crash_time": "2025-09-29T18:42:13Z",
  "signal": 11,
  "signal_name": "SIGSEGV",
  "description": "Segmentation fault",
  "thread_id": "fetcher",
  "last_operation": "phonebook_fetch",
  "uptime_at_crash": 3600,
  "memory_at_crash_mb": 8.2,
  "cpu_at_crash_pct": 12.5,
  "crash_count_24h": 2,
  "backtrace": [
    "phonebook_fetcher.c:142",
    "pthread_start:0x7f4a2",
    "clone:0x4e4d0"
  ],
  "context": {
    "active_calls": 0,
    "pending_probes": 3,
    "last_successful_fetch": "2025-09-29T18:30:00Z",
    "mesh_utilization": 0.45
  }
}
```

**Note:** `neighbors_seen` moved to `path_result` as it represents a network observation (routing topology visibility) rather than agent software health.

### 5.3 Database (single Postgres + TimescaleDB)
- **Hypertables**
  - `path_probe(time, src, dst, route_json, rtt_ms, jitter_ms, loss_pct, window_s, neighbors_seen)`
  - `hop_quality(time, from_node, to_node, lq, nlq, etx)`
  - `agent_health(time, node, cpu_pct, mem_mb, queue_len, uptime_seconds, restart_count, threads_responsive, health_score)`
  - `crash_reports(time, node, crash_time, signal, signal_name, description, thread_id, last_operation, uptime_at_crash, memory_at_crash_mb, cpu_at_crash_pct, crash_count_24h, backtrace_json, context_json)`
- **Relational tables**
  - `incidents(id, kind, scope_json, severity, impact_json, cause, next_steps, evidence_json, started_at, ended_at)`
  - `nodes(id, name, lat, lon, elevation_m, last_seen, first_seen, version, status, crash_history_summary)`
    - `status`: 'active' (seen < 30 min), 'inactive' (30 min - 24h), 'dead' (> 24h, removed from UI)
  - `links(id, a, b, link_type, tech, channel, notes)` *(optional for planning)*
    - `link_type`: 'rf' or 'tunnel'
- **Retention**
  - Raw hypertables: **90 days** (compressed after 7 days).
  - **Crash reports**: **1 year** (critical for debugging patterns).
  - Optional **continuous aggregates** for hour/day rollups kept **1 year**.

---

## 6) Issue Detection (Rule Set v1)

**General approach:** Sliding-window evaluation with **baseline vs now** comparisons where applicable. Incidents have lifecycle: **open → (update) → close**.

1) **RF Quality Drop (Link)**  
   - **Trigger:** ETX rising and `(LQ < 0.70 OR NLQ < 0.70)` averaged over **10 min**, compared to preceding **60 min** baseline.  
   - **Scope:** link (A↔B). **Severity:** major/critical depending on deviation.  
   - **Likely cause:** alignment/SNR/obstruction.  
   - **Next steps:** check alignment/LOS; consider channel change/narrowing.

2) **Jitter Burst (Path)**  
   - **Trigger:** `jitter_p95 > 30 ms` for **3 consecutive** probe windows.  
   - **Scope:** route (src→dst).  
   - **Likely cause:** congestion or interference.  
   - **Next steps:** reduce channel width; enable airtime fairness; schedule throughput micro-tests off-peak.

3) **Loss Spike (Path)**  
   - **Trigger:** `loss_pct > 2%` for **2 consecutive** windows OR `>1%` sustained **10 min**.  
   - **Scope:** route.  
   - **Next steps:** inspect RF metrics; check queue/buffer on involved hops.

4) **Route Flapping (Path)**  
   - **Trigger:** ≥ **3 distinct hop lists** for same src→dst within **5 min**.  
   - **Scope:** route; **Evidence:** hop sequence history.  
   - **Next steps:** stabilize weakest common hop; evaluate link priorities/policies.

5) **Bottleneck Link (Network-wide)**  
   - **Trigger:** Same hop appears as **worst hop** in ≥ **5** *bad* path_results in **30 min**.  
   - **Scope:** link.  
   - **Next steps:** add alt link, reposition antennas, change channel.

6) **Node Overload (Optional)**
   - **Trigger:** `cpu_pct > 80%` coincident with degradations on paths through the node.
   - **Scope:** node.
   - **Next steps:** reduce services; upgrade hardware.

7) **Repeated Crashes (Stability)**
   - **Trigger:** ≥ **2 crashes** in **24 hours** OR ≥ **3 crashes** in **7 days** for same node.
   - **Scope:** node.
   - **Severity:** major (impacts reliability).
   - **Likely cause:** software bug, memory corruption, hardware failure, environmental (temperature).
   - **Next steps:** review crash reports for common thread/operation; check logs; upgrade firmware; inspect hardware/power.
   - **Evidence:** Crash signal types, affected threads, memory levels at crash.

**Impact estimation:** number of affected routes in last **30–60 min** and optional "estimated users" mapping if available from site metadata.

**Closure:** incident closes after **stable** conditions for **15 min** (configurable). Crash incidents close after **7 days** without new crashes.

---

## 7) Probing Strategy & Load Budget
- **Coverage:** each node probes **2 neighbors + 1 rotating peer** every **40 s**; 5 s per peer.
- **Traffic per probe:** ≈ 64 kbps during the 5 s window (20 pps, small UDP payload).
- **Mesh-wide load:** windows **staggered** by node ID to avoid bursts; overall added traffic remains minimal even at dozens of nodes.
- **Adaptive backoff:** if congestion is detected network-wide, reduce rotating peers to 0 temporarily.

### 7.1 Resource-Constrained Environment Strategies

**Constraint awareness:**
- Target devices: **≤12 MB RAM**, low CPU (400-700 MHz MIPS/ARM), limited flash writes
- Mesh bandwidth: **1-10 Mbps typical**, high latency (50-200ms multi-hop)
- VoIP priority: monitoring must not disrupt voice calls
- **No data retention on agent:** Statistics, baselines, and trend analysis done by collector only

**Event-driven health reporting:**

```
Scheduled baseline:   agent_health every 4 hours (heartbeat)
Event-driven:         agent_health immediately on:
                      - CPU change > 20%
                      - Memory change > 10 MB
                      - Thread becomes unresponsive
                      - Restart occurred
                      - Health score drops significantly
```

**Benefits:** Immediate notification of problems while reducing bandwidth ~97% compared to fixed 40s intervals.

Agent dynamically adjusts reporting frequency based on:
- Software health score (from health monitoring system)
- Active VoIP call count
- Mesh utilization estimation

**Adaptive probe intensity:**

| Mode | Condition | Probe Frequency | Bandwidth |
|------|-----------|-----------------|-----------|
| DISABLED | Agent offline/overloaded | 0 probes | 0 |
| MINIMAL | Active VoIP calls > 0 | 1 neighbor/2 min | ~10 kbps |
| LIGHT | High mesh utilization (>70%) | 2 neighbors/40s | ~32 kbps |
| FULL | Normal operation | 2 neighbors + 1 peer/40s | ~64 kbps |

**Zero-copy memory management:**
- Pre-allocate message buffers (no malloc/free per message)
- Reuse buffers for encoding and transmission
- Circular buffer for unsent reports (max 3-5 entries when collector unreachable)
- **No historical data storage** - agent RAM usage is constant regardless of uptime

**Smart collector communication:**
- Detect collector unreachability via POST failures
- Exponential backoff: 5min → 15min → 30min on consecutive failures
- Discard oldest buffered reports when buffer full (no unbounded growth)
- Resume normal interval after successful POST

**Integration with VoIP operations:**
- Active call detection reduces probe intensity (minimal network overhead during calls)
- Implementation details: See `AREDN-Phonebook-With-Monitoring-FSD.md` Section 11 (Emergency Operation Modes)

**Flash wear protection:**
- **No graceful shutdowns:** Devices are unplugged without warning
- Health state operates entirely in **RAM** (`/tmp` on OpenWrt)
- Crash state written to `/tmp/meshmon_crash.bin` (RAM-backed, survives crash but not reboot)
- Persistent state (coordinates, last fetch time) written to `/tmp` every 5 minutes
- **No flash writes** during normal operation
- Post-reboot: Agent starts fresh, reports current state within 5 minutes

---

## 8) Crash Reporting System

**Goal:** Capture diagnostic information when agent crashes to enable debugging without impacting mesh performance.

**Design Principles:**
- Flash-friendly: all state in `/tmp` (RAM), no flash writes
- Lightweight: signal-safe crash handlers
- Immediate reporting: crash_report sent on next startup
- Breadcrumb tracking: thread-local operation markers for context

**Key Implementation Details:**
- Signal handlers for SIGSEGV, SIGBUS, SIGFPE, SIGABRT, SIGILL
- Crash state written to `/tmp/meshmon_crash.bin` (survives crash, not reboot)
- Post-restart: agent loads crash data, generates crash_report message, sends to collector
- Collector analyzes patterns: repeated crashes, signal distribution, memory correlation

**Full implementation specification:** See `AREDN-Phonebook-With-Monitoring-FSD.md` Section 3 (software_health.c integration)

### 8.1 Collector Crash Analysis

**Collector receives crash reports and:**

1. Stores in `crash_reports` hypertable
2. Analyzes patterns (repeated crashes, signal distribution, memory correlation)
3. Triggers incidents for persistent crash problems
4. Provides crash analysis API

### 8.2 OpenWISP UI Integration

**Crash information displayed in:**

1. **Node detail page:**
   - Crash history (last 7 days)
   - Most recent crash details
   - Crash rate indicator (healthy/warning/critical)

2. **Issues panel:**
   - "Repeated Crashes" incident shows:
     - Affected node
     - Crash frequency (2 in 24h)
     - Common signals/threads
     - Link to detailed crash reports
     - Next steps: "Review logs, check firmware version X.Y.Z for known issues"

3. **System health dashboard:**
   - Mesh-wide crash rate graph
   - Nodes with recent crashes highlighted on topology map

### 8.7 Privacy & Security

**Crash reports DO NOT include:**
- Call-ID, SIP headers, user IDs
- IP addresses of endpoints
- Phonebook content
- Configuration secrets

**Crash reports DO include:**
- Node identifier (public in mesh)
- Software version
- Signal type and description
- Thread/operation context
- Memory/CPU metrics
- Stack trace (instruction pointers only, no data)

### 8.8 Flash Wear Mitigation

**Zero flash writes design (no graceful shutdowns):**
- **All state in RAM:** `/tmp` is tmpfs (RAM-backed) on OpenWrt
- Crash state: `/tmp/meshmon_crash.bin` (~256 bytes, survives crash, lost on reboot)
- Persistent state: `/tmp/meshmon_state.json` (updated every 5 min, lost on reboot)
- **Crash persistence window:** Crash reports sent within ~10 seconds of restart (before next power loss)
- **Reboot handling:** Lost crash reports are acceptable (node came back healthy)
- **No flash writes ever** during normal operation

**Trade-off accepted:** Crash reports lost if:
1. Node crashes
2. procd restarts agent successfully
3. Node loses power before crash report sent (~10 second window)

This is acceptable because the node is healthy after restart, and repeated crash patterns will still be detected.

### 8.9 Configuration Options

```
[crash_reporting]
enabled = true                  # Enable crash detection
send_to_collector = true        # Send reports to Pi
backtrace_depth = 5             # Stack frames to capture (0-10)
persistent_state_interval = 300 # Seconds between state updates
max_crash_reports_buffered = 3  # If collector unreachable
```

---

## 9) Operational Requirements
- **Deployment:**  
  - Agent installed as a standard OpenWrt service (procd), config in `/etc/config/meshmon`.  
  - Pi runs Collector (Python) + Postgres/TimescaleDB via Docker (compose).  
- **Resilience:**  
  - Agent buffers a small number of windows if Pi is unreachable, with oldest-first drop when full.  
  - Collector durable writes (DB) with back-pressure handling.
- **Timekeeping:** NTP on nodes and Pi; collector tolerates small skews.
- **Configuration knobs (Agent):**  
  - `interval_s`, `window_s`, `neighbor_targets`, `rotate_peers`, `pi_url`, `queue_max`, `daemon (auto/olsr/babel)`, `max_kbps`.
- **Configuration knobs (Pi):**  
  - Rule thresholds, retention periods, incident auto-close time, registry behavior.

---

## 10) Integration with OpenWISP
- **UI embedding:** an OpenWISP panel that lists **Overall Health** and **Issues**, querying Pi `/issues`.  
- **Topology link:** “Show routes” opens OpenWISP topology view with specified path highlighted (pass src/dst/hops as parameters).  
- **No separate DB**: OpenWISP reads incidents from Pi API; mesh topology continues to be managed by OpenWISP’s own mechanisms.

---

## 11) Non-Functional Requirements

### 11.1 Agent Performance
- **RAM:** ≤ **12 MB** peak on low-end OpenWrt devices
  - Constant memory usage regardless of uptime
  - **No historical data retention** - only current operational state
  - Fixed-size buffers for all operations
- **CPU:** ≤ **5% average** on 400 MHz MIPS/ARM
  - Lightweight probe processing
  - Minimal JSON encoding
  - No statistical analysis (done by collector)
- **Network:** ≤ **64 kbps** peak during probe windows
  - Adaptive reduction during VoIP calls
  - Exponential backoff on collector failure
- **Flash:** **Zero writes** during normal operation
  - All state in RAM (`/tmp`)
  - No persistent storage requirements

### 11.2 Collector Performance
- **Scale:** Handles **30–100 nodes** comfortably on Raspberry Pi 4/5
- **Throughput:** Process **100+ messages/second** from agents
- **Storage:** TimescaleDB compression for long-term retention
- **Query:** Sub-second response for UI queries (issues, nodes, crashes)

### 11.3 Reliability
- **Agent:** No single probe failure should crash agent; continue operating with degraded data
- **Collector:** No single malformed payload should crash collector; schema validation with reject logging
- **Recovery:** Agent auto-recovers from collector outage; buffers and resumes
- **Resilience:** System tolerates node power loss, network partitions, and collector restarts

### 11.4 Maintainability
- **Logging:** Clear logs on both sides with ERROR/WARN/INFO/DEBUG levels
- **Schema:** Versioned JSON schema (`schema: meshmon.v1`) enables protocol evolution
- **Configuration:** All thresholds and intervals configurable without code changes
- **Monitoring:** Agent health and collector health self-reported

---

## 12) Risks & Mitigations
- **Routing daemon variance (OLSR/Babel versions):** implement tolerant parsers; fall back to neighbor-only checks if route parsing fails.  
- **Clock drift:** rely on collector receipt time if skew is large; flag health warning.  
- **Probe interference:** keep windows short, staggered; allow quick global rate reduction.  
- **Topology churn noise:** use hysteresis and baselines to avoid flapping incidents.

---

## 13) Design Decisions (confirmed)

### 13.1 Node Discovery & Lifecycle Management

**Decision:** Agents autonomously send data to Pi collector. No central registry required.

**Implementation:**
- Each agent configured with Pi collector URL (e.g., `http://pi-collector.local.mesh:5000/ingest`)
- Agent discovers other participants via routing daemon (OLSR/Babel neighbor tables)
- Collector tracks `last_seen` timestamp per node in `nodes` table
- **Dead node removal:** Nodes not heard from in **30 minutes** marked inactive; removed from UI after **24 hours**
- **Mesh dynamics:** Topology changes reflected automatically as agents report current routes

**Rationale:** Mesh topology is highly dynamic. Central registry would be stale immediately. Agent-driven reporting ensures real-time accuracy.

### 13.2 Radio Driver Statistics

**Decision:** Include radio driver stats as **optional fields** in `hop_result`.

**Extended schema:**
```json
{
  "type": "hop_result",
  "hop": { "from":"node-A", "to":"node-D" },
  "l2": {
    "lq": 0.86,
    "nlq": 0.91,
    "etx": 1.28,
    "signal_dbm": -67,
    "noise_dbm": -95,
    "tx_bitrate_mbps": 24.0,
    "link_type": "rf"
  }
}
```

**New fields (all optional, may be null):**
- `signal_dbm`: Received signal strength (dBm)
- `noise_dbm`: Noise floor (dBm)
- `tx_bitrate_mbps`: Current transmit bitrate
- `link_type`: `"rf"` (wireless) or `"tunnel"` (wired/VPN/DtD)

**Purpose:** Distinguish RF links from tunnel links; correlate RF degradation with signal/noise changes.

**Phase 1:** Implement infrastructure to capture these fields. May return null if unavailable.

### 13.3 Issue Detection Thresholds

**Decision:** Keep as **configurable options** in collector. Start with conservative defaults.

**Phase 1 focus:** IP layer monitoring (RTT, jitter, loss, route stability).

**Default thresholds (tunable via collector config):**
```yaml
rules:
  rf_quality_drop:
    enabled: true
    lq_threshold: 0.60        # Trigger below 60%
    duration_seconds: 600     # Sustained 10 minutes

  jitter_burst:
    enabled: true
    p95_threshold_ms: 50      # 95th percentile jitter
    consecutive_windows: 3

  loss_spike:
    enabled: true
    threshold_pct: 3.0        # 3% loss
    consecutive_windows: 2

  route_flapping:
    enabled: true
    distinct_routes: 4
    window_seconds: 600       # 10 minutes

  bottleneck_link:
    enabled: true
    bad_paths: 5
    window_seconds: 1800      # 30 minutes

  node_overload:
    enabled: false            # Phase 2
    cpu_threshold: 80

  repeated_crashes:
    enabled: true
    crashes_24h: 2
    crashes_7d: 3
```

**Rationale:** Network characteristics vary by deployment. Operators can tune based on false positive/negative rates.

### 13.4 Geographic Coordinates

**Decision:** Agent sends coordinates from AREDN node configuration; updates only on change.

**Implementation:**
- AREDN stores coordinates in node info (accessible via API or config file)
- Agent reads coordinates on startup and periodically checks for changes
- Send in `agent_health` **only when coordinates change** (delta detection)

**Extended `agent_health` schema:**
```json
{
  "type": "agent_health",
  "node": "node-A",
  "sent_at": "2025-09-29T18:41:00Z",
  "cpu_pct": 3.1,
  "mem_mb": 5.9,
  "queue_len": 0,
  "uptime_seconds": 86400,
  "restart_count": 0,
  "threads_responsive": true,
  "health_score": 98.5,
  "location": {
    "lat": 34.0522,
    "lon": -118.2437,
    "elevation_m": 120,
    "source": "aredn_node_config"
  }
}
```

**Location field sent:**
- On agent first startup
- When coordinates change in AREDN config
- **Not sent** if unchanged (saves bandwidth)

**Collector behavior:**
- Stores last known coordinates in `nodes` table
- Updates only when new location received
- Provides `/nodes/{id}/location` API for UI

**Investigation needed:** Determine AREDN API/file path for reading coordinates (e.g., `/etc/config/aredn`, UCI config, or HTTP API).

### 13.5 Crash Reporting Detail Level

**Decision:** No full crash dumps. Lightweight reports only.

**Crash report content (final):**
- Signal type and description
- Thread ID and last operation breadcrumb
- Memory/CPU at crash
- Crash frequency counter
- **Backtrace:** Instruction pointers only (5-10 frames, ~50 bytes)
- **No:** Memory dumps, register state, core dumps

**Rationale:** Minimize flash writes and network bandwidth. Backtrace instruction pointers provide sufficient debugging info when combined with binary symbol tables (offline analysis).

**Crash report size:** ~500 bytes (JSON) or ~150 bytes (binary format, future optimization).

---

## 14) Phased Delivery (for planning)
1) **MVP**: Agent sends `path_result` + `hop_result`; Collector stores; Issue Engine raises **RF drop**, **Jitter burst**, **Loss spike**; OpenWISP shows Issues panel.
2) **Route flapping & Bottleneck** rules; "Show routes" highlight.
3) **Tuning & planning aids**: add optional link metadata (band/channel), daily summary.
4) **Refinements**: asymmetry diagnostics (LQ≪NLQ), optional node overload rule.

---

## 15) Future Optimizations

### 15.1 Binary Protocol (Phase 2)

**Motivation:** JSON is verbose and CPU-intensive on constrained devices.

**Binary message format:**
```
Message Header (8 bytes):
[0-1]   Magic: 0x4D53 ("MS" = MeshStats)
[2]     Version: 0x01
[3]     Type: 0x01=agent_health, 0x02=path_result, 0x03=hop_result
[4-7]   Timestamp: uint32_t (Unix epoch)

Agent Health Body (12 bytes):
[8-9]   CPU: uint16_t (tenths of %, e.g., 31 = 3.1%)
[10-11] Memory: uint16_t (MB)
[12]    Queue: uint8_t
[13]    Neighbors: uint8_t
[14-15] Node ID hash: uint16_t
Total: 20 bytes vs ~150 bytes JSON (87% reduction)

Path Result Body (variable):
[8-9]   RTT: uint16_t (ms * 10)
[10-11] Jitter: uint16_t (ms * 10)
[12]    Loss: uint8_t (tenths of %)
[13]    Hop count: uint8_t
[14-n]  Route: array of node ID hashes (2 bytes each)
```

**Benefits:**
- 85-90% bandwidth reduction
- 95% CPU reduction (binary encoding vs JSON parsing)
- Trivial parsing in C (struct casting)

**Implementation notes:**
- Collector must support both JSON (human-readable) and binary (production) modes
- Add `Content-Type: application/x-meshmon-binary` header
- Schema version in header enables protocol evolution

### 15.2 Delta Encoding (Phase 3)

**Motivation:** Most agent health fields don't change between reports.

**Approach:**
```
Delta Message:
[0]   Fields changed bitmask:
      bit 0 = CPU
      bit 1 = Memory
      bit 2 = Queue
      bit 3 = Neighbors
      bit 4 = Health score
      (etc.)
[1-n] Only values for fields with bit set
```

**Example:**
- Full report: 20 bytes
- Delta (only CPU changed): 3 bytes (header=1, bitmask=1, cpu=1)
- Result: 85% reduction for stable systems

**Strategy:**
- Send full report every 12th message (hourly baseline)
- Send deltas for intermediate reports
- Collector reconstructs full state

### 15.3 Batch Compression (Phase 4)

**Motivation:** Reduce overhead when buffering multiple reports.

**Approach:**
```
Batch Header (4 bytes):
[0]   Message count: uint8_t
[1]   Compression: 0=none, 1=RLE, 2=zlib
[2-3] Compressed size: uint16_t
[4-n] Compressed payload
```

**Use cases:**
- Collector temporarily unreachable: batch 12 reports (1 hour)
- 12 individual JSON messages: ~1.8 KB
- 1 batched binary compressed: ~200 bytes (89% reduction)

**Implementation:**
- Agent accumulates reports in circular buffer
- Compress batch before POST retry
- Collector decompresses and processes individually

### 15.4 Adaptive Metrics Collection (Phase 5)

**Motivation:** Don't measure what isn't being used.

**Dynamic metric selection:**
```c
// Collector signals which metrics are needed
struct metric_config {
    bool collect_cpu;           // Always for node overload rule
    bool collect_memory;        // Only if leak detected network-wide
    bool collect_queue_depth;   // Only if buffering issues seen
    bool collect_thread_health; // Only if stability issues
};
```

**Benefits:**
- Further CPU/memory reduction
- Collector drives what's measured based on active Issue Engine rules
- Implemented via registry endpoint: `GET /registry/{node_id}/metrics_config`

### 15.5 Performance Targets (Binary + Delta + Compression)

| Metric | JSON Baseline | Optimized | Improvement |
|--------|---------------|-----------|-------------|
| **Bandwidth** | ~150 bytes/5min | ~5 bytes/5min | 97% |
| **CPU per report** | ~5ms | ~0.2ms | 96% |
| **Memory overhead** | ~2 MB | ~512 KB | 75% |
| **Batch (1h)** | ~1.8 KB | ~200 bytes | 89% |

**Implementation priority:**
1. Binary protocol (biggest single win)
2. Delta encoding (optimize stable systems)
3. Adaptive intervals (already in v1)
4. Batch compression (collector failure resilience)
5. Adaptive metrics (marginal gain, defer)

---

**End of FSD v1.0**
This document is intended to be implementation-ready without prescribing code. It defines behavior, data contracts, storage, detection logic, UI expectations, operational bounds, and optimization roadmap.
