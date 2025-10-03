// uac_ping.c - SIP OPTIONS and PING testing with RTT/jitter measurement
#define MODULE_NAME "UAC_PING"

#include "uac_ping.h"
#include "../common.h"
#include "uac.h"
#include <math.h>
#include <sys/time.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

// Helper: Get current time in milliseconds
static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000.0) + (tv.tv_usec / 1000.0);
}

// Helper: Build SIP OPTIONS request
static int build_options_message(char *buffer, size_t buffer_size,
                                  const char *phone_number,
                                  const char *local_ip,
                                  int local_port,
                                  const char *call_id,
                                  const char *via_branch) {
    int written = snprintf(buffer, buffer_size,
        "OPTIONS sip:%s@localnode.local.mesh:5060 SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:%d;branch=%s\r\n"
        "From: <sip:999900@%s:%d>;tag=%ld\r\n"
        "To: <sip:%s@localnode.local.mesh:5060>\r\n"
        "Call-ID: %s\r\n"
        "CSeq: 1 OPTIONS\r\n"
        "Contact: <sip:999900@%s:%d>\r\n"
        "Max-Forwards: 70\r\n"
        "User-Agent: AREDN-Phonebook-UAC/1.0\r\n"
        "Accept: application/sdp\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        phone_number,
        local_ip, local_port, via_branch,
        local_ip, local_port, random(),
        phone_number,
        call_id,
        local_ip, local_port);

    return (written >= buffer_size) ? -1 : 0;
}

// Helper: Wait for OPTIONS response and measure RTT
static float wait_for_options_response(int sockfd, const char *call_id, int timeout_ms) {
    double start_time = get_time_ms();
    char response[2048];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    fd_set readfds;
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);

    int ret = select(sockfd + 1, &readfds, NULL, NULL, &tv);
    if (ret <= 0) {
        // Timeout or error
        return -1.0;
    }

    ssize_t n = recvfrom(sockfd, response, sizeof(response) - 1, 0,
                         (struct sockaddr *)&from_addr, &from_len);
    if (n < 0) {
        return -1.0;
    }

    response[n] = '\0';

    // Check if response matches our Call-ID
    if (strstr(response, call_id) == NULL) {
        // Not our response, ignore
        return -1.0;
    }

    // Check if it's a valid response (200 OK or any response)
    if (strstr(response, "SIP/2.0") == NULL) {
        return -1.0;
    }

    double end_time = get_time_ms();
    float rtt = (float)(end_time - start_time);

    LOG_DEBUG("OPTIONS response received in %.2f ms", rtt);
    return rtt;
}

// Calculate statistics from RTT samples
void uac_calculate_ping_stats(float *samples, int sample_count, uac_ping_result_t *result) {
    if (sample_count == 0 || !result) {
        return;
    }

    // Initialize
    result->min_rtt_ms = samples[0];
    result->max_rtt_ms = samples[0];
    float sum = 0.0;

    // Calculate min, max, sum
    for (int i = 0; i < sample_count; i++) {
        if (samples[i] < result->min_rtt_ms) result->min_rtt_ms = samples[i];
        if (samples[i] > result->max_rtt_ms) result->max_rtt_ms = samples[i];
        sum += samples[i];
    }

    // Calculate average
    result->avg_rtt_ms = sum / sample_count;

    // Calculate jitter (standard deviation of RTT)
    float variance_sum = 0.0;
    for (int i = 0; i < sample_count; i++) {
        float diff = samples[i] - result->avg_rtt_ms;
        variance_sum += diff * diff;
    }
    result->jitter_ms = sqrtf(variance_sum / sample_count);

    // Calculate packet loss
    result->packet_loss_pct = ((float)(result->packets_sent - result->packets_received) /
                               result->packets_sent) * 100.0f;
}

