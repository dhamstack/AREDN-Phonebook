/*
 * udp_send.c - Simple SIP OPTIONS ping utility
 * Sends SIP OPTIONS request to check phone reachability
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

#define SIP_PORT 5060
#define RESPONSE_TIMEOUT 5  // seconds

void send_sip_options(const char *phone_number, const char *phone_ip) {
    int sockfd;
    struct sockaddr_in server_addr, recv_addr;
    char request[1024];
    char response[4096];
    socklen_t addr_len = sizeof(recv_addr);
    ssize_t recv_len;
    struct timeval tv, start_time, end_time;
    long rtt_ms;

    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return;
    }

    // Set receive timeout
    tv.tv_sec = RESPONSE_TIMEOUT;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SIP_PORT);
    server_addr.sin_addr.s_addr = inet_addr(phone_ip);

    // Build SIP OPTIONS request
    snprintf(request, sizeof(request),
        "OPTIONS sip:%s@%s SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.1:5060;branch=z9hG4bK%ld\r\n"
        "From: <sip:phonebook@10.0.0.1>;tag=%ld\r\n"
        "To: <sip:%s@%s>\r\n"
        "Call-ID: %ld@10.0.0.1\r\n"
        "CSeq: 1 OPTIONS\r\n"
        "Contact: <sip:phonebook@10.0.0.1:5060>\r\n"
        "Max-Forwards: 70\r\n"
        "User-Agent: AREDN-Phonebook/1.0\r\n"
        "Accept: application/sdp\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        phone_number, phone_ip,
        (long)time(NULL),
        (long)time(NULL) + 1,
        phone_number, phone_ip,
        (long)time(NULL) + 2);

    printf("Testing %s (%s)...\n", phone_number, phone_ip);

    // Send OPTIONS request
    gettimeofday(&start_time, NULL);
    if (sendto(sockfd, request, strlen(request), 0,
               (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("sendto failed");
        close(sockfd);
        return;
    }

    // Wait for response
    recv_len = recvfrom(sockfd, response, sizeof(response) - 1, 0,
                        (struct sockaddr *)&recv_addr, &addr_len);
    gettimeofday(&end_time, NULL);

    if (recv_len < 0) {
        printf("  X No response (timeout after %d seconds)\n", RESPONSE_TIMEOUT);
    } else {
        response[recv_len] = '\0';

        // Calculate RTT in milliseconds
        rtt_ms = (end_time.tv_sec - start_time.tv_sec) * 1000 +
                 (end_time.tv_usec - start_time.tv_usec) / 1000;

        // Extract status line
        char *status_line = strtok(response, "\r\n");
        if (status_line && strstr(status_line, "200 OK")) {
            printf("  OK - %s (RTT: %ld ms)\n", status_line, rtt_ms);
        } else if (status_line) {
            printf("  Response: %s (RTT: %ld ms)\n", status_line, rtt_ms);
        } else {
            printf("  Invalid response\n");
        }
    }

    close(sockfd);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <phone_number> [phone_ip]\n", argv[0]);
        printf("   or: %s --test-all\n", argv[0]);
        printf("\nTest all default phones:\n");
        printf("  441530 (10.197.143.20) - HB9BLA-1 on VM-1 LAN\n");
        printf("  441533 (10.51.55.234)  - HB9BLA-4 on HAP-2 LAN\n");
        printf("  648730 (10.32.73.134)  - HB9TSI remote phone\n");
        return 1;
    }

    if (strcmp(argv[1], "--test-all") == 0) {
        // Test all default phones
        send_sip_options("441530", "10.197.143.20");
        send_sip_options("441533", "10.51.55.234");
        send_sip_options("648730", "10.32.73.134");
    } else {
        const char *phone_number = argv[1];
        const char *phone_ip;

        if (argc >= 3) {
            phone_ip = argv[2];
        } else {
            // Default IP mappings
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

        send_sip_options(phone_number, phone_ip);
    }

    return 0;
}
