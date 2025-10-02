/*
 * sip_quality_test.c - SIP and RTP quality testing utility
 * Tests both SIP signaling and RTP media quality with RTCP metrics
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdint.h>

#define SIP_PORT 5060
#define RESPONSE_TIMEOUT 5
#define RTP_PAYLOAD_TYPE 0  // PCMU
#define PTIME 40            // ms per packet
#define TEST_DURATION_MS 1200
#define RTCP_WAIT_MS 1000

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

// Test statistics
typedef struct {
    long sip_rtt_ms;
    int rtp_packets_sent;
    int rtcp_rr_received;
    uint32_t jitter_samples;
    uint8_t fraction_lost;
    uint32_t cum_packets_lost;
    long media_rtt_ms;
} test_stats_t;

// Get current NTP timestamp
void get_ntp_time(uint32_t *ntp_sec, uint32_t *ntp_frac) {
    struct timeval tv;
    gettimeofday(&tv, NULL);

    // NTP epoch is Jan 1, 1900; Unix epoch is Jan 1, 1970
    // Difference is 2208988800 seconds
    *ntp_sec = tv.tv_sec + 2208988800UL;
    *ntp_frac = (uint32_t)((double)tv.tv_usec * 4294.967296);
}

// Send RTCP Sender Report + SDES compound packet
int send_rtcp_sr(int sockfd, struct sockaddr_in *addr, uint32_t ssrc,
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

// Parse RTCP RR and extract metrics
int parse_rtcp_rr(uint8_t *buf, int len, test_stats_t *stats, uint32_t lsr,
                  struct timeval *sr_sent_time) {
    if (len < sizeof(rtcp_rr_t)) return -1;

    rtcp_rr_t *rr = (rtcp_rr_t *)buf;

    if (rr->pt != 201) return -1; // Not an RR

    stats->jitter_samples = ntohl(rr->jitter);
    stats->fraction_lost = rr->fraction_lost;
    stats->cum_packets_lost = (rr->cum_lost[0] << 16) |
                              (rr->cum_lost[1] << 8) |
                              rr->cum_lost[2];

    // Calculate RTT if LSR and DLSR are present
    uint32_t rr_lsr = ntohl(rr->lsr);
    uint32_t rr_dlsr = ntohl(rr->dlsr);

    if (rr_lsr != 0 && rr_lsr == lsr) {
        struct timeval now;
        gettimeofday(&now, NULL);

        // Calculate RTT in NTP units (1/65536 seconds)
        uint32_t now_ntp_sec, now_ntp_frac;
        get_ntp_time(&now_ntp_sec, &now_ntp_frac);
        uint32_t now_ntp = ((now_ntp_sec & 0xFFFF) << 16) | ((now_ntp_frac >> 16) & 0xFFFF);

        // RTT = now - LSR - DLSR
        uint32_t rtt_ntp = now_ntp - rr_lsr - rr_dlsr;
        stats->media_rtt_ms = (long)((rtt_ntp / 65.536)); // Convert to ms
    }

    stats->rtcp_rr_received = 1;
    return 0;
}

// Test SIP OPTIONS (returns RTT in ms, -1 on failure)
long test_sip_options(const char *phone_number, const char *phone_ip, int verbose) {
    int sockfd;
    struct sockaddr_in server_addr, recv_addr;
    char request[1024];
    char response[4096];
    socklen_t addr_len = sizeof(recv_addr);
    ssize_t recv_len;
    struct timeval tv, start_time, end_time;
    long rtt_ms = -1;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) return -1;

    tv.tv_sec = RESPONSE_TIMEOUT;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SIP_PORT);
    server_addr.sin_addr.s_addr = inet_addr(phone_ip);

    snprintf(request, sizeof(request),
        "OPTIONS sip:%s@%s SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.1:5060;branch=z9hG4bK%ld\r\n"
        "From: <sip:test@10.0.0.1>;tag=%ld\r\n"
        "To: <sip:%s@%s>\r\n"
        "Call-ID: %ld@10.0.0.1\r\n"
        "CSeq: 1 OPTIONS\r\n"
        "Contact: <sip:test@10.0.0.1:5060>\r\n"
        "Max-Forwards: 70\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        phone_number, phone_ip,
        (long)time(NULL), (long)time(NULL) + 1,
        phone_number, phone_ip, (long)time(NULL) + 2);

    gettimeofday(&start_time, NULL);
    if (sendto(sockfd, request, strlen(request), 0,
               (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        return -1;
    }

    recv_len = recvfrom(sockfd, response, sizeof(response) - 1, 0,
                        (struct sockaddr *)&recv_addr, &addr_len);
    gettimeofday(&end_time, NULL);

    if (recv_len > 0) {
        response[recv_len] = '\0';
        if (strstr(response, "200 OK")) {
            rtt_ms = (end_time.tv_sec - start_time.tv_sec) * 1000 +
                     (end_time.tv_usec - start_time.tv_usec) / 1000;
        }
    }

    close(sockfd);
    return rtt_ms;
}

// Send SIP INVITE with auto-answer
int send_invite(int sockfd, struct sockaddr_in *addr, const char *phone_number,
                const char *phone_ip, int rtp_port, char *call_id_out,
                char *from_tag_out, char *response, int response_size, int verbose) {
    char request[2048];
    long rand_val = time(NULL);

    snprintf(call_id_out, 64, "%ld@10.0.0.1", rand_val);
    snprintf(from_tag_out, 32, "%ld", rand_val + 1);

    snprintf(request, sizeof(request),
        "INVITE sip:%s@%s SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.1:5060;branch=z9hG4bK%ld\r\n"
        "From: <sip:test@10.0.0.1>;tag=%s\r\n"
        "To: <sip:%s@%s>\r\n"
        "Call-ID: %s\r\n"
        "CSeq: 1 INVITE\r\n"
        "Contact: <sip:test@10.0.0.1:5060>\r\n"
        "Max-Forwards: 70\r\n"
        "Call-Info: answer-after=0\r\n"
        "Alert-Info: info=alert-autoanswer\r\n"
        "Content-Type: application/sdp\r\n"
        "Content-Length: %d\r\n"
        "\r\n"
        "v=0\r\n"
        "o=test %ld 1 IN IP4 10.0.0.1\r\n"
        "s=Test Call\r\n"
        "c=IN IP4 10.0.0.1\r\n"
        "t=0 0\r\n"
        "m=audio %d RTP/AVP 0\r\n"
        "a=rtpmap:0 PCMU/8000\r\n"
        "a=ptime:40\r\n"
        "a=sendrecv\r\n",
        phone_number, phone_ip, rand_val, from_tag_out, phone_number, phone_ip,
        call_id_out, 145 + (rtp_port > 9999 ? 5 : rtp_port > 999 ? 4 : 3),
        rand_val, rtp_port);

    if (sendto(sockfd, request, strlen(request), 0,
               (struct sockaddr *)addr, sizeof(*addr)) < 0) {
        return -1;
    }

    // Wait for responses (may get 100 Trying, 180 Ringing, then 200 OK)
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    int got_180 = 0;

    // Extend timeout if we get 180 Ringing (phone needs time to be answered)
    struct timeval extended_tv = {30, 0};  // 30 seconds for manual answer

    while (1) {
        ssize_t len = recvfrom(sockfd, response, response_size - 1, 0,
                              (struct sockaddr *)&from, &fromlen);
        if (len > 0) {
            response[len] = '\0';

            // Check for final responses
            if (strstr(response, "200 OK") != NULL) {
                return 0;  // Success
            }
            if (strstr(response, "486 Busy") != NULL ||
                strstr(response, "603 Decline") != NULL ||
                strstr(response, "404 Not Found") != NULL ||
                strstr(response, "487 Request Terminated") != NULL) {
                return -1;  // Phone rejected
            }
            if (strstr(response, "180 Ringing") != NULL) {
                // Phone is ringing, extend timeout to allow manual answer
                if (!got_180) {
                    got_180 = 1;
                    if (verbose) printf("  Phone ringing (auto-answer not supported), waiting up to 30s...\n");
                    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &extended_tv, sizeof(extended_tv));
                }
            }
            // 100 Trying or 180 Ringing - keep waiting
        } else {
            // Timeout or error
            return -1;
        }
    }
    return -1;
}

// Send ACK
void send_ack(int sockfd, struct sockaddr_in *addr, const char *phone_number,
              const char *phone_ip, const char *call_id, const char *from_tag,
              const char *to_tag) {
    char request[1024];
    snprintf(request, sizeof(request),
        "ACK sip:%s@%s SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.1:5060;branch=z9hG4bK%ld\r\n"
        "From: <sip:test@10.0.0.1>;tag=%s\r\n"
        "To: <sip:%s@%s>;tag=%s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: 1 ACK\r\n"
        "Max-Forwards: 70\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        phone_number, phone_ip, (long)time(NULL),
        from_tag, phone_number, phone_ip, to_tag, call_id);

    sendto(sockfd, request, strlen(request), 0,
           (struct sockaddr *)addr, sizeof(*addr));
}

// Send BYE
void send_bye(int sockfd, struct sockaddr_in *addr, const char *phone_number,
              const char *phone_ip, const char *call_id, const char *from_tag,
              const char *to_tag) {
    char request[1024];
    snprintf(request, sizeof(request),
        "BYE sip:%s@%s SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.1:5060;branch=z9hG4bK%ld\r\n"
        "From: <sip:test@10.0.0.1>;tag=%s\r\n"
        "To: <sip:%s@%s>;tag=%s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: 2 BYE\r\n"
        "Max-Forwards: 70\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        phone_number, phone_ip, (long)time(NULL),
        from_tag, phone_number, phone_ip, to_tag, call_id);

    sendto(sockfd, request, strlen(request), 0,
           (struct sockaddr *)addr, sizeof(*addr));
}

// Extract To tag from SIP response
int extract_to_tag(const char *response, char *to_tag, int max_len) {
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
int parse_sdp(const char *sdp, int *rtp_port, int *rtcp_port, char *ip, int ip_size) {
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

// Run full media quality test
int test_media_quality(const char *phone_number, const char *phone_ip,
                       test_stats_t *stats, int verbose) {
    int sip_sock, rtp_sock, rtcp_sock;
    struct sockaddr_in sip_addr, rtp_addr, rtcp_addr;
    char sip_response[4096];
    char call_id[64], from_tag[32], to_tag[32];
    int rtp_port = 10000 + (rand() % 1000) * 2;
    int rtcp_port = rtp_port + 1;
    uint32_t ssrc = 0x12345678;
    uint32_t lsr = 0;
    struct timeval sr_time;

    memset(stats, 0, sizeof(*stats));

    // Create sockets
    sip_sock = socket(AF_INET, SOCK_DGRAM, 0);
    rtp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    rtcp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sip_sock < 0 || rtp_sock < 0 || rtcp_sock < 0) {
        if (sip_sock >= 0) close(sip_sock);
        if (rtp_sock >= 0) close(rtp_sock);
        if (rtcp_sock >= 0) close(rtcp_sock);
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
        if (verbose) printf("  Failed to bind RTP socket to port %d\n", rtp_port);
        close(sip_sock);
        close(rtp_sock);
        close(rtcp_sock);
        return -1;
    }
    if (bind(rtcp_sock, (struct sockaddr *)&local_rtcp, sizeof(local_rtcp)) < 0) {
        if (verbose) printf("  Failed to bind RTCP socket to port %d\n", rtcp_port);
        close(sip_sock);
        close(rtp_sock);
        close(rtcp_sock);
        return -1;
    }

    // Setup SIP address
    memset(&sip_addr, 0, sizeof(sip_addr));
    sip_addr.sin_family = AF_INET;
    sip_addr.sin_port = htons(SIP_PORT);
    sip_addr.sin_addr.s_addr = inet_addr(phone_ip);

    // Set timeout
    struct timeval tv = {RESPONSE_TIMEOUT, 0};
    setsockopt(sip_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Send INVITE
    if (verbose) printf("  Sending INVITE...\n");
    int invite_result = send_invite(sip_sock, &sip_addr, phone_number, phone_ip, rtp_port,
                                    call_id, from_tag, sip_response, sizeof(sip_response), verbose);
    if (invite_result < 0) {
        if (verbose) {
            printf("  INVITE failed\n");
            if (strlen(sip_response) > 0) {
                printf("  Response received:\n");
                char *line = strtok(sip_response, "\r\n");
                while (line && strlen(line) > 0) {
                    printf("    %s\n", line);
                    line = strtok(NULL, "\r\n");
                }
            } else {
                printf("  No response (timeout)\n");
            }
        }
        goto cleanup;
    }

    // Extract To tag
    if (extract_to_tag(sip_response, to_tag, sizeof(to_tag)) < 0) {
        if (verbose) {
            printf("  No To tag in response\n");
            printf("  Response was:\n%s\n", sip_response);
        }
        goto cleanup;
    }

    // Parse SDP from 200 OK to get phone's RTP/RTCP ports and IP
    int phone_rtp_port = 0;
    int phone_rtcp_port = 0;
    char phone_media_ip[64] = {0};
    strncpy(phone_media_ip, phone_ip, sizeof(phone_media_ip) - 1); // Default to phone IP

    if (parse_sdp(sip_response, &phone_rtp_port, &phone_rtcp_port, phone_media_ip, sizeof(phone_media_ip)) == 0) {
        if (verbose) printf("  Phone RTP: %s:%d, RTCP: %d\n", phone_media_ip, phone_rtp_port, phone_rtcp_port);
    } else {
        if (verbose) printf("  Warning: Could not parse SDP, using defaults\n");
        phone_rtp_port = rtp_port; // Fallback
        phone_rtcp_port = rtp_port + 1;
    }

    // Send ACK
    if (verbose) printf("  Sending ACK, starting RTP...\n");
    send_ack(sip_sock, &sip_addr, phone_number, phone_ip, call_id, from_tag, to_tag);

    // Setup RTP destination using parsed SDP
    memset(&rtp_addr, 0, sizeof(rtp_addr));
    rtp_addr.sin_family = AF_INET;
    rtp_addr.sin_port = htons(phone_rtp_port);
    rtp_addr.sin_addr.s_addr = inet_addr(phone_media_ip);

    memset(&rtcp_addr, 0, sizeof(rtcp_addr));
    rtcp_addr.sin_family = AF_INET;
    rtcp_addr.sin_port = htons(phone_rtcp_port);
    rtcp_addr.sin_addr.s_addr = inet_addr(phone_media_ip);

    // Send RTCP SR+SDES compound immediately after ACK
    gettimeofday(&sr_time, NULL);
    send_rtcp_sr(rtcp_sock, &rtcp_addr, ssrc, 0, 0, 0, &lsr);
    if (verbose) printf("  Sent initial RTCP SR+SDES\n");

    // Send RTP packets for TEST_DURATION_MS
    uint16_t seq = 0;
    uint32_t timestamp = 0;
    int packets_to_send = TEST_DURATION_MS / PTIME;
    int sr_at_1s = 1000 / PTIME;  // Send SR again at ~1s

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

        stats->rtp_packets_sent++;
        timestamp += 320; // 40ms at 8000 Hz

        // Send another SR at ~1s to encourage RR
        if (i == sr_at_1s) {
            send_rtcp_sr(rtcp_sock, &rtcp_addr, ssrc, timestamp,
                        stats->rtp_packets_sent, stats->rtp_packets_sent * 160, &lsr);
            if (verbose) printf("  Sent mid-stream RTCP SR\n");
        }

        usleep(PTIME * 1000); // Wait 40ms
    }

    // Send final RTCP SR
    send_rtcp_sr(rtcp_sock, &rtcp_addr, ssrc, timestamp,
                 stats->rtp_packets_sent, stats->rtp_packets_sent * 160, &lsr);
    if (verbose) printf("  Sent final RTCP SR, waiting for RR...\n");

    // Wait for RTCP RR - allow 2 seconds
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(rtcp_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t rtcp_buf[512];
    struct sockaddr_in rtcp_from;
    socklen_t rtcp_from_len = sizeof(rtcp_from);
    ssize_t rtcp_len = recvfrom(rtcp_sock, rtcp_buf, sizeof(rtcp_buf), 0,
                                (struct sockaddr *)&rtcp_from, &rtcp_from_len);
    if (rtcp_len > 0) {
        if (verbose) {
            printf("  Received %ld bytes RTCP from %s:%d\n",
                   (long)rtcp_len,
                   inet_ntoa(rtcp_from.sin_addr),
                   ntohs(rtcp_from.sin_port));
            printf("  RTCP packet type: %u\n", rtcp_buf[1]);
        }
        parse_rtcp_rr(rtcp_buf, rtcp_len, stats, lsr, &sr_time);
        if (verbose && stats->rtcp_rr_received) {
            printf("  Got RTCP RR: jitter=%u samples, loss=%u pkts\n",
                   stats->jitter_samples, stats->cum_packets_lost);
        } else if (verbose) {
            printf("  RTCP packet was not an RR or parse failed\n");
        }
    } else {
        if (verbose) printf("  No RTCP received (timeout or error)\n");
    }

    // Send BYE
    if (verbose) printf("  Sending BYE...\n");
    send_bye(sip_sock, &sip_addr, phone_number, phone_ip, call_id, from_tag, to_tag);

cleanup:
    close(sip_sock);
    close(rtp_sock);
    close(rtcp_sock);
    return (stats->rtp_packets_sent > 0) ? 0 : -1;
}

// Run complete test suite
void run_quality_test(const char *phone_number, const char *phone_ip, int count, int media_test) {
    printf("Testing %s (%s) - %d iterations\n", phone_number, phone_ip, count);

    if (media_test) {
        // Run media quality test
        test_stats_t stats;
        if (test_media_quality(phone_number, phone_ip, &stats, 1) == 0) {
            printf("\nMedia Test Results:\n");
            printf("  RTP packets sent: %d\n", stats.rtp_packets_sent);
            if (stats.rtcp_rr_received) {
                double jitter_ms = (double)stats.jitter_samples / 8.0; // Convert to ms
                printf("  RTCP RR received: YES\n");
                printf("  Jitter: %.1f ms\n", jitter_ms);
                printf("  Packet loss: %u packets (%.1f%%)\n",
                       stats.cum_packets_lost,
                       100.0 * stats.cum_packets_lost / stats.rtp_packets_sent);
                if (stats.media_rtt_ms > 0) {
                    printf("  Media RTT: %ld ms\n", stats.media_rtt_ms);
                }
            } else {
                printf("  RTCP RR received: NO\n");
            }
        } else {
            printf("\nMedia test FAILED\n");
        }
        return;
    }

    // Run SIP OPTIONS tests
    long min_rtt = -1, max_rtt = 0, total_rtt = 0;
    int success_count = 0;

    for (int i = 0; i < count; i++) {
        long rtt = test_sip_options(phone_number, phone_ip, 0);
        if (rtt >= 0) {
            printf("  [%d/%d] SIP RTT: %ld ms\n", i+1, count, rtt);
            success_count++;
            total_rtt += rtt;
            if (min_rtt < 0 || rtt < min_rtt) min_rtt = rtt;
            if (rtt > max_rtt) max_rtt = rtt;

            if (i < count - 1) usleep(100000); // 100ms between tests
        } else {
            printf("  [%d/%d] FAILED\n", i+1, count);
        }
    }

    if (success_count > 0) {
        printf("\nSIP Statistics:\n");
        printf("  Success: %d/%d (%.1f%%)\n", success_count, count,
               100.0 * success_count / count);
        printf("  RTT: min=%ld avg=%ld max=%ld ms\n",
               min_rtt, total_rtt / success_count, max_rtt);
    } else {
        printf("\nAll tests FAILED\n");
    }
}

int main(int argc, char *argv[]) {
    int count = 5; // Default 5 iterations
    int media_test = 0;

    if (argc < 2) {
        printf("Usage: %s [--media] <phone_number> [phone_ip] [count]\n", argv[0]);
        printf("   or: %s [--media] --test-all [count]\n", argv[0]);
        printf("\nOptions:\n");
        printf("  --media   Run full media quality test (INVITE + RTP + RTCP)\n");
        printf("\nTest phones with statistics (default: 5 SIP OPTIONS pings):\n");
        printf("  441530 (10.197.143.20) - HB9BLA-1 on VM-1 LAN\n");
        printf("  441533 (10.51.55.234)  - HB9BLA-4 on HAP-2 LAN\n");
        printf("  648730 (10.32.73.134)  - HB9TSI remote phone\n");
        printf("\nExamples:\n");
        printf("  %s 441530              # 5 OPTIONS pings to 441530\n", argv[0]);
        printf("  %s --test-all 10       # 10 OPTIONS pings to all phones\n", argv[0]);
        printf("  %s --media 441530      # Full media test to 441530\n", argv[0]);
        return 1;
    }

    int arg_idx = 1;
    if (strcmp(argv[arg_idx], "--media") == 0) {
        media_test = 1;
        arg_idx++;
        if (arg_idx >= argc) {
            printf("Error: Missing phone number after --media\n");
            return 1;
        }
    }

    if (strcmp(argv[arg_idx], "--test-all") == 0) {
        if (argc > arg_idx + 1) count = atoi(argv[arg_idx + 1]);
        run_quality_test("441530", "10.197.143.20", count, media_test);
        if (!media_test) printf("\n");
        run_quality_test("441533", "10.51.55.234", count, media_test);
        if (!media_test) printf("\n");
        run_quality_test("648730", "10.32.73.134", count, media_test);
    } else {
        const char *phone_number = argv[arg_idx];
        const char *phone_ip;

        if (argc > arg_idx + 1 && argv[arg_idx + 1][0] != '-') {
            phone_ip = argv[arg_idx + 1];
            if (argc > arg_idx + 2) count = atoi(argv[arg_idx + 2]);
        } else {
            if (strcmp(phone_number, "441530") == 0) {
                phone_ip = "10.197.143.20";
            } else if (strcmp(phone_number, "441533") == 0) {
                phone_ip = "10.51.55.234";
            } else if (strcmp(phone_number, "648730") == 0) {
                phone_ip = "10.32.73.134";
            } else {
                printf("Error: Unknown phone number. Please provide IP address.\n");
                return 1;
            }
        }

        run_quality_test(phone_number, phone_ip, count, media_test);
    }

    return 0;
}
