#define MODULE_NAME "PASSIVE_SAFETY"

#include "passive_safety.h"
#include "../common.h"
#include "../call-sessions/call_sessions.h"
#include "../config_loader/config_loader.h"
#include "../file_utils/file_utils.h"
#include "../software_health/software_health.h" // For health monitoring heartbeats

// Thread health tracking
time_t g_fetcher_last_heartbeat = 0;
time_t g_updater_last_heartbeat = 0;
pthread_t g_passive_safety_tid = 0;

// ============================================================================
// WEEK 1: ESSENTIAL PASSIVE SAFETY FEATURES
// ============================================================================

// 1. CALL SESSION CLEANUP - Remove stale call sessions that consume resources
void passive_cleanup_stale_call_sessions(void) {
    time_t now = time(NULL);
    int cleaned_count = 0;

    for (int i = 0; i < MAX_CALL_SESSIONS; i++) {
        if (call_sessions[i].in_use) {
            // Clean up sessions older than 2 hours (likely abandoned)
            // In emergency situations, calls shouldn't last this long without activity
            time_t session_age = now - call_sessions[i].creation_time;

            if (session_age > 7200) { // 2 hours = 7200 seconds
                LOG_INFO("Cleaning up stale call session: %s (age: %ld seconds)",
                         call_sessions[i].call_id, session_age);
                terminate_call_session(&call_sessions[i]);
                cleaned_count++;
            }
        }
    }

    if (cleaned_count > 0) {
        LOG_INFO("Passive cleanup freed %d stale call sessions", cleaned_count);
    }
}

// 2. CONFIGURATION SELF-CORRECTION - Fix common deployment mistakes
void validate_and_correct_config(void) {
    bool config_corrected = false;

    // Validate phonebook fetch interval (prevent too-frequent updates)
    if (g_pb_interval_seconds < 300) { // Less than 5 minutes is too aggressive
        LOG_WARN("Phonebook interval %d too small, correcting to 1800 seconds",
                 g_pb_interval_seconds);
        g_pb_interval_seconds = 1800; // 30 minutes
        config_corrected = true;
    }

    // Validate status update interval
    if (g_status_update_interval_seconds < 60) { // Less than 1 minute is too aggressive
        LOG_WARN("Status update interval %d too small, correcting to 600 seconds",
                 g_status_update_interval_seconds);
        g_status_update_interval_seconds = 600; // 10 minutes
        config_corrected = true;
    }

    // Ensure we have at least one phonebook server
    if (g_num_phonebook_servers == 0) {
        LOG_WARN("No phonebook servers configured, adding default server");
        strncpy(g_phonebook_servers_list[0].host, "localnode.local.mesh",
                sizeof(g_phonebook_servers_list[0].host) - 1);
        strncpy(g_phonebook_servers_list[0].port, "80",
                sizeof(g_phonebook_servers_list[0].port) - 1);
        strncpy(g_phonebook_servers_list[0].path, "/phonebook.csv",
                sizeof(g_phonebook_servers_list[0].path) - 1);
        g_num_phonebook_servers = 1;
        config_corrected = true;
    }

    if (config_corrected) {
        LOG_INFO("Configuration automatically corrected for optimal operation");
    }
}

