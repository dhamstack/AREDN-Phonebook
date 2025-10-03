/*
 * phone_quality_monitor.h - Background VoIP Quality Monitoring
 *
 * Periodically tests registered phones and stores quality metrics.
 * Integrated into the main SIP server to use its socket and IP.
 */

#ifndef PHONE_QUALITY_MONITOR_H
#define PHONE_QUALITY_MONITOR_H

#include "../sip_quality_lib.h"
#include <time.h>

#define MAX_QUALITY_RECORDS 100

// Quality test record for a phone
typedef struct {
    char phone_number[32];
    char phone_ip[64];
    time_t last_test_time;
    voip_probe_result_t last_result;
    int valid;  // 1 if record contains data, 0 if empty
} phone_quality_record_t;

// Configuration for quality monitoring
typedef struct {
    int enabled;
    int test_interval_sec;     // How often to test each phone
    int cycle_delay_sec;       // Delay between testing different phones
    voip_probe_config_t probe_config;
} quality_monitor_config_t;

// Context passed to monitor thread
typedef struct {
    int sip_socket;            // Main SIP server socket
    char server_ip[64];        // Server's IP address
    quality_monitor_config_t config;
} quality_monitor_context_t;

/**
 * Initialize quality monitor
 *
 * @param sip_sock Main SIP server socket
 * @param server_ip Server's IP address (for SIP headers)
 * @return 0 on success, -1 on error
 */
int quality_monitor_init(int sip_sock, const char *server_ip);

/**
 * Start quality monitor thread
 *
 * @return 0 on success, -1 on error
 */
int quality_monitor_start(void);

/**
 * Stop quality monitor thread
 */
void quality_monitor_stop(void);

/**
 * Get quality record for a phone
 *
 * @param phone_number Phone number to look up
 * @param record Output: quality record (if found)
 * @return 0 if found, -1 if not found
 */
int quality_monitor_get_record(const char *phone_number, phone_quality_record_t *record);

/**
 * Get all quality records
 *
 * @param records Output array
 * @param max_records Maximum records to return
 * @return Number of records returned
 */
int quality_monitor_get_all_records(phone_quality_record_t *records, int max_records);

/**
 * Quality monitor thread function (internal)
 */
void* quality_monitor_thread(void *arg);

/**
 * Handle SIP response routed from main server (called by main SIP receive loop)
 *
 * @param buffer SIP message buffer
 * @param len Length of message
 */
void quality_monitor_handle_response(const char *buffer, int len);

/**
 * Dequeue SIP response from response queue (internal, for sip_quality_lib.c)
 *
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @param timeout_ms Timeout in milliseconds
 * @return Number of bytes read, 0 on timeout, -1 on error
 */
int quality_monitor_dequeue_response(char *buffer, int buffer_size, int timeout_ms);

#endif /* PHONE_QUALITY_MONITOR_H */
