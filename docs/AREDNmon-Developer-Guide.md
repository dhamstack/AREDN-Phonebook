# MeshMon Pi Collector — Developer Guide (v0.1)

**Goal:** Build and run the **Raspberry Pi collector/analyzer** for the AREDN mesh monitoring system.
**Audience:** Developer implementing the Pi-side software.
**Style:** Step-by-step, down to command level.
**Stack decisions (already made):**
- **Language:** Python (collector)
- **DB:** PostgreSQL + TimescaleDB (single database)
- **Runtime:** Docker (Compose)
- **Transport:** HTTP POST (JSON), no auth/TLS (closed AREDN)
- **Agents:** C on OpenWrt send JSON payloads to `/ingest`

---

## 0) High-Level Responsibilities (Pi)
1. Ingest JSON from router agents and write to TimescaleDB.
2. Issue Engine: evaluate sliding windows; open/close incidents from rules.
3. Serve API to the UI layer (OpenWISP panel later): `/issues`, `/issues/{id}`, `/healthz`, `/registry` (optional).
4. Ops: retention, compression, backup/restore, logs.

---

## 1) Prerequisites on the Raspberry Pi
- Raspberry Pi 4/5 with 64-bit OS (Raspberry Pi OS (64-bit) or Ubuntu Server 22.04+).
- Internet for initial image pulls.
- `curl`, `git`.

### Install Docker & Compose
```bash
apt update
apt install -y curl ca-certificates
curl -fsSL https://get.docker.com | sh
sudo usermod -aG docker $USER
newgrp docker
docker run --rm hello-world
docker compose version
```

---

## 2) Repository Layout
Create this structure:
```
arednmon/
├─ docker-compose.yml
├─ api/
│  ├─ Dockerfile
│  ├─ requirements.txt
│  ├─ app.py                 # FastAPI app (ingest, issues, health)
│  ├─ issue_engine.py        # rule evaluation
│  ├─ db.py                  # DB connection + init
│  ├─ schemas.py             # pydantic models for JSON payloads
│  ├─ sql/
│  │  └─ init.sql            # DDL for tables, hypertables, policies
│  └─ config.py              # env handling
├─ Makefile                  # convenience commands (optional)
└─ README.md
```
Create it:
```bash
mkdir -p ~/arednmon/api/sql
cd ~/arednmon
```

---

## 3) Compose File
Create `nano docker-compose.yml`:
```yaml
services:
  db:
    image: timescale/timescaledb-ha:pg16-all
    environment:
      POSTGRES_DB: meshmon
      POSTGRES_USER: mesh
      POSTGRES_PASSWORD: meshpass
    ports:
      - "5432:5432"
    # use a named volume to avoid permission headaches in LXC
    volumes:
      - dbdata:/home/postgres/pgdata/data
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U mesh -d meshmon"]
      interval: 5s
      timeout: 5s
      retries: 20
    restart: unless-stopped

  api:
    build: ./api
    environment:
      DB_DSN: postgresql://mesh:meshpass@db:5432/meshmon
      WORKERS: "2"
      LOG_LEVEL: "info"
    ports:
      - "8080:8080"
    depends_on:
      db:
        condition: service_healthy
    restart: unless-stopped

volumes:
  dbdata:
```

---

