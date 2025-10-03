// common.h
#ifndef COMMON_H
#define COMMON_H

// --- Standard Library Includes (Essential for type definitions) ---
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <ctype.h>   // For isspace in trim_whitespace (now in config_loader.c)
#include <errno.h>   // For strerror

// POSIX/System Includes
#include <unistd.h>  // For getpid, access, sleep
#include <sys/syscall.h> // For syscall(SYS_gettid)
#include <sys/select.h> // For fd_set, select, struct timeval
#include <sys/time.h>   // For gettimeofday, struct timeval
#include <sys/socket.h> // For socket, bind, setsockopt, sockaddr, socklen_t, AF_INET, SOCK_DGRAM, SOL_SOCKET, SO_REUSEADDR
#include <netinet/in.h> // For sockaddr_in, INADDR_ANY, htons
#include <arpa/inet.h>  // For inet_ntop, inet_pton
#include <netdb.h>   // For getaddrinfo, addrinfo, freeaddrinfo, gai_strerror
#include <pthread.h> // For pthread_mutex_t, pthread_cond_t, pthread_mutex_init, etc.
#include <sys/resource.h> // For setpriority, PRIO_PROCESS
#include <sched.h> // For sched_yield
#include <libgen.h>   // For dirname, basename
#include <signal.h>   // For signal handling (SIGUSR1)


// --- Application-specific Constants (remain hardcoded as agreed) -----------------------------------
#define AREDN_PHONEBOOK_VERSION "1.5.1"
#define APP_NAME "AREDN-Phonebook"
#define SIP_PORT 5060
#define MAX_SIP_MSG_LEN 2048

// --- Specific Max Lengths for CSV Fields ---
#define MAX_FIRST_NAME_LEN 20
#define MAX_NAME_LEN 20
#define MAX_CALLSIGN_LEN 10
#define MAX_PHONE_NUMBER_LEN 10 // For the numeric user ID from phonebook CSV

// Helper macros for stringifying numbers for sscanf format strings
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)


// Max Lengths for various strings (General/Overall)
#define MAX_USER_ID_LEN 16 // General SIP User ID length (could be different from phone number)

// Calculated MAX_DISPLAY_NAME_LEN for combined parts (First Name, Name, Callsign)
// Format: "FirstName LastName (Callsign)"
// Max Length = MAX_FIRST_NAME_LEN + 1 (space) + MAX_NAME_LEN + 1 (space) + 1 ('(') + MAX_CALLSIGN_LEN + 1 (')') + 1 (null terminator)
#define MAX_DISPLAY_NAME_LEN (MAX_FIRST_NAME_LEN + MAX_NAME_LEN + MAX_CALLSIGN_LEN + 5)

#define MAX_CONTACT_URI_LEN 256 // Still needed for parsing SIP messages, but not stored in RegisteredUser
#define MAX_IP_ADDR_LEN INET_ADDRSTRLEN // Defined from <arpa/inet.h> (still useful for general IP handling)

#define PID_FILE_PATH "/tmp/sip-proxy.pid"

#define MAX_REGISTERED_USERS 256
#define MAX_CALL_SESSIONS 10

#define AREDN_MESH_DOMAIN "localnode.local.mesh"

#define SIP_HANDLER_NICE_VALUE    -5
#define BACKGROUND_TASK_NICE_VALUE 10

// Phonebook Fetcher settings (Flash-friendly with temp downloads)
#define PB_CSV_TEMP_PATH "/tmp/phonebook_download.csv"
#define PB_CSV_PATH "/www/arednstack/phonebook.csv"
#define PB_XML_BASE_PATH "/tmp/phonebook.xml"
#define PB_XML_PUBLIC_PATH "/www/arednstack/phonebook_generic_direct.xml"
#define PB_LAST_GOOD_CSV_HASH_PATH "/www/arednstack/phonebook.csv.hash"

#define HASH_LENGTH 16

// Defines for phonebook server list array sizes (remain hardcoded)
#define MAX_PB_SERVERS 5
#define MAX_SERVER_HOST_LEN 256
#define MAX_SERVER_PORT_LEN 16
#define MAX_SERVER_PATH_LEN 512
#define MAX_CONFIG_PATH_LEN 512


// --- Data Structures ---

// Structure for a configurable phonebook server
typedef struct {
    char host[MAX_SERVER_HOST_LEN];
    char port[MAX_SERVER_PORT_LEN];
    char path[MAX_SERVER_PATH_LEN];
} ConfigurableServer;

// Call Session States
typedef enum {
    CALL_STATE_FREE,
    CALL_STATE_INVITE_SENT,
    CALL_STATE_RINGING,
    CALL_STATE_ESTABLISHED,
    CALL_STATE_TERMINATING
} CallState;

// Registered User Structure
typedef struct {
    char user_id[MAX_PHONE_NUMBER_LEN]; // User ID from phonebook/REGISTER
    char display_name[MAX_DISPLAY_NAME_LEN];
    bool is_active;                     // Active = user is registered / known, has valid DNS entry
    bool is_known_from_directory;       // Did this entry originate from the CSV directory?
    // Contact information from REGISTER (for routing quality tests)
    char contact_uri[MAX_CONTACT_URI_LEN];  // Full Contact URI from REGISTER
    char ip_address[MAX_IP_ADDR_LEN];       // Extracted IP from Contact
    int port;                                // Extracted port from Contact (-1 if not specified)
    time_t registration_time;                // When the user last registered
} RegisteredUser;

