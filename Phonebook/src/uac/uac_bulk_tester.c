#define MODULE_NAME "UAC_BULK"

#include "uac_bulk_tester.h"
#include "../common.h"
#include "../config_loader/config_loader.h"
#include "../passive_safety/passive_safety.h"
#include "uac.h"
#include "uac_ping.h"
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
        int phones_online = 0;      // Phones that responded to SIP OPTIONS or INVITE
        int phones_offline = 0;     // DNS resolved but no SIP response
        float total_avg_rtt = 0.0;  // Sum of average RTTs for calculating overall average
        int rtt_count = 0;          // Count of phones with valid RTT measurements

        // Lock the user table and iterate through all registered users
        pthread_mutex_lock(&registered_users_mutex);

        for (int i = 0; i < MAX_REGISTERED_USERS; i++) {
            RegisteredUser *user = &registered_users[i];

            // Skip empty slots
            if (user->user_id[0] == '\0') {
                continue;
            }

            // Only test phone numbers starting with configured prefix
            if (strncmp(user->user_id, g_uac_test_prefix, strlen(g_uac_test_prefix)) != 0) {
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

                // Release mutex before testing (tests may take time)
                pthread_mutex_unlock(&registered_users_mutex);

                // ====================================================
                // PHASE 1: SIP OPTIONS Ping Test (Latency/Jitter)
                // ====================================================
                LOG_INFO("Testing %s (%s) with OPTIONS ping (%d pings)...",
                         user->user_id, user->display_name, g_uac_options_ping_count);

                uac_ping_result_t ping_result = uac_options_ping_test(
                    user->user_id, g_server_ip, g_uac_options_ping_count);

                if (ping_result.online) {
                    phones_online++;
                    tests_triggered++;

                    LOG_INFO("✓ Phone %s ONLINE (OPTIONS)", user->user_id);
                    LOG_INFO("  Packets: %d sent, %d received (%.1f%% loss)",
                             ping_result.packets_sent, ping_result.packets_received,
                             ping_result.packet_loss_pct);
                    LOG_INFO("  RTT: min=%.2f ms, avg=%.2f ms, max=%.2f ms, jitter=%.2f ms",
                             ping_result.min_rtt_ms, ping_result.avg_rtt_ms,
                             ping_result.max_rtt_ms, ping_result.jitter_ms);

                    // Track RTT stats for summary
                    total_avg_rtt += ping_result.avg_rtt_ms;
                    rtt_count++;

                    // Re-acquire mutex and continue to next user
                    pthread_mutex_lock(&registered_users_mutex);
                    continue;
                } else {
                    LOG_WARN("✗ Phone %s no response to OPTIONS ping", user->user_id);
                }

                // ====================================================
                // PHASE 2: SIP INVITE Test (Optional - only if enabled)
                // ====================================================
                if (g_uac_call_test_enabled) {
                    LOG_INFO("OPTIONS ping failed, trying INVITE test for %s...", user->user_id);

                    // Wait for UAC to return to IDLE state before making call
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
                        LOG_INFO("✓ UAC INVITE test triggered for %s (%s)", user->user_id, user->display_name);

                        // Poll UAC state rapidly to minimize ring time
                        // Cancel as soon as we detect RINGING state
                        int poll_count = 0;
                        int max_polls = 20; // 20 * 50ms = 1 second max (phones respond in <100ms)
                        uac_call_state_t state = UAC_STATE_IDLE;

                        while (poll_count < max_polls) {
                            usleep(50000); // Sleep 50ms between polls
                            poll_count++;
                            state = uac_get_state();

                            if (state == UAC_STATE_RINGING || state == UAC_STATE_ESTABLISHED) {
                                // Phone responded - cancel/hangup immediately
                                break;
                            } else if (state == UAC_STATE_IDLE) {
                                // Error response (like 488) - already reset
                                break;
                            }
                        }

                        // Handle final state
                        if (state == UAC_STATE_CALLING) {
                            // Phone never responded - offline
                            LOG_WARN("✗ Phone %s OFFLINE (no INVITE response)", user->user_id);
                            phones_offline++;
                        } else if (state == UAC_STATE_RINGING) {
                            LOG_INFO("✓ Phone %s ONLINE (ringing) - canceling", user->user_id);
                            phones_online++;
                            uac_cancel_call();
                            sleep(1); // Wait for CANCEL to be sent
                        } else if (state == UAC_STATE_ESTABLISHED) {
                            LOG_INFO("✓ Phone %s ONLINE (answered) - hanging up", user->user_id);
                            phones_online++;
                            uac_hang_up();
                            sleep(1); // Wait for BYE to be sent
                        } else if (state == UAC_STATE_IDLE) {
                            // Got error response (like 488) - phone is online but rejected
                            LOG_INFO("✓ Phone %s ONLINE (rejected call)", user->user_id);
                            phones_online++;
                        }

                        // Force reset to IDLE after test (for error responses like 488)
                        if (uac_get_state() != UAC_STATE_IDLE) {
                            LOG_DEBUG("Force resetting UAC to IDLE after test (state: %s)",
                                      uac_state_to_string(uac_get_state()));
                            uac_reset_state();
                        }
                    } else {
                        LOG_WARN("✗ Failed to trigger UAC INVITE test for %s (%s)", user->user_id, user->display_name);
                        uac_reset_state(); // Reset even on failure
                    }
                } else {
                    // INVITE testing disabled and OPTIONS failed - phone is offline
                    LOG_WARN("✗ Phone %s OFFLINE (no OPTIONS response, INVITE test disabled)", user->user_id);
                    phones_offline++;
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
        LOG_INFO("Phones ONLINE: %d | Phones OFFLINE: %d (DNS ok, no SIP response)",
                 phones_online, phones_offline);

        if (rtt_count > 0) {
            float overall_avg_rtt = total_avg_rtt / rtt_count;
            LOG_INFO("Network Performance: Average RTT across %d phones: %.2f ms",
                     rtt_count, overall_avg_rtt);
        }

        // Wait for next cycle
        LOG_INFO("Next UAC bulk test in %d seconds...", g_uac_test_interval_seconds);
        sleep(g_uac_test_interval_seconds);
    }

    LOG_INFO("UAC Bulk Tester thread exiting");
    return NULL;
}