## 4) Database DDL (init SQL)
Create `nano api/sql/init.sql`:
```sql
CREATE EXTENSION IF NOT EXISTS timescaledb;

CREATE TABLE IF NOT EXISTS incidents(
  id TEXT PRIMARY KEY,
  kind TEXT NOT NULL,
  scope JSONB NOT NULL,
  severity TEXT NOT NULL,
  impact JSONB NOT NULL,
  cause TEXT,
  next_steps TEXT[],
  evidence JSONB,
  started_at TIMESTAMPTZ NOT NULL,
  ended_at TIMESTAMPTZ
);

CREATE TABLE IF NOT EXISTS path_probe(
  time TIMESTAMPTZ NOT NULL,
  src TEXT NOT NULL,
  dst TEXT NOT NULL,
  route JSONB NOT NULL,
  rtt_ms DOUBLE PRECISION,
  jitter_ms DOUBLE PRECISION,
  loss_pct DOUBLE PRECISION,
  window_s SMALLINT NOT NULL DEFAULT 5
);
SELECT create_hypertable('path_probe','time', if_not_exists => true);

CREATE TABLE IF NOT EXISTS hop_quality(
  time TIMESTAMPTZ NOT NULL,
  from_node TEXT NOT NULL,
  to_node TEXT NOT NULL,
  lq DOUBLE PRECISION,
  nlq DOUBLE PRECISION,
  etx DOUBLE PRECISION
);
SELECT create_hypertable('hop_quality','time', if_not_exists => true);

CREATE INDEX IF NOT EXISTS path_probe_time_idx ON path_probe (time DESC, src, dst);
CREATE INDEX IF NOT EXISTS hop_quality_time_idx ON hop_quality (time DESC, from_node, to_node);

ALTER TABLE path_probe SET (timescaledb.compress);
ALTER TABLE hop_quality SET (timescaledb.compress);
SELECT add_compression_policy('path_probe', INTERVAL '7 days') ON CONFLICT DO NOTHING;
SELECT add_compression_policy('hop_quality', INTERVAL '7 days') ON CONFLICT DO NOTHING;
SELECT add_retention_policy('path_probe', INTERVAL '90 days') ON CONFLICT DO NOTHING;
SELECT add_retention_policy('hop_quality', INTERVAL '90 days') ON CONFLICT DO NOTHING;

CREATE MATERIALIZED VIEW IF NOT EXISTS path_probe_hourly
WITH (timescaledb.continuous) AS
  SELECT time_bucket('1 hour', time) AS bucket,
         src, dst,
         avg(rtt_ms) AS rtt_ms_avg,
         approx_percentile(0.95, jitter_ms) AS jitter_ms_p95,
         avg(loss_pct) AS loss_pct_avg
  FROM path_probe
  GROUP BY bucket, src, dst;
SELECT add_retention_policy('path_probe_hourly', INTERVAL '365 days') ON CONFLICT DO NOTHING;
```

---

## 5) API Container (FastAPI + Uvicorn)
Create `nano api/Dockerfile`:
```dockerfile
FROM python:3.11-slim

# System deps (psycopg)
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential libpq-dev && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY . .

ENV PYTHONUNBUFFERED=1
EXPOSE 8080
CMD ["uvicorn", "app:app", "--host", "0.0.0.0", "--port", "8080", "--workers", "2"]
```

Create `nano api/requirements.txt`:
```
fastapi==0.115.0
uvicorn[standard]==0.30.6
pydantic==2.8.2
psycopg2-binary==2.9.9
python-dateutil==2.9.0.post0
```

---

## 6) Minimal App Files

Create `nano api/config.py`:
```python
import os

DB_DSN = os.getenv("DB_DSN", "postgresql://mesh:meshpass@db:5432/meshmon")
LOG_LEVEL = os.getenv("LOG_LEVEL", "info")
```

Create `nano api/db.py`:
```python
import psycopg2
from psycopg2.extras import Json

def get_conn(dsn):
    return psycopg2.connect(dsn)

def init_db(conn):
    with conn, conn.cursor() as cur:
        with open("sql/init.sql", "r", encoding="utf-8") as f:
            cur.execute(f.read())

def insert_path_probe(conn, rec):
    sql = """INSERT INTO path_probe(time, src, dst, route, rtt_ms, jitter_ms, loss_pct, window_s)
             VALUES (%s,%s,%s,%s,%s,%s,%s,%s)"""
    with conn, conn.cursor() as cur:
        cur.execute(sql, (
            rec["sent_at"], rec["src"], rec["dst"],
            Json(rec["route"]), rec["metrics"].get("rtt_ms_avg"),
            rec["metrics"].get("jitter_ms_rfc3550"),
            rec["metrics"].get("loss_pct"), rec.get("window_s", 5)
        ))

def insert_hop_quality(conn, rec):
    sql = """INSERT INTO hop_quality(time, from_node, to_node, lq, nlq, etx)
             VALUES (%s,%s,%s,%s,%s,%s)"""
    l2 = rec.get("l2", {})
    hop = rec.get("hop", {})
    with conn, conn.cursor() as cur:
        cur.execute(sql, (
            rec["sent_at"], hop.get("from"), hop.get("to"),
            l2.get("lq"), l2.get("nlq"), l2.get("etx")
        ))
```

Create `nano api/schemas.py`:
```python
from pydantic import BaseModel, Field, AwareDatetime
from typing import List, Optional, Literal, Dict, Any

class PathMetrics(BaseModel):
    rtt_ms_avg: Optional[float] = None
    jitter_ms_rfc3550: Optional[float] = None
    loss_pct: Optional[float] = None

class PathResult(BaseModel):
    schema: Literal["meshmon.v1"]
    type: Literal["path_result"]
    src: str
    dst: str
    sent_at: AwareDatetime
    window_s: int = 5
    route: List[str]
    metrics: PathMetrics
    agent: Optional[Dict[str, Any]] = None

class Hop(BaseModel):
    from_: str = Field(..., alias="from")
    to: str

class L2(BaseModel):
    lq: Optional[float] = None
    nlq: Optional[float] = None
    etx: Optional[float] = None

class HopResult(BaseModel):
    schema: Literal["meshmon.v1"]
    type: Literal["hop_result"]
    src: str
    dst: str
    sent_at: AwareDatetime
    hop_index: int
    hop: Hop
    l2: L2

class AgentHealth(BaseModel):
    schema: Literal["meshmon.v1"]
    type: Literal["agent_health"]
    node: str
    sent_at: AwareDatetime
    cpu_pct: float
    mem_mb: float
    queue_len: int
    neighbors_seen: int
```

