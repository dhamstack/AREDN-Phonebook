# AREDN Phonebook

AREDN Phonebook is a SIP server that provides directory services for Amateur Radio Emergency Data Network (AREDN) mesh networks. During normal times, It automatically fetches a phonebook from common servers and maintains a copy on the router, making it easy for SIP phones to access directory listings across the mesh network. The router stores this phonebook so the lates copied version is always available.

## Features

- **Automatic Directory Updates**: Downloads phonebook from mesh servers every 30 minutes
- **Emergency Resilience**: Survives power outages with persistent storage
- **Flash-Friendly**: Minimizes writes to preserve router memory
- **Plug-and-Play**: Works immediately after installation
- **Phone Integration**: Provides XML directory for SIP phones (tested with Yealink)
- **Passive Safety**: Self-healing with automatic error recovery

## Installation

### Download

1. Go to the [Releases page](https://github.com/dhamstack/AREDN-Phonebook/releases)
2. Download the latest `AREDN-Phonebook-x.x.x-x_[architecture].ipk` file for your device:
   - **ath79**: Most common AREDN routers (e.g., Ubiquiti, MikroTik)
   - **x86**: PC-based AREDN nodes
   - **ipq40xx**: Some newer routers

### Install via AREDN Web Interface

1. **Access AREDN Node**: Connect to your AREDN node's web interface

2. **Navigate to Administration**: Go to **Administration** → **Package Management**

   ![image-20250925093930979](C:\Users\AndreasSpiess\AppData\Roaming\Typora\typora-user-images\image-20250925093930979.png)

3. **Upload Package**:
   - Click **Choose File** and select your downloaded `.ipk` file

     ![image-20250925094011144](C:\Users\AndreasSpiess\AppData\Roaming\Typora\typora-user-images\image-20250925094011144.png)

4. **Install**: Click **Fetch and Install**

## Configuration (optional, not needed for most users)

The phonebook server automatically configures itself. Default settings:

- **Configuration**: `/etc/sipserver.conf`
- **Service Commands**: `/etc/init.d/AREDN-Phonebook start|stop|restart|status`
- **SIP Port**: 5060
- **Directory URL**: `http://[your-node].local.mesh/arednstack/phonebook_generic_direct.xml`

## Phone Setup

Configure your SIP phone to use the node's directory:

1. **Directory URL**: `http://localnode.local.mesh/arednstack/phonebook_generic_direct.xml`
2. **SIP Server**: `localnode.local.mesh`
3. **Refresh**: Directory updates automatically every xx seconds from router (your Update Time Interval)

## Troubleshooting

### Check Service Status
```bash
ps | grep AREDN-Phonebook
logread | grep "AREDN-Phonebook"
```

### Verify Directory Files
```bash
ls -la /www/arednstack/phonebook*
curl http://localhost/arednstack/phonebook_generic_direct.xml
```

### Common Issues

- **No directory showing**: Wait up to 30 minutes for first download
- **Service not starting**: Check logs with `logread | tail -50`
- **Permission errors**: Ensure `/www/arednstack/` directory exists

## Technical Details

- **Emergency Boot**: Loads the existing phonebook immediately on startup
- **Persistent Storage**: Survives power cycles using `/www/arednstack/`
- **Flash Protection**: Only writes when phonebook content changes
- **Multi-threaded**: Background fetching doesn't affect SIP performance
- **Auto-healing**: Recovers from network failures and corrupt data

## Support

- **Issues**: [GitHub Issues](https://github.com/dhamstack/AREDN-Phonebook/issues)
- **Documentation**: [Functional Specification](AREDN-phonebook-fsd.md)
- **AREDN Community**: [AREDN Forums](https://www.arednmesh.org/)

## License

This project is released under open source license for amateur radio emergency communications.