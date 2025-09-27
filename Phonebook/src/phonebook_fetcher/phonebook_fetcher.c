#define MODULE_NAME "FETCHER" // Define MODULE_NAME at the top of the file

#include "phonebook_fetcher.h"
#include "../common.h" // This includes necessary system headers and core types
#include "../config_loader/config_loader.h" // For g_pb_interval_seconds, g_phonebook_servers_list, g_num_phonebook_servers
#include "../user_manager/user_manager.h"
#include "../file_utils/file_utils.h"
#include "../csv_processor/csv_processor.h"
#include "../passive_safety/passive_safety.h" // For heartbeat tracking

// Note: Global extern declarations moved to common.h
// extern int g_pb_interval_seconds; // Declared in common.h
// extern ConfigurableServer g_phonebook_servers_list[MAX_PB_SERVERS]; // Declared in common.h
// extern int g_num_phonebook_servers; // Declared in common.h


int ensure_phonebook_directory_exists(const char *path) {
    return file_utils_ensure_directory_exists(path);
}

int publish_phonebook_xml(const char *source_filepath) {
    pthread_mutex_lock(&phonebook_file_mutex);
    int publish_success = 0;

    char public_path_copy[MAX_CONFIG_PATH_LEN]; // MAX_CONFIG_PATH_LEN from common.h
    strncpy(public_path_copy, PB_XML_PUBLIC_PATH, sizeof(public_path_copy) - 1); // PB_XML_PUBLIC_PATH from common.h
    public_path_copy[sizeof(public_path_copy) - 1] = '\0';
    char *public_dir = dirname(public_path_copy); // dirname from common.h

    if (file_utils_ensure_directory_exists(public_dir) != 0) {
        LOG_ERROR("Critical: Failed to ensure public directory '%s' for publish. Exiting publish.", public_dir);
        pthread_mutex_unlock(&phonebook_file_mutex);
        return 1;
    }

    // Passive Safety: Use safe file operation with automatic rollback
    safe_phonebook_file_operation(source_filepath, PB_XML_PUBLIC_PATH);

    // Check if the operation succeeded
    if (access(PB_XML_PUBLIC_PATH, F_OK) == 0) {
        LOG_INFO("Phonebook XML safely published at %s.", PB_XML_PUBLIC_PATH);
        publish_success = 1;
    } else {
        LOG_ERROR("Safe file operation failed for XML publish");
    }

    if (publish_success) {
        pthread_mutex_lock(&updater_trigger_mutex);
        pthread_cond_signal(&updater_trigger_cond);
        pthread_mutex_unlock(&updater_trigger_mutex);
        LOG_INFO("Signaled Status Updater for new phonebook.");
    } else {
        LOG_INFO("Phonebook XML publishing failed.");
        if (access(source_filepath, F_OK) == 0) { // access from common.h
            if (remove(source_filepath) != 0) { // remove from common.h
                LOG_WARN("Failed to delete temporary XML file '%s' after failed publish. Error: %s", source_filepath, strerror(errno));
            } else {
                LOG_DEBUG("Deleted temporary XML file '%s' after failed publish.", source_filepath);
            }
        }
    }
    pthread_mutex_unlock(&phonebook_file_mutex);
    return publish_success ? 0 : 1;
}

static bool initial_population_done = false;

