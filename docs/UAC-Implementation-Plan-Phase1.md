# UAC Implementation Plan - Phase 1
## Connect, Send INVITE, and Send BYE

### Overview

Phase 1 implements a basic UAC (User Agent Client) module integrated into the existing AREDN-Phonebook codebase. The goal is to establish the foundation for load testing by implementing a single-call flow: connect to the SIP server, send INVITE, and send BYE.

**Integration Strategy:** Add UAC as a new module within `Phonebook/src/uac/` that reuses existing infrastructure (logging, configuration, etc.)

---

## Phase 1 Requirements (from UAC.md)

### Functional Goals (Section 1)
1. ✅ Make the Call (INVITE) - send INVITE to SIP Server on port 5060
2. ✅ Handle the Response Chain - listen for 100 Trying, 180 Ringing, 200 OK
3. ✅ Seal the Deal (ACK) - send ACK after receiving 200 OK
4. ❌ Simulate the Conversation (RTP) - **Deferred to Phase 2**
5. ✅ Hang Up (BYE) - send BYE to end the call
6. ✅ Describe Itself (SDP) - include SDP in INVITE

### Networking Rules (Section 2)
1. ✅ SIP Signaling Port - bind to unique port (not 5060), e.g., 5070
2. ✅ Advertising Via Header - include unique port in all Via headers
3. ❌ Media Ports (RTP/RTCP) - **Deferred to Phase 2**
4. ❌ RTP Port Pool - **Deferred to Phase 2**

---

## File Structure

```
Phonebook/src/uac/
├── uac.h                    # Public API and data structures
├── uac.c                    # Main UAC coordinator
├── uac_sip_builder.c        # SIP message builder (INVITE, ACK, BYE)
└── uac_sip_parser.c         # SIP response parser (100, 180, 200)
```

**Integration Points:**
- `Phonebook/src/main.c` - Initialize UAC module
- `Phonebook/src/common.h` - Add UAC function prototypes
- `Phonebook/Makefile` - Add UAC source files

---

## Data Structures

### Call State (Simplified for Phase 1)

```c
// uac.h

typedef enum {
    UAC_STATE_IDLE,           // No call
    UAC_STATE_CALLING,        // INVITE sent, waiting for response
    UAC_STATE_RINGING,        // 180 Ringing received
    UAC_STATE_ESTABLISHED,    // 200 OK received, ACK sent
    UAC_STATE_TERMINATING,    // BYE sent, waiting for 200 OK
    UAC_STATE_TERMINATED      // Call ended
} uac_call_state_t;

typedef struct {
    uac_call_state_t state;
    char call_id[256];        // Unique Call-ID
    char from_tag[64];        // From tag
    char to_tag[64];          // To tag (from 200 OK)
    char target_number[32];   // Called number (e.g., "441530")
    int cseq;                 // CSeq counter
    struct sockaddr_in server_addr;  // SIP server address (5060)
} uac_call_t;

typedef struct {
    int sockfd;               // UAC socket (bound to port 5070)
    int local_port;           // UAC port (5070)
    char local_ip[64];        // UAC IP address
    uac_call_t call;          // Single call for Phase 1
} uac_context_t;
```

---

## Core Functions

### 1. Initialization

```c
// uac.c

#define UAC_SIP_PORT 5070
#define MODULE_NAME "UAC"

static uac_context_t g_uac_ctx;

int uac_init(const char *local_ip) {
    // Create UDP socket
    g_uac_ctx.sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_uac_ctx.sockfd < 0) {
        LOG_ERROR("Failed to create UAC socket");
        return -1;
    }

    // Bind to unique port (5070)
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = inet_addr(local_ip);
    bind_addr.sin_port = htons(UAC_SIP_PORT);

    if (bind(g_uac_ctx.sockfd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        LOG_ERROR("Failed to bind UAC socket to %s:%d", local_ip, UAC_SIP_PORT);
        close(g_uac_ctx.sockfd);
        return -1;
    }

    strncpy(g_uac_ctx.local_ip, local_ip, sizeof(g_uac_ctx.local_ip) - 1);
    g_uac_ctx.local_port = UAC_SIP_PORT;
    g_uac_ctx.call.state = UAC_STATE_IDLE;

    LOG_INFO("UAC initialized on %s:%d", local_ip, UAC_SIP_PORT);
    return 0;
}

void uac_shutdown(void) {
    if (g_uac_ctx.sockfd >= 0) {
        close(g_uac_ctx.sockfd);
        g_uac_ctx.sockfd = -1;
    }
    LOG_INFO("UAC shutdown complete");
}
```

