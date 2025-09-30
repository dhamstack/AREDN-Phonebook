# 📞 AREDN Phonebook with Mesh Monitoring

> 🎯 **Emergency-Ready SIP Directory Service + Network Quality Monitoring for Amateur Radio Mesh Networks**

AREDN Phonebook is a SIP proxy server that provides directory services and optional network monitoring for Amateur Radio Emergency Data Network (AREDN) mesh networks. It automatically fetches and maintains a phonebook from mesh servers, while optionally monitoring network quality to help identify issues before they affect emergency communications.

## ✨ Core Features

### 📞 Phonebook Services
- 🔄 **Automatic Directory Updates**: Downloads phonebook from mesh servers every 30 minutes
- 🛡️ **Emergency Resilience**: Survives power outages with persistent storage
- 💾 **Flash-Friendly**: Minimizes writes to preserve router memory
- 🔌 **Plug-and-Play**: Works immediately after installation
- 📱 **Phone Integration**: Provides XML directory for SIP phones (tested with Yealink)
- 🔧 **Passive Safety**: Self-healing with automatic error recovery

### 📡 Network Monitoring (Optional)
- 📊 **Network Status**: RTT, jitter, packet loss measurements
- 🔍 **Link Technology Detection**: Identifies RF vs tunnel links
- 🏥 **Health Monitoring**: Tracks software health, crashes, memory usage
- 🌐 **Local Access**: CGI endpoints for on-node diagnostics
- 📤 **Remote Reporting**: Optional centralized monitoring via collector
- ⚡ **Event-Driven**: Reports immediately on problems, baseline every 4 hours

## 📦 Installation

### 🔗 Download

1. Go to the [📥 Releases page](https://github.com/dhamstack/AREDN-Phonebook/releases)
2. Download the latest `AREDN-Phonebook-x.x.x-x_[architecture].ipk` file for your device:
   - 🏠 **ath79**: Most common AREDN routers (e.g., Ubiquiti, MikroTik)
   - 💻 **x86**: PC-based AREDN nodes
   - 🔧 **ipq40xx**: Some newer routers

### 🌐 Install via AREDN Web Interface

1. 🌐 **Access AREDN Node**: Connect to your AREDN node's web interface

2. ⚙️ **Navigate to Administration**: Go to **Administration** → **Package Management**

   ![Package Management Screen](images/package-management.png)

3. 📤 **Upload Package**:
   - Click **Choose File** and select your downloaded `.ipk` file

     ![Upload Package Dialog](images/upload-package.png)

4. ⚡ **Install**: Click **Fetch and Install**

## ⚙️ Configuration (optional, not needed for most users)

The phonebook server automatically configures itself. Default settings:

- 📄 **Configuration**: `/etc/sipserver.conf`
- 🔧 **Service Commands**: `/etc/init.d/AREDN-Phonebook start|stop|restart|status`
- 🔌 **SIP Port**: 5060
- 🌐 **Directory URL**: `http://[your-node].local.mesh/arednstack/phonebook_generic_direct.xml`

## 📱 Phone Setup

Configure your SIP phone to use the node's directory:

1. 🔗 **Directory URL**: `http://localnode.local.mesh/arednstack/phonebook_generic_direct.xml`
2. 📡 **SIP Server**: `localnode.local.mesh`
3. 🔄 **Refresh**: Directory updates automatically every xx seconds from router (your Update Time Interval)

## 🔗 CGI Endpoints

### 📞 Phonebook Endpoints
- **`/cgi-bin/loadphonebook`** (GET): Triggers immediate phonebook reload
- **`/cgi-bin/showphonebook`** (GET): Returns current phonebook as JSON

### 📡 Monitoring Endpoints (Optional)
- **`/cgi-bin/health`** (GET): Phonebook health status (CPU, memory, threads, SIP service)
- **`/cgi-bin/network`** (GET): Network performance data (RTT, jitter, loss, hop analysis)
- **`/cgi-bin/crash`** (GET): Crash history (last 5 crashes with stack traces)
- **`/cgi-bin/connectioncheck?target=node-name`** (GET): Query connectivity to specific node

**Example:**
```bash
curl http://localnode.local.mesh/cgi-bin/health
curl http://localnode.local.mesh/cgi-bin/network
```

## 🔧 Troubleshooting

### ✅ Check Service Status
```bash
ps | grep AREDN-Phonebook
logread | grep "AREDN-Phonebook"
```

### 📂 Verify Directory Files
```bash
ls -la /www/arednstack/phonebook*
curl http://localhost/arednstack/phonebook_generic_direct.xml
```

### ⚠️ Common Issues

- 📅 **No directory showing**: Wait up to 30 minutes for first download
- 🚫 **Service not starting**: Check logs with `logread | tail -50`
- 🔒 **Permission errors**: Ensure `/www/arednstack/` directory exists

## 🔬 Technical Details

- 🚀 **Emergency Boot**: Loads the existing phonebook immediately on startup
- 💾 **Persistent Storage**: Survives power cycles using `/www/arednstack/`
- 🛡️ **Flash Protection**: Only writes when phonebook content changes
- 🧵 **Multi-threaded**: Background fetching doesn't affect SIP performance
- 🔧 **Auto-healing**: Recovers from network failures and corrupt data

## 📚 Documentation

- 📄 **Phonebook FSD**: [`docs/AREDN-phonebook-fsd.md`](docs/AREDN-phonebook-fsd.md) - Original phonebook implementation
- 📄 **Monitoring FSD**: [`docs/AREDN-Phonebook-With-Monitoring-FSD.md`](docs/AREDN-Phonebook-With-Monitoring-FSD.md) - Agent implementation with monitoring
- 🏗️ **Architecture**: [`docs/AREDNmon-Architecture.md`](docs/AREDNmon-Architecture.md) - System architecture and collector design

## 🆘 Support

- 🐛 **Issues**: [GitHub Issues](https://github.com/dhamstack/AREDN-Phonebook/issues)
- 🌐 **AREDN Community**: [AREDN Forums](https://www.arednmesh.org/)

## 📄 License

This project is released under open source license for amateur radio emergency communications.
