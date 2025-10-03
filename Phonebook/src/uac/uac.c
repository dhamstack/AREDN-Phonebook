// uac.c - SIP User Agent Client Core Implementation
#include "uac.h"
#include "../common.h"
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>

#define MODULE_NAME "UAC"

// Forward declarations for builder/parser functions
extern int uac_build_invite(char *buffer, size_t buffer_size, uac_call_t *call,
                             const char *local_ip, int local_port);
extern int uac_build_ack(char *buffer, size_t buffer_size, uac_call_t *call,
                         const char *local_ip, int local_port);
extern int uac_build_bye(char *buffer, size_t buffer_size, uac_call_t *call,
                         const char *local_ip, int local_port);
extern int uac_extract_to_tag(const char *response, char *to_tag_out, size_t out_size);

// Global UAC context
static uac_context_t g_uac_ctx = {
    .sockfd = -1,
    .local_port = 0,
    .local_ip = {0},
    .call = {
        .state = UAC_STATE_IDLE,
        .call_id = {0},
        .from_tag = {0},
        .to_tag = {0},
        .target_number = {0},
        .cseq = 0
    }
};

// Initialize UAC module
int uac_init(const char *local_ip) {
    LOG_DEBUG("[UAC_INIT] Starting UAC initialization");
    LOG_DEBUG("[UAC_INIT] Local IP parameter: %s", local_ip ? local_ip : "NULL");

    if (!local_ip || strlen(local_ip) == 0) {
        LOG_ERROR("[UAC_INIT] Invalid local IP provided to UAC");
        return -1;
    }

    LOG_DEBUG("[UAC_INIT] Creating UDP socket for UAC");
    // Create UDP socket
    g_uac_ctx.sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_uac_ctx.sockfd < 0) {
        LOG_ERROR("[UAC_INIT] Failed to create UAC socket: %s", strerror(errno));
        return -1;
    }
    LOG_DEBUG("[UAC_INIT] Socket created successfully (fd=%d)", g_uac_ctx.sockfd);

    // Bind to unique port (5070)
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = inet_addr(local_ip);
    bind_addr.sin_port = htons(UAC_SIP_PORT);

    LOG_DEBUG("[UAC_INIT] Attempting to bind to %s:%d", local_ip, UAC_SIP_PORT);
    if (bind(g_uac_ctx.sockfd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        LOG_ERROR("[UAC_INIT] Failed to bind UAC socket to %s:%d: %s", local_ip, UAC_SIP_PORT, strerror(errno));
        close(g_uac_ctx.sockfd);
        g_uac_ctx.sockfd = -1;
        return -1;
    }

    strncpy(g_uac_ctx.local_ip, local_ip, sizeof(g_uac_ctx.local_ip) - 1);
    g_uac_ctx.local_port = UAC_SIP_PORT;
    g_uac_ctx.call.state = UAC_STATE_IDLE;

    LOG_INFO("[UAC_INIT] ✓ UAC initialized on %s:%d (Phone: %s)", local_ip, UAC_SIP_PORT, UAC_PHONE_NUMBER);
    LOG_DEBUG("[UAC_INIT] UAC context - sockfd=%d, local_ip=%s, local_port=%d, state=%s",
              g_uac_ctx.sockfd, g_uac_ctx.local_ip, g_uac_ctx.local_port,
              uac_state_to_string(g_uac_ctx.call.state));
    return 0;
}

// Shutdown UAC module
void uac_shutdown(void) {
    LOG_DEBUG("[UAC_SHUTDOWN] Starting UAC shutdown");
    if (g_uac_ctx.sockfd >= 0) {
        LOG_DEBUG("[UAC_SHUTDOWN] Closing socket fd=%d", g_uac_ctx.sockfd);
        close(g_uac_ctx.sockfd);
        g_uac_ctx.sockfd = -1;
    }
    LOG_INFO("[UAC_SHUTDOWN] ✓ UAC shutdown complete");
}

// Get UAC socket file descriptor
int uac_get_sockfd(void) {
    return g_uac_ctx.sockfd;
}

// Get current call state
uac_call_state_t uac_get_state(void) {
    return g_uac_ctx.call.state;
}

// Get call state as string
const char* uac_state_to_string(uac_call_state_t state) {
    switch (state) {
        case UAC_STATE_IDLE:        return "IDLE";
        case UAC_STATE_CALLING:     return "CALLING";
        case UAC_STATE_RINGING:     return "RINGING";
        case UAC_STATE_ESTABLISHED: return "ESTABLISHED";
        case UAC_STATE_TERMINATING: return "TERMINATING";
        case UAC_STATE_TERMINATED:  return "TERMINATED";
        default:                    return "UNKNOWN";
    }
}

