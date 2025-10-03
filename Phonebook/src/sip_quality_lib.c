/*
 * sip_quality_lib.c - VoIP Quality Testing Library
 *
 * Implementation of phone quality testing via SIP OPTIONS probe.
 */

#include "sip_quality_lib.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

#define SIP_PORT 5060

// Get monotonic timestamp in seconds (for RTT calculation)
static double now_monotonic(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

// Get local IP address that can reach a destination
static int get_local_ip(const char *dest_ip, char *local_ip, int local_ip_size) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(5060);
    dest_addr.sin_addr.s_addr = inet_addr(dest_ip);

    // Connect (doesn't send packets, just sets up routing)
    if (connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
        close(sock);
        return -1;
    }

    // Get local address
    struct sockaddr_in local_addr;
    socklen_t addr_len = sizeof(local_addr);
    if (getsockname(sock, (struct sockaddr *)&local_addr, &addr_len) < 0) {
        close(sock);
        return -1;
    }

    // Convert IP to string before closing socket
    const char *ip_str = inet_ntoa(local_addr.sin_addr);
    if (!ip_str) {
        close(sock);
        return -1;
    }

    strncpy(local_ip, ip_str, local_ip_size - 1);
    local_ip[local_ip_size - 1] = '\0';

    close(sock);
    return 0;
}

// Send SIP OPTIONS request
static int send_options(int sockfd, struct sockaddr_in *addr, const char *phone_number,
                       const char *phone_ip, const char *local_ip, const char *callid,
                       const char *branch, int cseq) {
    char request[1024];
    const char *debug = getenv("SIP_DEBUG");
    int is_debug = (debug && strcmp(debug, "1") == 0);

    snprintf(request, sizeof(request),
        "OPTIONS sip:%s@%s SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:5060;branch=%s\r\n"
        "From: <sip:monitor@%s>;tag=%ld\r\n"
        "To: <sip:%s@%s>\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %d OPTIONS\r\n"
        "Contact: <sip:monitor@%s:5060>\r\n"
        "Max-Forwards: 70\r\n"
        "User-Agent: AREDN-Phonebook-Monitor\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        phone_number, phone_ip,
        local_ip, branch,
        local_ip, (long)time(NULL),
        phone_number, phone_ip,
        callid,
        cseq,
        local_ip);

    if (is_debug) {
        fprintf(stderr, "[DEBUG] Sending OPTIONS to %s@%s\n", phone_number, phone_ip);
        fprintf(stderr, "[DEBUG] OPTIONS request:\n%s\n", request);
    }

    ssize_t sent = sendto(sockfd, request, strlen(request), 0,
                         (struct sockaddr *)addr, sizeof(*addr));
    if (sent < 0) {
        if (is_debug) fprintf(stderr, "[DEBUG] sendto() failed: %s\n", strerror(errno));
        return -1;
    }

    if (is_debug) {
        fprintf(stderr, "[DEBUG] OPTIONS sent (%zd bytes)\n", sent);
    }

    return 0;
}

// Wait for final SIP response matching branch
static int wait_for_final_response(int sockfd, const char *branch, int timeout_ms,
                                   int *resp_code, char *response_buf, int buf_size) {
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    struct timeval tv = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    const char *debug = getenv("SIP_DEBUG");
    int is_debug = (debug && strcmp(debug, "1") == 0);

    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (1) {
        ssize_t len = recvfrom(sockfd, response_buf, buf_size - 1, 0,
                              (struct sockaddr *)&from, &fromlen);
        if (len <= 0) {
            // Timeout or error
            if (is_debug) {
                fprintf(stderr, "[DEBUG] recvfrom() timeout/error: %s\n", strerror(errno));
            }
            return 0;  // timeout
        }

        response_buf[len] = '\0';

        if (is_debug) {
            fprintf(stderr, "[DEBUG] Received SIP response (%zd bytes):\n%s\n", len, response_buf);
        }

        // Check if this response matches our branch
        if (strstr(response_buf, branch) == NULL) {
            if (is_debug) fprintf(stderr, "[DEBUG] Response doesn't match branch, ignoring\n");
            continue;
        }

        // Parse status code from first line (e.g., "SIP/2.0 200 OK")
        char *status_line = response_buf;
        char *space1 = strchr(status_line, ' ');
        if (space1) {
            *resp_code = atoi(space1 + 1);

            // Check if this is a final response (>=200)
            if (*resp_code >= 200) {
                if (is_debug) {
                    fprintf(stderr, "[DEBUG] Final response: %d\n", *resp_code);
                }
                return 1;  // got final response
            } else {
                if (is_debug) {
                    fprintf(stderr, "[DEBUG] Provisional response: %d, waiting for final\n", *resp_code);
                }
                // Continue waiting for final response
            }
        }
    }

    return 0;  // timeout
}

