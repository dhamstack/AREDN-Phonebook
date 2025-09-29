# OpenWrt Mesh Agent — Developer Functional Specification (C) v0.9

**Audience:** C developer integrating the mesh-monitoring agent into an **existing SIP server** running on OpenWrt/AREDN nodes.  
**Goal:** Implement a lightweight module that measures mesh path/link quality and reports results to a Raspberry Pi collector via **HTTP POST (JSON)**. No TLS/auth required (closed AREDN).

---

## 1) Architecture Summary

The agent runs **in-process** with the SIP server (preferred) or as a sidecar daemon. It uses a small event loop (prefer **libubox/uloop**) to schedule periodic tasks:

1. **Discovery:** Determine participating peers (nodes running the agent).  
2. **Routing Adapter:** Query local routing daemon (**OLSR** or **Babel**) to obtain **neighbors (LQ/NLQ/ETX)** and **current route (hop list)** to each probe target.  
3. **Probe Engine:** Send UDP “voice-like” bursts for **5 s** per target, compute **loss, RTT, jitter (RFC3550)**.  
4. **Reporter:** Batch results as JSON and **POST** to the Pi endpoint using **uclient-http**.  
5. **Health Sampler:** Export agent health (version, CPU, RAM, queue length).

**Key property:** All routing data is obtained **locally** (127.0.0.1 or local socket). No extra mesh chatter.

---

## 2) Non-Goals / Constraints

- **No security** (TLS/auth) in transport.  
- **Strictly lightweight**: minimize CPU, RAM, and added RF traffic.  
- **No dependency on SIP signaling path**; probes use separate UDP sockets and may set **DSCP=EF (46)** optionally.  
- Runs on low-spec OpenWrt targets (MIPS/ARM, 64–128 MB RAM).

---

## 3) Processes & Integration Modes

### 3.1 In-Process (recommended)
- Link agent as a module into the SIP server binary.  
- Use SIP server’s main loop **if** it is libubox/uloop compatible; otherwise run a **dedicated uloop** in a worker thread.  
- Communication with SIP core: optional callbacks for lifecycle (start/stop), logging, and health exposure.

### 3.2 Sidecar Daemon (fallback)
- Separate procd service `meshmond`. Communicate with SIP server only for lifecycle/health (optional).

---

## 4) Module Breakdown (C)

1. **config.c / config.h**  
   - Reads `/etc/config/meshmon` (UCI).  
   - Validates ranges, writes runtime defaults.

2. **log.c / log.h**  
   - Uniform logging with levels (ERROR/WARN/INFO/DEBUG).  
   - Syslog + optional ring buffer (last 256 lines) for support dumps.

3. **routing.c / routing.h**  
   - **Autodetect:** try OLSR first, then Babel.  
   - **OLSR (jsoninfo):** HTTP GET `http://127.0.0.1:9090/` (exact path configurable). Parse neighbors (LQ, NLQ, ETX) and routes.  
   - **Babel (control socket):** read `/var/run/babeld.sock` using text protocol; parse `route`, `xroute`, `neigh`.  
   - APIs:  
     - `int routing_get_route(const char *dst, hop_list_t *out);`  
     - `int routing_get_neighbors(neighbor_list_t *out);`  
     - `int routing_refresh(void);` (cache TTL configurable)

4. **discovery.c / discovery.h**  
   - Build **participant set**: union of neighbors + any entries from Pi registry (optional).  
   - Filter to nodes advertising the agent (see §9.3 Agent Hello).  
   - Export iterator for probe selection (neighbors + rotating peer).

5. **probe.c / probe.h**  
   - UDP sockets (sender/receiver).  
   - **Packet format:** 12-byte header:  
     - `uint32_t seq`, `uint32_t send_ts_ms`, `uint16_t flow_id`, `uint16_t flags`  
   - **Schedule:** every `interval_s` start a **window** of `window_s` for each selected peer (staggered).  
   - **Sender:** 20 packets/s (configurable). Optional **DSCP EF** marking.  
   - **Receiver echo mode:** on receipt, immediately echo to sender (swap src/dst) unless in **rx-only** mode.  
   - **Metrics:**  
     - Loss = (sent - received)/sent  
     - RTT (if echo) = recv_ts - orig_send_ts (ms)  
     - **RFC3550 jitter**: exponential estimator over interarrival transit variation.

