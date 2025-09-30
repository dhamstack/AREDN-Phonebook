#include "user_manager.h" // This include remains the same, as the header will be in the same new directory
#include "../common.h" // This now includes necessary system headers and core types

#define MODULE_NAME "USER"

static void trim_whitespace(char *str) {
    if (!str || *str == '\0') {
        return;
    }

    char *first_char = str;
    while (isspace((unsigned char)*first_char)) {
        first_char++;
    }

    char *end_char = first_char + strlen(first_char) - 1;
    while (end_char > first_char && isspace((unsigned char)*end_char)) {
        end_char--;
    }

    *(end_char + 1) = '\0';

    if (str != first_char) {
        memmove(str, first_char, strlen(first_char) + 1);
    }
}


RegisteredUser* find_registered_user(const char *user_id) {
    pthread_mutex_lock(&registered_users_mutex);
    for (int i = 0; i < MAX_REGISTERED_USERS; i++) {
        // Only consider active users for find_registered_user logic
        if (registered_users[i].user_id[0] != '\0' &&
            registered_users[i].is_active) { // Still check is_active
            if (strcmp(registered_users[i].user_id, user_id) == 0) {
                pthread_mutex_unlock(&registered_users_mutex);
                return &registered_users[i];
            }
        }
    }
    pthread_mutex_unlock(&registered_users_mutex);
    return NULL;
}

// Simplified add_or_update_registered_user
RegisteredUser* add_or_update_registered_user(const char *user_id,
                                          const char *display_name,
                                          int expires) {
    LOG_DEBUG("add_or_update_registered_user called for user '%s' (Display: '%s'), expires %d.",
                user_id, display_name, expires);

    pthread_mutex_lock(&registered_users_mutex);

    RegisteredUser *user = NULL;
    // Try to find an existing user slot
    for (int i = 0; i < MAX_REGISTERED_USERS; i++) {
        if (registered_users[i].user_id[0] != '\0' &&
            strcmp(registered_users[i].user_id, user_id) == 0) {
            user = &registered_users[i];
            break;
        }
    }

    if (user) {
        // User found
        if (expires > 0) {
            // user->user_id is already MAX_PHONE_NUMBER_LEN, assumed to be same as user_id from REGISTER
            // strncpy(user->user_id, user_id, MAX_PHONE_NUMBER_LEN - 1); // Not needed here, user_id is the key
            user->user_id[MAX_PHONE_NUMBER_LEN - 1] = '\0'; // Ensure null-termination if user_id was updated
            // No longer storing contact_uri, ip_address, port, registration_time here
            
            if (strlen(display_name) > 0 && strcmp(user->display_name, display_name) != 0) {
                strncpy(user->display_name, display_name, MAX_DISPLAY_NAME_LEN - 1);
                user->display_name[MAX_DISPLAY_NAME_LEN - 1] = '\0';
            }
            if (!user->is_active) {
                user->is_active = true;
                // If this was a directory user that became active via register
                if(user->is_known_from_directory) {
                    LOG_INFO("Directory user '%s' (%s) now dynamically active.", user_id, user->display_name);
                } else {
                    num_registered_users++; // Count dynamic registrations
                    LOG_INFO("Activated existing dynamic registration for user '%s' (%s). Total active dynamic: %d.", user_id, user->display_name, num_registered_users);
                }
            } else {
                LOG_INFO("Refreshed dynamic registration for user '%s' (%s).", user_id, user->display_name);
            }
        } else { // expires == 0, deactivate
            if (user->is_active) {
                user->is_active = false;
                if(!user->is_known_from_directory) { // Only decrement if it was a purely dynamic registration
                   num_registered_users--;
                   LOG_INFO("Deactivated dynamic registration for user '%s' (%s). Remaining active dynamic: %d.", user_id, user->display_name, num_registered_users);
                   // Clear the slot if it was purely dynamic and now inactive
                   user->user_id[0] = '\0';
                   user->display_name[0] = '\0';
                } else {
                    LOG_INFO("Dynamic registration for directory user '%s' (%s) expired. Still known via directory.", user_id, user->display_name);
                }
            } else {
                LOG_DEBUG("Attempted to deactivate already inactive user '%s'.", user_id);
            }
        }
        pthread_mutex_unlock(&registered_users_mutex);
        return user;
    } else {
        // User not found, attempt to add new dynamic registration
        if (expires > 0) {
            if (num_registered_users + num_directory_entries < MAX_REGISTERED_USERS) {
                for (int i = 0; i < MAX_REGISTERED_USERS; i++) {
                    if (registered_users[i].user_id[0] == '\0') { // Found empty slot
                        RegisteredUser *newu = &registered_users[i];
                        strncpy(newu->user_id, user_id, MAX_PHONE_NUMBER_LEN - 1);
                        newu->user_id[MAX_PHONE_NUMBER_LEN - 1] = '\0';
                        strncpy(newu->display_name, display_name, MAX_DISPLAY_NAME_LEN - 1);
                        newu->display_name[MAX_DISPLAY_NAME_LEN - 1] = '\0';
                        newu->is_active = true;
                        newu->is_known_from_directory = false; // This is a new dynamic registration
                        num_registered_users++;
                        LOG_INFO("New dynamic registration for user '%s' (%s). Total active dynamic: %d.", user_id, display_name, num_registered_users);
                        pthread_mutex_unlock(&registered_users_mutex);
                        return newu;
                    }
                }
            }
            LOG_WARN("Max registered users/directory slots reached, cannot register '%s'.", user_id);
            pthread_mutex_unlock(&registered_users_mutex);
            return NULL;
        } else {
            LOG_DEBUG("Attempted to deactivate non-existent user '%s' with expires 0.", user_id);
            pthread_mutex_unlock(&registered_users_mutex);
            return NULL;
        }
    }
}