### 2. Make Call (INVITE)

```c
// uac.c

int uac_make_call(const char *target_number, const char *server_ip) {
    if (g_uac_ctx.call.state != UAC_STATE_IDLE) {
        LOG_ERROR("Call already in progress");
        return -1;
    }

    // Setup server address
    memset(&g_uac_ctx.call.server_addr, 0, sizeof(g_uac_ctx.call.server_addr));
    g_uac_ctx.call.server_addr.sin_family = AF_INET;
    g_uac_ctx.call.server_addr.sin_addr.s_addr = inet_addr(server_ip);
    g_uac_ctx.call.server_addr.sin_port = htons(5060);

    // Generate Call-ID and tags
    snprintf(g_uac_ctx.call.call_id, sizeof(g_uac_ctx.call.call_id),
             "uac-%ld@%s", time(NULL), g_uac_ctx.local_ip);
    snprintf(g_uac_ctx.call.from_tag, sizeof(g_uac_ctx.call.from_tag),
             "tag-%ld", random());
    g_uac_ctx.call.to_tag[0] = '\0';  // No To tag yet
    strncpy(g_uac_ctx.call.target_number, target_number,
            sizeof(g_uac_ctx.call.target_number) - 1);
    g_uac_ctx.call.cseq = 1;

    // Build INVITE message
    char invite_msg[2048];
    if (uac_build_invite(invite_msg, sizeof(invite_msg), &g_uac_ctx.call,
                         g_uac_ctx.local_ip, g_uac_ctx.local_port) < 0) {
        LOG_ERROR("Failed to build INVITE message");
        return -1;
    }

    // Send INVITE
    ssize_t sent = sendto(g_uac_ctx.sockfd, invite_msg, strlen(invite_msg), 0,
                          (struct sockaddr*)&g_uac_ctx.call.server_addr,
                          sizeof(g_uac_ctx.call.server_addr));
    if (sent < 0) {
        LOG_ERROR("Failed to send INVITE");
        return -1;
    }

    g_uac_ctx.call.state = UAC_STATE_CALLING;
    LOG_INFO("INVITE sent to %s for %s", server_ip, target_number);
    return 0;
}
```

### 3. Build INVITE Message

```c
// uac_sip_builder.c

#define MODULE_NAME "UAC_BUILDER"

int uac_build_invite(char *buffer, size_t buffer_size, uac_call_t *call,
                     const char *local_ip, int local_port) {
    // For Phase 1, use minimal SDP (RTP not actually sent)
    char sdp[512];
    snprintf(sdp, sizeof(sdp),
        "v=0\r\n"
        "o=UAC %ld %ld IN IP4 %s\r\n"
        "s=Load Test Call\r\n"
        "c=IN IP4 %s\r\n"
        "t=0 0\r\n"
        "m=audio 10000 RTP/AVP 0\r\n"  // Placeholder port (not used in Phase 1)
        "a=rtpmap:0 PCMU/8000\r\n",
        time(NULL), time(NULL), local_ip, local_ip);

    int content_length = strlen(sdp);

    int written = snprintf(buffer, buffer_size,
        "INVITE sip:%s@localnode.local.mesh:5060 SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:%d;branch=z9hG4bK%ld\r\n"
        "From: <sip:uac@%s:%d>;tag=%s\r\n"
        "To: <sip:%s@localnode.local.mesh:5060>\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %d INVITE\r\n"
        "Contact: <sip:uac@%s:%d>\r\n"
        "Max-Forwards: 70\r\n"
        "User-Agent: AREDN-Phonebook-UAC/1.0\r\n"
        "Content-Type: application/sdp\r\n"
        "Content-Length: %d\r\n"
        "\r\n"
        "%s",
        call->target_number,
        local_ip, local_port, random(),
        local_ip, local_port, call->from_tag,
        call->target_number,
        call->call_id,
        call->cseq,
        local_ip, local_port,
        content_length,
        sdp);

    if (written >= buffer_size) {
        LOG_ERROR("INVITE message truncated");
        return -1;
    }

    return 0;
}
```

### 4. Handle Responses

