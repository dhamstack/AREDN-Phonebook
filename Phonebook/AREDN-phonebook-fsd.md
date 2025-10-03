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

### 2.9 VoIP Quality Monitoring (`phone_quality_monitor/`, `sip_quality_lib`)

**Purpose**: Background VoIP quality testing for registered phones using short RTP/RTCP probe calls.

**Architecture**:
- **Response Queue**: Thread-safe circular buffer for SIP message routing
- **Shared Socket**: Uses main SIP server socket (port 5060) for probe calls
- **Message Routing**: Main server filters quality monitor responses by From header

#### 2.9.1 Quality Monitor Thread (`phone_quality_monitor.c`)

**Main Loop**:
- Runs continuously with configurable test interval (default: 300s)
- Tests all reachable phones (both CSV directory and dynamic registrations)
- Uses DNS resolution (`{user_id}.local.mesh`) to verify phone is online
- Delays between tests (default: 1s) to avoid network congestion

**Data Structures**:
- `phone_quality_record_t`: Stores test results per phone
  - `phone_number[32]`: Phone extension
  - `phone_ip[64]`: Resolved IP address
  - `last_test_time`: Timestamp of last test
  - `last_result`: VoIP probe result structure
  - `valid`: Record validity flag

- `response_queue_entry_t`: Circular buffer for SIP responses
  - `buffer[4096]`: SIP message buffer
  - `len`: Message length
  - `valid`: Entry validity flag

**Key Functions**:
- `quality_monitor_init()`: Initializes monitor with server socket and IP
- `quality_monitor_start()`: Spawns monitoring thread
- `quality_monitor_stop()`: Gracefully stops monitoring
- `quality_monitor_handle_response()`: Enqueues SIP responses from main server
- `quality_monitor_dequeue_response()`: Dequeues responses with timeout
- `quality_monitor_get_record()`: Retrieves quality data for a phone
- `quality_monitor_get_all_records()`: Exports all quality records

**Configuration**:
- `enabled`: Enable/disable monitoring (default: 1)
- `test_interval_sec`: Interval between test cycles (default: 300)
- `cycle_delay_sec`: Delay between individual phone tests (default: 1)
- `probe_config`: VoIP probe parameters (timeouts, RTP settings)

**Output**:
- JSON file: `/tmp/phone_quality.json` with test results
- Format: Array of phone objects with metrics (RTT, jitter, loss, status)
- Updated after each test cycle
- Consumed by CGI endpoint `/www/cgi-bin/quality`

#### 2.9.2 SIP Quality Library (`sip_quality_lib.c`)

**Purpose**: Performs VoIP quality tests via short INVITE+RTP/RTCP probe calls.

**Test Flow**:
1. **OPTIONS Probe**: Verify phone responds (fast check)
2. **INVITE**: Initiate test call with auto-answer hints
3. **RTP Burst**: Send ~1.2s of RTP packets (PCMU, ptime=40ms)
4. **RTCP Exchange**: Send SR, receive RR for metrics
5. **BYE**: Terminate call and extract metrics

**Probe Configuration** (`voip_probe_config_t`):
- `invite_timeout_ms`: INVITE response timeout (default: 5000)
- `burst_duration_ms`: RTP burst length (default: 1200)
- `rtp_ptime_ms`: RTP packet interval (default: 40)
- `rtcp_wait_ms`: Time to wait for RTCP RR (default: 500)

**Probe Results** (`voip_probe_result_t`):
- `status`: Test outcome (SUCCESS, BUSY, NO_RR, TIMEOUT, ERROR, NO_ANSWER)
- `sip_rtt_ms`: SIP round-trip time (INVITE → 200 OK)
- `icmp_rtt_ms`: Network RTT via ICMP ping
- `media_rtt_ms`: Media RTT from RTCP (LSR + DLSR)
- `jitter_ms`: Jitter from RTCP RR (converted from timestamp units)
- `loss_fraction`: Packet loss percentage (from RTCP RR)
- `packets_lost`: Lost packet count
- `packets_sent`: Total packets sent
- `status_reason[128]`: Human-readable status description

**SIP Message Construction**:
- From: `<sip:test@{server_ip}>` (unique signature for filtering)
- To: `<sip:{phone_number}@{phone_ip}>`
- Call-Info: `answer-after=0` (auto-answer hint)
- Alert-Info: `alert-autoanswer` (Grandstream compatibility)
- User-Agent: `AREDN-Phonebook-Monitor`

**SDP Offer**:
```
v=0
o=test {timestamp} 1 IN IP4 {server_ip}
s=Quality Test
c=IN IP4 {server_ip}
t=0 0
m=audio {rtp_port} RTP/AVP 0
a=rtcp:{rtcp_port}
a=rtpmap:0 PCMU/8000
a=ptime:40
a=maxptime:40
a=sendrecv
```

**RTP/RTCP Processing**:
- RTP: PCMU codec (payload type 0), 40ms ptime, 320 samples/packet
- RTCP SR: Sent at t=0 and t≈1.0s with NTP timestamp for RTT calculation
- RTCP RR: Parsed for jitter (timestamp units → ms), loss fraction, cumulative loss
- Local jitter calculation: RFC 3550 interarrival jitter algorithm as fallback

