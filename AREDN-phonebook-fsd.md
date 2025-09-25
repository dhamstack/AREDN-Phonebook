# AREDN Phonebook - Functional Specification Document v1.4.1

## 1. Overview

The AREDN Phonebook is an **emergency-resilient multi-threaded SIP proxy server** designed specifically for Amateur Radio Emergency Data Network (AREDN) mesh networks. It acts as a self-healing call routing hub that:

1. **Fetches phonebook data** from AREDN mesh servers via HTTP with emergency boot capability
2. **Manages SIP user registrations** and call routing with persistent storage
3. **Provides DNS-based call resolution** using `.local.mesh` domain
4. **Publishes phonebook data** as XML for web access with flash-friendly optimization
5. **Implements passive safety systems** for autonomous emergency operation

The system elegantly bridges **HTTP-based phonebook distribution** with **SIP call routing**, providing centralized directory services for distributed AREDN mesh voice communications while ensuring **emergency resilience through persistent storage and self-healing capabilities**.

### 1.1 Target Platforms

- **Atheros AR79xx architecture** (MikroTik devices)
- **IPQ40xx architecture** (AREDN mesh devices)
- **Intel 64-bit architecture** (x86_64 devices)

### 1.2 System Architecture

The system follows a modular C architecture with **4 main threads** and comprehensive synchronization for emergency resilience:

- **Main Thread**: Handles incoming SIP messages on UDP port 5060
- **Phonebook Fetcher Thread**: Downloads CSV phonebook data with emergency boot and flash-friendly optimization
- **Status Updater Thread**: Processes phonebook XML and manages user status updates
- **Passive Safety Thread**: Provides autonomous self-healing, thread recovery, and graceful degradation
- **Modular Components**: Separated functionality for maintainability and emergency operation

**Emergency Features:**
- **Emergency Boot**: Loads existing phonebook immediately on startup from persistent storage
- **Flash Protection**: Minimizes write operations to preserve router memory (99% reduction)
- **Thread Recovery**: Automatically detects and restarts hung background threads
- **Call Session Cleanup**: Removes stale sessions to prevent resource exhaustion
- **Configuration Self-Correction**: Fixes common deployment mistakes automatically

**Synchronization mechanisms:**
- `registered_users_mutex`: Protects user database operations
- `phonebook_file_mutex`: Protects file system operations with safe file handling
- `updater_trigger_cond`: Coordinates between fetcher and updater threads
- **Thread Health Tracking**: Heartbeat monitoring for passive safety recovery

## 2. Core Components

### 2.1 SIP Protocol Handling (`sip_core/`)

**Purpose**: Handles all SIP protocol message processing, parsing, and routing.

#### 2.1.1 Message Parsing Functions
- `extract_sip_header()`: Extracts specific SIP headers from messages
- `parse_user_id_from_uri()`: Parses user ID from SIP URIs
- `extract_uri_from_header()`: Extracts complete URIs from headers
- `extract_tag_from_header()`: Extracts tag parameters from headers
- `get_sip_method()`: Identifies SIP method (REGISTER, INVITE, etc.)

#### 2.1.2 Registration Process
**REGISTER Method Handling:**
1. Client sends `REGISTER` to server
2. Server extracts user ID and display name from SIP headers
3. Calls `add_or_update_registered_user()` to update database
4. User marked as active and available for calls
5. Server responds `200 OK` with 3600-second expiry
6. **No authentication required** (mesh network trust model)

#### 2.1.3 Call Establishment Process
**INVITE Method Handling:**
1. Client sends `INVITE` with target user ID
2. Server looks up target using `find_registered_user()`
3. **DNS resolution**: Constructs hostname `{user_id}.local.mesh`
4. Creates call session using `create_call_session()` for tracking
5. Server sends "100 Trying" response to caller
6. INVITE proxied to resolved callee address with reconstructed Request-URI
7. Callee responses proxied back to caller via session data
8. Call state updated based on response codes