```c
// uac.c

int uac_process_response(const char *response, size_t response_len) {
    // Parse status line
    int status_code = 0;
    if (sscanf(response, "SIP/2.0 %d", &status_code) != 1) {
        LOG_ERROR("Failed to parse SIP response");
        return -1;
    }

    LOG_INFO("Received SIP response: %d", status_code);

    switch (status_code) {
        case 100:  // Trying
            if (g_uac_ctx.call.state == UAC_STATE_CALLING) {
                LOG_INFO("Call setup in progress");
            }
            break;

        case 180:  // Ringing
            if (g_uac_ctx.call.state == UAC_STATE_CALLING) {
                g_uac_ctx.call.state = UAC_STATE_RINGING;
                LOG_INFO("Phone is ringing");
            }
            break;

        case 200:  // OK
            if (g_uac_ctx.call.state == UAC_STATE_RINGING ||
                g_uac_ctx.call.state == UAC_STATE_CALLING) {
                // Extract To tag
                uac_extract_to_tag(response, g_uac_ctx.call.to_tag,
                                   sizeof(g_uac_ctx.call.to_tag));

                // Send ACK
                uac_send_ack();

                g_uac_ctx.call.state = UAC_STATE_ESTABLISHED;
                LOG_INFO("Call established, ACK sent");
            } else if (g_uac_ctx.call.state == UAC_STATE_TERMINATING) {
                g_uac_ctx.call.state = UAC_STATE_TERMINATED;
                LOG_INFO("Call terminated successfully");
            }
            break;

        default:
            LOG_WARN("Unexpected response code: %d", status_code);
            break;
    }

    return 0;
}
```

### 5. Send ACK

```c
// uac.c

int uac_send_ack(void) {
    char ack_msg[1024];

    int written = snprintf(ack_msg, sizeof(ack_msg),
        "ACK sip:%s@localnode.local.mesh:5060 SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:%d;branch=z9hG4bK%ld\r\n"
        "From: <sip:uac@%s:%d>;tag=%s\r\n"
        "To: <sip:%s@localnode.local.mesh:5060>;tag=%s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %d ACK\r\n"
        "Max-Forwards: 70\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        g_uac_ctx.call.target_number,
        g_uac_ctx.local_ip, g_uac_ctx.local_port, random(),
        g_uac_ctx.local_ip, g_uac_ctx.local_port, g_uac_ctx.call.from_tag,
        g_uac_ctx.call.target_number, g_uac_ctx.call.to_tag,
        g_uac_ctx.call.call_id,
        g_uac_ctx.call.cseq);

    if (written >= sizeof(ack_msg)) {
        LOG_ERROR("ACK message truncated");
        return -1;
    }

    ssize_t sent = sendto(g_uac_ctx.sockfd, ack_msg, strlen(ack_msg), 0,
                          (struct sockaddr*)&g_uac_ctx.call.server_addr,
                          sizeof(g_uac_ctx.call.server_addr));
    if (sent < 0) {
        LOG_ERROR("Failed to send ACK");
        return -1;
    }

    LOG_INFO("ACK sent");
    return 0;
}
```

### 6. Hang Up (BYE)

```c
// uac.c

int uac_hang_up(void) {
    if (g_uac_ctx.call.state != UAC_STATE_ESTABLISHED) {
        LOG_ERROR("No established call to hang up");
        return -1;
    }

    char bye_msg[1024];
    g_uac_ctx.call.cseq++;  // Increment CSeq for BYE

    int written = snprintf(bye_msg, sizeof(bye_msg),
        "BYE sip:%s@localnode.local.mesh:5060 SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:%d;branch=z9hG4bK%ld\r\n"
        "From: <sip:uac@%s:%d>;tag=%s\r\n"
        "To: <sip:%s@localnode.local.mesh:5060>;tag=%s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %d BYE\r\n"
        "Max-Forwards: 70\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        g_uac_ctx.call.target_number,
        g_uac_ctx.local_ip, g_uac_ctx.local_port, random(),
        g_uac_ctx.local_ip, g_uac_ctx.local_port, g_uac_ctx.call.from_tag,
        g_uac_ctx.call.target_number, g_uac_ctx.call.to_tag,
        g_uac_ctx.call.call_id,
        g_uac_ctx.call.cseq);

    if (written >= sizeof(bye_msg)) {
        LOG_ERROR("BYE message truncated");
        return -1;
    }

    ssize_t sent = sendto(g_uac_ctx.sockfd, bye_msg, strlen(bye_msg), 0,
                          (struct sockaddr*)&g_uac_ctx.call.server_addr,
                          sizeof(g_uac_ctx.call.server_addr));
    if (sent < 0) {
        LOG_ERROR("Failed to send BYE");
        return -1;
    }

    g_uac_ctx.call.state = UAC_STATE_TERMINATING;
    LOG_INFO("BYE sent");
    return 0;
}
```