void *phonebook_fetcher_thread(void *arg) {
    (void)arg;
    LOG_INFO("Phonebook fetcher started. Checking for existing phonebook data.");

    // Emergency boot sequence: Load existing phonebook immediately if available
    if (access(PB_CSV_PATH, F_OK) == 0) {
        LOG_INFO("Found existing phonebook CSV at '%s'. Loading immediately for service availability.", PB_CSV_PATH);
        populate_registered_users_from_csv(PB_CSV_PATH);
        LOG_INFO("Emergency boot: SIP user database loaded from persistent storage. Directory entries: %d.", num_directory_entries);
        initial_population_done = true;

        // Convert to XML for web interface
        char existing_xml_temp_path[MAX_CONFIG_PATH_LEN];
        if (csv_processor_convert_csv_to_xml_and_get_path(existing_xml_temp_path, sizeof(existing_xml_temp_path)) == 0) {
            publish_phonebook_xml(existing_xml_temp_path);
            LOG_INFO("Emergency boot: XML phonebook published from existing data.");
        }
    } else {
        LOG_INFO("No existing phonebook found. Service will be available after first successful fetch.");
    }

    LOG_INFO("Entering main phonebook fetch loop.");
    while (1) { // Changed from while (keep_running) to while (1)
        // Passive Safety: Update heartbeat for thread recovery monitoring
        g_fetcher_last_heartbeat = time(NULL);

        LOG_INFO("Starting new fetcher cycle.");
        char new_csv_hash[HASH_LENGTH + 1]; // HASH_LENGTH from common.h
        char last_good_csv_hash[HASH_LENGTH + 1];

        if (csv_processor_download_csv() != 0) {
            LOG_ERROR("CSV download failed. Skipping this cycle.");
            goto end_fetcher_cycle;
        }

        // Calculate hash of downloaded temp file (in RAM)
        if (csv_processor_calculate_file_conceptual_hash(PB_CSV_TEMP_PATH, new_csv_hash, sizeof(new_csv_hash)) != 0) {
            LOG_ERROR("Failed to calculate hash for downloaded CSV. Skipping this cycle.");
            remove(PB_CSV_TEMP_PATH); // Clean up temp file
            goto end_fetcher_cycle;
        }

        // Read existing hash from flash (only if we have persistent data)
        FILE *hash_fp = fopen(PB_LAST_GOOD_CSV_HASH_PATH, "r");
        if (hash_fp) {
            if (fgets(last_good_csv_hash, sizeof(last_good_csv_hash), hash_fp) != NULL) {
                last_good_csv_hash[strcspn(last_good_csv_hash, "\r\n")] = '\0';
                LOG_DEBUG("Last good CSV hash: %s", last_good_csv_hash);
            } else {
                LOG_INFO("Could not read last good CSV hash. Assuming change.");
                last_good_csv_hash[0] = '\0';
            }
            fclose(hash_fp);
        } else {
            LOG_INFO("No last good CSV hash file found. Assuming change for first run.");
            last_good_csv_hash[0] = '\0';
        }
        LOG_DEBUG("New CSV hash: %s", new_csv_hash);

        // Flash-friendly comparison: Only write to flash if data actually changed
        if (strcmp(new_csv_hash, last_good_csv_hash) == 0 && initial_population_done) {
            LOG_INFO("Downloaded CSV is identical to flash copy. No flash write needed - preserving flash lifespan.");
            remove(PB_CSV_TEMP_PATH); // Clean up unchanged temp file
            goto end_fetcher_cycle;
        } else {
            if (!initial_population_done) {
                LOG_INFO("Initial population required. Moving temp CSV to persistent storage.");
            } else {
                LOG_INFO("CSV content changed. Updating persistent storage (flash write).");
            }

            // Cross-filesystem move: copy temp file to flash, then remove temp
            if (file_utils_copy_file(PB_CSV_TEMP_PATH, PB_CSV_PATH) != 0) {
                LOG_ERROR("Failed to copy temp CSV to persistent storage");
                remove(PB_CSV_TEMP_PATH); // Clean up temp file
                goto end_fetcher_cycle;
            }
            remove(PB_CSV_TEMP_PATH); // Clean up temp file after successful copy
            LOG_INFO("CSV successfully copied to persistent storage with minimal flash wear.");
        }

        LOG_INFO("Populating SIP users from CSV for phonebook update.");
        populate_registered_users_from_csv(PB_CSV_PATH);
        LOG_INFO("SIP user database populated from CSV. Total directory entries: %d.", num_directory_entries); // num_directory_entries from common.h
        initial_population_done = true;

        LOG_INFO("Initiating XML conversion...");
        char fetched_xml_temp_path[MAX_CONFIG_PATH_LEN];
        if (csv_processor_convert_csv_to_xml_and_get_path(fetched_xml_temp_path, sizeof(fetched_xml_temp_path)) == 0) {
            LOG_INFO("XML conversion successful.");

            if (publish_phonebook_xml(fetched_xml_temp_path) == 0) {
                // Only update hash in flash if we haven't already written this hash
                if (strcmp(new_csv_hash, last_good_csv_hash) != 0) {
                    FILE *hash_fp_write = fopen(PB_LAST_GOOD_CSV_HASH_PATH, "w");
                    if (hash_fp_write) {
                        fprintf(hash_fp_write, "%s\n", new_csv_hash);
                        fclose(hash_fp_write);
                        LOG_INFO("Flash write: Updated CSV hash to '%s' (flash wear minimized).", new_csv_hash);
                    } else {
                        LOG_ERROR("Failed to write new CSV hash to '%s'. Error: %s", PB_LAST_GOOD_CSV_HASH_PATH, strerror(errno));
                    }
                } else {
                    LOG_DEBUG("Hash unchanged, skipping flash write for hash file.");
                }
            } else {
                LOG_WARN("XML publish failed, not updating hash file.");
            }

            // Keep CSV in persistent storage for emergency availability - do not delete

        } else {
            LOG_WARN("XML conversion failed. Keeping CSV in persistent storage for emergency availability.");
            // Do not delete CSV even if XML conversion fails - emergency data preservation
        }
        LOG_INFO("Finished fetcher cycle.");

        end_fetcher_cycle:;
        LOG_INFO("Sleeping %d seconds...", g_pb_interval_seconds); // Use global g_pb_interval_seconds
        for (int i = 0; i < g_pb_interval_seconds; i++) {
            // Check for webhook-triggered reload request
            if (phonebook_reload_requested) {
                phonebook_reload_requested = 0; // Reset flag
                LOG_INFO("Webhook reload requested - interrupting sleep to fetch phonebook immediately");
                break; // Exit sleep loop and restart fetch cycle
            }
            sleep(1); // sleep from common.h
        }
    }
    LOG_INFO("Phonebook fetcher thread exiting.");
    return NULL;
}