RegisteredUser* add_csv_user_to_registered_users_table(const char *user_id_numeric,
                                   const char *display_name) {
    pthread_mutex_lock(&registered_users_mutex);

    RegisteredUser *existing = NULL;
    for (int i = 0; i < MAX_REGISTERED_USERS; i++) {
        if (registered_users[i].user_id[0] != '\0' &&
            strcmp(registered_users[i].user_id, user_id_numeric) == 0) {
            existing = &registered_users[i];
            break;
        }
    }

    if (existing) {
        // User found (could be existing directory entry or a dynamic reg for this ID)
        if (strcmp(existing->display_name, display_name) != 0) {
            strncpy(existing->display_name, display_name, MAX_DISPLAY_NAME_LEN - 1);
            existing->display_name[MAX_DISPLAY_NAME_LEN - 1] = '\0';
            LOG_DEBUG("Updated display name for existing CSV/directory user '%s' to '%s'.", user_id_numeric, display_name);
        } else {
            LOG_DEBUG("CSV/directory user '%s' already exists with same display name.", user_id_numeric);
        }
        existing->is_known_from_directory = true; // Confirm it's from directory
        // Keep active, regardless of previous dynamic state (since it's in the directory)
        if (!existing->is_active) {
            existing->is_active = true; // Mark active if it was inactive
            // num_registered_users not incremented as this is not a new dynamic registration
            // num_directory_entries is incremented only when new slot is used
            LOG_INFO("CSV/directory user '%s' (%s) marked active from phonebook.", user_id_numeric, display_name);
        }

        pthread_mutex_unlock(&registered_users_mutex);
        return existing;
    }

    // User not found, add as new directory entry
    if (num_registered_users + num_directory_entries < MAX_REGISTERED_USERS) {
        for (int i = 0; i < MAX_REGISTERED_USERS; i++) {
            if (registered_users[i].user_id[0] == '\0') { // Found empty slot
                RegisteredUser *u = &registered_users[i];
                // user_id_numeric is now sanitized by populate_registered_users_from_csv before this call
                strncpy(u->user_id, user_id_numeric, MAX_PHONE_NUMBER_LEN - 1);
                u->user_id[MAX_PHONE_NUMBER_LEN - 1] = '\0';
                strncpy(u->display_name, display_name, MAX_DISPLAY_NAME_LEN - 1);
                u->display_name[MAX_DISPLAY_NAME_LEN - 1] = '\0';

                u->is_active = true; // Directory users are considered active by default
                u->is_known_from_directory = true;
                num_directory_entries++;
                // Changed log level from INFO to DEBUG and removed total count
                LOG_DEBUG("Added new CSV/directory user '%s' (%s).", user_id_numeric, display_name);
                pthread_mutex_unlock(&registered_users_mutex);
                return u;
            }
        }
    }
    LOG_WARN("Failed to add CSV/directory user '%s' (%s): Max directory/registered users reached (%d).", user_id_numeric, display_name, MAX_REGISTERED_USERS);
    pthread_mutex_unlock(&registered_users_mutex);
    return NULL;
}


