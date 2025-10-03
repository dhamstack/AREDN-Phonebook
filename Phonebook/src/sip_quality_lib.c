/*
 * sip_quality_lib.c - Phone quality testing via SIP OPTIONS
 */

#include "sip_quality_lib.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <poll.h>

#define SIP_PORT 5060

static double now_monotonic(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// Calculate ICMP checksum
static uint16_t icmp_checksum(uint16_t *buf, int len) {
    uint32_t sum = 0;
    while (len > 1) {
        sum += *buf++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(uint8_t *)buf;
    }
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return ~sum;
}

// Send ICMP ping and measure RTT
static long ping_host(const char *ip, int timeout_ms) {
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        // Not running as root, can't send raw ICMP
        return -1;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = inet_addr(ip)
    };

    // Build ICMP echo request
    struct {
        struct icmp hdr;
        char data[32];
    } packet;

    memset(&packet, 0, sizeof(packet));
    packet.hdr.icmp_type = ICMP_ECHO;
    packet.hdr.icmp_code = 0;
    packet.hdr.icmp_id = getpid();
    packet.hdr.icmp_seq = 1;
    packet.hdr.icmp_cksum = icmp_checksum((uint16_t *)&packet, sizeof(packet));

    double t0 = now_monotonic();

    if (sendto(sock, &packet, sizeof(packet), 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    // Wait for reply
    struct pollfd pfd = {.fd = sock, .events = POLLIN};
    if (poll(&pfd, 1, timeout_ms) <= 0) {
        close(sock);
        return -1;
    }

    char buf[1024];
    struct sockaddr_in from;
    socklen_t len = sizeof(from);
    ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &len);

    double rtt_ms = (now_monotonic() - t0) * 1000.0;
    close(sock);

    if (n < 0) return -1;

    // Verify it's an echo reply from the right host
    struct ip *ip_hdr = (struct ip *)buf;
    struct icmp *icmp_hdr = (struct icmp *)(buf + (ip_hdr->ip_hl << 2));

    if (icmp_hdr->icmp_type == ICMP_ECHOREPLY &&
        icmp_hdr->icmp_id == getpid() &&
        from.sin_addr.s_addr == addr.sin_addr.s_addr) {
        return (long)rtt_ms;
    }

    return -1;
}

static int get_local_ip(const char *dest_ip, char *local_ip, int size) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in dest = {
        .sin_family = AF_INET,
        .sin_port = htons(SIP_PORT),
        .sin_addr.s_addr = inet_addr(dest_ip)
    };

    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        close(sock);
        return -1;
    }

    struct sockaddr_in local;
    socklen_t len = sizeof(local);
    if (getsockname(sock, (struct sockaddr *)&local, &len) < 0) {
        close(sock);
        return -1;
    }

    const char *ip = inet_ntoa(local.sin_addr);
    if (!ip) {
        close(sock);
        return -1;
    }

    strncpy(local_ip, ip, size - 1);
    local_ip[size - 1] = '\0';
    close(sock);
    return 0;
}