Create `nano api/issue_engine.py` (starter rule):
```python
from datetime import datetime
from psycopg2.extras import Json

def upsert_incident(conn, inc):
    sql = """INSERT INTO incidents (id, kind, scope, severity, impact, cause, next_steps, evidence, started_at, ended_at)
             VALUES (%(id)s, %(kind)s, %(scope)s, %(severity)s, %(impact)s, %(cause)s, %(next_steps)s, %(evidence)s, %(started_at)s, %(ended_at)s)
             ON CONFLICT (id) DO UPDATE SET
               severity=EXCLUDED.severity,
               impact=EXCLUDED.impact,
               cause=EXCLUDED.cause,
               next_steps=EXCLUDED.next_steps,
               evidence=EXCLUDED.evidence,
               ended_at=EXCLUDED.ended_at"""
    with conn, conn.cursor() as cur:
        cur.execute(sql, inc)

def run_rules_once(conn, now: datetime):
    # Placeholder: implement rule queries here (e.g., jitter bursts)
    pass
```

Create `nano api/app.py`:
```python
from fastapi import FastAPI, Request, HTTPException
from datetime import datetime, timezone
from config import DB_DSN
from db import get_conn, init_db, insert_path_probe, insert_hop_quality
from schemas import PathResult, HopResult, AgentHealth
from issue_engine import run_rules_once

app = FastAPI(title="MeshMon API")
_conn = None

@app.on_event("startup")
def _startup():
    global _conn
    _conn = get_conn(DB_DSN)
    init_db(_conn)

@app.get("/healthz")
def healthz():
    return {"ok": True}

@app.post("/ingest")
async def ingest(request: Request):
    payload = await request.json()
    now = datetime.now(timezone.utc)

    def handle(rec):
        t = rec.get("type")
        if t == "path_result":
            obj = PathResult.model_validate(rec)
            insert_path_probe(_conn, obj.model_dump())
        elif t == "hop_result":
            obj = HopResult.model_validate(rec)
            insert_hop_quality(_conn, obj.model_dump(by_alias=True))
        elif t == "agent_health":
            _ = AgentHealth.model_validate(rec)
        else:
            raise HTTPException(status_code=400, detail=f"unknown type: {t}")

    if isinstance(payload, list):
        for rec in payload:
            handle(rec)
    else:
        handle(payload)

    run_rules_once(_conn, now)
    return {"accepted": True}

@app.get("/issues")
def list_issues():
    q = "SELECT id, kind, scope, severity, impact, cause, next_steps, evidence, started_at, ended_at FROM incidents WHERE ended_at IS NULL ORDER BY started_at DESC LIMIT 200"
    with _conn, _conn.cursor() as cur:
        cur.execute(q)
        rows = cur.fetchall()
    keys = ["id","kind","scope","severity","impact","cause","next_steps","evidence","started_at","ended_at"]
    return [dict(zip(keys, r)) for r in rows]

@app.get("/issues/{iid}")
def get_issue(iid: str):
    q = "SELECT id, kind, scope, severity, impact, cause, next_steps, evidence, started_at, ended_at FROM incidents WHERE id=%s"
    with _conn, _conn.cursor() as cur:
        cur.execute(q, (iid,))
        r = cur.fetchone()
        if not r:
            raise HTTPException(404, "not found")
    keys = ["id","kind","scope","severity","impact","cause","next_steps","evidence","started_at","ended_at"]
    return dict(zip(keys, r))
```

---

## 7) Build & Run
```bash
cd ~/arednmon
docker compose build
docker compose up -d
docker compose logs -f
```

Initialize DB (run init.sql):
```bash
docker compose exec -T db psql -U mesh -d meshmon -c "SELECT version();"
docker compose exec -T db psql -U mesh -d meshmon -f /dev/stdin < api/sql/init.sql
docker compose exec -T db psql -U mesh -d meshmon -c "\dt"
```

---

## 8) Smoke Tests (curl + SQL)

### Health
```bash
curl -s http://localhost:8080/healthz
```