// Send multiple SIP OPTIONS requests and measure RTT/jitter
uac_ping_result_t uac_options_ping_test(const char *phone_number,
                                         const char *server_ip,
                                         int ping_count) {
    uac_ping_result_t result = {0};

    if (!phone_number || !server_ip || ping_count <= 0 || ping_count > MAX_PING_SAMPLES) {
        LOG_ERROR("Invalid parameters for OPTIONS ping test");
        return result;
    }

    LOG_INFO("Starting OPTIONS ping test to %s (%d pings)", phone_number, ping_count);

    // Get UAC socket (we'll reuse the existing UAC socket)
    int sockfd = uac_get_sockfd();
    if (sockfd < 0) {
        LOG_ERROR("UAC not initialized");
        return result;
    }

    // Get local IP (use server_ip for now, should get from UAC context)
    char local_ip[64];
    strncpy(local_ip, server_ip, sizeof(local_ip) - 1);
    local_ip[sizeof(local_ip) - 1] = '\0';
    int local_port = 5070; // UAC port

    result.packets_sent = ping_count;
    result.packets_received = 0;

    for (int i = 0; i < ping_count; i++) {
        // Generate unique Call-ID and Via branch for this ping
        char call_id[128];
        char via_branch[64];
        snprintf(call_id, sizeof(call_id), "ping-%ld-%d@%s", time(NULL), i, local_ip);
        snprintf(via_branch, sizeof(via_branch), "z9hG4bK%ld%d", random(), i);

        // Build OPTIONS message
        char options_msg[1024];
        if (build_options_message(options_msg, sizeof(options_msg),
                                  phone_number, local_ip, local_port,
                                  call_id, via_branch) < 0) {
            LOG_ERROR("Failed to build OPTIONS message");
            continue;
        }

        // Set up server address
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        inet_pton(AF_INET, server_ip, &server_addr.sin_addr);
        server_addr.sin_port = htons(5060);

        // Send OPTIONS request and start timer
        ssize_t sent = sendto(sockfd, options_msg, strlen(options_msg), 0,
                             (struct sockaddr*)&server_addr, sizeof(server_addr));
        if (sent < 0) {
            LOG_WARN("Failed to send OPTIONS ping %d: %s", i + 1, strerror(errno));
            continue;
        }

        LOG_DEBUG("Sent OPTIONS ping %d/%d to %s", i + 1, ping_count, phone_number);

        // Wait for response and measure RTT (1 second timeout)
        float rtt = wait_for_options_response(sockfd, call_id, 1000);

        if (rtt > 0) {
            result.samples[result.packets_received] = rtt;
            result.packets_received++;
            result.online = true;
            LOG_DEBUG("OPTIONS ping %d: RTT = %.2f ms", i + 1, rtt);
        } else {
            LOG_DEBUG("OPTIONS ping %d: No response", i + 1);
        }

        // Wait 200ms between pings to avoid flooding
        if (i < ping_count - 1) {
            usleep(200000); // 200ms
        }
    }

    // Calculate statistics if we got any responses
    if (result.packets_received > 0) {
        uac_calculate_ping_stats(result.samples, result.packets_received, &result);

        LOG_INFO("OPTIONS ping test complete: %s", phone_number);
        LOG_INFO("  Packets: %d sent, %d received (%.1f%% loss)",
                 result.packets_sent, result.packets_received, result.packet_loss_pct);
        LOG_INFO("  RTT: min=%.2f ms, avg=%.2f ms, max=%.2f ms, jitter=%.2f ms",
                 result.min_rtt_ms, result.avg_rtt_ms, result.max_rtt_ms, result.jitter_ms);
    } else {
        LOG_WARN("OPTIONS ping test failed: No responses from %s", phone_number);
    }

    return result;
}

// SIP PING test (placeholder - PING is not standard SIP, using OPTIONS instead)
uac_ping_result_t uac_ping_ping_test(const char *phone_number,
                                      const char *server_ip,
                                      int ping_count) {
    // SIP doesn't have a standard PING method
    // We'll use OPTIONS for both tests (they measure the same thing)
    LOG_DEBUG("Using OPTIONS for PING test (SIP has no standard PING method)");
    return uac_options_ping_test(phone_number, server_ip, ping_count);
}