#### 2.1.4 Call Termination Process
**BYE Method Handling:**
- Finds call session by Call-ID
- Determines caller vs callee by comparing addresses
- BYE proxied to other party
- Server responds "200 OK" to BYE sender
- Call session terminated and resources freed

**CANCEL Method Handling:**
- Only valid for INVITE_SENT or RINGING states
- Proxies CANCEL to callee
- Responds with "200 OK"
- Terminates call session

- **ACK**: Acknowledges call establishment
  - Proxies ACK to callee for ESTABLISHED calls

- **OPTIONS**: Capability negotiation
  - Responds with "200 OK" and supported methods

#### 2.1.3 Response Handling
- Processes SIP responses (200 OK, 180 Ringing, etc.)
- Routes responses back to original caller using stored call session data
- Updates call session state based on response codes
- Handles error responses (4xx, 5xx) by terminating sessions

#### 2.1.4 Address Resolution
- Uses DNS resolution for call routing
- Constructs hostnames: `{user_id}.local.mesh`
- Always uses port 5060 for SIP communication
- Falls back to "404 Not Found" if resolution fails

### 2.2 User Manager (`user_manager/`)

**Purpose**: Manages registered SIP users and phonebook directory entries.

**Data Structure**: `RegisteredUser`
- `user_id[MAX_PHONE_NUMBER_LEN]`: Numeric user identifier
- `display_name[MAX_DISPLAY_NAME_LEN]`: Full name (format: "FirstName LastName (Callsign)")
- `is_active`: Boolean indicating current registration status
- `is_known_from_directory`: Boolean indicating phonebook origin

**Key Functions**:

#### 2.2.1 User Lookup
- `find_registered_user()`: Finds active users by user_id
- Thread-safe with mutex protection
- Only returns users with `is_active = true`

#### 2.2.2 Dynamic Registration
- `add_or_update_registered_user()`: Handles SIP REGISTER requests
- Creates new users or updates existing ones
- Manages expiration (expires=0 deactivates registration)
- Differentiates between directory users and dynamic registrations
- Tracks counts: `num_registered_users` (dynamic), `num_directory_entries` (phonebook)

#### 2.2.3 Phonebook Integration
- `populate_registered_users_from_csv()`: Loads users from CSV phonebook
- `add_csv_user_to_registered_users_table()`: Adds directory entries
- Marks users as `is_known_from_directory = true`
- Handles UTF-8 sanitization and whitespace trimming
- Format: "FirstName LastName (Callsign)" for display names

#### 2.2.4 Data Management
- `init_registered_users_table()`: Clears all user data
- Thread-safe operations with `registered_users_mutex`
- Maximum capacity: `MAX_REGISTERED_USERS`

### 2.3 Call Session Management (`call-sessions/`)

**Purpose**: Tracks active SIP calls and maintains routing state.

**Data Structure**: `CallSession`
- `call_id[MAX_CONTACT_URI_LEN]`: Unique call identifier
- `state`: Call state (FREE, INVITE_SENT, RINGING, ESTABLISHED, TERMINATING)
- `original_caller_addr`: Address of call initiator
- `callee_addr`: Resolved address of call recipient
- `from_tag`, `to_tag`: SIP dialog identifiers
- `in_use`: Boolean indicating slot availability

**Key Functions**:

#### 2.3.1 Session Management
- `create_call_session()`: Allocates new call session slot
- `find_call_session_by_callid()`: Locates active sessions
- `terminate_call_session()`: Cleans up session data
- `init_call_sessions()`: Initializes session table

#### 2.3.2 State Tracking
- **CALL_STATE_FREE**: Unused session slot
- **CALL_STATE_INVITE_SENT**: INVITE proxied, awaiting response
- **CALL_STATE_RINGING**: 180 Ringing or 183 Session Progress received
- **CALL_STATE_ESTABLISHED**: 200 OK received for INVITE
- **CALL_STATE_TERMINATING**: BYE or CANCEL processed

#### 2.3.3 Routing Logic
- Stores both caller and callee addresses for bidirectional routing
- Routes responses back to `original_caller_addr`
- Routes requests to `callee_addr` (resolved via DNS)
- Handles both caller-initiated and callee-initiated BYE requests

