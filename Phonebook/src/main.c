// main.c

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

// Define MODULE_NAME specific to main.c
#define MODULE_NAME "MAIN"

// Global arrays for registered users and call sessions (DEFINED here)
RegisteredUser registered_users[MAX_REGISTERED_USERS];
CallSession call_sessions[MAX_CALL_SESSIONS];

// Other global variables (DEFINED here)
// volatile sig_atomic_t keep_running = 1; // REMOVED
// volatile sig_atomic_t phonebook_updated_flag = 0; // REMOVED as related to signal handling
int num_registered_users = 0;
int num_directory_entries = 0;

// Mutexes and Condition Variables (DEFINED here)
pthread_mutex_t registered_users_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t phonebook_file_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t updater_trigger_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t updater_trigger_cond = PTHREAD_COND_INITIALIZER;

// sig_handler function REMOVED

// sockaddr_to_ip_str prototype is in common.h, definition remains here
const char* sockaddr_to_ip_str(const struct sockaddr_in* addr) {
    static char ip_str[INET_ADDRSTRLEN];
    if (addr == NULL) return "NULL_ADDR";
    inet_ntop(AF_INET, &(addr->sin_addr), ip_str, sizeof(ip_str));
    return ip_str;
}

int main(int argc, char *argv[]) {
    int sockfd;
    struct sockaddr_in servaddr, cliaddr; // Defined here
    char buffer[MAX_SIP_MSG_LEN]; // Max SIP message length
    socklen_t len; // socklen_t defined in common.h through sys/socket.h
    ssize_t n;
    fd_set readfds;
    struct timeval tv;
    pthread_t fetcher_tid = 0;
    pthread_t status_updater_tid = 0;
    int reuse_addr = 1;
    int retval;

    log_init(APP_NAME); // APP_NAME is defined in common.h
    LOG_INFO("Starting main function for %s process (PID %d).", MODULE_NAME, getpid());

    // --- Load configuration from file ---
    load_configuration("/etc/sipserver.conf"); // Call the loader function

    LOG_INFO("Attempting to set process priority...");
    if (setpriority(PRIO_PROCESS, 0, SIP_HANDLER_NICE_VALUE) == -1) { // SIP_HANDLER_NICE_VALUE from common.h
        LOG_WARN("Failed to set process priority to %d: %s", SIP_HANDLER_NICE_VALUE, strerror(errno));
    } else {
        LOG_INFO("Process priority set to %d.", SIP_HANDLER_NICE_VALUE);
    }
    LOG_DEBUG("Process priority setting attempted.");

    LOG_INFO("SIP Server %s starting...", SIP_SERVER_VERSION); // SIP_SERVER_VERSION from common.h
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

    LOG_INFO("SIP Server listening on UDP port %d", SIP_PORT);
    LOG_INFO("Entering main SIP message processing loop.");

    while (1) { // Changed from while(keep_running) to while(1)
        len = sizeof(cliaddr);
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        tv.tv_sec = 1; tv.tv_usec = 0;
        retval = select(sockfd + 1, &readfds, NULL, NULL, &tv);

        if (retval < 0) {
            // if (errno == EINTR) continue; // REMOVED
            LOG_ERROR("select() error.");
            break; // Exit on select error
        } else if (retval == 0) {
            continue;
        }

        n = recvfrom(sockfd, buffer, MAX_SIP_MSG_LEN - 1, 0,
                     (struct sockaddr*)&cliaddr, &len);
        if (n < 0) {
            // if (errno == EINTR) continue; // REMOVED
            LOG_ERROR("recvfrom failed.");
            continue;
        }
        buffer[n] = '\0';

        process_incoming_sip_message(sockfd, buffer, n, &cliaddr, len);
    }
    // This code block will now only be reached if an unrecoverable error in the main loop occurs.
    LOG_WARN("Main SIP message processing loop unexpectedly terminated.");

    close(sockfd);

    LOG_INFO("Destroying mutexes and condition variables...");
    pthread_mutex_destroy(&registered_users_mutex);
    pthread_mutex_destroy(&phonebook_file_mutex);
    pthread_mutex_destroy(&updater_trigger_mutex);
    pthread_cond_destroy(&updater_trigger_cond);
    LOG_DEBUG("Mutexes and condition variables destroyed.");

    LOG_INFO("SIP Server shut down."); // Changed from "shut down cleanly"
    log_shutdown();

    LOG_INFO("Main function exiting.");
    return EXIT_FAILURE; // Indicate abnormal exit if loop breaks
}