/*
 * sip_quality_lib.h - VoIP Quality Testing Library
 *
 * Public API for testing phone quality via short RTP/RTCP probe calls.
 * Measures media RTT, jitter, and packet loss.
 */

#ifndef SIP_QUALITY_LIB_H
#define SIP_QUALITY_LIB_H

#include <stdint.h>

// Probe status codes
typedef enum {
    PROBE_SUCCESS = 0,        // Call succeeded, metrics valid
    PROBE_BUSY,               // Phone returned 486 Busy
    PROBE_NO_RR,              // RTP sent but no RTCP RR received
    PROBE_SIP_TIMEOUT,        // No SIP response within timeout
    PROBE_SIP_ERROR,          // Other SIP error (404, 603, etc)
    PROBE_NO_ANSWER           // Phone rang but didn't answer
} probe_status_t;

// Configuration for quality probe
typedef struct {
    int burst_duration_ms;    // RTP burst duration (default: 1200)
    int rtp_ptime_ms;         // RTP packet interval (default: 40)
    int rtcp_wait_ms;         // Time to wait for RTCP RR (default: 2000)
    int invite_timeout_ms;    // SIP INVITE timeout (default: 5000)
} probe_config_t;

// Result from quality probe
typedef struct {
    probe_status_t status;

    // Metrics (only valid if status == PROBE_SUCCESS)
    long media_rtt_ms;        // Media round-trip time from RTCP LSR/DLSR
    double jitter_ms;         // Interarrival jitter in milliseconds
    double loss_fraction;     // Packet loss as fraction (0.0 = 0%, 1.0 = 100%)
    uint32_t packets_lost;    // Cumulative packets lost
    uint32_t packets_sent;    // Total packets sent during probe

    // Diagnostics
    char status_reason[128];  // Human-readable status/error description
} probe_result_t;

/**
 * Test phone quality via short probe call
 *
 * @param phone_number Phone number to test (e.g., "441530")
 * @param phone_ip     Phone IP address (e.g., "10.197.143.20")
 * @param result       Output: probe results
 * @param config       Configuration (use NULL for defaults)
 * @return 0 on success, -1 on error
 */
int test_phone_quality(const char *phone_number,
                       const char *phone_ip,
                       probe_result_t *result,
                       const probe_config_t *config);

/**
 * Test phone quality using an existing SIP socket (for server integration)
 *
 * @param sip_sock     Existing SIP socket to use (or -1 to create new)
 * @param phone_number Phone number to test (e.g., "441530")
 * @param phone_ip     Phone IP address (e.g., "10.197.143.20")
 * @param server_ip    Server's IP address to use in SIP headers
 * @param result       Output: probe results
 * @param config       Configuration (use NULL for defaults)
 * @return 0 on success, -1 on error
 */
int test_phone_quality_with_socket(int sip_sock,
                                   const char *phone_number,
                                   const char *phone_ip,
                                   const char *server_ip,
                                   probe_result_t *result,
                                   const probe_config_t *config);

/**
 * Get default configuration
 */
probe_config_t get_default_config(void);

/**
 * Get status name as string
 */
const char* probe_status_str(probe_status_t status);

#endif /* SIP_QUALITY_LIB_H */