### 2.4 Passive Safety System (`passive_safety/`)

**Purpose**: Provides autonomous self-healing and emergency resilience without human intervention.

The passive safety system operates continuously in the background, silently monitoring system health and automatically correcting issues before they impact emergency communications. This system is designed to ensure maximum uptime during critical situations.

#### 2.4.1 Call Session Management
**Stale Session Cleanup:**
- Monitors all active call sessions for abandonment
- Automatically terminates sessions older than 2 hours
- Prevents resource exhaustion in high-usage scenarios
- Logs cleanup actions for operational awareness
- Runs every 5 minutes to maintain system health

**Implementation:**
```c
void passive_cleanup_stale_call_sessions(void) {
    time_t now = time(NULL);
    for (int i = 0; i < MAX_CALL_SESSIONS; i++) {
        if (call_sessions[i].in_use) {
            time_t session_age = now - call_sessions[i].creation_time;
            if (session_age > 7200) { // 2 hours
                terminate_call_session(&call_sessions[i]);
            }
        }
    }
}
```

#### 2.4.2 Configuration Self-Correction
**Automatic Parameter Adjustment:**
- Validates phonebook fetch intervals (minimum 5 minutes)
- Corrects status update intervals (minimum 1 minute)
- Ensures at least one phonebook server is configured
- Prevents aggressive polling that could impact network performance
- Logs all corrections for transparency

#### 2.4.3 Graceful Degradation
**Adaptive Load Management:**
- Monitors active call session usage continuously
- Automatically reduces background activity under high load
- Doubles phonebook fetch interval when >80% call capacity reached
- Restores normal operation when load decreases
- Maintains SIP service priority over background tasks

#### 2.4.4 Thread Recovery System
**Hung Thread Detection:**
- Tracks heartbeat timestamps for critical threads
- Phonebook fetcher: 30-minute timeout detection
- Status updater: 20-minute timeout detection
- Automatically cancels and restarts hung threads
- Maintains service continuity without manual intervention

#### 2.4.5 Safe File Operations
**Atomic File Handling:**
- Creates backup of existing files before updates
- Verifies file integrity before committing changes
- Automatic rollback on corruption detection
- Never leaves system in broken state
- Protects against partial writes and power failures

**Operation Sequence:**
1. Create backup of current file
2. Write new data to temporary file
3. Verify temporary file integrity
4. Atomic rename to replace destination
5. Rollback if any step fails
6. Clean up temporary files

### 2.5 Phonebook Management System

**Purpose**: Downloads, processes, and publishes phonebook data from AREDN mesh servers with emergency resilience.

#### 2.5.1 Emergency Boot Sequence (`phonebook_fetcher/`)
**Immediate Service Availability:**
```c
// Emergency boot sequence: Load existing phonebook immediately if available
if (access(PB_CSV_PATH, F_OK) == 0) {
    LOG_INFO("Found existing phonebook CSV. Loading immediately for service availability.");
    populate_registered_users_from_csv(PB_CSV_PATH);
    LOG_INFO("Emergency boot: SIP user database loaded from persistent storage.");
    initial_population_done = true;
}
```

**Emergency Boot Features:**
- Checks for existing phonebook data immediately on startup
- Loads users from persistent storage (`/www/arednstack/phonebook.csv`)
- Provides instant directory service availability
- Continues with normal operation after emergency population
- Converts existing data to XML for web interface

#### 2.5.2 Flash-Friendly CSV Download Process
**Optimized Fetcher Thread Workflow:**
1. **Server Selection**: Tries configured servers in sequence until successful download
2. **Download to RAM**: Fetches CSV to temporary path (`/tmp/phonebook_download.csv`)
3. **Hash Comparison**: Calculates conceptual hash and compares with stored hash
4. **Flash Write Decision**: Only writes to flash if content actually changed
5. **Cross-Filesystem Copy**: Uses `file_utils_copy_file()` to move data to persistent storage

