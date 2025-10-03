#ifndef PASSIVE_SAFETY_H
#define PASSIVE_SAFETY_H

#include "../common.h"

// Passive safety functions - self-healing without reporting
// Designed for static phonebook environment (no dynamic user changes)

// Week 1: Essential cleanup and self-correction
void passive_cleanup_stale_call_sessions(void);
void validate_and_correct_config(void);
void enable_graceful_degradation_if_needed(void);
void cleanup_orphaned_phonebook_files(void);

// Week 2: File protection and thread recovery
void safe_phonebook_file_operation(const char *source_path, const char *dest_path);
void passive_thread_recovery_check(void);

// Background safety thread
void *passive_safety_thread(void *arg);

// Thread health tracking (minimal)
extern time_t g_fetcher_last_heartbeat;
extern time_t g_updater_last_heartbeat;
extern time_t g_bulk_tester_last_heartbeat;
extern pthread_t g_passive_safety_tid;

#endif // PASSIVE_SAFETY_H