### Ingest one `path_result`
```bash
curl -s http://localhost:8080/ingest \
 -H 'Content-Type: application/json' \
 -d '{
  "schema":"meshmon.v1","type":"path_result",
  "src":"node-A","dst":"node-K",
  "sent_at":"2025-09-29T18:41:05Z","window_s":5,
  "route":["node-A","node-D","node-H","node-K"],
  "metrics":{"rtt_ms_avg":72.3,"jitter_ms_rfc3550":31.8,"loss_pct":0.9}
 }'
```

### Ingest a few windows (batch)
```bash
cat > /tmp/batch.json <<'EOF'
[
 {"schema":"meshmon.v1","type":"path_result","src":"node-A","dst":"node-K","sent_at":"2025-09-29T18:41:05Z","window_s":5,"route":["node-A","node-D","node-H","node-K"],"metrics":{"rtt_ms_avg":70,"jitter_ms_rfc3550":35,"loss_pct":0.5}},
 {"schema":"meshmon.v1","type":"path_result","src":"node-A","dst":"node-K","sent_at":"2025-09-29T18:41:45Z","window_s":5,"route":["node-A","node-D","node-H","node-K"],"metrics":{"rtt_ms_avg":75,"jitter_ms_rfc3550":33,"loss_pct":0.8}},
 {"schema":"meshmon.v1","type":"path_result","src":"node-A","dst":"node-K","sent_at":"2025-09-29T18:42:25Z","window_s":5,"route":["node-A","node-D","node-H","node-K"],"metrics":{"rtt_ms_avg":73,"jitter_ms_rfc3550":38,"loss_pct":0.6}}
]
EOF

curl -s http://localhost:8080/ingest \
 -H 'Content-Type: application/json' \
 --data-binary @/tmp/batch.json
```

### Check tables
```bash
docker compose exec -T db psql -U mesh -d meshmon -c "SELECT COUNT(*) FROM path_probe;"
docker compose exec -T db psql -U mesh -d meshmon -c "SELECT * FROM path_probe ORDER BY time DESC LIMIT 5;"
```

### Issues API (should show an incident when rules are implemented)
```bash
curl -s http://localhost:8080/issues | jq
```

---

## 9) Daily Operations

### Backups
```bash
mkdir -p backups
docker compose exec -T db pg_dump -U mesh meshmon | gzip > backups/meshmon_$(date +%F).sql.gz
```

### Restore (from a fresh db)
```bash
gunzip -c backups/meshmon_YYYY-MM-DD.sql.gz | docker compose exec -T db psql -U mesh -d meshmon
```

### Retention & compression status
```bash
docker compose exec -T db psql -U mesh -d meshmon -c "\dx"
docker compose exec -T db psql -U mesh -d meshmon -c "SELECT * FROM timescaledb_information.jobs;"
```

---

## 10) Hardening & Next Steps (when needed)
- Move `run_rules_once` to a background scheduler (APScheduler or separate worker).
- Implement the remaining rule set (RF drop, loss spike, flapping, bottleneck, node overload).
- Add `/registry` endpoint if you want Pi-managed participant list.
- Add **OpenWISP panel** to consume `/issues` and link into topology view.
- Optional: add token/TLS later (reverse proxy) without changing the agent contract).

---

## 11) JSON Contracts (Appendix)

### `path_result`
```json
{
  "schema": "meshmon.v1",
  "type": "path_result",
  "src": "node-A",
  "dst": "node-K",
  "sent_at": "2025-09-29T18:41:05Z",
  "window_s": 5,
  "route": ["node-A","node-D","node-H","node-K"],
  "metrics": {
    "rtt_ms_avg": 72.3,
    "jitter_ms_rfc3550": 11.8,
    "loss_pct": 0.9
  },
  "agent": { "ver": "0.1.0", "cpu_pct": 4.2, "mem_mb": 6.8 }
}
```

### `hop_result`
```json
{
  "schema": "meshmon.v1",
  "type": "hop_result",
  "src": "node-A",
  "dst": "node-K",
  "sent_at": "2025-09-29T18:41:05Z",
  "hop_index": 1,
  "hop": { "from": "node-A", "to": "node-D" },
  "l2": { "lq": 0.86, "nlq": 0.91, "etx": 1.28 }
}
```

### `agent_health`
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

---

## 12) Troubleshooting
- API won’t start: check DB health (`docker compose ps`), inspect API logs.
- `init.sql` not applied: run the `psql` command again (see §7).
- No incidents appear: ensure test payloads cross thresholds (e.g., jitter > 30 for 3 windows).
- Space usage high: verify compression jobs, retention policies (see §9).
- Slow queries: add indexes on frequent filters (time, src/dst, from/to).

---

**End of Developer Guide v0.1**