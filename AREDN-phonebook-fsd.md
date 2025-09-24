# AREDN Phonebook SIP Server - Functional Specification Document

## 1. Overview

The AREDN Phonebook SIP Server is a **multi-threaded SIP proxy server** designed specifically for Amateur Radio Emergency Data Network (AREDN) mesh networks. It acts as a centralized call routing hub that:

1. **Fetches phonebook data** from AREDN mesh servers via HTTP
2. **Manages SIP user registrations** and call routing
3. **Provides DNS-based call resolution** using `.local.mesh` domain
4. **Publishes phonebook data** as XML for web access

The system elegantly bridges **HTTP-based phonebook distribution** with **SIP call routing**, providing centralized directory services for distributed AREDN mesh voice communications.

### 1.1 Target Platforms

- **Atheros AR79xx architecture** (MikroTik devices)
- **IPQ40xx architecture** (AREDN mesh devices)
- **Intel 64-bit architecture** (x86_64 devices)

### 1.2 System Architecture

The system follows a modular C architecture with **3 main threads** and careful synchronization:

- **Main Thread**: Handles incoming SIP messages on UDP port 5060
- **Phonebook Fetcher Thread**: Downloads CSV phonebook data periodically and converts to XML
- **Status Updater Thread**: Processes phonebook XML and manages user status updates
- **Modular Components**: Separated functionality for maintainability

**Synchronization mechanisms:**
- `registered_users_mutex`: Protects user database operations
- `phonebook_file_mutex`: Protects file system operations
- `updater_trigger_cond`: Coordinates between fetcher and updater threads

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

### 2.4 Phonebook Management System

**Purpose**: Downloads, processes, and publishes phonebook data from AREDN mesh servers.

#### 2.4.1 CSV Download Process (`phonebook_fetcher/`)
**Fetcher Thread Workflow:**
1. **Server Selection**: Tries configured servers in sequence until successful download
2. **Download CSV**: Fetches phonebook with format `FirstName,LastName,Callsign,Location,Telephone`
3. **Change Detection**: Calculates file hash to detect changes vs previous version
4. **Skip Processing**: If identical to previous version (after initial population)

#### 2.4.2 Data Processing Pipeline
**CSV to User Database:**
1. Populates user database via `populate_registered_users_from_csv()`
2. Converts CSV to XML via `csv_processor_convert_csv_to_xml_and_get_path()`
3. Publishes XML to public path via `publish_phonebook_xml()`
4. Updates hash file on successful processing
5. Signals status updater thread for additional processing

#### 2.4.3 Status Updates (`status_updater/`)
**XML Processing and Status Management:**
- **Thread Coordination**: Triggered by fetcher signals or timer intervals
- **XML Parsing**: Reads published XML from `PB_XML_PUBLIC_PATH`
- **Data Extraction**: Parses XML entries and extracts name/telephone data
- **Status Updates**: Marks users active/inactive based on XML presence
- **Name Cleanup**: Strips leading asterisks from names (inactive markers)
- **Database Sync**: Updates display names with latest phonebook data

#### 2.4.4 File Management
- Creates necessary directories using `file_utils_ensure_directory_exists()`
- Publishes XML to `PB_XML_PUBLIC_PATH` for web access
- Maintains hash file at `PB_LAST_GOOD_CSV_HASH_PATH`
- Cleans up temporary files after processing
- Handles user lifecycle: creation, activation, deactivation
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

The project uses GitHub Actions to automatically build OpenWRT IPK packages for multiple target architectures. The CI/CD pipeline is defined in `.github/workflows/build-ipk.yml` and provides automated building, testing, and deployment capabilities.

#### 8.1.1 Trigger Conditions

The build pipeline is triggered by:
- **Tag pushes**: Any tag following semantic versioning format (`*.*.*`, e.g., `1.2.3`)
- **Pull requests**: Validates builds before merging changes

#### 8.1.2 Build Matrix

The pipeline builds packages for multiple target architectures:

| Architecture | Target | OpenWRT SDK |
|-------------|--------|-------------|
| ath79/generic | AR79xx devices (MikroTik) | openwrt-sdk-23.05.3-ath79-generic |
| x86/64 | Intel 64-bit devices | openwrt-sdk-23.05.3-x86-64 |
| ipq40xx/generic | IPQ40xx devices (AREDN mesh) | openwrt-sdk-23.05.3-ipq40xx-generic |

#### 8.1.3 Build Process

1. **Environment Setup**:
   - Uses Ubuntu latest runner
   - Downloads appropriate OpenWRT SDK 23.05.3 for each target
   - Extracts SDK to build environment

2. **Package Injection**:
   - Copies `Phonebook/` directory to SDK package directory
   - Preserves configuration files and build scripts
   - Ensures proper directory structure for OpenWRT build system

3. **Compilation**:
   - Runs `make defconfig` to configure build environment
   - Executes `make package/phonebook/compile V=s` with verbose output
   - Uses all available CPU cores (`-j$(nproc)`) for parallel compilation

4. **Release Management**:
   - Creates GitHub release automatically for tagged versions
   - Uploads compiled IPK files as release assets
   - Names assets as `phonebook-{architecture}.ipk`

#### 8.1.4 Artifact Outputs

For each successful build:
- **phonebook-ath79.ipk**: Package for Atheros AR79xx architecture
- **phonebook-x86.ipk**: Package for Intel x86_64 architecture
- **phonebook-ipq40xx.ipk**: Package for IPQ40xx architecture

### 8.2 Local Development Build

For developers working on the codebase locally:

```bash
cd Phonebook
make defconfig
make package/phonebook/compile V=s
```

**Requirements**:
- OpenWRT SDK properly installed and configured
- Cross-compilation toolchain for target architecture
- Build dependencies (make, gcc, etc.)

### 8.3 Deployment Process

#### 8.3.1 Creating Releases

To trigger automatic IPK generation:

1. **Tag the release**:
   ```bash
   git tag 1.0.0
   git push origin 1.0.0
   ```

2. **GitHub Actions automatically**:
   - Builds IPKs for all target architectures
   - Creates release page with version notes
   - Attaches IPK files for download

#### 8.3.2 Installation on AREDN Nodes

1. Download appropriate IPK file for target architecture
2. Copy to AREDN node via web interface or SCP
3. Install using OpenWRT package manager:
   ```bash
   opkg install phonebook-{architecture}.ipk
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

This functional specification provides a comprehensive overview of the AREDN Phonebook SIP Server implementation based on the current codebase analysis.