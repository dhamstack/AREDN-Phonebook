// uac_ping.h - SIP OPTIONS and PING testing with RTT/jitter measurement
#ifndef UAC_PING_H
#define UAC_PING_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

// Maximum number of ping samples to collect
#define MAX_PING_SAMPLES 20

// Ping statistics result structure
typedef struct {
    bool online;              // Phone responded to at least one request
    int packets_sent;         // Total packets sent
    int packets_received;     // Total packets received
    float packet_loss_pct;    // Packet loss percentage
    float min_rtt_ms;         // Minimum RTT in milliseconds
    float max_rtt_ms;         // Maximum RTT in milliseconds
    float avg_rtt_ms;         // Average RTT in milliseconds
    float jitter_ms;          // Jitter (variance in RTT) in milliseconds
    float samples[MAX_PING_SAMPLES]; // Individual RTT samples
} uac_ping_result_t;

/**
 * Send multiple SIP OPTIONS requests to a phone and measure RTT/jitter
 * @param phone_number Target phone number (e.g., "441530")
 * @param server_ip SIP server/proxy IP address
 * @param ping_count Number of OPTIONS requests to send
 * @return Ping statistics result
 */
uac_ping_result_t uac_options_ping_test(const char *phone_number,
                                         const char *server_ip,
                                         int ping_count);

/**
 * Send multiple SIP PING requests to a phone and measure RTT/jitter
 * @param phone_number Target phone number (e.g., "441530")
 * @param server_ip SIP server/proxy IP address
 * @param ping_count Number of PING requests to send
 * @return Ping statistics result
 */
uac_ping_result_t uac_ping_ping_test(const char *phone_number,
                                      const char *server_ip,
                                      int ping_count);

/**
 * Calculate ping statistics from RTT samples
 * @param samples Array of RTT samples in milliseconds
 * @param sample_count Number of samples
 * @param result Output statistics structure
 */
void uac_calculate_ping_stats(float *samples, int sample_count, uac_ping_result_t *result);

#endif // UAC_PING_H