6. **calc.c / calc.h**  
   - RFC3550 jitter computation and summary stats per window (avg RTT, p95 jitter optional, loss%).  
   - Helpers for classifying window as OK / JITTER / LOSS.

7. **reporter.c / reporter.h**  
   - Build JSON **path_result** and **hop_result** records (see §8).  
   - Batch array; POST via **uclient-http** to `pi_url` + `/ingest`.  
   - Retries with backoff on failure; bounded queue to disk (ubus blobmsg or raw JSON lines).

8. **health.c / health.h**  
   - Sample CPU%, RAM usage, queue length, neighbors_seen.  
   - Emit **agent_health** record every `health_interval_s`.

9. **scheduler.c / scheduler.h**  
   - Drives periodic tasks via **uloop timers**.  
   - Ensures probe windows do not overlap; maintains per-peer staggering.

10. **main.c**  
    - Wiring, initialization, signal handling, graceful shutdown.

---

## 5) Configuration (UCI) — `/etc/config/meshmon`

```uci
config meshmon 'main'
    option enabled '1'
    option pi_url 'http://10.0.0.5:8080'   # no trailing slash
    option interval_s '40'                 # seconds between probe cycles
    option window_s '5'                    # length of each probe window
    option neighbor_targets '2'            # neighbors per cycle
    option rotate_peers '1'                # additional non-neighbor target(s)
    option max_kbps '80'                   # cap per active probe (approx)
    option daemon 'auto'                   # auto | olsr | babel
    option routing_cache_s '5'             # min seconds between routing polls
    option health_interval_s '300'         # agent_health POST interval
    option echo_mode '1'                   # 1=echo replies for RTT, 0=rx-only
    option dscp_ef '1'                     # mark probes with DSCP EF (46)
    option log_level 'INFO'                # ERROR|WARN|INFO|DEBUG
    option probe_port '40050'              # UDP port for probes
    option hello_port '40051'              # UDP port for agent hello (optional)
```

**Runtime reload:** `reload_config` via procd triggers `meshmon` to re-read UCI without restart.

---

## 6) Network Behavior & Budgets

- **Traffic per probe:** ~64 kbps during **window_s** (20 pps, ~160 B/pkt).  
- **Cycle:** 2 neighbors + 1 rotating peer → up to **3 windows** per interval.  
- **Staggering:** start offset = hash(node_id) % interval_s to spread load.  
- **QoS:** set **DSCP EF** optionally to emulate voice behavior and to avoid starving probes.

---

## 7) Routing Introspection Details

### 7.1 OLSR (jsoninfo)
- Endpoint default: `http://127.0.0.1:9090/` (configurable).
- Parse:  
  - **Neighbors:** `LQ`, `NLQ`, `ETX`, iface, next-hop.  
  - **Routes:** For a `dst`, collect hop chain (via next-hop recursion if needed).  
- Caching: minimum `routing_cache_s` between polls; invalidate on route change events if available.

### 7.2 Babel (control socket)
- Path default: `/var/run/babeld.sock`.  
- Commands: `dump`, parse `add route`, `add xroute`, `add neighbour`.  
- Compute per-destination **selected route** and neighbor metrics.  
- Caching identical to OLSR path.

---

## 8) JSON Payloads (Wire Contract)

### 8.1 `path_result`
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

### 8.2 `hop_result`
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

### 8.3 `agent_health`
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

**Batching:** Either POST arrays (`[ {...}, {...} ]`) or newline-delimited JSON.  
**Endpoint:** `POST {pi_url}/ingest` (no auth).  
**Timeouts:** Connect 1s, overall 3s; retry with exponential backoff up to 60s.

---

## 9) Discovery of Participating Nodes

### 9.1 Implicit (routing tables only)
- Assume any node in route/neighbor tables is a potential peer; attempt probe; if no echo/heard after K attempts, mark **non-participant** for 10 min.

### 9.2 Registry (optional, Pi-driven)
- `GET {pi_url}/registry` returns list of known participants (id, IP/host). Agent merges with local discovery.

