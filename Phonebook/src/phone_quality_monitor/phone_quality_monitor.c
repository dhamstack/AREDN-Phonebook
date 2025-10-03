/*
 * phone_quality_monitor.c - Background VoIP Quality Monitoring Implementation
 */

#include "phone_quality_monitor.h"
#include "../common.h"
#include "../user_manager/user_manager.h"
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#define MODULE_NAME "QUALITY"

// Global state
static phone_quality_record_t g_quality_records[MAX_QUALITY_RECORDS];
static pthread_mutex_t g_records_mutex = PTHREAD_MUTEX_INITIALIZER;
static quality_monitor_context_t g_monitor_context;
static pthread_t g_monitor_tid = 0;
static volatile int g_monitor_running = 0;

// Initialize quality monitor
int quality_monitor_init(int sip_sock, const char *server_ip) {
    memset(&g_monitor_context, 0, sizeof(g_monitor_context));
    memset(g_quality_records, 0, sizeof(g_quality_records));

    g_monitor_context.sip_socket = sip_sock;
    if (server_ip) {
        strncpy(g_monitor_context.server_ip, server_ip, sizeof(g_monitor_context.server_ip) - 1);
    }

    // Set default configuration
    g_monitor_context.config.enabled = 1;
    g_monitor_context.config.test_interval_sec = 300;  // 5 minutes
    g_monitor_context.config.cycle_delay_sec = 10;     // 10 seconds between phones
    g_monitor_context.config.probe_config = get_default_config();

    LOG_INFO("Quality monitor initialized (socket=%d, server_ip=%s)",
             sip_sock, server_ip ? server_ip : "auto");

    return 0;
}

// Start quality monitor thread
int quality_monitor_start(void) {
    if (g_monitor_running) {
        LOG_WARN("Quality monitor already running");
        return -1;
    }

    if (!g_monitor_context.config.enabled) {
        LOG_INFO("Quality monitor disabled by configuration");
        return 0;
    }

    g_monitor_running = 1;
    if (pthread_create(&g_monitor_tid, NULL, quality_monitor_thread, &g_monitor_context) != 0) {
        LOG_ERROR("Failed to create quality monitor thread");
        g_monitor_running = 0;
        return -1;
    }

    LOG_INFO("Quality monitor thread started");
    return 0;
}

// Stop quality monitor thread
void quality_monitor_stop(void) {
    if (!g_monitor_running) return;

    LOG_INFO("Stopping quality monitor thread...");
    g_monitor_running = 0;

    if (g_monitor_tid != 0) {
        pthread_join(g_monitor_tid, NULL);
        g_monitor_tid = 0;
    }

    LOG_INFO("Quality monitor thread stopped");
}

// Store quality test result
static void store_quality_result(const char *phone_number, const char *phone_ip,
                                 const voip_probe_result_t *result) {
    pthread_mutex_lock(&g_records_mutex);

    // Find existing record or empty slot
    int slot = -1;
    for (int i = 0; i < MAX_QUALITY_RECORDS; i++) {
        if (g_quality_records[i].valid &&
            strcmp(g_quality_records[i].phone_number, phone_number) == 0) {
            slot = i;
            break;
        }
    }

    // If not found, find empty slot
    if (slot == -1) {
        for (int i = 0; i < MAX_QUALITY_RECORDS; i++) {
            if (!g_quality_records[i].valid) {
                slot = i;
                break;
            }
        }
    }

    // Store result
    if (slot >= 0) {
        strncpy(g_quality_records[slot].phone_number, phone_number,
                sizeof(g_quality_records[slot].phone_number) - 1);
        strncpy(g_quality_records[slot].phone_ip, phone_ip,
                sizeof(g_quality_records[slot].phone_ip) - 1);
        g_quality_records[slot].last_test_time = time(NULL);
        memcpy(&g_quality_records[slot].last_result, result, sizeof(voip_probe_result_t));
        g_quality_records[slot].valid = 1;
    } else {
        LOG_WARN("Quality records full, cannot store result for %s", phone_number);
    }

    pthread_mutex_unlock(&g_records_mutex);
}