static int send_options(int sock, struct sockaddr_in *addr, const char *phone_number,
                       const char *phone_ip, const char *local_ip, const char *callid,
                       const char *branch) {
    char request[1024];
    int is_debug = getenv("SIP_DEBUG") && strcmp(getenv("SIP_DEBUG"), "1") == 0;

    snprintf(request, sizeof(request),
        "OPTIONS sip:%s@%s SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:5060;branch=%s\r\n"
        "From: <sip:monitor@%s>;tag=%ld\r\n"
        "To: <sip:%s@%s>\r\n"
        "Call-ID: %s\r\n"
        "CSeq: 1 OPTIONS\r\n"
        "Contact: <sip:monitor@%s:5060>\r\n"
        "Max-Forwards: 70\r\n"
        "User-Agent: AREDN-Phonebook-Monitor\r\n"
        "Content-Length: 0\r\n\r\n",
        phone_number, phone_ip,
        local_ip, branch,
        local_ip, (long)time(NULL),
        phone_number, phone_ip,
        callid, local_ip);

    if (is_debug) {
        fprintf(stderr, "[DEBUG] OPTIONS → %s@%s\n%s\n", phone_number, phone_ip, request);
    }

    if (sendto(sock, request, strlen(request), 0, (struct sockaddr *)addr, sizeof(*addr)) < 0) {
        if (is_debug) fprintf(stderr, "[DEBUG] sendto() failed: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

static int wait_for_response(int sock, const char *branch, int timeout_ms, int *resp_code) {
    int is_debug = getenv("SIP_DEBUG") && strcmp(getenv("SIP_DEBUG"), "1") == 0;
    char buf[4096];
    struct sockaddr_in from;
    socklen_t len = sizeof(from);
    double deadline = now_monotonic() + timeout_ms / 1000.0;

    while (1) {
        double remaining = deadline - now_monotonic();
        if (remaining <= 0) {
            if (is_debug) fprintf(stderr, "[DEBUG] Timeout\n");
            return 0;
        }

        struct pollfd pfd = {.fd = sock, .events = POLLIN};
        int rc = poll(&pfd, 1, (int)(remaining * 1000));

        if (rc <= 0) {
            if (is_debug) fprintf(stderr, "[DEBUG] Timeout\n");
            return 0;
        }

        ssize_t n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&from, &len);
        if (n <= 0) continue;

        buf[n] = '\0';
        if (is_debug) fprintf(stderr, "[DEBUG] Response:\n%s\n", buf);

        if (!strstr(buf, branch)) {
            if (is_debug) fprintf(stderr, "[DEBUG] Branch mismatch, ignoring\n");
            continue;
        }

        char *space = strchr(buf, ' ');
        if (space) {
            *resp_code = atoi(space + 1);
            if (*resp_code >= 200) {
                if (is_debug) fprintf(stderr, "[DEBUG] Final: %d\n", *resp_code);
                return 1;
            }
            if (is_debug) fprintf(stderr, "[DEBUG] Provisional: %d\n", *resp_code);
        }
    }
}

static int test_phone_internal(int ext_sock, const char *phone_number, const char *phone_ip,
                               const char *server_ip, voip_probe_result_t *result,
                               const voip_probe_config_t *config) {
    voip_probe_config_t defaults = {.timeout_ms = 5000};
    if (!config) config = &defaults;

    memset(result, 0, sizeof(*result));
    result->status = VOIP_PROBE_SIP_ERROR;
    strcpy(result->status_reason, "Not started");

    int is_debug = getenv("SIP_DEBUG") && strcmp(getenv("SIP_DEBUG"), "1") == 0;
    int sock = ext_sock >= 0 ? ext_sock : socket(AF_INET, SOCK_DGRAM, 0);
    int sock_created = (ext_sock < 0);

    if (sock < 0) {
        strcpy(result->status_reason, "Socket creation failed");
        return -1;
    }

    char local_ip[64];
    const char *env_ip = getenv("SIP_LOCAL_IP");

    if (server_ip && *server_ip) {
        strncpy(local_ip, server_ip, sizeof(local_ip) - 1);
        if (is_debug) fprintf(stderr, "[DEBUG] Using server IP: %s\n", local_ip);
    } else if (env_ip && *env_ip) {
        strncpy(local_ip, env_ip, sizeof(local_ip) - 1);
        if (is_debug) fprintf(stderr, "[DEBUG] Using SIP_LOCAL_IP: %s\n", local_ip);
    } else if (get_local_ip(phone_ip, local_ip, sizeof(local_ip)) < 0) {
        strcpy(result->status_reason, "Failed to get local IP");
        if (sock_created) close(sock);
        return -1;
    }
    local_ip[sizeof(local_ip) - 1] = '\0';

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(SIP_PORT),
        .sin_addr.s_addr = inet_addr(phone_ip)
    };

    long rand_val = time(NULL) ^ (long)phone_number;
    char callid[64], branch[64];
    snprintf(callid, sizeof(callid), "%ld@%s", rand_val, local_ip);
    snprintf(branch, sizeof(branch), "z9hG4bK%ld", rand_val + 1);

    if (is_debug) {
        fprintf(stderr, "[DEBUG] Testing %s @ %s (local: %s)\n", phone_number, phone_ip, local_ip);
    }

    // First, ping the host for baseline network RTT
    result->icmp_rtt_ms = ping_host(phone_ip, 1000);
    if (is_debug && result->icmp_rtt_ms >= 0) {
        fprintf(stderr, "[DEBUG] ICMP ping: %ld ms\n", result->icmp_rtt_ms);
    }

    double t0 = now_monotonic();

    if (send_options(sock, &addr, phone_number, phone_ip, local_ip, callid, branch) < 0) {
        strcpy(result->status_reason, "Failed to send OPTIONS");
        if (sock_created) close(sock);
        return -1;
    }

    int resp_code = 0;
    int rc = wait_for_response(sock, branch, config->timeout_ms, &resp_code);

    double rtt_ms = (now_monotonic() - t0) * 1000.0;
    result->sip_rtt_ms = (long)rtt_ms;

    if (rc == 0) {
        result->status = VOIP_PROBE_SIP_TIMEOUT;
        snprintf(result->status_reason, sizeof(result->status_reason),
                 "No response within %dms", config->timeout_ms);
    } else if (resp_code / 100 == 2) {
        result->status = VOIP_PROBE_SUCCESS;
        snprintf(result->status_reason, sizeof(result->status_reason),
                 "Phone reachable (%d)", resp_code);
        if (is_debug) fprintf(stderr, "[DEBUG] ✓ %s: %.1f ms\n", phone_number, rtt_ms);
    } else if (resp_code == 486) {
        result->status = VOIP_PROBE_BUSY;
        snprintf(result->status_reason, sizeof(result->status_reason), "Busy (486)");
    } else {
        result->status = VOIP_PROBE_SIP_ERROR;
        snprintf(result->status_reason, sizeof(result->status_reason), "SIP error %d", resp_code);
    }

    if (sock_created) close(sock);
    return (result->status == VOIP_PROBE_SUCCESS) ? 0 : -1;
}

int test_phone_quality(const char *phone_number, const char *phone_ip,
                       voip_probe_result_t *result, const voip_probe_config_t *config) {
    return test_phone_internal(-1, phone_number, phone_ip, NULL, result, config);
}

int test_phone_quality_with_socket(int sock, const char *phone_number, const char *phone_ip,
                                   const char *server_ip, voip_probe_result_t *result,
                                   const voip_probe_config_t *config) {
    return test_phone_internal(sock, phone_number, phone_ip, server_ip, result, config);
}

voip_probe_config_t get_default_config(void) {
    return (voip_probe_config_t){.timeout_ms = 5000};
}

const char* voip_probe_status_str(voip_probe_status_t status) {
    switch (status) {
        case VOIP_PROBE_SUCCESS:     return "SUCCESS";
        case VOIP_PROBE_BUSY:        return "BUSY";
        case VOIP_PROBE_SIP_TIMEOUT: return "TIMEOUT";
        case VOIP_PROBE_SIP_ERROR:   return "ERROR";
        default:                     return "UNKNOWN";
    }
}