### 9.3 Agent Hello (optional, UDP)
- Periodic small UDP multicast/unicast on `hello_port` with `node_id`, `ver`. On receipt, mark peer as **participant** for 10 min.

---

## 10) Timers & State Machines

- **Main cycle** (`interval_s`): pick targets (2 neighbors + 1 rotating) → for each target: run **window_s** probe.  
- **Routing refresh**: min every `routing_cache_s`, or on probe start for target `dst`.  
- **Reporter flush**: after each window; also periodic flush every 10 s.  
- **Health tick**: every `health_interval_s`.  
- **Backoff on POST failure**: 1s, 2s, 4s, … up to 60s; drop oldest when queue > `queue_max` (config).

---

## 11) Error Handling & Degradation

- If routing daemon not detected: degrade to **neighbor-only ping** (no hop list) and set `route:["unknown"]`.  
- If echo is disabled or peer not echoing: compute only **loss**; RTT optional via sender timestamps if peer sends periodic acks.  
- If POST fails: queue to disk (JSON lines) up to `queue_max_windows`; on recovery, backfill FIFO.  
- If CPU > `cpu_cap_pct` (optional), **skip rotating peer** to reduce load.

---

## 12) Resource Budgets (targets)

- **Code size:** ≤ 300–500 KB text; linked libs may add 200–500 KB (strip symbols).  
- **RAM:** ≤ 12 MB peak during probe windows (typical 4–8 MB).  
- **CPU:** < 5% on single-core MIPS during normal operation.  
- **Flash writes:** queue uses preallocated file; rotate to avoid wear.

---

## 13) Build & Packaging (OpenWrt)

- **Dependencies:** `libubox`, `libblobmsg-json`, `uclient-http`, `json-c` (+ `libnl-tiny` optional).  
- **Makefile:** standard OpenWrt package; install to `/usr/sbin/meshmon`; UCI default in `/etc/config/meshmon`; init script for procd if sidecar.  
- **Compiler flags:** `-O2 -pipe -fno-common -ffunction-sections -fdata-sections -s` and link with `--gc-sections`.

---

## 14) Logging & Diagnostics

- Log levels via UCI; default INFO.  
- On demand: dump last **N windows** summary; neighbor table; last POST status.  
- `ubus` method `meshmon.dump` (optional) to retrieve current state for support.

---

## 15) Test Plan (Developer)

1. **Unit**: jitter calculator (RFC3550 vectors), JSON builders, routing parsers (fixtures for OLSR/Babel).  
2. **Integration (qemu/OpenWrt)**: loopback echo, synthetic loss/jitter injection; verify metrics and POST.  
3. **Field**: deploy 3 nodes; verify stagger, load budget, and incident generation on the Pi.  
4. **Resilience**: kill Pi; ensure queueing/backoff; recovery flush.  
5. **Routing switch**: swap OLSR ↔ Babel at runtime; agent autodetects and continues.

---

## 16) SIP Server Integration Notes

- **Socket ports**: use dedicated probe UDP port (default 40050). Do **not** reuse SIP media sockets.  
- **Threading**: if SIP server uses its own loop, either adapt to uloop or isolate agent in a worker thread; expose stop/join hooks on shutdown.  
- **Priority/QoS**: optional DSCP EF; ensure not to starve SIP RTP.  
- **Health export**: expose `meshmon` health in SIP server’s status page if present (e.g., last POST ok, peers probed, queue len).

---

## 17) Future Extensions (non-blocking)

- Per-radio stats (noise/CCQ) if drivers expose them.  
- mDNS/LLDP hints for participant discovery.  
- Optional TLS and per-node auth if security requirements change.  
- Burst scheduling aware of quiet periods (night modes).

---

## 18) Acceptance Checklist (Dev Done)

- [ ] Probes run to **2 neighbors + 1 rotating** per cycle; windows are staggered.  
- [ ] Route and hop list attached to **every** path_result (or “unknown” fallback).  
- [ ] Jitter/loss/RTT computed correctly (validated with synthetic tests).  
- [ ] POSTs succeed; batching and backoff implemented; queue survives reboot.  
- [ ] UCI config reload works without process restart.  
- [ ] CPU/RAM within budget on target device.  
- [ ] Clean logs; no crashes on malformed inputs; graceful shutdown.

---

**End — Developer FSD v0.9**