// Internal core implementation using OPTIONS probe
static int test_phone_quality_internal(int external_sip_sock, const char *phone_number,
                                       const char *phone_ip, const char *server_ip,
                                       voip_probe_result_t *result, const voip_probe_config_t *config) {
    // Use defaults if config not provided
    voip_probe_config_t default_config = get_default_config();
    if (!config) config = &default_config;

    // Initialize result
    memset(result, 0, sizeof(*result));
    result->status = VOIP_PROBE_SIP_ERROR;
    snprintf(result->status_reason, sizeof(result->status_reason), "Not started");

    const char *debug = getenv("SIP_DEBUG");
    int is_debug = (debug && strcmp(debug, "1") == 0);

    int sip_sock;
    int sip_sock_created = 0;
    struct sockaddr_in sip_addr;
    char response[4096];
    char callid[64], branch[64];
    int cseq = 1;
    int resp_code = 0;

    // Use external SIP socket if provided, otherwise create new one
    if (external_sip_sock >= 0) {
        sip_sock = external_sip_sock;
        sip_sock_created = 0;
    } else {
        sip_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sip_sock < 0) {
            snprintf(result->status_reason, sizeof(result->status_reason), "SIP socket creation failed");
            return -1;
        }
        sip_sock_created = 1;
    }

    // Get local IP address that can reach the phone
    char local_ip[64];
    const char *env_ip = getenv("SIP_LOCAL_IP");

    if (server_ip && strlen(server_ip) > 0) {
        // Use provided server IP (from integrated mode)
        strncpy(local_ip, server_ip, sizeof(local_ip) - 1);
        local_ip[sizeof(local_ip) - 1] = '\0';
        if (is_debug) fprintf(stderr, "[DEBUG] Using provided server IP: %s\n", local_ip);
    } else if (env_ip && strlen(env_ip) > 0) {
        // Use environment variable if set
        strncpy(local_ip, env_ip, sizeof(local_ip) - 1);
        local_ip[sizeof(local_ip) - 1] = '\0';
        if (is_debug) fprintf(stderr, "[DEBUG] Using SIP_LOCAL_IP: %s\n", local_ip);
    } else if (get_local_ip(phone_ip, local_ip, sizeof(local_ip)) < 0) {
        result->status = VOIP_PROBE_SIP_ERROR;
        snprintf(result->status_reason, sizeof(result->status_reason), "Failed to get local IP");
        if (is_debug) fprintf(stderr, "[DEBUG] Failed to get local IP for phone %s\n", phone_ip);
        goto cleanup;
    } else {
        if (is_debug) fprintf(stderr, "[DEBUG] Auto-detected local IP: %s\n", local_ip);
    }

    // Setup SIP address
    memset(&sip_addr, 0, sizeof(sip_addr));
    sip_addr.sin_family = AF_INET;
    sip_addr.sin_port = htons(SIP_PORT);
    sip_addr.sin_addr.s_addr = inet_addr(phone_ip);

    // Generate Call-ID and branch
    long rand_val = time(NULL) ^ (long)phone_number;
    snprintf(callid, sizeof(callid), "%ld@%s", rand_val, local_ip);
    snprintf(branch, sizeof(branch), "z9hG4bK%ld", rand_val + 1);

    if (is_debug) {
        fprintf(stderr, "[DEBUG] Testing phone: %s @ %s (local: %s)\n",
                        phone_number, phone_ip, local_ip);
    }

    // Timing: send OPTIONS and measure RTT
    double t0 = now_monotonic();

    if (send_options(sip_sock, &sip_addr, phone_number, phone_ip, local_ip,
                    callid, branch, cseq) < 0) {
        result->status = VOIP_PROBE_SIP_ERROR;
        snprintf(result->status_reason, sizeof(result->status_reason), "Failed to send OPTIONS");
        goto cleanup;
    }

    int rc = wait_for_final_response(sip_sock, branch, config->invite_timeout_ms,
                                     &resp_code, response, sizeof(response));

    double t1 = now_monotonic();
    double sip_rtt_ms = (t1 - t0) * 1000.0;

    if (rc == 0) {
        // Timeout
        result->status = VOIP_PROBE_SIP_TIMEOUT;
        snprintf(result->status_reason, sizeof(result->status_reason),
                 "No SIP response within %dms", config->invite_timeout_ms);
        if (is_debug) {
            fprintf(stderr, "[DEBUG] ✗ %s: Timeout\n", phone_number);
        }
    } else if (resp_code / 100 == 2) {
        // 2xx success
        result->status = VOIP_PROBE_SUCCESS;
        result->media_rtt_ms = (long)sip_rtt_ms;
        snprintf(result->status_reason, sizeof(result->status_reason),
                 "Phone reachable (OPTIONS %d)", resp_code);
        if (is_debug) {
            fprintf(stderr, "[DEBUG] ✓ %s: RTT=%.1f ms (response: %d)\n",
                    phone_number, sip_rtt_ms, resp_code);
        }
    } else if (resp_code == 486) {
        // Busy
        result->status = VOIP_PROBE_BUSY;
        result->media_rtt_ms = (long)sip_rtt_ms;
        snprintf(result->status_reason, sizeof(result->status_reason),
                 "Phone busy (486)");
        if (is_debug) {
            fprintf(stderr, "[DEBUG] ✗ %s: Busy (RTT=%.1f ms)\n", phone_number, sip_rtt_ms);
        }
    } else {
        // Other error (404, 5xx, etc.)
        result->status = VOIP_PROBE_SIP_ERROR;
        result->media_rtt_ms = (long)sip_rtt_ms;
        snprintf(result->status_reason, sizeof(result->status_reason),
                 "SIP error %d", resp_code);
        if (is_debug) {
            fprintf(stderr, "[DEBUG] ✗ %s: Error %d (RTT=%.1f ms)\n",
                    phone_number, resp_code, sip_rtt_ms);
        }
    }