// Call Session Structure
typedef struct {
    bool in_use;
    char call_id[MAX_CONTACT_URI_LEN];
    char cseq[MAX_CONTACT_URI_LEN];
    char from_tag[64];
    char to_tag[64];
    struct sockaddr_in caller_addr;
    struct sockaddr_in callee_addr;
    struct sockaddr_in original_caller_addr;
    CallState state;
    time_t creation_time;  // For passive cleanup of stale sessions
} CallSession;


// --- Global Variable Declarations (defined in main.c or config_loader.c) ---
// extern volatile sig_atomic_t keep_running; // REMOVED
// extern volatile sig_atomic_t phonebook_updated_flag; // REMOVED
extern volatile sig_atomic_t phonebook_reload_requested; // For webhook-triggered reload

// These are defined in config_loader.c and populated from sipserver.conf
extern int g_pb_interval_seconds;
extern int g_status_update_interval_seconds;
extern ConfigurableServer g_phonebook_servers_list[MAX_PB_SERVERS];
extern int g_num_phonebook_servers;

// These are defined in main.c
extern RegisteredUser registered_users[MAX_REGISTERED_USERS];
extern int num_registered_users; // Count of active dynamic registrations
extern int num_directory_entries; // Count of entries populated from CSV directory
extern CallSession call_sessions[MAX_CALL_SESSIONS];

// Thread IDs (defined in main.c, used by passive safety)
extern pthread_t fetcher_tid;
extern pthread_t status_updater_tid;

// Mutexes and Condition Variables (defined in main.c)
extern pthread_mutex_t registered_users_mutex;
extern pthread_mutex_t phonebook_file_mutex;
extern pthread_mutex_t updater_trigger_mutex;
extern pthread_cond_t updater_trigger_cond;


// --- Logging Macros and Function Declarations ---
// MODULE_NAME will be defined at the top of each .c file, NOT here.
// #ifndef MODULE_NAME // REMOVED
// #define MODULE_NAME "COMMON_DEFAULT" // Fallback if not defined in a .c file // REMOVED
// #endif // REMOVED

#define LOG_LEVEL_NONE      0
#define LOG_LEVEL_ERROR     1
#define LOG_LEVEL_WARNING   2
#define LOG_LEVEL_INFO      3
#define LOG_LEVEL_DEBUG     4

void log_init(const char* app_name);
void log_shutdown(void);
void log_message(int level, const char* app_name_in, const char* module_name_in, const char *format, ...);

extern int g_log_level;
void set_log_level(int level);

void gemini_debug_log(const char *format, ...);

#define LOG_ERROR(format, ...)   log_message(LOG_LEVEL_ERROR, APP_NAME, MODULE_NAME, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...)    log_message(LOG_LEVEL_WARNING, APP_NAME, MODULE_NAME, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...)    log_message(LOG_LEVEL_INFO, APP_NAME, MODULE_NAME, format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...)   log_message(LOG_LEVEL_DEBUG, APP_NAME, MODULE_NAME, format, ##__VA_ARGS__)


// --- Common Utility Function Declarations ---
const char* sockaddr_to_ip_str(const struct sockaddr_in* addr);

// Function to sanitize UTF-8 strings (now globally declared here)
extern void sanitize_utf8(const char *in, char *out, size_t out_sz);


// File Utils Function Declarations (prototypes)
int file_utils_copy_file(const char *src, const char *dst);
int file_utils_ensure_directory_exists(const char *path);
int file_utils_publish_file_to_destination(const char *source_path, const char *destination_path);


// --- Function prototypes for modules used by main.c (if not already in their own headers) ---
// Note: Ideally, these would be in their respective module headers.
// However, since common.h includes many standard headers, this acts as a central point.
// If you have sip_core.h, phonebook_fetcher.h, etc., they should declare their own functions.
// For now, assuming they are declared within common.h scope due to previous structure.

// SIP Core
void process_incoming_sip_message(int sockfd, const char *buffer, ssize_t n,
                                  const struct sockaddr_in *cliaddr, socklen_t cli_len);

// User Manager
RegisteredUser* find_registered_user(const char *user_id);
RegisteredUser* add_or_update_registered_user(const char *user_id, const char *display_name, int expires,
                                              const char *contact_uri, const char *ip_address, int port);
RegisteredUser* add_csv_user_to_registered_users_table(const char *user_id_numeric, const char *display_name);
void init_registered_users_table();
void populate_registered_users_from_csv(const char *filepath);
void load_directory_from_xml(const char *filepath); // Deprecated but retained prototype

// Call Sessions
CallSession* find_call_session_by_callid(const char *call_id);
CallSession* create_call_session();
void terminate_call_session(CallSession *session);
void init_call_sessions();


#endif // COMMON_H
