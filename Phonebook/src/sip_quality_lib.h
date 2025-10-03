/*
 * sip_quality_lib.h - VoIP Quality Testing Library
 *
 * Public API for testing phone quality via short RTP/RTCP probe calls.
 * Measures media RTT, jitter, and packet loss.
 */

#ifndef SIP_QUALITY_LIB_H
#define SIP_QUALITY_LIB_H

#include <stdint.h>

typedef enum {
    VOIP_PROBE_SUCCESS = 0,
    VOIP_PROBE_BUSY,
    VOIP_PROBE_SIP_TIMEOUT,
    VOIP_PROBE_SIP_ERROR
} voip_probe_status_t;

typedef struct {
    int timeout_ms;
} voip_probe_config_t;

typedef struct {
    voip_probe_status_t status;
    long sip_rtt_ms;
    long icmp_rtt_ms;  // ICMP ping RTT for comparison
    char status_reason[128];

    // Legacy fields (unused, kept for compatibility)
    double jitter_ms;
    double loss_fraction;
    uint32_t packets_lost;
    uint32_t packets_sent;
} voip_probe_result_t;

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
                       voip_probe_result_t *result,
                       const voip_probe_config_t *config);

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
                                   voip_probe_result_t *result,
                                   const voip_probe_config_t *config);

/**
 * Get default configuration
 */
voip_probe_config_t get_default_config(void);

/**
 * Get status name as string
 */
const char* voip_probe_status_str(voip_probe_status_t status);

#endif /* SIP_QUALITY_LIB_H */
