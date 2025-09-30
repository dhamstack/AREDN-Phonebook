#define MODULE_NAME "PROBE_ENGINE"

#include "probe_engine.h"
#include "../log_manager/log_manager.h"
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>

static int probe_socket = -1;
static mesh_monitor_config_t probe_config;
static bool engine_running = false;
static char local_node[64] = {0};

// Probe response tracking
#define MAX_PENDING_PROBES 100
typedef struct {
    uint32_t sequence;
    struct timeval sent_time;
    char dst_ip[16];
} pending_probe_t;

static pending_probe_t pending_probes[MAX_PENDING_PROBES];
static int pending_probe_count = 0;
static pthread_mutex_t pending_mutex = PTHREAD_MUTEX_INITIALIZER;

int probe_engine_init(mesh_monitor_config_t *config) {
    if (!config) return -1;

    LOG_INFO("Initializing probe engine");

    // Copy configuration
    memcpy(&probe_config, config, sizeof(mesh_monitor_config_t));

    // Get local node name
    if (gethostname(local_node, sizeof(local_node) - 1) != 0) {
        strcpy(local_node, "unknown");
    }

    // Create UDP socket for probes
    probe_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (probe_socket < 0) {
        LOG_ERROR("Failed to create probe socket: %s", strerror(errno));
        return -1;
    }

    // Set socket to non-blocking
    int flags = fcntl(probe_socket, F_GETFL, 0);
    fcntl(probe_socket, F_SETFL, flags | O_NONBLOCK);

    // Bind to probe port
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(probe_config.probe_port);

    if (bind(probe_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("Failed to bind probe socket to port %d: %s",
                  probe_config.probe_port, strerror(errno));
        close(probe_socket);
        probe_socket = -1;
        return -1;
    }

    // Set DSCP EF (Expedited Forwarding) if requested
    if (probe_config.dscp_ef) {
        int tos = 0xB8;  // DSCP EF (46 << 2)
        if (setsockopt(probe_socket, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) < 0) {
            LOG_WARN("Failed to set DSCP EF on probe socket");
        }
    }

    engine_running = true;
    LOG_INFO("Probe engine initialized (port=%d)", probe_config.probe_port);
    return 0;
}

void probe_engine_shutdown(void) {
    engine_running = false;

    if (probe_socket >= 0) {
        close(probe_socket);
        probe_socket = -1;
    }

    LOG_INFO("Probe engine shutdown");
}

int send_probes(const char *dst_ip, int count, int interval_ms) {
    if (!engine_running || !dst_ip || probe_socket < 0) {
        return -1;
    }

    struct sockaddr_in dst_addr;
    memset(&dst_addr, 0, sizeof(dst_addr));
    dst_addr.sin_family = AF_INET;
    dst_addr.sin_port = htons(probe_config.probe_port);

    if (inet_pton(AF_INET, dst_ip, &dst_addr.sin_addr) <= 0) {
        LOG_ERROR("Invalid destination IP: %s", dst_ip);
        return -1;
    }

    int sent = 0;
    for (int i = 0; i < count; i++) {
        probe_packet_t packet;
        memset(&packet, 0, sizeof(packet));

        struct timeval now;
        gettimeofday(&now, NULL);

        packet.sequence = htonl(i);
        packet.timestamp_sec = htonl(now.tv_sec);
        packet.timestamp_usec = htonl(now.tv_usec);
        strncpy(packet.src_node, local_node, sizeof(packet.src_node) - 1);

        ssize_t n = sendto(probe_socket, &packet, sizeof(packet), 0,
                           (struct sockaddr *)&dst_addr, sizeof(dst_addr));

        if (n < 0) {
            LOG_ERROR("Failed to send probe to %s: %s", dst_ip, strerror(errno));
            continue;
        }

        // Track pending probe
        pthread_mutex_lock(&pending_mutex);
        if (pending_probe_count < MAX_PENDING_PROBES) {
            pending_probes[pending_probe_count].sequence = i;
            pending_probes[pending_probe_count].sent_time = now;
            strncpy(pending_probes[pending_probe_count].dst_ip, dst_ip, 15);
            pending_probe_count++;
        }
        pthread_mutex_unlock(&pending_mutex);

        sent++;

        // Sleep between probes
        if (i < count - 1 && interval_ms > 0) {
            usleep(interval_ms * 1000);
        }
    }

    LOG_DEBUG("Sent %d probes to %s", sent, dst_ip);
    return sent;
}

void* probe_responder_thread(void *arg) {
    LOG_INFO("Probe responder thread started");

    char buffer[1024];
    struct sockaddr_in src_addr;
    socklen_t addr_len = sizeof(src_addr);

    while (engine_running) {
        ssize_t n = recvfrom(probe_socket, buffer, sizeof(buffer), 0,
                             (struct sockaddr *)&src_addr, &addr_len);

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(10000);  // 10ms sleep
                continue;
            }
            LOG_ERROR("recvfrom error: %s", strerror(errno));
            break;
        }

        if (n < sizeof(probe_packet_t)) {
            continue;  // Invalid packet size
        }

        // Echo the packet back
        ssize_t sent = sendto(probe_socket, buffer, n, 0,
                              (struct sockaddr *)&src_addr, addr_len);

        if (sent < 0) {
            LOG_ERROR("Failed to echo probe: %s", strerror(errno));
        }
    }

    LOG_INFO("Probe responder thread stopped");
    return NULL;
}

