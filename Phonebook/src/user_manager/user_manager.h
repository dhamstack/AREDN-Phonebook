// user_manager.h
#ifndef USER_MANAGER_H
#define USER_MANAGER_H

#include "../common.h" // For RegisteredUser type and other common definitions

// Function prototypes for user management
RegisteredUser* find_registered_user(const char *user_id);
RegisteredUser* add_or_update_registered_user(const char *user_id, const char *display_name, int expires,
                                              const char *contact_uri, const char *ip_address, int port);
RegisteredUser* add_csv_user_to_registered_users_table(const char *user_id_numeric, const char *display_name);
void init_registered_users_table();
void populate_registered_users_from_csv(const char *filepath);
void load_directory_from_xml(const char *filepath); // Deprecated but retained prototype
int get_registered_user_count();

#endif // USER_MANAGER_H