cleanup:
    if (sip_sock_created) close(sip_sock);
    return (result->status == VOIP_PROBE_SUCCESS) ? 0 : -1;
}

// Public API: Standalone mode (creates own socket)
int test_phone_quality(const char *phone_number, const char *phone_ip,
                       voip_probe_result_t *result, const voip_probe_config_t *config) {
    return test_phone_quality_internal(-1, phone_number, phone_ip, NULL, result, config);
}

// Public API: Integrated mode (uses existing socket and server IP)
int test_phone_quality_with_socket(int sip_sock, const char *phone_number,
                                   const char *phone_ip, const char *server_ip,
                                   voip_probe_result_t *result, const voip_probe_config_t *config) {
    return test_phone_quality_internal(sip_sock, phone_number, phone_ip, server_ip, result, config);
}

// Get default configuration
voip_probe_config_t get_default_config(void) {
    voip_probe_config_t config;
    config.burst_duration_ms = 0;        // Not used for OPTIONS
    config.rtp_ptime_ms = 0;             // Not used for OPTIONS
    config.rtcp_wait_ms = 0;             // Not used for OPTIONS
    config.invite_timeout_ms = 5000;     // 5 second timeout for OPTIONS
    return config;
}

// Get status name as string
const char* voip_probe_status_str(voip_probe_status_t status) {
    switch (status) {
        case VOIP_PROBE_SUCCESS:     return "SUCCESS";
        case VOIP_PROBE_BUSY:        return "BUSY";
        case VOIP_PROBE_NO_RR:       return "NO_RR";
        case VOIP_PROBE_SIP_TIMEOUT: return "SIP_TIMEOUT";
        case VOIP_PROBE_SIP_ERROR:   return "SIP_ERROR";
        case VOIP_PROBE_NO_ANSWER:   return "NO_ANSWER";
        default:                     return "UNKNOWN";
    }
}
