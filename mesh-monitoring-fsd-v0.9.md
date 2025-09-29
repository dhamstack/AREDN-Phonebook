# Mesh Monitoring System — Functional Specification Document (FSD) 

**Purpose:** Provide a clear, implementation-ready blueprint for a **lightweight, C-based agent on OpenWrt/AREDN nodes** and a **Python-based collector/analyzer on a Raspberry Pi**, with **OpenWISP** as the primary UI container. Focus is on **actionable issues**, not raw metrics.

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
- **Agents** run on participating routers. They:
  - Autodetect routing daemon (**OLSR** or **Babel**).
  - Read **neighbors** (LQ, NLQ, ETX) and **current route/hops** locally (localhost; no mesh chatter).
  - Send lightweight **UDP probe windows** to selected peers (auto-discovered participants).
  - Compute per-window **RTT, jitter (RFC3550), loss**, and package with **hop list** + **link qualities**.
  - POST results as **JSON** to the Pi collector.

- **Collector/Analyzer (Pi)**:
  - Receives JSON via **HTTP POST** (no TLS/auth required).
  - Stores into **TimescaleDB** hypertables + relational tables.
  - Runs an **Issue Engine** (rule-based) that opens/closes incidents.
  - Exposes **REST API** for issues (& optional registry).
  - Is embedded into **OpenWISP** UI via a small panel (no separate portal).

---

## 4) Functional Requirements

### 4.1 Issues (what the user sees)
- **Overall Health**: Green / Yellow / Red (computed from open incidents’ severities/impacts).
- **Issues list** (default view): cards with  
  - Title, Scope (link/node/route), Severity, Impact (e.g., routes affected), Start time, **Likely cause**, **Next steps**, Evidence summary.  
- **Details drawer** (per issue): timeline, affected routes, pattern (burst vs steady), references to involved nodes/links, and a short playbook.
- **Route highlight**: “Show routes” opens topology view (OpenWISP) with affected path highlighted.

### 4.2 Agent (Router)
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
- **Reports**:  
  - POST **batched JSON** to Pi `/ingest`.  
  - Include **agent health** (version, CPU%, RAM MB, local queue length) every ~5 min.

### 4.3 Collector/Analyzer (Pi)
- **Ingestion**: Accept JSON (`path_result`, `hop_result`, `agent_health`). Validate schema and timestamps, insert into DB.
- **Issue Engine** (rules below) evaluates sliding windows to **open**, **update**, and **close** incidents.
- **APIs**:  
  - `GET /issues?status=open&since=X` — list (for UI panel)  
  - `GET /issues/{id}` — details  
  - `GET /issues/by-object?link=A--B | node=N | route=src,dst`  
  - `GET /registry` — optional, list of known participants (node id, name)
- **OpenWISP UI**: simple panel that calls `/issues` and integrates with the topology view for highlighting.

---

## 5) Data Model

### 5.1 JSON from Agent → Pi (wire contract)
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
  "agent": { "ver":"0.1.0", "cpu_pct":4.2, "mem_mb":6.8 }
}
```

**Hop quality (per hop observed for that path/window)**
```json
{
  "schema": "meshmon.v1",
  "type": "hop_result",
  "src": "node-A",
  "dst": "node-K",
  "sent_at": "2025-09-29T18:41:05Z",
  "hop_index": 1,
  "hop": { "from":"node-A", "to":"node-D" },
  "l2": { "lq": 0.86, "nlq": 0.91, "etx": 1.28 }
}
```

**Agent health (periodic)**
```json
{
  "schema": "meshmon.v1",
  "type": "agent_health",
  "node": "node-A",
  "sent_at": "2025-09-29T18:41:00Z",
  "cpu_pct": 3.1,
  "mem_mb": 5.9,
  "queue_len": 0,
  "neighbors_seen": 5
}
```

### 5.2 Database (single Postgres + TimescaleDB)
- **Hypertables**  
  - `path_probe(time, src, dst, route_json, rtt_ms, jitter_ms, loss_pct, window_s)`  
  - `hop_quality(time, from_node, to_node, lq, nlq, etx)`
- **Relational tables**  
  - `incidents(id, kind, scope_json, severity, impact_json, cause, next_steps, evidence_json, started_at, ended_at)`  
  - `nodes(id, name, lat, lon, tags)` *(optional registry)*  
  - `links(id, a, b, tech, channel, notes)` *(optional for planning)*
- **Retention**  
  - Raw hypertables: **90 days** (compressed after 7 days).  
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

**Impact estimation:** number of affected routes in last **30–60 min** and optional “estimated users” mapping if available from site metadata.

**Closure:** incident closes after **stable** conditions for **15 min** (configurable).

---

## 7) Probing Strategy & Load Budget
- **Coverage:** each node probes **2 neighbors + 1 rotating peer** every **40 s**; 5 s per peer.  
- **Traffic per probe:** ≈ 64 kbps during the 5 s window (20 pps, small UDP payload).  
- **Mesh-wide load:** windows **staggered** by node ID to avoid bursts; overall added traffic remains minimal even at dozens of nodes.  
- **Adaptive backoff:** if congestion is detected network-wide, reduce rotating peers to 0 temporarily.

---

## 8) Operational Requirements
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

## 9) Integration with OpenWISP
- **UI embedding:** an OpenWISP panel that lists **Overall Health** and **Issues**, querying Pi `/issues`.  
- **Topology link:** “Show routes” opens OpenWISP topology view with specified path highlighted (pass src/dst/hops as parameters).  
- **No separate DB**: OpenWISP reads incidents from Pi API; mesh topology continues to be managed by OpenWISP’s own mechanisms.

---

## 10) Non-Functional Requirements
- **Performance:**  
  - Agent RAM ≤ **12 MB** peak on low-end OpenWrt devices.  
  - Collector handles **30–100 nodes** comfortably on a Pi 4/5.  
- **Reliability:** No single malformed payload should crash collector; schema validation with reject logging.  
- **Maintainability:** Clear logs on both sides; versioned JSON schema (`schema: meshmon.v1`).

---

## 11) Risks & Mitigations
- **Routing daemon variance (OLSR/Babel versions):** implement tolerant parsers; fall back to neighbor-only checks if route parsing fails.  
- **Clock drift:** rely on collector receipt time if skew is large; flag health warning.  
- **Probe interference:** keep windows short, staggered; allow quick global rate reduction.  
- **Topology churn noise:** use hysteresis and baselines to avoid flapping incidents.

---

## 12) Open Questions (to confirm before build)
- Preferred **participant registry** approach: Pi-managed registry vs. pure routing-table discovery (+ a small “agent alive” signal).  
- Include **radio driver stats** (noise/CCQ) in `hop_result` if available on AREDN hardware?  
- Default **thresholds** for your RF environment (initial values in §6 are safe starting points).  
- **Geographic coordinates** source of truth: OpenWISP device records vs. agent hints.

---

## 13) Phased Delivery (for planning)
1) **MVP**: Agent sends `path_result` + `hop_result`; Collector stores; Issue Engine raises **RF drop**, **Jitter burst**, **Loss spike**; OpenWISP shows Issues panel.  
2) **Route flapping & Bottleneck** rules; “Show routes” highlight.  
3) **Tuning & planning aids**: add optional link metadata (band/channel), daily summary.  
4) **Refinements**: asymmetry diagnostics (LQ≪NLQ), optional node overload rule.

---

**End of FSD v0.9**  
This document is intended to be implementation-ready without prescribing code. It defines behavior, data contracts, storage, detection logic, UI expectations, and operational bounds.