// Make a call
int uac_make_call(const char *target_number, const char *server_ip) {
    LOG_INFO("[UAC_CALL] Making call to %s via server %s",
             target_number ? target_number : "NULL",
             server_ip ? server_ip : "NULL");

    if (!target_number || !server_ip) {
        LOG_ERROR("[UAC_CALL] Invalid parameters to uac_make_call");
        return -1;
    }

    if (g_uac_ctx.call.state != UAC_STATE_IDLE) {
        LOG_ERROR("[UAC_CALL] Call already in progress (state: %s)", uac_state_to_string(g_uac_ctx.call.state));
        return -1;
    }

    if (g_uac_ctx.sockfd < 0) {
        LOG_ERROR("[UAC_CALL] UAC not initialized (sockfd=%d)", g_uac_ctx.sockfd);
        return -1;
    }

    LOG_DEBUG("[UAC_CALL] Current state: %s, sockfd: %d",
              uac_state_to_string(g_uac_ctx.call.state), g_uac_ctx.sockfd);

    // Setup server address
    memset(&g_uac_ctx.call.server_addr, 0, sizeof(g_uac_ctx.call.server_addr));
    g_uac_ctx.call.server_addr.sin_family = AF_INET;
    g_uac_ctx.call.server_addr.sin_addr.s_addr = inet_addr(server_ip);
    g_uac_ctx.call.server_addr.sin_port = htons(5060);

    LOG_DEBUG("[UAC_CALL] Server address set to %s:5060", server_ip);

    // Generate Call-ID and tags
    snprintf(g_uac_ctx.call.call_id, sizeof(g_uac_ctx.call.call_id),
             "uac-%ld@%s", time(NULL), g_uac_ctx.local_ip);
    snprintf(g_uac_ctx.call.from_tag, sizeof(g_uac_ctx.call.from_tag),
             "tag-%ld", random());
    g_uac_ctx.call.to_tag[0] = '\0';  // No To tag yet
    strncpy(g_uac_ctx.call.target_number, target_number,
            sizeof(g_uac_ctx.call.target_number) - 1);
    g_uac_ctx.call.cseq = 1;

    LOG_DEBUG("[UAC_CALL] Call-ID: %s", g_uac_ctx.call.call_id);
    LOG_DEBUG("[UAC_CALL] From-tag: %s", g_uac_ctx.call.from_tag);
    LOG_DEBUG("[UAC_CALL] CSeq: %d", g_uac_ctx.call.cseq);

    // Build INVITE message
    char invite_msg[2048];
    LOG_DEBUG("[UAC_CALL] Building INVITE message");
    if (uac_build_invite(invite_msg, sizeof(invite_msg), &g_uac_ctx.call,
                         g_uac_ctx.local_ip, g_uac_ctx.local_port) < 0) {
        LOG_ERROR("[UAC_CALL] Failed to build INVITE message");
        return -1;
    }

    LOG_DEBUG("[UAC_CALL] INVITE message built (%zu bytes)", strlen(invite_msg));

    // Send INVITE
    LOG_DEBUG("[UAC_CALL] Sending INVITE to %s:5060", server_ip);
    ssize_t sent = sendto(g_uac_ctx.sockfd, invite_msg, strlen(invite_msg), 0,
                          (struct sockaddr*)&g_uac_ctx.call.server_addr,
                          sizeof(g_uac_ctx.call.server_addr));
    if (sent < 0) {
        LOG_ERROR("[UAC_CALL] Failed to send INVITE: %s", strerror(errno));
        return -1;
    }

    LOG_DEBUG("[UAC_CALL] INVITE sent successfully (%zd bytes)", sent);

    g_uac_ctx.call.state = UAC_STATE_CALLING;
    LOG_INFO("[UAC_CALL] ✓ INVITE sent to %s for %s (Call-ID: %s, state: %s)",
             server_ip, target_number, g_uac_ctx.call.call_id,
             uac_state_to_string(g_uac_ctx.call.state));
    return 0;
}

// Send ACK
static int uac_send_ack(void) {
    char ack_msg[1024];

    if (uac_build_ack(ack_msg, sizeof(ack_msg), &g_uac_ctx.call,
                      g_uac_ctx.local_ip, g_uac_ctx.local_port) < 0) {
        LOG_ERROR("Failed to build ACK message");
        return -1;
    }

    ssize_t sent = sendto(g_uac_ctx.sockfd, ack_msg, strlen(ack_msg), 0,
                          (struct sockaddr*)&g_uac_ctx.call.server_addr,
                          sizeof(g_uac_ctx.call.server_addr));
    if (sent < 0) {
        LOG_ERROR("Failed to send ACK: %s", strerror(errno));
        return -1;
    }

    LOG_INFO("ACK sent");
    return 0;
}

