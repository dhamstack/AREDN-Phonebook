#define MODULE_NAME "UPDATER"

#include "status_updater.h"
#include "../common.h" // This includes necessary system headers and core types
#include "../config_loader/config_loader.h" // For g_status_update_interval_seconds
#include "../phonebook_fetcher/phonebook_fetcher.h"
#include "../file_utils/file_utils.h"
#include "../passive_safety/passive_safety.h" // For heartbeat tracking


typedef struct {
    char name[256]; // Name is a combined field, so still use general display name limits or larger.
    char telephone[MAX_PHONE_NUMBER_LEN]; // Use new constant
} TempPhonebookEntry;


static void strip_leading_asterisks(char *name) {
    if (!name || *name == '\0') {
        return;
    }
    char *ptr = name;
    while (*ptr != '\0' && (*ptr == '*' || isspace((unsigned char)*ptr))) {
        ptr++;
    }
    if (ptr != name) {
        memmove(name, ptr, strlen(ptr) + 1);
    }
}

// Re-define trim_whitespace here locally if not globally available,
// to ensure it's used for cleaning XML lines.
// It was made static in config_loader.c, so it's not globally available from common.h.
// Best to make a local copy or explicitly declare in common.h and define elsewhere globally.
// For now, let's make a local copy to ensure it compiles.
static char* trim_line_whitespace(char *str) {
    char *end;

    // Trim leading space
    while(isspace((unsigned char)*str)) str++;

    if(*str == 0)  // All spaces?
        return str;

    // Trim trailing space
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;

    // Write new null terminator character
    end[1] = '\0';

    return str;
}