**Flash Optimization Benefits:**
- Reduces flash writes from ~240/day to ~1-2/day (99% reduction)
- Preserves router memory lifespan in embedded environments
- Downloads always occur in RAM to avoid unnecessary flash wear
- Hash-based change detection prevents redundant writes

#### 2.5.3 Data Processing Pipeline with Safe File Operations
**Enhanced CSV to User Database Pipeline:**
1. Populates user database via `populate_registered_users_from_csv()`
2. Converts CSV to XML via `csv_processor_convert_csv_to_xml_and_get_path()`
3. **Safe XML Publishing**: Uses `safe_phonebook_file_operation()` for atomic updates
4. Updates hash file only on successful processing (prevents corruption)
5. Signals status updater thread for additional processing

**Safe File Publishing Process:**
```c
void safe_phonebook_file_operation(const char *source_path, const char *dest_path) {
    // 1. Create backup of current file
    // 2. Copy new data to temporary file
    // 3. Verify temporary file integrity
    // 4. Atomic rename (replace destination)
    // 5. Rollback if any step fails
}
```

#### 2.5.4 Status Updates (`status_updater/`)
**Enhanced XML Processing and Status Management:**
- **Thread Coordination**: Triggered by fetcher signals or timer intervals
- **XML Parsing**: Reads published XML from persistent storage (`/www/arednstack/`)
- **Data Extraction**: Parses XML entries and extracts name/telephone data
- **Status Updates**: Marks users active/inactive based on XML presence
- **Name Cleanup**: Strips leading asterisks from names (inactive markers)
- **Database Sync**: Updates display names with latest phonebook data
- **Heartbeat Updates**: Updates `g_updater_last_heartbeat` for passive safety monitoring

#### 2.5.5 Persistent File Management
**Emergency-Resilient Storage:**
- **Persistent Paths**: All critical data stored in `/www/arednstack/` (survives reboots)
- **CSV Storage**: `/www/arednstack/phonebook.csv` (persistent user data)
- **Hash Storage**: `/www/arednstack/phonebook.csv.hash` (change detection)
- **XML Publication**: `/www/arednstack/phonebook_generic_direct.xml` (web access)
- **Temporary Files**: `/tmp/` used only for downloads (RAM-based)

**File Management Features:**
- Creates necessary directories using `file_utils_ensure_directory_exists()`
- Atomic file operations prevent corruption during power failures
- Cleanup of temporary files after successful processing
- Cross-filesystem copying for flash optimization
- Maintains synchronization between phonebook and user database

### 2.6 Configuration Loader (`config_loader/`)

**Purpose**: Loads runtime configuration from `/etc/sipserver.conf`.

**Configuration Parameters**:
- `PB_INTERVAL_SECONDS`: Phonebook fetch interval (default: 3600)
- `STATUS_UPDATE_INTERVAL_SECONDS`: Status update interval (default: 600)
- `SIP_HANDLER_NICE_VALUE`: Process priority for SIP handling (default: -5)
- `PHONEBOOK_SERVER`: Server definitions (host,port,path format)

**Key Functions**:
- `load_configuration()`: Parses configuration file
- Handles missing files gracefully (uses defaults)
- Supports multiple phonebook servers (up to `MAX_PB_SERVERS`)
- Validates numeric parameters and provides warnings

### 2.7 CSV Processor (`csv_processor/`)

**Purpose**: Handles CSV phonebook download, parsing, and XML conversion.

**Key Functions**:

#### 2.7.1 Download Management
- `csv_processor_download_csv()`: Downloads from configured servers
- Tries servers in order until successful download
- Uses HTTP requests to fetch CSV files
- Handles connection failures and retries next server

#### 2.7.2 Data Processing
- `sanitize_utf8()`: Cleans UTF-8 encoding in CSV data
- Parses CSV format: FirstName,LastName,Callsign,Location,Telephone
- Validates required fields (telephone number mandatory)
- Handles malformed lines gracefully with warnings

#### 2.7.3 XML Conversion
- `csv_processor_convert_csv_to_xml_and_get_path()`: Converts CSV to XML
- XML escapes special characters in data
- Creates structured XML format for web publication
- Generates temporary XML files for processing