### 7. Response Parser Helper

```c
// uac_sip_parser.c

#define MODULE_NAME "UAC_PARSER"

int uac_extract_to_tag(const char *response, char *to_tag_out, size_t out_size) {
    const char *to_line = strstr(response, "\nTo:");
    if (!to_line) {
        to_line = strstr(response, "\nt:");
    }
    if (!to_line) {
        LOG_ERROR("No To header found");
        return -1;
    }

    const char *tag_start = strstr(to_line, "tag=");
    if (!tag_start) {
        LOG_WARN("No tag in To header");
        to_tag_out[0] = '\0';
        return 0;
    }

    tag_start += 4;  // Skip "tag="
    const char *tag_end = strpbrk(tag_start, ";\r\n ");
    if (!tag_end) {
        tag_end = tag_start + strlen(tag_start);
    }

    size_t tag_len = tag_end - tag_start;
    if (tag_len >= out_size) {
        tag_len = out_size - 1;
    }

    strncpy(to_tag_out, tag_start, tag_len);
    to_tag_out[tag_len] = '\0';

    LOG_DEBUG("Extracted To tag: %s", to_tag_out);
    return 0;
}
```

---

## Integration into main.c

```c
// main.c

#include "uac/uac.h"

int main(int argc, char *argv[]) {
    // ... existing initialization ...

    // Initialize UAC module (after SIP server is bound)
    char server_ip[64];
    if (get_server_ip(server_ip, sizeof(server_ip)) == 0) {
        if (uac_init(server_ip) == 0) {
            LOG_INFO("UAC module initialized");
        } else {
            LOG_WARN("UAC module initialization failed");
        }
    }

    // ... existing main loop ...

    // In main loop, also check UAC socket for responses
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);  // SIP server socket
    FD_SET(uac_get_sockfd(), &readfds);  // UAC socket

    int max_fd = (sockfd > uac_get_sockfd()) ? sockfd : uac_get_sockfd();

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    int activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);

    if (activity > 0) {
        if (FD_ISSET(sockfd, &readfds)) {
            // Handle SIP server messages (existing code)
            // ...
        }

        if (FD_ISSET(uac_get_sockfd(), &readfds)) {
            // Handle UAC responses
            char response[2048];
            ssize_t n = recvfrom(uac_get_sockfd(), response, sizeof(response) - 1, 0, NULL, NULL);
            if (n > 0) {
                response[n] = '\0';
                uac_process_response(response, n);
            }
        }
    }

    // ... cleanup ...
    uac_shutdown();
}
```

---

## Testing Phase 1

### Test Case 1: Basic Call Flow

```bash
# Start SIP server
./AREDN-Phonebook

# In another terminal, trigger UAC test call
# (via webhook or command line argument)
curl -X POST http://localhost/cgi-bin/uac_test?target=441530
```

**Expected Flow:**
1. UAC sends INVITE to server (port 5060)
2. Server forwards INVITE to 441530
3. UAC receives 100 Trying
4. UAC receives 180 Ringing
5. UAC receives 200 OK
6. UAC sends ACK
7. Call established
8. UAC sends BYE
9. UAC receives 200 OK
10. Call terminated

### Test Case 2: Port Uniqueness Verification

```bash
# Verify UAC is using port 5070
netstat -an | grep 5070

# Verify SIP server still on 5060
netstat -an | grep 5060
```

---

## Phase 1 Deliverables

- [ ] `uac.h` - Public API and data structures
- [ ] `uac.c` - Core UAC implementation (init, make_call, hang_up, process_response)
- [ ] `uac_sip_builder.c` - INVITE, ACK, BYE message builders
- [ ] `uac_sip_parser.c` - Response parser (extract To tag)
- [ ] Integration into `main.c` - UAC socket in select() loop
- [ ] Updated `Makefile` - Build UAC module
- [ ] Test webhook `/cgi-bin/uac_test` - Trigger test calls

---

## Future Phases

**Phase 2:** Add RTP media simulation
- RTP port pool management
- Send PCMU packets during call
- Measure media metrics

**Phase 3:** Multiple concurrent calls
- Array of `uac_call_t` structures
- Thread or async handling
- Performance metrics collection

**Phase 4:** Load testing configuration
- Configurable call count
- Call duration control
- Metrics export (JSON)