void *status_updater_thread(void *arg) {
    (void)arg;
    LOG_INFO("Status updater started. Entering main loop.");

    struct timespec ts;

    while (1) { // Changed from while (keep_running) to while (1)
        // Passive Safety: Update heartbeat for thread recovery monitoring
        g_updater_last_heartbeat = time(NULL);

        pthread_mutex_lock(&updater_trigger_mutex);
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += g_status_update_interval_seconds;

        int wait_status = pthread_cond_timedwait(&updater_trigger_cond, &updater_trigger_mutex, &ts);
        pthread_mutex_unlock(&updater_trigger_mutex);

        // if (!keep_running) { // REMOVED
        //     break;
        // }

        LOG_INFO("Starting new update cycle.");

        if (wait_status == 0) {
            LOG_INFO("Triggered by Phonebook Fetcher signal.");
        } else if (wait_status == ETIMEDOUT) {
            LOG_INFO("Running on schedule (every %d seconds).", g_status_update_interval_seconds);
        } else {
            LOG_ERROR("pthread_cond_timedwait failed: %s", strerror(wait_status));
        }

        pthread_mutex_lock(&phonebook_file_mutex);
        FILE *f_input_xml = fopen(PB_XML_PUBLIC_PATH, "r");
        if (!f_input_xml) {
            LOG_WARN("Public phonebook %s not found or not readable. Waiting for it to be created/published by fetcher. Error: %s", PB_XML_PUBLIC_PATH, strerror(errno));
            pthread_mutex_unlock(&phonebook_file_mutex);
            sleep(1);
            continue;
        }
        LOG_INFO("Successfully opened public phonebook XML: %s", PB_XML_PUBLIC_PATH);

        pthread_mutex_unlock(&phonebook_file_mutex);

        char temp_xml_path_updater[MAX_CONFIG_PATH_LEN];
        strncpy(temp_xml_path_updater, "/tmp/phonebook_temp", sizeof(temp_xml_path_updater) - 1);
        temp_xml_path_updater[sizeof(temp_xml_path_updater) - 1] = '\0';

        FILE *f_output_xml = fopen(temp_xml_path_updater, "w");
        if (!f_output_xml) {
            LOG_ERROR("Failed to open temporary output file %s for writing. Error: %s", temp_xml_path_updater, strerror(errno));
            fclose(f_input_xml);
            continue;
        }
        LOG_INFO("Successfully opened temporary output XML: %s", temp_xml_path_updater);


        char line[1024];
        TempPhonebookEntry current_entry;
        bool in_directory_entry = false;
        int active_phones = 0;
        int inactive_phones = 0;
        int total_entries_read_from_xml = 0;

        fprintf(f_output_xml, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<YealinkIPPhoneDirectory>\n");

        while(fgets(line, sizeof(line), f_input_xml)) {
            // Trim leading/trailing whitespace from the line immediately
            char *trimmed_line = trim_line_whitespace(line);
            trimmed_line[strcspn(trimmed_line, "\r\n")] = '\0'; // Remove remaining newline

            if (strstr(trimmed_line, "<DirectoryEntry>")) {
                in_directory_entry = true;
                memset(&current_entry, 0, sizeof(current_entry));
                continue;
            }
            if (strstr(trimmed_line, "</DirectoryEntry>")) {
                char hostname[MAX_USER_ID_LEN + sizeof(AREDN_MESH_DOMAIN) + 1];
                snprintf(hostname, sizeof(hostname), "%s.%s", current_entry.telephone, AREDN_MESH_DOMAIN);

                bool is_active = false;
                struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_DGRAM}, *res;

                int gai_status = getaddrinfo(hostname, NULL, &hints, &res);

                if (gai_status == 0) {
                    is_active = true;
                    freeaddrinfo(res);
                }

                strip_leading_asterisks(current_entry.name);

                if (is_active) {
                    char temp_name_buffer[sizeof(current_entry.name) + 3];
                    if (strlen(current_entry.name) + 2 < sizeof(temp_name_buffer)) {
                        snprintf(temp_name_buffer, sizeof(temp_name_buffer), "* %s", current_entry.name);
                        strncpy(current_entry.name, temp_name_buffer, sizeof(current_entry.name)-1);
                        current_entry.name[sizeof(current_entry.name)-1] = '\0';
                    } else {
                        LOG_WARN("Display name for %s too long to prepend '* '.", current_entry.telephone);
                    }
                    active_phones++;
                    LOG_DEBUG("Entry %d: '%s' (Tel:%s) Active:YES", total_entries_read_from_xml + 1, current_entry.name, current_entry.telephone);
                } else {
                    inactive_phones++;
                    LOG_DEBUG("Entry %d: '%s' (Tel:%s) Active:NO", total_entries_read_from_xml + 1, current_entry.name, current_entry.telephone);
                }

                fprintf(f_output_xml, "  <DirectoryEntry>\n    <Name>%s</Name>\n    <Telephone>%s</Telephone>\n  </DirectoryEntry>\n",
                        current_entry.name, current_entry.telephone);

                in_directory_entry = false;
                total_entries_read_from_xml++;
                continue;
            }

            if (in_directory_entry) {
                char temp_val[256];
                if (strstr(trimmed_line, "<Name>")) { // Use trimmed_line here
                    if (sscanf(trimmed_line, "<Name>%255[^<]</Name>", temp_val) == 1) { // Removed leading spaces from format
                        strncpy(current_entry.name, temp_val, sizeof(current_entry.name) - 1);
                        current_entry.name[sizeof(current_entry.name) - 1] = '\0';
                    } else {
                        LOG_WARN("Failed to parse Name from line: '%s'", trimmed_line); // Log trimmed_line
                    }
                }
                else if (strstr(trimmed_line, "<Telephone>")) { // Use trimmed_line here
                    char format_str[64];
                    // Formats: "<Telephone>%<length>[^<]</Telephone>"
                    // Since line is already trimmed, no need for %*[ \t]
                    snprintf(format_str, sizeof(format_str), "<Telephone>%%%d[^<]</Telephone>", MAX_PHONE_NUMBER_LEN - 1);

                    if (sscanf(trimmed_line, format_str, temp_val) == 1) {
                        strncpy(current_entry.telephone, temp_val, sizeof(current_entry.telephone) - 1);
                        current_entry.telephone[sizeof(current_entry.telephone) - 1] = '\0';
                    } else {
                        LOG_WARN("Failed to parse Telephone from line: '%s'", trimmed_line); // Log trimmed_line
                    }
                }
            }
        }
        if (ferror(f_input_xml)) {
            LOG_ERROR("FILE ERROR: ferror() is true. Last entry processed: %d. Error: %s", total_entries_read_from_xml, strerror(errno));
        }
        if (feof(f_input_xml)) {
            LOG_INFO("EOF reached. Last entry processed: %d.", total_entries_read_from_xml);
        } else {
            LOG_WARN("Loop terminated unexpectedly (neither EOF nor ferror). Last entry processed: %d.", total_entries_read_from_xml);
        }


        fprintf(f_output_xml, "</YealinkIPPhoneDirectory>\n");

        fclose(f_input_xml);
        fflush(f_output_xml);
        fsync(fileno(f_output_xml));
        fclose(f_output_xml);

        if (publish_phonebook_xml(temp_xml_path_updater) != 0) {
            LOG_ERROR("Failed to publish updated phonebook. Processed entries: %d.", total_entries_read_from_xml);
        } else {
            LOG_INFO("Public phonebook updated. Active: %d, Inactive: %d, Total: %d (from input XML).", active_phones, inactive_phones, total_entries_read_from_xml);
        }
        if (access(temp_xml_path_updater, F_OK) == 0) {
            if (remove(temp_xml_path_updater) != 0) {
                LOG_WARN("Failed to delete temporary XML file '%s' at end of cycle. Error: %s", temp_xml_path_updater, strerror(errno));
            } else {
                LOG_DEBUG("Deleted temporary XML file '%s' at end of cycle.", temp_xml_path_updater);
            }
        }

        LOG_INFO("Finished update cycle.");
    }

    LOG_INFO("Status updater exiting.");
    return NULL;
}