void init_registered_users_table() {
    pthread_mutex_lock(&registered_users_mutex);
    for (int i = 0; i < MAX_REGISTERED_USERS; i++) {
        registered_users[i].is_active = false;
        registered_users[i].is_known_from_directory = false;
        registered_users[i].user_id[0] = '\0';
        registered_users[i].display_name[0] = '\0';
        // No need to clear removed fields
    }
    num_registered_users = 0; // Reset dynamic count
    num_directory_entries = 0; // Reset directory count
    LOG_DEBUG("Initialized user tables (cleared all entries).");
    pthread_mutex_unlock(&registered_users_mutex);
}

void populate_registered_users_from_csv(const char *filepath) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        LOG_ERROR("Failed to open CSV phonebook file '%s' for populating registered users.",
                    filepath);
        return;
    }
    LOG_INFO("Populating registered users from CSV '%s'...", filepath);

    init_registered_users_table(); // Clear all existing entries first

    char line[2048];
    int ln = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (ln++ == 0) continue; // Skip header

        char *cols[5] = {NULL};
        char *p = line;
        for (int i=0; i<5; i++) {
            if (i<4) {
                char *c = strchr(p, ',');
                if (c) {
                    *c = '\0';
                    cols[i] = p;
                    p = c+1;
                } else {
                    cols[i] = p;
                    p = NULL;
                }
            } else {
                cols[i] = p;
            }
            if (!p && i < 4) { // Only break if missing expected columns before the last one
                LOG_WARN("Line %d has fewer than 5 columns. Missing column %d and subsequent. Line: '%.*s'", ln, i+1, (int)strcspn(line, "\r\n"), line);
                break;
            }
        }

        if (cols[4]) {
            char *e = strchr(cols[4], ','); // Handle potential extra commas in last field
            if (e) *e = '\0';
            cols[4][strcspn(cols[4], "\r\n")] = '\0'; // Remove newline
        }
        if (!cols[4] || !*cols[4]) {
            LOG_WARN("Skipping CSV row %d due to missing or empty Telephone number (column 5). Line: '%.*s'", ln, (int)strcspn(line, "\r\n"), line);
            continue;
        }

        char s0[MAX_FIRST_NAME_LEN]={0}, s1[MAX_NAME_LEN]={0}, s2[MAX_CALLSIGN_LEN]={0};
        char sanitized_user_id_numeric[MAX_PHONE_NUMBER_LEN] = {0}; // New buffer for sanitized user_id

        sanitize_utf8(cols[0] ? cols[0] : "", s0, sizeof(s0));
        sanitize_utf8(cols[1] ? cols[1] : "", s1, sizeof(s1));
        sanitize_utf8(cols[2] ? cols[2] : "", s2, sizeof(s2));
        sanitize_utf8(cols[4] ? cols[4] : "", sanitized_user_id_numeric, sizeof(sanitized_user_id_numeric)); // Sanitize user ID

        trim_whitespace(s0);
        trim_whitespace(s1);
        trim_whitespace(s2);
        trim_whitespace(sanitized_user_id_numeric); // Also trim whitespace from the sanitized user ID

        char full_name[MAX_DISPLAY_NAME_LEN];
        if (s0[0] && s1[0] && s2[0]) {
            snprintf(full_name, sizeof(full_name), "%s %s (%s)", s0, s1, s2);
        } else if (s0[0] && s1[0]) {
            snprintf(full_name, sizeof(full_name), "%s %s", s0, s1);
        } else if (s0[0]) {
            strncpy(full_name, s0, sizeof(full_name) - 1);
            full_name[sizeof(full_name) - 1] = '\0';
        } else if (s1[0]) {
            strncpy(full_name, s1, sizeof(full_name) - 1);
            full_name[sizeof(full_name) - 1] = '\0';
        } else if (s2[0]) {
            strncpy(full_name, s2, sizeof(full_name) - 1);
            full_name[sizeof(full_name) - 1] = '\0';
        }
        else {
            strncpy(full_name, "Unnamed", sizeof(full_name) - 1);
            full_name[sizeof(full_name) - 1] = '\0';
        }

        // Pass the new, sanitized_user_id_numeric buffer
        add_csv_user_to_registered_users_table(sanitized_user_id_numeric, full_name);
    }
    fclose(fp);
    LOG_INFO("Finished populating registered users from CSV. Total directory entries: %d.", num_directory_entries);
}

void load_directory_from_xml(const char *filepath) {
    LOG_WARN("load_directory_from_xml is deprecated for populating registered_users and should not be called for SIP server's user database. This function is retained for compatibility but its effect on registered_users is now ignored.");
}
