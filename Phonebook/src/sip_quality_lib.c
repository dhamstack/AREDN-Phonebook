/*
 * sip_quality_lib.c - VoIP Quality Testing Library
 *
 * Implementation of phone quality testing via short RTP/RTCP probe calls.
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

#define SIP_PORT 5060
#define RTP_PAYLOAD_TYPE 0  // PCMU

// RTP Header (RFC 3550)
typedef struct {
    uint8_t vpxcc;      // version(2), padding(1), extension(1), csrc count(4)
    uint8_t mpt;        // marker(1), payload type(7)
    uint16_t seq;       // sequence number
    uint32_t ts;        // timestamp
    uint32_t ssrc;      // synchronization source
} rtp_header_t;

// RTCP SR Header (simplified)
typedef struct {
    uint8_t vpxcc;      // version(2), padding(1), count(5)
    uint8_t pt;         // packet type (200 for SR)
    uint16_t length;    // length in 32-bit words - 1
    uint32_t ssrc;      // sender SSRC
    uint32_t ntp_sec;   // NTP timestamp - seconds
    uint32_t ntp_frac;  // NTP timestamp - fraction
    uint32_t rtp_ts;    // RTP timestamp
    uint32_t pkt_count; // sender's packet count
    uint32_t byte_count;// sender's byte count
} rtcp_sr_t;

// RTCP RR Header (simplified receive report)
typedef struct {
    uint8_t vpxcc;
    uint8_t pt;         // packet type (201 for RR)
    uint16_t length;
    uint32_t ssrc;      // receiver SSRC
    // Report block follows
    uint32_t reporter_ssrc;
    uint8_t fraction_lost;
    uint8_t cum_lost[3]; // 24-bit cumulative lost
    uint32_t highest_seq;
    uint32_t jitter;
    uint32_t lsr;       // last SR timestamp
    uint32_t dlsr;      // delay since last SR
} rtcp_rr_t;

// Get current NTP timestamp
static void get_ntp_time(uint32_t *ntp_sec, uint32_t *ntp_frac) {
    struct timeval tv;
    gettimeofday(&tv, NULL);

    // NTP epoch is Jan 1, 1900; Unix epoch is Jan 1, 1970
    // Difference is 2208988800 seconds
    *ntp_sec = tv.tv_sec + 2208988800UL;
    *ntp_frac = (uint32_t)((double)tv.tv_usec * 4294.967296);
}

// Send RTCP Sender Report + SDES compound packet
static int send_rtcp_sr(int sockfd, struct sockaddr_in *addr, uint32_t ssrc,
                 uint32_t rtp_ts, uint32_t pkt_count, uint32_t byte_count,
                 uint32_t *lsr_out) {
    uint8_t packet[256];
    int offset = 0;
    uint32_t ntp_sec, ntp_frac;

    get_ntp_time(&ntp_sec, &ntp_frac);

    // Build SR
    rtcp_sr_t *sr = (rtcp_sr_t *)(packet + offset);
    memset(sr, 0, sizeof(rtcp_sr_t));
    sr->vpxcc = 0x80;  // version 2, no padding, no reception reports
    sr->pt = 200;      // SR
    sr->length = htons(6); // 6 32-bit words after header
    sr->ssrc = htonl(ssrc);
    sr->ntp_sec = htonl(ntp_sec);
    sr->ntp_frac = htonl(ntp_frac);
    sr->rtp_ts = htonl(rtp_ts);
    sr->pkt_count = htonl(pkt_count);
    sr->byte_count = htonl(byte_count);
    offset += sizeof(rtcp_sr_t);

    // Save LSR (middle 32 bits of NTP timestamp)
    *lsr_out = ((ntp_sec & 0xFFFF) << 16) | ((ntp_frac >> 16) & 0xFFFF);

    // Add SDES with CNAME
    uint8_t *sdes = packet + offset;
    sdes[0] = 0x81;  // V=2, P=0, SC=1
    sdes[1] = 202;   // PT=SDES
    sdes[2] = 0;     // length high byte
    sdes[3] = 6;     // length = 6 (includes SSRC + CNAME item)
    memcpy(sdes + 4, &sr->ssrc, 4); // SSRC
    sdes[8] = 1;     // CNAME
    sdes[9] = 15;    // length
    memcpy(sdes + 10, "test@10.0.0.1  ", 15); // CNAME string (padded)
    sdes[25] = 0;    // END
    offset += 26;

    return sendto(sockfd, packet, offset, 0, (struct sockaddr *)addr, sizeof(*addr));
}

// Parse RTCP RR and extract metrics - FIXED RTT calculation
static int parse_rtcp_rr(uint8_t *buf, int len, probe_result_t *result, uint32_t lsr,
                  struct timeval *sr_sent_time) {
    if (len < sizeof(rtcp_rr_t)) return -1;

    rtcp_rr_t *rr = (rtcp_rr_t *)buf;

    if (rr->pt != 201) return -1; // Not an RR

    uint32_t jitter_samples = ntohl(rr->jitter);
    uint8_t fraction_lost = rr->fraction_lost;
    uint32_t cum_packets_lost = (rr->cum_lost[0] << 16) |
                                (rr->cum_lost[1] << 8) |
                                rr->cum_lost[2];

    // FIXED: Convert jitter from samples to milliseconds (8kHz codec)
    result->jitter_ms = (double)jitter_samples / 8.0;

    // FIXED: Convert loss fraction from 8-bit fixed point to 0.0-1.0
    result->loss_fraction = fraction_lost / 256.0;

    result->packets_lost = cum_packets_lost;

    // Calculate RTT if LSR and DLSR are present
    uint32_t rr_lsr = ntohl(rr->lsr);
    uint32_t rr_dlsr = ntohl(rr->dlsr);

    if (rr_lsr != 0 && rr_lsr == lsr) {
        // Calculate RTT in NTP units (1/65536 seconds)
        uint32_t now_ntp_sec, now_ntp_frac;
        get_ntp_time(&now_ntp_sec, &now_ntp_frac);
        uint32_t now_ntp = ((now_ntp_sec & 0xFFFF) << 16) | ((now_ntp_frac >> 16) & 0xFFFF);

        // RTT = now - LSR - DLSR
        uint32_t rtt_ntp = now_ntp - rr_lsr - rr_dlsr;

        // FIXED: Convert NTP units to milliseconds correctly
        double rtt_seconds = rtt_ntp / 65536.0;
        result->media_rtt_ms = (long)(rtt_seconds * 1000.0);
    }

    return 0;
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

// Send SIP INVITE with auto-answer - NOW CAPTURES SIP RTT
static int send_invite(int sockfd, struct sockaddr_in *addr, const char *phone_number,
                const char *phone_ip, int rtp_port, char *call_id_out,
                char *from_tag_out, char *response, int response_size,
                int timeout_ms, probe_status_t *status, char *status_reason,
                long *sip_rtt_ms, const char *local_ip) {
    char request[2048];
    long rand_val = time(NULL);

    snprintf(call_id_out, 64, "%ld@%s", rand_val, local_ip);
    snprintf(from_tag_out, 32, "%ld", rand_val + 1);

    snprintf(request, sizeof(request),
        "INVITE sip:%s@%s SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:5060;branch=z9hG4bK%ld\r\n"
        "From: <sip:test@%s>;tag=%s\r\n"
        "To: <sip:%s@%s>\r\n"
        "Call-ID: %s\r\n"
        "CSeq: 1 INVITE\r\n"
        "Contact: <sip:test@%s:5060>\r\n"
        "Max-Forwards: 70\r\n"
        "Call-Info: answer-after=0\r\n"
        "Alert-Info: info=alert-autoanswer\r\n"
        "Content-Type: application/sdp\r\n"
        "Content-Length: %d\r\n"
        "\r\n"
        "v=0\r\n"
        "o=test %ld 1 IN IP4 %s\r\n"
        "s=Test Call\r\n"
        "c=IN IP4 %s\r\n"
        "t=0 0\r\n"
        "m=audio %d RTP/AVP 0\r\n"
        "a=rtpmap:0 PCMU/8000\r\n"
        "a=ptime:40\r\n"
        "a=sendrecv\r\n",
        phone_number, phone_ip, local_ip, rand_val, local_ip, from_tag_out, phone_number, phone_ip,
        call_id_out, local_ip,
        145 + (rtp_port > 9999 ? 5 : rtp_port > 999 ? 4 : 3) + strlen(local_ip) * 3,
        rand_val, local_ip, local_ip, rtp_port);

    // Capture time BEFORE sending INVITE
    struct timeval invite_sent_time;
    gettimeofday(&invite_sent_time, NULL);

    if (sendto(sockfd, request, strlen(request), 0,
               (struct sockaddr *)addr, sizeof(*addr)) < 0) {
        return -1;
    }

    // Wait for responses with configured timeout (no 30-second extension)
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    struct timeval tv = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int got_180 = 0;

    while (1) {
        ssize_t len = recvfrom(sockfd, response, response_size - 1, 0,
                              (struct sockaddr *)&from, &fromlen);
        if (len > 0) {
            response[len] = '\0';

            // Check for final responses
            if (strstr(response, "200 OK") != NULL) {
                // Capture time when 200 OK received
                struct timeval ok_recv_time;
                gettimeofday(&ok_recv_time, NULL);

                // Calculate SIP RTT in milliseconds
                *sip_rtt_ms = (ok_recv_time.tv_sec - invite_sent_time.tv_sec) * 1000 +
                             (ok_recv_time.tv_usec - invite_sent_time.tv_usec) / 1000;

                *status = PROBE_SUCCESS;
                snprintf(status_reason, 128, "Call answered successfully");
                return 0;  // Success
            }
            if (strstr(response, "486 Busy") != NULL) {
                *status = PROBE_BUSY;
                snprintf(status_reason, 128, "Phone busy (486)");
                return -1;
            }
            if (strstr(response, "603 Decline") != NULL ||
                strstr(response, "404 Not Found") != NULL ||
                strstr(response, "487 Request Terminated") != NULL) {
                *status = PROBE_SIP_ERROR;
                snprintf(status_reason, 128, "SIP error response");
                return -1;
            }
            if (strstr(response, "180 Ringing") != NULL) {
                got_180 = 1;
            }
            // 100 Trying or 180 Ringing - keep waiting
        } else {
            // Timeout or error
            if (got_180) {
                *status = PROBE_NO_ANSWER;
                snprintf(status_reason, 128, "Phone rang but no answer within timeout");
            } else {
                *status = PROBE_SIP_TIMEOUT;
                snprintf(status_reason, 128, "No SIP response within %dms", timeout_ms);
            }
            return -1;
        }
    }
    return -1;
}

// Send ACK
static void send_ack(int sockfd, struct sockaddr_in *addr, const char *phone_number,
              const char *phone_ip, const char *call_id, const char *from_tag,
              const char *to_tag, const char *local_ip) {
    char request[1024];
    snprintf(request, sizeof(request),
        "ACK sip:%s@%s SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:5060;branch=z9hG4bK%ld\r\n"
        "From: <sip:test@%s>;tag=%s\r\n"
        "To: <sip:%s@%s>;tag=%s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: 1 ACK\r\n"
        "Max-Forwards: 70\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        phone_number, phone_ip, local_ip, (long)time(NULL),
        local_ip, from_tag, phone_number, phone_ip, to_tag, call_id);

    sendto(sockfd, request, strlen(request), 0,
           (struct sockaddr *)addr, sizeof(*addr));
}

// Send BYE
static void send_bye(int sockfd, struct sockaddr_in *addr, const char *phone_number,
              const char *phone_ip, const char *call_id, const char *from_tag,
              const char *to_tag, const char *local_ip) {
    char request[1024];
    snprintf(request, sizeof(request),
        "BYE sip:%s@%s SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:5060;branch=z9hG4bK%ld\r\n"
        "From: <sip:test@%s>;tag=%s\r\n"
        "To: <sip:%s@%s>;tag=%s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: 2 BYE\r\n"
        "Max-Forwards: 70\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        phone_number, phone_ip, local_ip, (long)time(NULL),
        local_ip, from_tag, phone_number, phone_ip, to_tag, call_id);

    sendto(sockfd, request, strlen(request), 0,
           (struct sockaddr *)addr, sizeof(*addr));
}

// Extract To tag from SIP response
static int extract_to_tag(const char *response, char *to_tag, int max_len) {
    const char *to_line = strstr(response, "To:");
    if (!to_line) to_line = strstr(response, "to:");
    if (!to_line) return -1;

    const char *tag_start = strstr(to_line, "tag=");
    if (!tag_start) return -1;
    tag_start += 4;

    const char *tag_end = strpbrk(tag_start, ";\r\n >");
    int len = tag_end ? (tag_end - tag_start) : strlen(tag_start);
    if (len >= max_len) len = max_len - 1;

    strncpy(to_tag, tag_start, len);
    to_tag[len] = '\0';
    return 0;
}

// Parse SDP to extract phone's RTP port, RTCP port, and IP
static int parse_sdp(const char *sdp, int *rtp_port, int *rtcp_port, char *ip, int ip_size) {
    // Find m=audio line
    const char *m_line = strstr(sdp, "m=audio ");
    if (!m_line) return -1;

    if (sscanf(m_line, "m=audio %d", rtp_port) != 1) {
        return -1;
    }

    // Default RTCP port is RTP + 1
    *rtcp_port = *rtp_port + 1;

    // Look for explicit a=rtcp: line
    const char *rtcp_line = strstr(sdp, "a=rtcp:");
    if (rtcp_line) {
        sscanf(rtcp_line, "a=rtcp:%d", rtcp_port);
    }

    // Find c=IN IP4 line for connection address
    const char *c_line = strstr(sdp, "c=IN IP4 ");
    if (c_line) {
        c_line += 9; // Skip "c=IN IP4 "
        const char *end = strpbrk(c_line, " \r\n");
        int len = end ? (end - c_line) : strlen(c_line);
        if (len >= ip_size) len = ip_size - 1;
        strncpy(ip, c_line, len);
        ip[len] = '\0';
    }

    return 0;
}

// RTP statistics tracking structure
typedef struct {
    int initialized;
    uint16_t first_seq;
    uint16_t highest_seq;
    uint32_t packets_received;
    uint32_t prev_timestamp;
    double prev_transit;
    double jitter;
    struct timeval prev_arrival;
} rtp_stats_t;

// Process received RTP packet and update statistics (RFC 3550)
static void process_rtp_packet(uint8_t *packet, ssize_t len, rtp_stats_t *stats) {
    if (len < sizeof(rtp_header_t)) return;

    rtp_header_t *rtp = (rtp_header_t *)packet;
    uint16_t seq = ntohs(rtp->seq);
    uint32_t timestamp = ntohl(rtp->ts);
    struct timeval arrival_time;
    gettimeofday(&arrival_time, NULL);

    // Convert arrival time to milliseconds
    double arrival_ms = arrival_time.tv_sec * 1000.0 + arrival_time.tv_usec / 1000.0;

    if (!stats->initialized) {
        // First packet
        stats->first_seq = seq;
        stats->highest_seq = seq;
        stats->packets_received = 1;
        stats->prev_timestamp = timestamp;
        stats->prev_transit = arrival_ms - (timestamp / 8.0);  // Convert to ms
        stats->jitter = 0;
        stats->prev_arrival = arrival_time;
        stats->initialized = 1;
    } else {
        // Update highest sequence number (handle wrap-around)
        int16_t diff = (int16_t)(seq - stats->highest_seq);
        if (diff > 0) {
            stats->highest_seq = seq;
        }

        stats->packets_received++;

        // Calculate jitter (RFC 3550 A.8)
        double transit = arrival_ms - (timestamp / 8.0);  // Convert RTP timestamp to ms
        double d = transit - stats->prev_transit;
        if (d < 0) d = -d;
        stats->jitter = stats->jitter + (d - stats->jitter) / 16.0;

        stats->prev_transit = transit;
        stats->prev_timestamp = timestamp;
        stats->prev_arrival = arrival_time;
    }
}

// Drain incoming RTP packets from socket (non-blocking)
static void drain_rtp_packets(int rtp_sock, rtp_stats_t *stats) {
    uint8_t rtp_buf[2048];
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);

    while (1) {
        ssize_t len = recvfrom(rtp_sock, rtp_buf, sizeof(rtp_buf), MSG_DONTWAIT,
                              (struct sockaddr *)&from, &fromlen);
        if (len <= 0) {
            break;  // No more packets or error
        }
        process_rtp_packet(rtp_buf, len, stats);
    }
}

// Public API implementation
int test_phone_quality(const char *phone_number, const char *phone_ip,
                       probe_result_t *result, const probe_config_t *config) {
    // Use defaults if config not provided
    probe_config_t default_config = get_default_config();
    if (!config) config = &default_config;

    // Initialize result
    memset(result, 0, sizeof(*result));
    result->status = PROBE_SIP_ERROR;
    snprintf(result->status_reason, sizeof(result->status_reason), "Not started");

    int sip_sock, rtp_sock, rtcp_sock;
    struct sockaddr_in sip_addr, rtp_addr, rtcp_addr;
    char sip_response[4096];
    char call_id[64], from_tag[32], to_tag[32];
    int rtp_port = 10000 + (rand() % 1000) * 2;
    int rtcp_port = rtp_port + 1;
    uint32_t ssrc = 0x12345678;
    uint32_t lsr = 0;
    struct timeval sr_time;

    // Create sockets
    sip_sock = socket(AF_INET, SOCK_DGRAM, 0);
    rtp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    rtcp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sip_sock < 0 || rtp_sock < 0 || rtcp_sock < 0) {
        if (sip_sock >= 0) close(sip_sock);
        if (rtp_sock >= 0) close(rtp_sock);
        if (rtcp_sock >= 0) close(rtcp_sock);
        snprintf(result->status_reason, sizeof(result->status_reason), "Socket creation failed");
        return -1;
    }

    // Bind RTP and RTCP sockets to the ports we advertise in SDP
    struct sockaddr_in local_rtp, local_rtcp;
    memset(&local_rtp, 0, sizeof(local_rtp));
    local_rtp.sin_family = AF_INET;
    local_rtp.sin_addr.s_addr = INADDR_ANY;
    local_rtp.sin_port = htons(rtp_port);

    memset(&local_rtcp, 0, sizeof(local_rtcp));
    local_rtcp.sin_family = AF_INET;
    local_rtcp.sin_addr.s_addr = INADDR_ANY;
    local_rtcp.sin_port = htons(rtcp_port);

    if (bind(rtp_sock, (struct sockaddr *)&local_rtp, sizeof(local_rtp)) < 0) {
        close(sip_sock);
        close(rtp_sock);
        close(rtcp_sock);
        snprintf(result->status_reason, sizeof(result->status_reason), "Failed to bind RTP socket");
        return -1;
    }
    if (bind(rtcp_sock, (struct sockaddr *)&local_rtcp, sizeof(local_rtcp)) < 0) {
        close(sip_sock);
        close(rtp_sock);
        close(rtcp_sock);
        snprintf(result->status_reason, sizeof(result->status_reason), "Failed to bind RTCP socket");
        return -1;
    }

    // Get local IP address that can reach the phone
    char local_ip[64];
    const char *env_ip = getenv("SIP_LOCAL_IP");
    if (env_ip && strlen(env_ip) > 0) {
        // Use environment variable if set
        strncpy(local_ip, env_ip, sizeof(local_ip) - 1);
        local_ip[sizeof(local_ip) - 1] = '\0';
    } else if (get_local_ip(phone_ip, local_ip, sizeof(local_ip)) < 0) {
        result->status = PROBE_SIP_ERROR;
        snprintf(result->status_reason, sizeof(result->status_reason), "Failed to get local IP");
        goto cleanup;
    }

    // Setup SIP address
    memset(&sip_addr, 0, sizeof(sip_addr));
    sip_addr.sin_family = AF_INET;
    sip_addr.sin_port = htons(SIP_PORT);
    sip_addr.sin_addr.s_addr = inet_addr(phone_ip);

    // Send INVITE with configured timeout and capture SIP RTT
    long sip_rtt_ms = 0;
    int invite_result = send_invite(sip_sock, &sip_addr, phone_number, phone_ip, rtp_port,
                                    call_id, from_tag, sip_response, sizeof(sip_response),
                                    config->invite_timeout_ms, &result->status, result->status_reason,
                                    &sip_rtt_ms, local_ip);
    if (invite_result < 0) {
        goto cleanup;
    }

    // Extract To tag
    if (extract_to_tag(sip_response, to_tag, sizeof(to_tag)) < 0) {
        result->status = PROBE_SIP_ERROR;
        snprintf(result->status_reason, sizeof(result->status_reason), "No To tag in 200 OK");
        goto cleanup;
    }

    // Parse SDP from 200 OK to get phone's RTP/RTCP ports and IP
    int phone_rtp_port = 0;
    int phone_rtcp_port = 0;
    char phone_media_ip[64] = {0};
    strncpy(phone_media_ip, phone_ip, sizeof(phone_media_ip) - 1); // Default to phone IP

    if (parse_sdp(sip_response, &phone_rtp_port, &phone_rtcp_port, phone_media_ip, sizeof(phone_media_ip)) != 0) {
        phone_rtp_port = rtp_port; // Fallback
        phone_rtcp_port = rtp_port + 1;
    }

    // Send ACK
    send_ack(sip_sock, &sip_addr, phone_number, phone_ip, call_id, from_tag, to_tag, local_ip);

    // Setup RTP destination using parsed SDP
    memset(&rtp_addr, 0, sizeof(rtp_addr));
    rtp_addr.sin_family = AF_INET;
    rtp_addr.sin_port = htons(phone_rtp_port);
    rtp_addr.sin_addr.s_addr = inet_addr(phone_media_ip);

    memset(&rtcp_addr, 0, sizeof(rtcp_addr));
    rtcp_addr.sin_family = AF_INET;
    rtcp_addr.sin_port = htons(phone_rtcp_port);
    rtcp_addr.sin_addr.s_addr = inet_addr(phone_media_ip);

    // Make RTP socket non-blocking to receive packets from phone
    int flags = fcntl(rtp_sock, F_GETFL, 0);
    fcntl(rtp_sock, F_SETFL, flags | O_NONBLOCK);

    // Initialize RTP statistics
    rtp_stats_t rtp_stats;
    memset(&rtp_stats, 0, sizeof(rtp_stats));

    // Send RTCP SR+SDES compound immediately after ACK (keep for compatibility)
    gettimeofday(&sr_time, NULL);
    send_rtcp_sr(rtcp_sock, &rtcp_addr, ssrc, 0, 0, 0, &lsr);

    // Send RTP packets for configured duration
    uint16_t seq = 0;
    uint32_t timestamp = 0;
    int packets_to_send = config->burst_duration_ms / config->rtp_ptime_ms;
    int sr_at_1s = 1000 / config->rtp_ptime_ms;  // Send SR again at ~1s

    for (int i = 0; i < packets_to_send; i++) {
        rtp_header_t rtp;
        uint8_t payload[160]; // 160 bytes for 40ms PCMU

        memset(&rtp, 0, sizeof(rtp));
        rtp.vpxcc = 0x80; // version 2
        rtp.mpt = RTP_PAYLOAD_TYPE;
        rtp.seq = htons(seq++);
        rtp.ts = htonl(timestamp);
        rtp.ssrc = htonl(ssrc);

        memset(payload, 0xFF, sizeof(payload)); // Silence (PCMU)

        // Send RTP header + payload
        uint8_t packet[sizeof(rtp) + sizeof(payload)];
        memcpy(packet, &rtp, sizeof(rtp));
        memcpy(packet + sizeof(rtp), payload, sizeof(payload));

        sendto(rtp_sock, packet, sizeof(packet), 0,
               (struct sockaddr *)&rtp_addr, sizeof(rtp_addr));

        result->packets_sent++;
        timestamp += 320; // 40ms at 8000 Hz

        // Drain any incoming RTP packets from phone
        drain_rtp_packets(rtp_sock, &rtp_stats);

        // Send another SR at ~1s to encourage RR (keep for compatibility)
        if (i == sr_at_1s) {
            send_rtcp_sr(rtcp_sock, &rtcp_addr, ssrc, timestamp,
                        result->packets_sent, result->packets_sent * 160, &lsr);
        }

        usleep(config->rtp_ptime_ms * 1000);
    }

    // Send final RTCP SR (keep for compatibility)
    send_rtcp_sr(rtcp_sock, &rtcp_addr, ssrc, timestamp,
                 result->packets_sent, result->packets_sent * 160, &lsr);

    // Drain any remaining RTP packets
    drain_rtp_packets(rtp_sock, &rtp_stats);

    // Wait a bit more for any late RTP packets
    usleep(config->rtcp_wait_ms * 1000);
    drain_rtp_packets(rtp_sock, &rtp_stats);

    // Calculate LOCAL RTP-based metrics
    if (rtp_stats.initialized && rtp_stats.packets_received >= 5) {
        // SUCCESS: We received enough RTP packets
        result->status = PROBE_SUCCESS;

        // Store SIP RTT in media_rtt_ms field
        result->media_rtt_ms = sip_rtt_ms;

        // Store locally calculated jitter
        result->jitter_ms = rtp_stats.jitter;

        // Calculate packet loss
        uint32_t expected = (uint32_t)(rtp_stats.highest_seq - rtp_stats.first_seq + 1);
        uint32_t lost = expected - rtp_stats.packets_received;
        result->packets_lost = lost;
        result->loss_fraction = (double)lost / (double)expected;

        snprintf(result->status_reason, sizeof(result->status_reason),
                 "Probe successful with local RTP metrics (%u packets received)",
                 rtp_stats.packets_received);
    } else {
        // FAILURE: Not enough RTP packets received
        result->status = PROBE_NO_RR;
        snprintf(result->status_reason, sizeof(result->status_reason),
                 "No/insufficient RTP received from phone (%u packets, need 5)",
                 rtp_stats.packets_received);
    }

    // Send BYE
    send_bye(sip_sock, &sip_addr, phone_number, phone_ip, call_id, from_tag, to_tag, local_ip);

cleanup:
    close(sip_sock);
    close(rtp_sock);
    close(rtcp_sock);
    return (result->status == PROBE_SUCCESS) ? 0 : -1;
}

// Get default configuration
probe_config_t get_default_config(void) {
    probe_config_t config;
    config.burst_duration_ms = 1200;
    config.rtp_ptime_ms = 40;
    config.rtcp_wait_ms = 2000;
    config.invite_timeout_ms = 5000;
    return config;
}

// Get status name as string
const char* probe_status_str(probe_status_t status) {
    switch (status) {
        case PROBE_SUCCESS:     return "SUCCESS";
        case PROBE_BUSY:        return "BUSY";
        case PROBE_NO_RR:       return "NO_RR";
        case PROBE_SIP_TIMEOUT: return "SIP_TIMEOUT";
        case PROBE_SIP_ERROR:   return "SIP_ERROR";
        case PROBE_NO_ANSWER:   return "NO_ANSWER";
        default:                return "UNKNOWN";
    }
}