#### 2.7.4 Hash Calculation
- `csv_processor_calculate_file_conceptual_hash()`: Generates file checksums
- Uses simple checksum algorithm for change detection
- Enables incremental processing (skip unchanged files)
- Stores hash as hexadecimal string

### 2.8 File Utils (`file_utils/`)

**Purpose**: Provides file system operations and utilities.

**Key Functions**:
- `file_utils_ensure_directory_exists()`: Creates directory paths
- `file_utils_publish_file_to_destination()`: Copies files atomically
- File existence checking and validation
- Cross-platform file operations

### 2.9 Log Manager (`log_manager/`)

**Purpose**: Centralized logging system with multiple severity levels.

**Log Levels**:
- `LOG_ERROR`: Critical errors requiring attention
- `LOG_WARN`: Warning conditions
- `LOG_INFO`: Informational messages
- `LOG_DEBUG`: Detailed debugging information

**Features**:
- Module-specific logging (MODULE_NAME macro)
- Thread-safe logging operations
- Configurable log levels
- Timestamp and process/thread identification

## 3. Network Communication & Configuration

### 3.1 Network Protocols
**SIP Communication:**
- **Transport**: UDP on port 5060
- **Message Size**: Maximum 2048 bytes (`MAX_SIP_MSG_LEN`)
- **Encoding**: UTF-8 with sanitization
- **Authentication**: None (mesh network trust model)

**HTTP Downloads:**
- **Protocol**: HTTP/1.1 for CSV phonebook downloads
- **Method**: GET requests to configured phonebook servers
- **Format**: CSV with specific column structure
- **Timeout**: Configurable per server

**DNS Resolution:**
- **Domain**: `.local.mesh` for AREDN mesh networks
- **Format**: `{user_id}.local.mesh`
- **Protocol**: Standard DNS A record lookups
- **Fallback**: Error response if resolution fails

### 3.2 Configuration System (`config_loader/`)
**Runtime Configuration** via `/etc/sipserver.conf`:
- `PB_INTERVAL_SECONDS`: Phonebook fetch interval (default: 3600)
- `STATUS_UPDATE_INTERVAL_SECONDS`: Status update interval (default: 600)
- `SIP_HANDLER_NICE_VALUE`: Process priority for SIP handling (default: -5)
- `PHONEBOOK_SERVER`: Server definitions (host,port,path format)

**File Paths:**
- **Config File**: `/etc/sipserver.conf`
- **CSV Storage**: `PB_CSV_PATH` (temporary)
- **XML Publication**: `PB_XML_PUBLIC_PATH` (web accessible)
- **Hash Storage**: `PB_LAST_GOOD_CSV_HASH_PATH`

**System Limits:**
- **Max Users**: `MAX_REGISTERED_USERS` (256)
- **Max Call Sessions**: `MAX_CALL_SESSIONS` (10)
- **Max Phonebook Servers**: `MAX_PB_SERVERS` (5)
- **String Lengths**: Various `MAX_*_LEN` constants

## 6. Error Handling

### 6.1 SIP Errors
- **404 Not Found**: User not registered or DNS resolution failed
- **503 Service Unavailable**: Maximum call sessions reached
- **481 Call/Transaction Does Not Exist**: Invalid Call-ID for BYE/CANCEL
- **501 Not Implemented**: Unsupported SIP methods

### 6.2 System Errors
- **File Access**: Graceful handling of missing/unreadable files
- **Network Errors**: Retry logic for phonebook downloads
- **Memory Limits**: Maximum user/session limits enforced
- **Threading Errors**: Mutex protection and error logging

### 6.3 Recovery Mechanisms
- **Default Configuration**: Uses defaults if config file missing
- **Hash Comparison**: Skips processing for unchanged phonebooks
- **Resource Cleanup**: Automatic session termination and cleanup
- **Continuous Operation**: Threads continue despite individual failures

## 7. Security Considerations