// Calculate RTT, jitter, and packet loss from probe responses (RFC3550)
int calculate_probe_metrics(const char *dst_ip, probe_result_t *result) {
    if (!dst_ip || !result) return -1;

    memset(result, 0, sizeof(probe_result_t));
    strncpy(result->dst_ip, dst_ip, sizeof(result->dst_ip) - 1);
    result->timestamp = time(NULL);

    // Collect responses from probe socket (with timeout)
    char buffer[1024];
    struct sockaddr_in src_addr;
    socklen_t addr_len = sizeof(src_addr);
    struct timeval timeout;
    fd_set readfds;

    float rtt_samples[MAX_PENDING_PROBES];
    int rtt_count = 0;
    int expected_responses = 0;

    pthread_mutex_lock(&pending_mutex);
    expected_responses = pending_probe_count;
    pthread_mutex_unlock(&pending_mutex);

    if (expected_responses == 0) {
        LOG_DEBUG("No pending probes to calculate metrics for");
        return -1;
    }

    // Wait for responses (100ms timeout per probe)
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;

    for (int attempts = 0; attempts < 50 && rtt_count < expected_responses; attempts++) {
        FD_ZERO(&readfds);
        FD_SET(probe_socket, &readfds);

        int ready = select(probe_socket + 1, &readfds, NULL, NULL, &timeout);
        if (ready <= 0) {
            continue;  // Timeout or error
        }

        ssize_t n = recvfrom(probe_socket, buffer, sizeof(buffer), 0,
                             (struct sockaddr *)&src_addr, &addr_len);

        if (n < sizeof(probe_packet_t)) {
            continue;  // Invalid packet
        }

        probe_packet_t *response = (probe_packet_t *)buffer;
        struct timeval recv_time;
        gettimeofday(&recv_time, NULL);

        uint32_t seq = ntohl(response->sequence);
        uint32_t sent_sec = ntohl(response->timestamp_sec);
        uint32_t sent_usec = ntohl(response->timestamp_usec);

        // Find matching pending probe
        pthread_mutex_lock(&pending_mutex);
        for (int i = 0; i < pending_probe_count; i++) {
            if (pending_probes[i].sequence == seq &&
                strcmp(pending_probes[i].dst_ip, dst_ip) == 0) {

                // Calculate RTT in milliseconds
                long sec_diff = recv_time.tv_sec - sent_sec;
                long usec_diff = recv_time.tv_usec - sent_usec;
                float rtt_ms = (sec_diff * 1000.0) + (usec_diff / 1000.0);

                if (rtt_ms >= 0 && rtt_ms < 10000.0) {  // Sanity check (< 10 seconds)
                    rtt_samples[rtt_count++] = rtt_ms;
                }
                break;
            }
        }
        pthread_mutex_unlock(&pending_mutex);
    }

    // Calculate packet loss percentage
    if (expected_responses > 0) {
        result->loss_pct = 100.0 * (1.0 - ((float)rtt_count / (float)expected_responses));
    } else {
        result->loss_pct = 100.0;
    }

    // Calculate average RTT
    if (rtt_count > 0) {
        float rtt_sum = 0.0;
        for (int i = 0; i < rtt_count; i++) {
            rtt_sum += rtt_samples[i];
        }
        result->rtt_ms_avg = rtt_sum / rtt_count;

        // Calculate jitter (RFC3550 Section 6.4.1 - interarrival jitter)
        // Simplified: standard deviation of RTT differences
        if (rtt_count > 1) {
            float jitter_sum = 0.0;
            for (int i = 1; i < rtt_count; i++) {
                float diff = rtt_samples[i] - rtt_samples[i-1];
                jitter_sum += (diff < 0 ? -diff : diff);  // Absolute difference
            }
            result->jitter_ms = jitter_sum / (rtt_count - 1);
        } else {
            result->jitter_ms = 0.0;
        }
    } else {
        result->rtt_ms_avg = 0.0;
        result->jitter_ms = 0.0;
        result->loss_pct = 100.0;
    }

    // Clear pending probes for this destination
    pthread_mutex_lock(&pending_mutex);
    int write_idx = 0;
    for (int read_idx = 0; read_idx < pending_probe_count; read_idx++) {
        if (strcmp(pending_probes[read_idx].dst_ip, dst_ip) != 0) {
            if (write_idx != read_idx) {
                pending_probes[write_idx] = pending_probes[read_idx];
            }
            write_idx++;
        }
    }
    pending_probe_count = write_idx;
    pthread_mutex_unlock(&pending_mutex);

    // Log results
    LOG_DEBUG("Probe metrics for %s: RTT=%.2fms, Jitter=%.2fms, Loss=%.1f%% (%d/%d)",
              dst_ip, result->rtt_ms_avg, result->jitter_ms, result->loss_pct,
              rtt_count, expected_responses);

    return 0;
}