// 3. GRACEFUL DEGRADATION - Adapt to high load automatically
void enable_graceful_degradation_if_needed(void) {
    static time_t last_check = 0;
    time_t now = time(NULL);

    // Check every 60 seconds
    if (now - last_check < 60) {
        return;
    }
    last_check = now;

    // Count active call sessions
    int active_calls = 0;
    for (int i = 0; i < MAX_CALL_SESSIONS; i++) {
        if (call_sessions[i].in_use) {
            active_calls++;
        }
    }

    // If approaching call session limits, reduce background activity
    if (active_calls > (MAX_CALL_SESSIONS * 0.8)) {
        // Double phonebook fetch interval to reduce background load
        if (g_pb_interval_seconds < 7200) { // Don't exceed 2 hours
            g_pb_interval_seconds *= 2;
            LOG_INFO("High call load detected (%d/%d), reducing phonebook fetch frequency to %d seconds",
                     active_calls, MAX_CALL_SESSIONS, g_pb_interval_seconds);
        }
    }
    // Restore normal interval when load decreases
    else if (active_calls < (MAX_CALL_SESSIONS * 0.5)) {
        if (g_pb_interval_seconds > 1800) { // Restore to minimum 30 minutes
            g_pb_interval_seconds = 1800;
            LOG_INFO("Call load normalized (%d/%d), restored phonebook fetch frequency",
                     active_calls, MAX_CALL_SESSIONS);
        }
    }
}

// 3.5. ORPHANED FILE CLEANUP - Remove any leftover backup/temp files
void cleanup_orphaned_phonebook_files(void) {
    char backup_path[512];
    char temp_path[512];

    // Check for orphaned backup and temp files
    snprintf(backup_path, sizeof(backup_path), "%s.backup", PB_XML_PUBLIC_PATH);
    snprintf(temp_path, sizeof(temp_path), "%s.temp", PB_XML_PUBLIC_PATH);

    // Remove orphaned backup file
    if (access(backup_path, F_OK) == 0) {
        if (remove(backup_path) == 0) {
            LOG_INFO("Cleaned up orphaned backup file: %s", backup_path);
        } else {
            LOG_WARN("Failed to remove orphaned backup file: %s", backup_path);
        }
    }

    // Remove orphaned temp file
    if (access(temp_path, F_OK) == 0) {
        if (remove(temp_path) == 0) {
            LOG_INFO("Cleaned up orphaned temp file: %s", temp_path);
        } else {
            LOG_WARN("Failed to remove orphaned temp file: %s", temp_path);
        }
    }
}

// ============================================================================
// WEEK 2: ENHANCED RESILIENCE FEATURES
// ============================================================================

// 4. SMART FILE HANDLING - Never corrupt phonebook data
void safe_phonebook_file_operation(const char *source_path, const char *dest_path) {
    char backup_path[512];
    char temp_path[512];
    bool rollback_needed = false;

    // Create backup and temporary file paths
    snprintf(backup_path, sizeof(backup_path), "%s.backup", dest_path);
    snprintf(temp_path, sizeof(temp_path), "%s.temp", dest_path);

    // Step 1: Create backup of current file (if it exists)
    if (access(dest_path, F_OK) == 0) {
        if (file_utils_copy_file(dest_path, backup_path) != 0) {
            LOG_ERROR("Failed to create backup before phonebook update");
            return; // Abort if we can't create backup
        }
    }

    // Step 2: Copy new data to temporary file
    if (file_utils_copy_file(source_path, temp_path) != 0) {
        LOG_ERROR("Failed to create temporary file for phonebook update");
        // Clean up backup file if we created one
        if (access(backup_path, F_OK) == 0) {
            remove(backup_path);
        }
        return; // Abort if copy fails
    }

    // Step 3: Verify temporary file integrity (basic check)
    FILE *verify_fp = fopen(temp_path, "r");
    if (!verify_fp) {
        LOG_ERROR("Cannot verify temporary phonebook file integrity");
        remove(temp_path); // Clean up failed temp file
        // Clean up backup file if we created one
        if (access(backup_path, F_OK) == 0) {
            remove(backup_path);
        }
        return;
    }

    // Basic verification: file should have some content
    fseek(verify_fp, 0, SEEK_END);
    long file_size = ftell(verify_fp);
    fclose(verify_fp);

    if (file_size < 50) { // Phonebook should be at least 50 bytes
        LOG_ERROR("Phonebook file appears corrupted (size: %ld bytes), aborting update", file_size);
        remove(temp_path);
        // Clean up backup file if we created one
        if (access(backup_path, F_OK) == 0) {
            remove(backup_path);
        }
        return;
    }

    // Step 4: Atomic rename (replace destination with verified temp file)
    if (rename(temp_path, dest_path) != 0) {
        LOG_ERROR("Failed to replace phonebook file, attempting rollback");
        rollback_needed = true;
    }

    // Step 5: Rollback if needed
    if (rollback_needed && access(backup_path, F_OK) == 0) {
        if (rename(backup_path, dest_path) == 0) {
            LOG_INFO("Successfully rolled back to previous phonebook version");
        } else {
            LOG_ERROR("Rollback failed - phonebook may be unavailable");
            // Clean up backup file even if rollback failed
            remove(backup_path);
        }
    } else if (!rollback_needed) {
        // Success - clean up backup
        remove(backup_path);
        LOG_DEBUG("Phonebook update completed successfully");
    } else {
        // Backup doesn't exist but rollback was needed - clean up anyway
        remove(backup_path);
    }

    // Always clean up any remaining temporary files
    remove(temp_path);
}