### 7.1 Trust Model
- **No Authentication**: Relies on mesh network physical security
- **DNS Trust**: Assumes DNS infrastructure integrity
- **Local Network**: Designed for closed AREDN mesh networks

### 7.2 Input Validation
- **UTF-8 Sanitization**: Cleans incoming CSV data
- **Buffer Protection**: Fixed-size buffers with bounds checking
- **SIP Parsing**: Robust parsing with malformed message handling
- **XML Escaping**: Prevents XML injection in generated output

### 7.3 Resource Protection
- **Maximum Limits**: Enforced limits on users and sessions
- **File Access**: Controlled file system access patterns
- **Thread Safety**: Mutex protection for shared data structures
- **Memory Management**: Static allocation with bounds checking

## 8. Build System and Deployment

### 8.1 GitHub Actions CI/CD Pipeline

The project uses GitHub Actions to automatically build OpenWRT IPK packages for multiple target architectures. The CI/CD pipeline is defined in `.github/workflows/build-ipk.yml` and provides automated building, testing, and deployment capabilities for the AREDN-Phonebook emergency communication system.

#### 8.1.1 Trigger Conditions

The build pipeline is triggered by:
- **Tag pushes**: Any tag following semantic versioning format (`*.*.*`, e.g., `1.4.1`)
- **Pull requests**: Validates builds before merging changes
- **Manual dispatch**: Allows on-demand building for testing

#### 8.1.2 Build Matrix

The pipeline builds packages for multiple target architectures commonly used in AREDN networks:

| Architecture | Target | OpenWRT SDK | Common Devices |
|-------------|--------|-------------|----------------|
| ath79/generic | AR79xx devices | openwrt-sdk-23.05.3-ath79-generic | MikroTik, Ubiquiti |
| x86/64 | Intel 64-bit devices | openwrt-sdk-23.05.3-x86-64 | PC-based AREDN nodes |
| ipq40xx/generic | IPQ40xx devices | openwrt-sdk-23.05.3-ipq40xx-generic | Modern AREDN mesh routers |

#### 8.1.3 Enhanced Build Process

1. **Environment Setup**:
   - Uses Ubuntu latest runner with enhanced toolchain
   - Downloads appropriate OpenWRT SDK 23.05.3 for each target
   - Configures cross-compilation environment
   - Sets up static linking for embedded deployment

2. **Package Injection**:
   - Copies `Phonebook/` directory to SDK package directory as `AREDN-Phonebook`
   - Preserves passive safety configuration and emergency boot scripts
   - Ensures proper ProCD service integration for OpenWRT

3. **Static Compilation**:
   - Runs `make defconfig` to configure build environment
   - Executes `make package/AREDN-Phonebook/compile V=s` with verbose output
   - Uses static linking (`-static`) to eliminate runtime dependencies
   - Compiles with emergency resilience and flash optimization features

4. **Release Management**:
   - Creates GitHub release automatically for tagged versions
   - Uploads compiled IPK files with descriptive names
   - Generates release notes with emergency feature highlights
   - Names assets as `AREDN-Phonebook-{version}-{architecture}.ipk`

#### 8.1.4 Artifact Outputs

For each successful build (e.g., v1.4.1):
- **AREDN-Phonebook-1.4.1-1_ath79.ipk**: Emergency-resilient package for AR79xx
- **AREDN-Phonebook-1.4.1-1_x86_64.ipk**: Package for Intel 64-bit devices
- **AREDN-Phonebook-1.4.1-1_ipq40xx.ipk**: Package for IPQ40xx mesh devices

**Package Contents:**
- Statically-linked binary with passive safety features
- ProCD service scripts for automatic startup
- Emergency boot configuration
- Flash-friendly optimization settings

### 8.2 Local Development Build

For developers working on the codebase locally:

```bash
cd Phonebook
make defconfig
make package/AREDN-Phonebook/compile V=s
```

**Requirements**:
- OpenWRT SDK properly installed and configured
- Cross-compilation toolchain for target architecture
- Build dependencies (make, gcc, pthread support)
- Static linking capability for embedded deployment