// Get quality record for a phone
int quality_monitor_get_record(const char *phone_number, phone_quality_record_t *record) {
    pthread_mutex_lock(&g_records_mutex);

    for (int i = 0; i < MAX_QUALITY_RECORDS; i++) {
        if (g_quality_records[i].valid &&
            strcmp(g_quality_records[i].phone_number, phone_number) == 0) {
            memcpy(record, &g_quality_records[i], sizeof(phone_quality_record_t));
            pthread_mutex_unlock(&g_records_mutex);
            return 0;
        }
    }

    pthread_mutex_unlock(&g_records_mutex);
    return -1;
}

// Get all quality records
int quality_monitor_get_all_records(phone_quality_record_t *records, int max_records) {
    pthread_mutex_lock(&g_records_mutex);

    int count = 0;
    for (int i = 0; i < MAX_QUALITY_RECORDS && count < max_records; i++) {
        if (g_quality_records[i].valid) {
            memcpy(&records[count], &g_quality_records[i], sizeof(phone_quality_record_t));
            count++;
        }
    }

    pthread_mutex_unlock(&g_records_mutex);
    return count;
}

// Write quality records to JSON file
static void write_quality_json(void) {
    FILE *fp = fopen("/tmp/phone_quality.json", "w");
    if (!fp) {
        LOG_ERROR("Failed to open /tmp/phone_quality.json for writing");
        return;
    }

    pthread_mutex_lock(&g_records_mutex);

    fprintf(fp, "{\"phones\":[");

    int first = 1;
    for (int i = 0; i < MAX_QUALITY_RECORDS; i++) {
        if (!g_quality_records[i].valid) continue;

        if (!first) fprintf(fp, ",");
        first = 0;

        phone_quality_record_t *r = &g_quality_records[i];
        fprintf(fp,
                "{\"number\":\"%s\","
                "\"ip\":\"%s\","
                "\"last_test\":%ld,"
                "\"status\":\"%s\","
                "\"media_rtt_ms\":%ld,"
                "\"jitter_ms\":%.2f,"
                "\"loss_percent\":%.2f,"
                "\"packets_lost\":%u,"
                "\"packets_sent\":%u,"
                "\"reason\":\"%s\"}",
                r->phone_number,
                r->phone_ip,
                (long)r->last_test_time,
                voip_probe_status_str(r->last_result.status),
                r->last_result.media_rtt_ms,
                r->last_result.jitter_ms,
                r->last_result.loss_fraction * 100.0,
                r->last_result.packets_lost,
                r->last_result.packets_sent,
                r->last_result.status_reason
        );
    }

    fprintf(fp, "]}");

    pthread_mutex_unlock(&g_records_mutex);

    fclose(fp);
}

