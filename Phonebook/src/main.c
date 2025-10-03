// main.c

#include <syslog.h> // For direct syslog calls in signal handlers

// Undefine syslog macros that conflict with our LOG_ macros
#undef LOG_EMERG
#undef LOG_ALERT
#undef LOG_CRIT
#undef LOG_ERR
#undef LOG_WARNING
#undef LOG_NOTICE
#undef LOG_INFO
#undef LOG_DEBUG

#include "common.h" // This now includes most necessary headers and common declarations
#include "config_loader/config_loader.h" // Include the new config loader header

// --- Module Headers (for function prototypes not already in common.h) ---
// It's good practice to include specific module headers for their prototypes,
// even if common.h might also declare some. This helps with modularity.
#include "sip_core/sip_core.h"          // For process_incoming_sip_message, etc.
#include "phonebook_fetcher/phonebook_fetcher.h" // For phonebook_fetcher_thread, etc.
#include "status_updater/status_updater.h"   // For status_updater_thread, etc.
#include "user_manager/user_manager.h"   // For user management functions
#include "call-sessions/call_sessions.h" // For call session management functions
#include "passive_safety/passive_safety.h" // For passive safety and self-healing
#include "uac/uac.h"                    // For UAC load testing module

// Define MODULE_NAME specific to main.c
#define MODULE_NAME "MAIN"

// Global arrays for registered users and call sessions (DEFINED here)
RegisteredUser registered_users[MAX_REGISTERED_USERS];
CallSession call_sessions[MAX_CALL_SESSIONS];

// Other global variables (DEFINED here)
// volatile sig_atomic_t keep_running = 1; // REMOVED
// volatile sig_atomic_t phonebook_updated_flag = 0; // REMOVED as related to signal handling
volatile sig_atomic_t phonebook_reload_requested = 0; // For webhook-triggered reload
int num_registered_users = 0;
int num_directory_entries = 0;

// Thread IDs for passive safety monitoring
pthread_t fetcher_tid = 0;
pthread_t status_updater_tid = 0;

// Mutexes and Condition Variables (DEFINED here)
pthread_mutex_t registered_users_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t phonebook_file_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t updater_trigger_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t updater_trigger_cond = PTHREAD_COND_INITIALIZER;

// Global flag for UAC test trigger
static volatile sig_atomic_t uac_test_requested = 0;

// Signal handler for webhook-triggered phonebook reload
void phonebook_reload_signal_handler(int sig) {
    if (sig == SIGUSR1) {
        phonebook_reload_requested = 1;
        LOG_INFO("Received SIGUSR1 - immediate phonebook reload requested via webhook");
    }
}

// Signal handler for UAC test trigger
void uac_test_signal_handler(int sig) {
    if (sig == SIGUSR2) {
        uac_test_requested = 1;
        syslog(6, "[UAC_SIGNAL] SIGUSR2 received - setting uac_test_requested flag"); // 6 = LOG_INFO
    }
}

// sockaddr_to_ip_str prototype is in common.h, definition remains here
const char* sockaddr_to_ip_str(const struct sockaddr_in* addr) {
    static char ip_str[INET_ADDRSTRLEN];
    if (addr == NULL) return "NULL_ADDR";
    inet_ntop(AF_INET, &(addr->sin_addr), ip_str, sizeof(ip_str));
    return ip_str;
}

// Get server IP address for UAC binding
// AREDN nodes have multiple interfaces (DTD, WAN, LAN)
// For SIP/UAC, we want the LAN address where phones connect
static int get_server_ip(char *ip_buffer, size_t buffer_size) {
    // Try environment variable first (allows override)
    const char *env_ip = getenv("SIP_SERVER_IP");
    if (env_ip && strlen(env_ip) > 0) {
        strncpy(ip_buffer, env_ip, buffer_size - 1);
        ip_buffer[buffer_size - 1] = '\0';
        LOG_INFO("Using SIP_SERVER_IP from environment: %s", ip_buffer);
        return 0;
    }

    // Try to get IP from the socket we just bound
    // This gives us the actual IP the SIP server is listening on
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        LOG_WARN("Failed to create socket for IP detection");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(5061); // Different port to avoid conflict

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        LOG_WARN("Failed to bind socket for IP detection");
        return -1;
    }

    // Connect to mesh network address to determine routing
    // AREDN mesh uses 10.x.x.x, so connect to a mesh IP
    struct sockaddr_in mesh_addr;
    memset(&mesh_addr, 0, sizeof(mesh_addr));
    mesh_addr.sin_family = AF_INET;
    mesh_addr.sin_addr.s_addr = inet_addr("10.0.0.1"); // Generic mesh IP
    mesh_addr.sin_port = htons(5060);

    if (connect(sock, (struct sockaddr*)&mesh_addr, sizeof(mesh_addr)) < 0) {
        close(sock);
        LOG_WARN("Failed to connect socket for IP detection");
        return -1;
    }

    // Get the local address
    socklen_t addr_len = sizeof(addr);
    if (getsockname(sock, (struct sockaddr*)&addr, &addr_len) < 0) {
        close(sock);
        LOG_WARN("Failed to get socket name for IP detection");
        return -1;
    }

    close(sock);

    // Convert to string
    if (inet_ntop(AF_INET, &addr.sin_addr, ip_buffer, buffer_size) == NULL) {
        LOG_WARN("Failed to convert IP address to string");
        return -1;
    }

    LOG_INFO("Detected server IP: %s", ip_buffer);
    return 0;
}