### 8.3 Emergency Deployment Process

#### 8.3.1 Creating Emergency Releases

To trigger automatic IPK generation for emergency updates:

1. **Tag the release**:
   ```bash
   git tag 1.4.1
   git push origin 1.4.1
   ```

2. **GitHub Actions automatically**:
   - Builds IPKs for all AREDN target architectures
   - Creates release page with emergency feature notes
   - Attaches IPK files with descriptive names
   - Validates emergency resilience features

#### 8.3.2 Installation on AREDN Nodes

**Via AREDN Web Interface (Recommended):**
1. Download appropriate IPK file for your device architecture
2. Access AREDN node web interface
3. Navigate to **Administration** → **Package Management**
4. Click **Choose File** and select downloaded IPK
5. Click **Upload Package** then **Install**
6. **Reboot node** to activate emergency features

**Via Command Line:**
```bash
# Upload IPK file to router, then:
opkg install AREDN-Phonebook-*.ipk

# Start the emergency-resilient service
/etc/init.d/AREDN-Phonebook start

# Enable automatic startup (critical for emergency deployment)
/etc/init.d/AREDN-Phonebook enable

# Verify emergency boot capability
logread | grep "Emergency boot"
```

**Verification Commands:**
```bash
# Check service status
ps | grep AREDN-Phonebook

# Verify persistent storage setup
ls -la /www/arednstack/

# Test emergency boot
/etc/init.d/AREDN-Phonebook restart
logread | tail -20 | grep "AREDN-Phonebook"
```

### 8.4 Maintenance and Updates

#### 8.4.1 SDK Version Management

The build system uses OpenWRT 23.05.3 SDK. To update:

1. Modify SDK URLs in `.github/workflows/build-ipk.yml`
2. Test compatibility with new SDK version
3. Update documentation to reflect supported OpenWRT versions

#### 8.4.2 Adding New Architectures

To support additional AREDN hardware:

1. Identify required OpenWRT SDK for target architecture
2. Add new entry to build matrix in workflow file
3. Test build process with new architecture
4. Update documentation with supported platforms

#### 8.4.3 Build Troubleshooting

Common build issues and solutions:

- **SDK Download Failures**: Check OpenWRT download URLs and versions
- **Compilation Errors**: Verify source code compatibility with OpenWRT toolchain
- **Missing Dependencies**: Ensure all required libraries are available in SDK
- **Permission Issues**: Verify GitHub Actions permissions for release creation

The automated build system ensures consistent, reproducible packages across all supported AREDN hardware platforms while minimizing manual intervention and potential deployment errors.

## 9. Health Reporting System

### 9.1 Reporting Overview

The AREDN Phonebook SIP Server includes a comprehensive health reporting system designed to provide network-wide visibility and proactive monitoring for emergency communication networks. The system transmits three types of messages to phonebook servers for centralized collection and analysis.

### 9.2 Message Types

#### 9.2.1 ALARM Messages (Immediate Transmission)

Critical issues requiring immediate attention are transmitted instantly to enable rapid response during emergencies.

**Service Critical Alarms:**
- `service_crashed` - SIP proxy service stopped responding
- `thread_hung` - Background thread not responding for >30 minutes
- `memory_critical` - Memory usage exceeds 95%
- `no_call_sessions` - All call session slots exhausted
- `config_error` - Configuration file corrupted or invalid

**Network Critical Alarms:**
- `node_isolated` - Cannot reach any mesh neighbors
- `phonebook_servers_down` - All phonebook servers unreachable for >1 hour
- `dns_failure` - DNS resolution completely failed

#### 9.2.2 WARNING Messages (Every 6 Hours)

Non-critical issues that require scheduled attention are reported every 6 hours to prevent alarm fatigue while maintaining operational awareness.

**Service Warnings:**
- `memory_high` - Memory usage >85% for >1 hour
- `call_success_low` - Call success rate <70% for >30 minutes
- `phonebook_stale` - Phonebook not updated for >6 hours
- `thread_restart` - Background thread was automatically restarted
- `high_sip_errors` - SIP error rate >10% for >2 hours

