#define MODULE_NAME "UAC_BULK"

#include "uac_bulk_tester.h"
#include "../common.h"
#include "../config_loader/config_loader.h"
#include "../passive_safety/passive_safety.h"
#include "uac.h"
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

void *uac_bulk_tester_thread(void *arg) {
    (void)arg;

    LOG_INFO("UAC Bulk Tester thread started. Interval: %d seconds", g_uac_test_interval_seconds);

    // If interval is 0, bulk testing is disabled
    if (g_uac_test_interval_seconds <= 0) {
        LOG_INFO("UAC bulk testing disabled (interval = %d). Thread exiting.", g_uac_test_interval_seconds);
        return NULL;
    }

    // Wait for initial phonebook load (give phonebook_fetcher time to populate users)
    LOG_INFO("Waiting 60 seconds for initial phonebook load...");
    sleep(60);

    while (1) {
        // Passive Safety: Update heartbeat
        g_bulk_tester_last_heartbeat = time(NULL);

        LOG_INFO("=== Starting UAC bulk test cycle ===");

        int total_users = 0;
        int dns_resolved = 0;
        int dns_failed = 0;
        int tests_triggered = 0;

        // Lock the user table and iterate through all registered users
        pthread_mutex_lock(&registered_users_mutex);

        for (int i = 0; i < MAX_REGISTERED_USERS; i++) {
            RegisteredUser *user = &registered_users[i];

            // Skip empty slots
            if (user->user_id[0] == '\0') {
                continue;
            }

            total_users++;

            // Build hostname for DNS check: <phone_number>.local.mesh
            char hostname[MAX_USER_ID_LEN + sizeof(AREDN_MESH_DOMAIN) + 2];
            snprintf(hostname, sizeof(hostname), "%s.%s", user->user_id, AREDN_MESH_DOMAIN);

            // Check DNS resolution
            struct addrinfo hints = {0};
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_DGRAM;
            struct addrinfo *res = NULL;

            int gai_status = getaddrinfo(hostname, NULL, &hints, &res);

            if (gai_status == 0) {
                // DNS resolved - node is reachable
                dns_resolved++;

                // Get IP address for logging
                char ip_str[INET_ADDRSTRLEN] = "unknown";
                if (res && res->ai_family == AF_INET) {
                    struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
                    inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));
                }

                LOG_INFO("[%d/%d] Testing %s (%s) - DNS resolved to %s",
                         dns_resolved, total_users, user->user_id, user->display_name, ip_str);

                freeaddrinfo(res);

                // Release mutex before making UAC call (UAC may take time)
                pthread_mutex_unlock(&registered_users_mutex);

                // Wait for UAC to return to IDLE state before making next call
                // The UAC only supports one call at a time
                int wait_count = 0;
                while (uac_get_state() != UAC_STATE_IDLE && wait_count < 10) {
                    sleep(1);
                    wait_count++;
                }

                if (uac_get_state() != UAC_STATE_IDLE) {
                    LOG_WARN("✗ UAC busy (state: %s), forcing reset before testing %s (%s)",
                             uac_state_to_string(uac_get_state()), user->user_id, user->display_name);
                    uac_reset_state();
                }

                // Trigger UAC test call using global server IP
                if (uac_make_call(user->user_id, g_server_ip) == 0) {
                    tests_triggered++;
                    LOG_INFO("✓ UAC test triggered for %s (%s)", user->user_id, user->display_name);

                    // Wait for call to complete (receive response and reset to IDLE)
                    sleep(3);

                    // Force reset to IDLE after test (especially if error response)
                    if (uac_get_state() != UAC_STATE_IDLE) {
                        LOG_DEBUG("Force resetting UAC to IDLE after test");
                        uac_reset_state();
                    }
                } else {
                    LOG_WARN("✗ Failed to trigger UAC test for %s (%s)", user->user_id, user->display_name);
                    uac_reset_state(); // Reset even on failure
                }

                // Re-acquire mutex for next iteration
                pthread_mutex_lock(&registered_users_mutex);

            } else {
                // DNS failed - node not reachable (don't log to reduce noise)
                dns_failed++;
            }
        }

        pthread_mutex_unlock(&registered_users_mutex);

        LOG_INFO("=== UAC bulk test cycle complete ===");
        LOG_INFO("Total users: %d | DNS resolved: %d | DNS failed: %d | Tests triggered: %d",
                 total_users, dns_resolved, dns_failed, tests_triggered);

        // Wait for next cycle
        LOG_INFO("Next UAC bulk test in %d seconds...", g_uac_test_interval_seconds);
        sleep(g_uac_test_interval_seconds);
    }

    LOG_INFO("UAC Bulk Tester thread exiting");
    return NULL;
}