int main(int argc, char *argv[]) {
    int sockfd;
    struct sockaddr_in servaddr, cliaddr; // Defined here
    char buffer[MAX_SIP_MSG_LEN]; // Max SIP message length
    socklen_t len; // socklen_t defined in common.h through sys/socket.h
    ssize_t n;
    fd_set readfds;
    struct timeval tv;
    int reuse_addr = 1;
    int retval;

    log_init(APP_NAME); // APP_NAME is defined in common.h
    LOG_INFO("Starting main function for %s process (PID %d).", MODULE_NAME, getpid());

    // --- Load configuration from file ---
    load_configuration("/etc/sipserver.conf"); // Call the loader function

    // --- Passive Safety: Self-correct configuration ---
    validate_and_correct_config(); // Fix common config errors automatically

    // --- Register signal handlers ---
    signal(SIGUSR1, phonebook_reload_signal_handler);
    LOG_INFO("Registered SIGUSR1 handler for webhook-triggered phonebook reload");

    signal(SIGUSR2, uac_test_signal_handler);
    LOG_INFO("Registered SIGUSR2 handler for UAC test calls");

    LOG_INFO("Attempting to set process priority...");
    if (setpriority(PRIO_PROCESS, 0, SIP_HANDLER_NICE_VALUE) == -1) { // SIP_HANDLER_NICE_VALUE from common.h
        LOG_WARN("Failed to set process priority to %d: %s", SIP_HANDLER_NICE_VALUE, strerror(errno));
    } else {
        LOG_INFO("Process priority set to %d.", SIP_HANDLER_NICE_VALUE);
    }
    LOG_DEBUG("Process priority setting attempted.");

    LOG_INFO("AREDN Phonebook %s starting...", AREDN_PHONEBOOK_VERSION); // AREDN_PHONEBOOK_VERSION from common.h
    LOG_INFO("Initializing mutexes and condition variables...");

    // Mutex initializations
    if (pthread_mutex_init(&registered_users_mutex, NULL) != 0) {
        LOG_ERROR("User mutex init failed.");
        return EXIT_FAILURE;
    }
    LOG_DEBUG("registered_users_mutex initialized.");

    if (pthread_mutex_init(&phonebook_file_mutex, NULL) != 0) {
        LOG_ERROR("Phonebook file mutex init failed.");
        pthread_mutex_destroy(&registered_users_mutex);
        return EXIT_FAILURE;
    }
    LOG_DEBUG("phonebook_file_mutex initialized.");

    if (pthread_mutex_init(&updater_trigger_mutex, NULL) != 0) {
        LOG_ERROR("Updater trigger mutex init failed.");
        pthread_mutex_destroy(&registered_users_mutex);
        pthread_mutex_destroy(&phonebook_file_mutex);
        return EXIT_FAILURE;
    }
    LOG_DEBUG("updater_trigger_mutex initialized.");

    if (pthread_cond_init(&updater_trigger_cond, NULL) != 0) {
        LOG_ERROR("Updater trigger cond init failed.");
        pthread_mutex_destroy(&registered_users_mutex);
        pthread_mutex_destroy(&phonebook_file_mutex);
        pthread_mutex_destroy(&updater_trigger_mutex);
        return EXIT_FAILURE;
    }
    LOG_DEBUG("updater_trigger_cond initialized.");

    // Directly use the path "/tmp" since TEMPORARY_FILES macro was removed
    LOG_INFO("Ensuring temporary files directory '%s' exists...", "/tmp");
    if (file_utils_ensure_directory_exists("/tmp") != 0) {
        LOG_ERROR("Failed to create temporary files directory '%s'. Exiting.", "/tmp");
        return EXIT_FAILURE;
    }
    LOG_DEBUG("Temporary files directory '%s' ensured.", "/tmp");

    LOG_INFO("Ensuring public XML directory '%s' exists...", PB_XML_PUBLIC_PATH); // PB_XML_PUBLIC_PATH from common.h
    char public_path_copy[MAX_CONFIG_PATH_LEN]; // MAX_CONFIG_PATH_LEN from common.h
    strncpy(public_path_copy, PB_XML_PUBLIC_PATH, sizeof(public_path_copy) - 1);
    public_path_copy[sizeof(public_path_copy) - 1] = '\0';
    if (file_utils_ensure_directory_exists(dirname(public_path_copy)) != 0) {
        LOG_ERROR("Initial public XML directory setup failed. Exiting.");
        return EXIT_FAILURE;
    }
    LOG_DEBUG("Public XML directory '%s' ensured.", PB_XML_PUBLIC_PATH);

    if (access(PB_XML_PUBLIC_PATH, F_OK) == 0) {
        LOG_INFO("Deleting existing production XML file: %s", PB_XML_PUBLIC_PATH);
        if (remove(PB_XML_PUBLIC_PATH) != 0) {
            LOG_WARN("Failed to delete production XML file. Error: %s", strerror(errno));
        }
    }
    LOG_DEBUG("Existing public XML file checked/deleted.");

    LOG_INFO("Creating phonebook fetcher thread...");
    if (pthread_create(&fetcher_tid, NULL, phonebook_fetcher_thread, NULL) != 0) {
        LOG_ERROR("Failed to create phonebook fetcher thread.");
        return EXIT_FAILURE;
    }
    LOG_DEBUG("Phonebook fetcher thread launched.");
    LOG_DEBUG("Fetcher thread TID: %lu", (unsigned long)fetcher_tid);

    LOG_INFO("Creating status updater thread...");
    if (pthread_create(&status_updater_tid, NULL, status_updater_thread, NULL) != 0) {
        LOG_ERROR("Failed to create status updater thread.");
        return EXIT_FAILURE;
    }
    LOG_DEBUG("Status updater thread launched.");
    LOG_DEBUG("Updater thread TID: %lu", (unsigned long)status_updater_tid);

    LOG_INFO("Creating passive safety thread...");
    if (pthread_create(&g_passive_safety_tid, NULL, passive_safety_thread, NULL) != 0) {
        LOG_ERROR("Failed to create passive safety thread.");
        return EXIT_FAILURE;
    }
    LOG_DEBUG("Passive safety thread launched (silent self-healing enabled).");
    LOG_DEBUG("Passive safety thread TID: %lu", (unsigned long)g_passive_safety_tid);

    LOG_INFO("Initializing call sessions table...");
    init_call_sessions();
    LOG_DEBUG("Call sessions table initialized.");

    LOG_INFO("Creating SIP UDP socket...");
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        LOG_ERROR("Socket creation failed.");
        return EXIT_FAILURE;
    }
    LOG_DEBUG("SIP UDP socket created (sockfd: %d).", sockfd);

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) < 0) {
        LOG_WARN("setsockopt(SO_REUSEADDR) failed.");
    }
    LOG_DEBUG("Socket options set.");

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(SIP_PORT);
    LOG_DEBUG("Server address struct prepared (port: %d).", SIP_PORT);

    LOG_INFO("Attempting to bind to UDP port %d...", SIP_PORT);
    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        LOG_ERROR("Socket bind failed on port %d.", SIP_PORT);
        return EXIT_FAILURE;
    }
    LOG_INFO("Successfully bound to UDP port %d.", SIP_PORT);

    // Initialize UAC module (after SIP server is bound)
    LOG_INFO("[MAIN] Initializing UAC module");
    char server_ip[64];
    int have_server_ip = 0;

    // Try to get server IP for UAC
    syslog(6, "[UAC_INIT] Detecting server IP for UAC binding");
    if (get_server_ip(server_ip, sizeof(server_ip)) == 0) {
        syslog(6, "[UAC_INIT] Server IP detected: %s", server_ip);
        if (uac_init(server_ip) == 0) {
            have_server_ip = 1;
            syslog(6, "[UAC_INIT] ✓ UAC initialized on %s:%d (have_server_ip=%d)", server_ip, UAC_SIP_PORT, have_server_ip);
        } else {
            syslog(4, "[UAC_INIT] ✗ uac_init() failed");
        }
    } else {
        syslog(4, "[UAC_INIT] ✗ get_server_ip() failed - UAC not initialized");
    }

    syslog(6, "[MAIN_LOOP] Server listening on UDP port %d", SIP_PORT);
    syslog(6, "[MAIN_LOOP] Entering main loop (have_server_ip=%d, UAC port 5070)", have_server_ip);

    while (1) { // Changed from while(keep_running) to while(1)
        len = sizeof(cliaddr);
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        // Add UAC socket to select if initialized
        int max_fd = sockfd;
        if (have_server_ip && uac_get_sockfd() >= 0) {
            FD_SET(uac_get_sockfd(), &readfds);
            if (uac_get_sockfd() > max_fd) {
                max_fd = uac_get_sockfd();
            }
        }

        tv.tv_sec = 1; tv.tv_usec = 0;
        retval = select(max_fd + 1, &readfds, NULL, NULL, &tv);

        if (retval < 0) {
            if (errno == EINTR) {
                syslog(7, "[MAIN_LOOP] select() interrupted by signal (EINTR), continuing...");
                continue; // Signal interrupted select, continue loop
            }
            LOG_ERROR("select() error: %s", strerror(errno));
            break; // Exit on real select error
        } else if (retval == 0) {
            // Timeout - check for UAC test request
            syslog(7, "[MAIN_LOOP] Select timeout (uac_test_requested=%d, have_server_ip=%d)", uac_test_requested, have_server_ip);

            if (uac_test_requested && have_server_ip) {
                uac_test_requested = 0; // Reset flag
                syslog(6, "[UAC_TEST] ✓ Both flags true, processing UAC test request");

                // Read target number from file
                FILE *f = fopen("/tmp/uac_test_target", "r");
                if (f) {
                    char target[32] = {0};
                    if (fgets(target, sizeof(target), f)) {
                        // Remove newline
                        target[strcspn(target, "\r\n")] = 0;
                        syslog(6, "[UAC_TEST] Triggering UAC test call to %s via %s", target, server_ip); // 6 = LOG_INFO
                        if (uac_make_call(target, server_ip) == 0) {
                            syslog(6, "[UAC_TEST] ✓ UAC test call initiated successfully"); // 6 = LOG_INFO
                        } else {
                            syslog(3, "[UAC_TEST] ✗ UAC test call failed to initiate"); // 3 = LOG_ERR
                        }
                    }
                    fclose(f);
                    unlink("/tmp/uac_test_target");
                } else {
                    syslog(4, "[UAC_TEST] UAC test requested but no target file found at /tmp/uac_test_target"); // 4 = LOG_WARNING
                }
            } else if (uac_test_requested && !have_server_ip) {
                syslog(4, "[UAC_TEST] UAC test requested but have_server_ip=0, cannot make call"); // 4 = LOG_WARNING
                uac_test_requested = 0;
            }
            continue;
        }

        // Handle SIP server socket
        if (FD_ISSET(sockfd, &readfds)) {
            n = recvfrom(sockfd, buffer, MAX_SIP_MSG_LEN - 1, 0,
                         (struct sockaddr*)&cliaddr, &len);
            if (n < 0) {
                // if (errno == EINTR) continue; // REMOVED
                LOG_ERROR("recvfrom failed on SIP socket.");
                continue;
            }
            buffer[n] = '\0';

            process_incoming_sip_message(sockfd, buffer, n, &cliaddr, len);
        }

        // Handle UAC socket responses
        if (have_server_ip && uac_get_sockfd() >= 0 && FD_ISSET(uac_get_sockfd(), &readfds)) {
            char uac_buffer[MAX_SIP_MSG_LEN];
            n = recvfrom(uac_get_sockfd(), uac_buffer, sizeof(uac_buffer) - 1, 0, NULL, NULL);
            if (n > 0) {
                uac_buffer[n] = '\0';
                uac_process_response(uac_buffer, n);
            } else if (n < 0) {
                LOG_ERROR("recvfrom failed on UAC socket.");
            }
        }
    }
    // This code block will now only be reached if an unrecoverable error in the main loop occurs.
    LOG_WARN("Main SIP message processing loop unexpectedly terminated.");

    // Shutdown UAC if initialized
    if (have_server_ip) {
        uac_shutdown();
    }

    close(sockfd);

    LOG_INFO("Destroying mutexes and condition variables...");
    pthread_mutex_destroy(&registered_users_mutex);
    pthread_mutex_destroy(&phonebook_file_mutex);
    pthread_mutex_destroy(&updater_trigger_mutex);
    pthread_cond_destroy(&updater_trigger_cond);
    LOG_DEBUG("Mutexes and condition variables destroyed.");

    LOG_INFO("AREDN Phonebook shut down."); // Changed from "shut down cleanly"
    log_shutdown();

    LOG_INFO("Main function exiting.");
    return EXIT_FAILURE; // Indicate abnormal exit if loop breaks
}