**Network Warnings:**
- `dns_degraded` - DNS resolution success rate <80%
- `network_slow` - Average response time >5 seconds
- `network_errors_high` - Network error rate >10%
- `phonebook_fetch_failed` - Cannot download phonebook updates

#### 9.2.3 TRAFFIC REPORT Messages (Every 6 Hours)

Comprehensive operational statistics for network usage analysis and emergency coordination planning.

**Node Statistics:**
- `uptime` - Seconds since service start
- `active_users` - Currently registered SIP users
- `memory_pct` - Current memory usage percentage
- `calls_6h` - Total calls initiated in last 6 hours
- `calls_success_rate` - Percentage of successful calls
- `avg_call_duration` - Average call length in seconds
- `sip_errors_6h` - Count of SIP protocol errors
- `dns_success_rate` - DNS resolution success percentage
- `avg_response_time_ms` - Network response time in milliseconds
- `phonebook_age_minutes` - Minutes since last phonebook update

**Call Detail Records (For Network Traffic Analysis):**
- `call_start` - Timestamp when call began
- `call_end` - Timestamp when call ended (null if ongoing)
- `caller_node` - Node hostname where call originated
- `callee_node` - Node hostname being called
- `caller_id` - SIP user ID making the call
- `callee_id` - SIP user ID receiving the call
- `call_success` - Boolean indicating successful call completion
- `call_duration` - Call duration in seconds (when call ends)
- `failure_reason` - Error code if call failed

### 9.3 JSON Message Structure

#### 9.3.1 Alarm Message Format
```json
{
  "type": "alarm",
  "timestamp": 1737750123,
  "node": "HB9ABC-mikrotik",
  "severity": "critical",
  "alarm_id": "service_crashed",
  "message": "SIP proxy service stopped responding"
}
```

#### 9.3.2 Traffic Report Format
```json
{
  "type": "traffic_report",
  "timestamp": 1737750123,
  "node": "HB9ABC-mikrotik",
  "period_hours": 6,
  "node_stats": {
    "uptime": 86400,
    "active_users": 12,
    "memory_pct": 45,
    "calls_6h": 15,
    "calls_success_rate": 94,
    "avg_call_duration": 180
  },
  "call_records": [
    {
      "call_start": 1737750123,
      "call_end": 1737750303,
      "caller_node": "HB9ABC-mikrotik",
      "callee_node": "emergency.local.mesh",
      "caller_id": "1234",
      "callee_id": "emergency",
      "call_success": true,
      "call_duration": 180
    }
  ]
}
```

### 9.4 Backend Network Analysis

The centralized backend processes raw node data to provide network-wide insights for emergency coordination:

#### 9.4.1 Traffic Pattern Analysis
- **Total Network Calls** - Aggregated call volume across all nodes
- **Peak Concurrent Usage** - Highest simultaneous active calls
- **Communication Matrix** - Inter-node calling patterns
- **Busiest Routes** - Most frequent caller/callee pairs
- **Geographic Hotspots** - Areas with highest call volume
- **Emergency Service Usage** - Calls to critical services

#### 9.4.2 Network Performance Assessment
- **Network-Wide Success Rate** - Average success rates across nodes
- **Network Response Quality** - Aggregated response times
- **Service Availability** - Percentage of nodes reporting healthy status
- **Resource Utilization** - Node capacity and performance metrics

#### 9.4.3 Emergency Operations Intelligence
- **Service Coverage** - Active voice service areas
- **Communication Efficiency** - Call setup times and reliability
- **Emergency Readiness** - Availability of critical communication paths
- **Network Reliability** - Consistency of service across mesh segments

### 9.5 Reporting Infrastructure

The reporting system leverages the existing phonebook server infrastructure to minimize network complexity and maximize reliability. Health reports are transmitted to the same servers used for phonebook distribution, ensuring consistent network paths and simplified configuration management.

This functional specification provides a comprehensive overview of the AREDN Phonebook SIP Server implementation based on the current codebase analysis.