// 5. THREAD RECOVERY - Auto-restart hung threads
void passive_thread_recovery_check(void) {
    time_t now = time(NULL);

    // Check phonebook fetcher thread health
    if (g_fetcher_last_heartbeat > 0 && (now - g_fetcher_last_heartbeat) > 1800) { // 30 minutes
        LOG_WARN("Phonebook fetcher thread appears hung (no heartbeat for %ld seconds)",
                 now - g_fetcher_last_heartbeat);

        // Attempt to cancel and restart the thread
        if (pthread_cancel(fetcher_tid) == 0) {
            // Create new fetcher thread
            extern void *phonebook_fetcher_thread(void *arg);
            if (pthread_create(&fetcher_tid, NULL, phonebook_fetcher_thread, NULL) == 0) {
                LOG_INFO("Successfully restarted phonebook fetcher thread");
                g_fetcher_last_heartbeat = now; // Reset heartbeat
            } else {
                LOG_ERROR("Failed to restart phonebook fetcher thread");
            }
        }
    }

    // Check status updater thread health
    if (g_updater_last_heartbeat > 0 && (now - g_updater_last_heartbeat) > 1200) { // 20 minutes
        LOG_WARN("Status updater thread appears hung (no heartbeat for %ld seconds)",
                 now - g_updater_last_heartbeat);

        // Attempt to cancel and restart the thread
        if (pthread_cancel(status_updater_tid) == 0) {
            // Create new status updater thread
            extern void *status_updater_thread(void *arg);
            if (pthread_create(&status_updater_tid, NULL, status_updater_thread, NULL) == 0) {
                LOG_INFO("Successfully restarted status updater thread");
                g_updater_last_heartbeat = now; // Reset heartbeat
            } else {
                LOG_ERROR("Failed to restart status updater thread");
            }
        }
    }
}

// ============================================================================
// PASSIVE SAFETY BACKGROUND THREAD
// ============================================================================

void *passive_safety_thread(void *arg) {
    (void)arg;
    LOG_INFO("Passive safety thread started - silent self-healing enabled");

    while (1) {
        // Health Monitoring: Update thread heartbeat
        if (g_health_config.enabled && g_health_config.thread_monitoring) {
            update_thread_heartbeat(THREAD_SAFETY);
        }

        // Run safety checks every 5 minutes
        sleep(300);

        // Week 1: Essential safety checks
        passive_cleanup_stale_call_sessions();
        enable_graceful_degradation_if_needed();
        cleanup_orphaned_phonebook_files();

        // Health monitoring periodic checks
        if (is_software_health_enabled()) {
            periodic_health_check();
        }

        // Week 2: Enhanced resilience checks (every 15 minutes)
        static int cycle_count = 0;
        if ((++cycle_count % 3) == 0) {
            passive_thread_recovery_check();
        }
    }

    return NULL;
}