// Hang up current call
int uac_hang_up(void) {
    if (g_uac_ctx.call.state != UAC_STATE_ESTABLISHED) {
        LOG_ERROR("No established call to hang up (state: %s)",
                  uac_state_to_string(g_uac_ctx.call.state));
        return -1;
    }

    char bye_msg[1024];
    g_uac_ctx.call.cseq++;  // Increment CSeq for BYE

    if (uac_build_bye(bye_msg, sizeof(bye_msg), &g_uac_ctx.call,
                      g_uac_ctx.local_ip, g_uac_ctx.local_port) < 0) {
        LOG_ERROR("Failed to build BYE message");
        return -1;
    }

    ssize_t sent = sendto(g_uac_ctx.sockfd, bye_msg, strlen(bye_msg), 0,
                          (struct sockaddr*)&g_uac_ctx.call.server_addr,
                          sizeof(g_uac_ctx.call.server_addr));
    if (sent < 0) {
        LOG_ERROR("Failed to send BYE: %s", strerror(errno));
        return -1;
    }

    g_uac_ctx.call.state = UAC_STATE_TERMINATING;
    LOG_INFO("BYE sent");
    return 0;
}

// Process incoming SIP response
int uac_process_response(const char *response, size_t response_len) {
    LOG_DEBUG("[UAC_RESPONSE] Received response (%zu bytes)", response_len);

    if (!response || response_len == 0) {
        LOG_ERROR("[UAC_RESPONSE] Invalid response parameters");
        return -1;
    }

    // Parse status line
    int status_code = 0;
    if (sscanf(response, "SIP/2.0 %d", &status_code) != 1) {
        LOG_ERROR("[UAC_RESPONSE] Failed to parse SIP response status line");
        LOG_DEBUG("[UAC_RESPONSE] Response: %.*s", (int)MIN(200, response_len), response);
        return -1;
    }

    LOG_INFO("[UAC_RESPONSE] ← Received %d response (state: %s)", status_code,
             uac_state_to_string(g_uac_ctx.call.state));
    LOG_DEBUG("[UAC_RESPONSE] First line: %.80s", response);

    switch (status_code) {
        case 100:  // Trying
            if (g_uac_ctx.call.state == UAC_STATE_CALLING) {
                LOG_INFO("Call setup in progress (100 Trying)");
            }
            break;

        case 180:  // Ringing
            if (g_uac_ctx.call.state == UAC_STATE_CALLING) {
                g_uac_ctx.call.state = UAC_STATE_RINGING;
                LOG_INFO("Phone is ringing (180 Ringing)");
            }
            break;

        case 200:  // OK
            if (g_uac_ctx.call.state == UAC_STATE_RINGING ||
                g_uac_ctx.call.state == UAC_STATE_CALLING) {
                // Extract To tag from 200 OK
                if (uac_extract_to_tag(response, g_uac_ctx.call.to_tag,
                                       sizeof(g_uac_ctx.call.to_tag)) < 0) {
                    LOG_WARN("Failed to extract To tag from 200 OK");
                }

                // Send ACK
                if (uac_send_ack() < 0) {
                    LOG_ERROR("Failed to send ACK");
                    return -1;
                }

                g_uac_ctx.call.state = UAC_STATE_ESTABLISHED;
                LOG_INFO("Call established (200 OK received, ACK sent)");

            } else if (g_uac_ctx.call.state == UAC_STATE_TERMINATING) {
                g_uac_ctx.call.state = UAC_STATE_TERMINATED;
                LOG_INFO("Call terminated successfully (200 OK for BYE)");

                // Reset call context
                g_uac_ctx.call.state = UAC_STATE_IDLE;
                memset(&g_uac_ctx.call, 0, sizeof(g_uac_ctx.call));
            }
            break;

        case 486:  // Busy Here
            LOG_WARN("Target phone busy (486 Busy Here)");
            g_uac_ctx.call.state = UAC_STATE_IDLE;
            break;

        case 487:  // Request Terminated
            LOG_WARN("Request terminated (487)");
            g_uac_ctx.call.state = UAC_STATE_IDLE;
            break;

        default:
            LOG_WARN("Unexpected response code: %d", status_code);
            break;
    }

    return 0;
}