**Key Functions**:
- `test_phone_quality()`: Standalone test (creates own socket)
- `test_phone_quality_with_socket()`: Integrated test (uses server socket)
- `recv_sip_response()`: Wrapper for socket/queue-based reception
- `send_invite()`: Constructs and sends INVITE with SDP
- `send_ack()`: Acknowledges 200 OK
- `send_bye()`: Terminates call
- `send_rtcp_sr()`: Sends RTCP Sender Report
- `parse_rtcp_rr()`: Extracts metrics from Receiver Report
- `get_default_config()`: Returns default probe configuration

**Socket Management**:
- Uses `use_response_queue` flag to switch between direct socket and response queue
- When `external_sip_sock >= 0`: Uses response queue (shared socket mode)
- When `external_sip_sock == -1`: Uses dedicated socket (standalone mode)
- Weak symbol stub for standalone builds without queue support

#### 2.9.3 Message Routing (`main.c`)

**Purpose**: Routes SIP responses between main server and quality monitor.

**Implementation**:
```c
// Check if this is a quality monitor response
// Quality monitor uses "From: <sip:test@" signature
if (strstr(buffer, "From: <sip:test@") != NULL ||
    strstr(buffer, "f: <sip:test@") != NULL) {
    quality_monitor_handle_response(buffer, n);
} else {
    process_incoming_sip_message(sockfd, buffer, n, &cliaddr, len);
}
```

**Rationale**:
- Prevents race condition: Both threads reading from same socket
- Main server receives all messages on port 5060
- Quality monitor responses identified by unique From header
- Normal SIP traffic processed by existing server logic
- Clean separation of concerns

#### 2.9.4 Response Queue Mechanism

**Circular Buffer**:
- Size: 10 entries (MAX_RESPONSE_QUEUE)
- Entry size: 4096 bytes (MAX_RESPONSE_SIZE)
- Thread-safe with mutex and condition variable
- Overflow handling: Drop oldest message (with warning)

**Queue Operations**:
- `quality_monitor_handle_response()`: Enqueue from main thread
  - Validates buffer length
  - Acquires mutex
  - Checks for overflow
  - Copies message to queue
  - Signals waiting thread
  - Releases mutex

- `quality_monitor_dequeue_response()`: Dequeue from monitor thread
  - Acquires mutex
  - Waits on condition variable with timeout
  - Copies message from queue
  - Updates read pointer
  - Releases mutex
  - Returns bytes read or 0 (timeout) or -1 (error)

**Synchronization**:
- `g_response_queue_mutex`: Protects queue data structure
- `g_response_queue_cond`: Signals new message availability
- `pthread_cond_timedwait()`: Implements timeout for probe waits

### 2.10 Log Manager (`log_manager/`)

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

### 3.5 VoIP Quality Monitoring Flow
1. **Initialization**:
   - Quality monitor initialized with server socket and IP
   - Response queue and synchronization primitives created
   - Monitor thread spawned

2. **Test Cycle**:
   - Monitor thread wakes up (interval or on demand)
   - Iterates through all registered users
   - DNS lookup for each user (`{user_id}.local.mesh`)
   - Skip unreachable phones (DNS failure)

3. **Individual Phone Test**:
   - **Phase 1 - OPTIONS**: Send OPTIONS, wait for 200 OK (fast check)
   - **Phase 2 - INVITE**: Send INVITE with auto-answer hints and SDP
   - Main server receives response on port 5060
   - Main server detects `From: <sip:test@` signature
   - Response enqueued to quality monitor queue
   - Monitor thread dequeues response (with timeout)
   - **Phase 3 - Media**: On 200 OK, send ACK and start RTP burst
   - Send RTP packets every 40ms for ~1.2s
   - Send RTCP SR at t=0 and t≈1.0s
   - Receive and parse RTCP RR from phone
   - Extract jitter, loss, RTT metrics
   - **Phase 4 - Termination**: Send BYE, receive 200 OK
   - Store results in quality records table

4. **Results Publication**:
   - Write all quality records to `/tmp/phone_quality.json`
   - JSON contains: phone number, IP, timestamp, status, metrics
   - CGI endpoint `/www/cgi-bin/quality` reads JSON for web display

5. **Error Handling**:
   - BUSY/NO_ANSWER: Phone in use or doesn't support auto-answer
   - TIMEOUT: No SIP response within configured timeout
   - NO_RR: Phone doesn't send RTCP Receiver Reports
   - SIP_ERROR: SIP error response (486, 603, etc.)
   - All failures logged with status reason

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

### 4.4 RTP/RTCP (Quality Monitoring)
- **RTP Protocol**: Real-time Transport Protocol for voice packets
- **Codec**: PCMU (G.711 μ-law), payload type 0
- **Packet Rate**: 40ms ptime (25 packets/second)
- **Sample Rate**: 8000 Hz (320 samples per packet)
- **RTCP Protocol**: RTP Control Protocol for statistics
- **RTCP SR**: Sender Report with NTP timestamp
- **RTCP RR**: Receiver Report with jitter, loss, RTT data
- **Port Allocation**: Dynamic RTP/RTCP port pairs (RTP=even, RTCP=odd)

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
- **Main Thread**: SIP message processing and response routing
- **Fetcher Thread**: Phonebook management (CSV download, XML conversion)
- **Updater Thread**: Status synchronization
- **Quality Monitor Thread**: VoIP quality testing for all phones
- **Synchronization**:
  - `registered_users_mutex`: Protects user database
  - `phonebook_file_mutex`: Protects phonebook file access
  - `updater_trigger_mutex` + `updater_trigger_cond`: Updater signaling
  - `g_response_queue_mutex` + `g_response_queue_cond`: Quality monitor responses

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