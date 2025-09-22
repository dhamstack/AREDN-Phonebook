# AREDN Phonebook SIP Server - Functional Specification Document

## 1. Overview

The AREDN Phonebook SIP Server is a specialized SIP proxy server designed for Amateur Radio Emergency Data Network (AREDN) mesh networks. The server provides centralized phonebook functionality by fetching CSV phonebook data from AREDN mesh servers and managing SIP user registrations and call routing between mesh nodes.

### 1.1 System Architecture

The system follows a modular C architecture with multi-threaded design:

- **Main Thread**: SIP message processing via UDP socket on port 5060
- **Phonebook Fetcher Thread**: Periodic CSV downloads and XML conversion
- **Status Updater Thread**: User status management and phonebook updates
- **Modular Components**: Separated functionality for maintainability

## 2. Core Components

### 2.1 SIP Core (`sip_core/`)

**Purpose**: Handles all SIP protocol message processing, parsing, and routing.

**Key Functions**:

#### 2.1.1 Message Parsing
- `extract_sip_header()`: Extracts specific SIP headers from messages
- `parse_user_id_from_uri()`: Parses user ID from SIP URIs
- `extract_uri_from_header()`: Extracts complete URIs from headers
- `extract_tag_from_header()`: Extracts tag parameters from headers
- `get_sip_method()`: Identifies SIP method (REGISTER, INVITE, etc.)

#### 2.1.2 SIP Method Handling
- **REGISTER**: Processes user registrations
  - Extracts user ID and display name from From header
  - Calls `add_or_update_registered_user()`
  - Responds with "200 OK" and sets expiry to 3600 seconds
  - No authentication required (mesh network trust model)

- **INVITE**: Handles call initiation
  - Looks up callee using `find_registered_user()`
  - Resolves callee hostname using DNS (format: `{user_id}.local.mesh`)
  - Creates call session using `create_call_session()`
  - Sends "100 Trying" response
  - Proxies INVITE to resolved callee address
  - Reconstructs message with new Request-URI

- **BYE**: Terminates calls
  - Finds call session by Call-ID
  - Determines caller vs callee by comparing addresses
  - Proxies BYE to other party
  - Responds with "200 OK"
  - Terminates call session

- **CANCEL**: Cancels pending calls
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

### 2.4 Phonebook Fetcher (`phonebook_fetcher/`)

**Purpose**: Downloads CSV phonebook data from configured AREDN mesh servers.

**Key Functions**:

#### 2.4.1 Main Thread Loop
- `phonebook_fetcher_thread()`: Main execution loop
- Runs continuously with configurable intervals (`g_pb_interval_seconds`)
- Downloads CSV, converts to XML, publishes results
- Handles hash-based change detection

#### 2.4.2 Download Process
1. Calls `csv_processor_download_csv()` to fetch from configured servers
2. Calculates file hash using `csv_processor_calculate_file_conceptual_hash()`
3. Compares with previous hash to detect changes
4. Skips processing if no changes detected (after initial population)

#### 2.4.3 Processing Pipeline
1. Populates user database via `populate_registered_users_from_csv()`
2. Converts CSV to XML via `csv_processor_convert_csv_to_xml_and_get_path()`
3. Publishes XML to public path via `publish_phonebook_xml()`
4. Updates hash file on successful processing
5. Signals status updater thread for additional processing

#### 2.4.4 File Management
- Creates necessary directories using `file_utils_ensure_directory_exists()`
- Publishes XML to `PB_XML_PUBLIC_PATH` for web access
- Maintains hash file at `PB_LAST_GOOD_CSV_HASH_PATH`
- Cleans up temporary files after processing

### 2.5 Status Updater (`status_updater/`)

**Purpose**: Processes phonebook XML and manages user status updates.

**Key Functions**:

#### 2.5.1 Thread Coordination
- `status_updater_thread()`: Main execution loop
- Triggered by phonebook fetcher signals or timer intervals
- Uses condition variable `updater_trigger_cond` for coordination
- Configurable interval: `g_status_update_interval_seconds`

#### 2.5.2 XML Processing
- Reads published XML from `PB_XML_PUBLIC_PATH`
- Parses XML entries and extracts name/telephone data
- Strips leading asterisks from names (inactive markers)
- Updates user status based on XML content

#### 2.5.3 Status Management
- Marks users as active/inactive based on XML presence
- Updates display names with latest phonebook data
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

## 3. Data Flow

### 3.1 Registration Flow
1. SIP client sends REGISTER request to server
2. `process_incoming_sip_message()` identifies REGISTER method
3. `add_or_update_registered_user()` updates user database
4. Server responds with "200 OK" and expires=3600
5. User marked as active and available for calls

### 3.2 Call Establishment Flow
1. Caller sends INVITE to server
2. Server looks up callee using `find_registered_user()`
3. DNS resolution for callee hostname (`{user_id}.local.mesh`)
4. `create_call_session()` allocates session tracking
5. Server sends "100 Trying" to caller
6. INVITE proxied to resolved callee address
7. Callee responses proxied back to caller via session data
8. Call state updated based on response codes

### 3.3 Phonebook Update Flow
1. Phonebook fetcher downloads CSV from configured servers
2. Hash calculation determines if changes exist
3. CSV parsed and user database populated
4. CSV converted to XML format
5. XML published to public web path
6. Status updater signaled for additional processing
7. Status updater reads XML and updates user statuses

### 3.4 Call Termination Flow
1. Either party sends BYE request
2. Server identifies call session by Call-ID
3. BYE proxied to other party
4. Server responds "200 OK" to BYE sender
5. Call session terminated and resources freed

## 4. Network Communication

### 4.1 SIP Protocol
- **Transport**: UDP on port 5060
- **Message Size**: Maximum 2048 bytes (`MAX_SIP_MSG_LEN`)
- **Encoding**: UTF-8 with sanitization
- **Authentication**: None (mesh network trust model)

### 4.2 HTTP Downloads
- **Protocol**: HTTP/1.1 for CSV downloads
- **Method**: GET requests to configured phonebook servers
- **Format**: CSV with specific column structure
- **Timeout**: Configurable per server

### 4.3 DNS Resolution
- **Domain**: `.local.mesh` for AREDN mesh networks
- **Format**: `{user_id}.local.mesh`
- **Protocol**: Standard DNS A record lookups
- **Fallback**: Error response if resolution fails

## 5. Configuration

### 5.1 File Paths
- **Config File**: `/etc/sipserver.conf`
- **CSV Storage**: `PB_CSV_PATH` (temporary)
- **XML Publication**: `PB_XML_PUBLIC_PATH` (web accessible)
- **Hash Storage**: `PB_LAST_GOOD_CSV_HASH_PATH`

### 5.2 Limits and Constants
- **Max Users**: `MAX_REGISTERED_USERS`
- **Max Call Sessions**: `MAX_CALL_SESSIONS`
- **Max Phonebook Servers**: `MAX_PB_SERVERS`
- **String Lengths**: Various `MAX_*_LEN` constants

### 5.3 Threading
- **Main Thread**: SIP message processing
- **Fetcher Thread**: Phonebook management
- **Updater Thread**: Status synchronization
- **Synchronization**: Mutexes and condition variables

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

This functional specification provides a comprehensive overview of the AREDN Phonebook SIP Server implementation based on the current codebase analysis.