// Quality monitor thread function
void* quality_monitor_thread(void *arg) {
    quality_monitor_context_t *ctx = (quality_monitor_context_t *)arg;

    LOG_INFO("Quality monitor thread running (interval=%d sec, cycle_delay=%d sec)",
             ctx->config.test_interval_sec, ctx->config.cycle_delay_sec);

    while (g_monitor_running) {
        // Get list of ALL users to test (both CSV directory and dynamic registrations)
        pthread_mutex_lock(&registered_users_mutex);

        // Copy user info to local array (minimize lock time)
        typedef struct {
            char phone_number[32];
            char phone_ip[64];
        } user_info_t;

        user_info_t users_to_test[MAX_REGISTERED_USERS];
        int test_count = 0;

        // Iterate through entire registered_users array (includes both CSV and dynamic users)
        for (int i = 0; i < MAX_REGISTERED_USERS; i++) {
            if (registered_users[i].user_id[0] != '\0') {
                // Resolve phone IP via DNS (user_id.local.mesh) to check if phone is online
                char hostname[256];
                snprintf(hostname, sizeof(hostname), "%s.local.mesh", registered_users[i].user_id);

                struct hostent *he = gethostbyname(hostname);
                if (he == NULL || he->h_addr_list[0] == NULL) {
                    // Skip if DNS resolution fails - phone is not online/reachable
                    continue;
                }

                // Phone is reachable - add to test list
                struct in_addr addr;
                memcpy(&addr, he->h_addr_list[0], sizeof(struct in_addr));
                const char *ip = inet_ntoa(addr);

                strncpy(users_to_test[test_count].phone_number,
                        registered_users[i].user_id,
                        sizeof(users_to_test[test_count].phone_number) - 1);
                strncpy(users_to_test[test_count].phone_ip, ip,
                        sizeof(users_to_test[test_count].phone_ip) - 1);

                test_count++;
            }
        }
        pthread_mutex_unlock(&registered_users_mutex);

        LOG_INFO("Quality monitor: Starting test cycle for %d phones", test_count);

        // Log which phones will be tested
        for (int i = 0; i < test_count && i < 10; i++) {
            LOG_INFO("  [%d/%d] %s -> %s", i+1, test_count,
                     users_to_test[i].phone_number, users_to_test[i].phone_ip);
        }
        if (test_count > 10) {
            LOG_INFO("  ... and %d more phones", test_count - 10);
        }

        // Test each phone
        int success_count = 0;
        int fail_count = 0;

        for (int i = 0; i < test_count && g_monitor_running; i++) {
            voip_probe_result_t result;

            LOG_INFO("[%d/%d] Testing phone %s (%s)...", i+1, test_count,
                     users_to_test[i].phone_number, users_to_test[i].phone_ip);

            int rc = test_phone_quality_with_socket(
                ctx->sip_socket,
                users_to_test[i].phone_number,
                users_to_test[i].phone_ip,
                ctx->server_ip,
                &result,
                &ctx->config.probe_config
            );

            // Store result
            store_quality_result(users_to_test[i].phone_number,
                                users_to_test[i].phone_ip, &result);

            if (result.status == VOIP_PROBE_SUCCESS) {
                success_count++;
                LOG_INFO("[%d/%d] ✓ Phone %s: RTT=%ld ms, Jitter=%.2f ms, Loss=%.1f%%, Packets=%u/%u",
                         i+1, test_count,
                         users_to_test[i].phone_number,
                         result.media_rtt_ms, result.jitter_ms,
                         result.loss_fraction * 100.0,
                         result.packets_sent - result.packets_lost,
                         result.packets_sent);
            } else {
                fail_count++;
                LOG_WARN("[%d/%d] ✗ Phone %s: %s - %s",
                         i+1, test_count,
                         users_to_test[i].phone_number,
                         voip_probe_status_str(result.status),
                         result.status_reason);
            }

            // Delay before testing next phone (if not last)
            if (i < test_count - 1 && g_monitor_running) {
                sleep(ctx->config.cycle_delay_sec);
            }
        }

        // Write quality data to JSON file for CGI endpoint
        write_quality_json();

        // Log cycle summary
        LOG_INFO("Quality monitor: Cycle complete - %d tested, %d succeeded, %d failed",
                 test_count, success_count, fail_count);
        if (success_count > 0) {
            LOG_INFO("Quality data written to /tmp/phone_quality.json (%d results)",
                     success_count + fail_count);
        }

        // Wait for next test interval
        if (g_monitor_running) {
            LOG_INFO("Quality monitor: Next test cycle in %d seconds",
                     ctx->config.test_interval_sec);

            // Sleep in small increments to allow quick shutdown
            for (int i = 0; i < ctx->config.test_interval_sec && g_monitor_running; i++) {
                sleep(1);
            }
        }
    }

    LOG_INFO("Quality monitor thread exiting");
    return NULL;
}
