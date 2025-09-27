# AREDN Health Message Backend - Simple Test Server

## Overview
Lightweight HTTP server to collect and display SIP server health messages from AREDN mesh nodes. Designed for OpenWRT/embedded environments.

## Architecture

### Core Components
1. **Message Collector** - HTTP endpoint for receiving JSON health messages
2. **Storage** - Simple file-based storage (no database required)
3. **Web Interface** - Basic HTML dashboard for viewing messages
4. **Log Rotation** - Automatic cleanup of old messages

### Message Types Handled
Based on FSD Chapter 9 specifications:

#### Immediate Alarms
- Thread recovery failures
- Memory exhaustion
- Critical configuration errors
- File system issues

#### Regular Reports (6-hour intervals)
- System status summary
- Call traffic statistics
- Performance metrics
- Warning conditions

## Implementation Design

### Backend Server (C/Lua hybrid for OpenWRT)

```
health-collector/
├── src/
│   ├── main.c              # HTTP server using libhttpd or uhttpd
│   ├── message_handler.c   # JSON parsing and storage
│   ├── web_interface.c     # Simple web dashboard
│   └── storage.c           # File-based message storage
├── www/
│   ├── index.html          # Dashboard interface
│   ├── style.css           # Basic styling
│   └── api.js              # AJAX for real-time updates
├── files/
│   └── etc/
│       ├── init.d/health-collector  # OpenWRT init script
│       └── health-collector.conf    # Configuration
└── Makefile               # OpenWRT package build
```

### Key Features

#### HTTP Endpoint
- **POST /health/report** - Accept JSON health messages
- **GET /** - Web dashboard
- **GET /api/messages** - JSON API for dashboard
- **GET /api/alarms** - Current active alarms only

#### Message Storage Format
```
/tmp/health-messages/
├── alarms/
│   ├── 2025-09-24-14-30-15-node1-thread-failure.json
│   └── 2025-09-24-15-45-22-node2-memory-critical.json
├── reports/
│   ├── 2025-09-24-12-00-00-node1-status.json
│   └── 2025-09-24-18-00-00-node1-status.json
└── traffic/
    ├── calls-2025-09-24.log
    └── calls-2025-09-25.log
```

#### Web Dashboard Features
- **Live Alarm Board** - Red alerts for critical issues
- **Node Status Grid** - Green/yellow/red status per node
- **Call Traffic Graph** - Network usage visualization
- **Search/Filter** - Find specific messages or time periods
- **Auto-refresh** - Real-time updates without page reload

#### Configuration
```ini
# /etc/health-collector.conf
listen_port=8080
storage_path=/tmp/health-messages
max_message_age_days=7
dashboard_refresh_seconds=30
max_concurrent_connections=10
```

## Resource Requirements

### Memory Usage
- Base server: ~2MB RAM
- Message storage: ~100KB per node per day
- Web interface: ~500KB static assets

### Storage Usage
- ~1MB per node per week (with 7-day retention)
- Automatic cleanup of old messages

### Network Usage
- Inbound: ~1KB per alarm, ~5KB per 6-hour report
- Dashboard: ~50KB initial load, ~5KB per refresh

## Deployment Strategy

### OpenWRT Package
```makefile
# Makefile snippet
PKG_NAME:=health-collector
PKG_VERSION:=1.0.0
DEPENDS:=+libmicrohttpd +cjson +uhttpd
```

### Installation Commands
```bash
# Install on AREDN mesh server
opkg update
opkg install health-collector
/etc/init.d/health-collector enable
/etc/init.d/health-collector start

# Access dashboard
http://[mesh-server-ip]:8080
```

### Security Considerations
- No authentication (internal mesh network only)
- Input validation on JSON messages
- Rate limiting to prevent spam
- No sensitive data storage

## Testing Scenarios

### Unit Tests
1. **Message Reception** - Verify JSON parsing of all message types
2. **Storage** - Confirm file creation and cleanup
3. **Dashboard** - Test web interface rendering
4. **API Endpoints** - Validate JSON responses

### Integration Tests
1. **SIP Server → Backend** - End-to-end message flow
2. **Multiple Nodes** - Concurrent message handling
3. **Load Testing** - Handle peak traffic periods
4. **Failure Recovery** - Server restart with existing data

### Manual Testing
1. **Dashboard Usability** - Operator can quickly identify issues
2. **Alarm Visibility** - Critical alerts are prominent
3. **Traffic Analysis** - Call patterns are clear
4. **Mobile Compatibility** - Works on smartphones/tablets

## Development Phases

### Phase 1: Core Collector (1 week)
- HTTP server with /health/report endpoint
- JSON message parsing and validation
- File-based storage implementation
- Basic logging and error handling

### Phase 2: Web Dashboard (1 week)
- HTML dashboard with live updates
- Alarm board and node status grid
- Call traffic visualization
- Search and filtering

### Phase 3: OpenWRT Integration (3 days)
- Package build system
- Init scripts and configuration
- Installation testing on AREDN nodes
- Documentation and deployment guide

### Phase 4: Testing & Optimization (2 days)
- Load testing and performance tuning
- Memory usage optimization
- Error handling improvements
- User acceptance testing

## Example Message Flow

```bash
# SIP Server sends alarm
curl -X POST http://mesh-server:8080/health/report \
  -H "Content-Type: application/json" \
  -d '{
    "timestamp": "2025-09-24T14:30:15Z",
    "node_callsign": "N0CALL-1",
    "message_type": "alarm",
    "severity": "critical",
    "component": "thread_monitor",
    "description": "Phonebook fetcher thread recovery failed",
    "details": {
      "thread_name": "phonebook_fetcher",
      "restart_attempts": 3,
      "last_heartbeat": "2025-09-24T14:25:10Z"
    }
  }'

# Dashboard shows red alert immediately
# Operator sees: "N0CALL-1: Thread Recovery Failed (14:30)"
```

This design provides a simple, testable backend that matches the OpenWRT/AREDN environment constraints while enabling comprehensive health monitoring.