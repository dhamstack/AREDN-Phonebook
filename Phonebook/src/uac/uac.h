// uac.h - SIP User Agent Client for Load Testing
#ifndef UAC_H
#define UAC_H

#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>

// UAC Configuration
#define UAC_SIP_PORT 5070
#define UAC_PHONE_NUMBER "999900"

// Call States
typedef enum {
    UAC_STATE_IDLE,           // No call
    UAC_STATE_CALLING,        // INVITE sent, waiting for response
    UAC_STATE_RINGING,        // 180 Ringing received
    UAC_STATE_ESTABLISHED,    // 200 OK received, ACK sent
    UAC_STATE_TERMINATING,    // BYE sent, waiting for 200 OK
    UAC_STATE_TERMINATED      // Call ended
} uac_call_state_t;

// Call Context (single call for Phase 1)
typedef struct {
    uac_call_state_t state;
    char call_id[256];        // Unique Call-ID
    char from_tag[64];        // From tag
    char to_tag[64];          // To tag (from 200 OK)
    char via_branch[64];      // Via branch (must be same for INVITE and CANCEL)
    char target_number[32];   // Called number (e.g., "441530")
    int cseq;                 // CSeq counter
    struct sockaddr_in server_addr;  // SIP server address (5060)
} uac_call_t;

// UAC Context
typedef struct {
    int sockfd;               // UAC socket (bound to port 5070)
    int local_port;           // UAC port (5070)
    char local_ip[64];        // UAC IP address
    uac_call_t call;          // Single call for Phase 1
} uac_context_t;

// Public API

/**
 * Initialize UAC module
 * @param local_ip IP address to bind UAC socket to
 * @return 0 on success, -1 on failure
 */
int uac_init(const char *local_ip);

/**
 * Shutdown UAC module
 */
void uac_shutdown(void);

/**
 * Get UAC socket file descriptor (for select loop)
 * @return UAC socket fd
 */
int uac_get_sockfd(void);

/**
 * Make a call to a target phone number
 * @param target_number Phone number to call (e.g., "441530")
 * @param server_ip SIP server IP address
 * @return 0 on success, -1 on failure
 */
int uac_make_call(const char *target_number, const char *server_ip);

/**
 * Cancel a ringing call (sends CANCEL)
 * @return 0 on success, -1 on failure
 */
int uac_cancel_call(void);

/**
 * Hang up current call (sends BYE)
 * @return 0 on success, -1 on failure
 */
int uac_hang_up(void);

/**
 * Process incoming SIP response
 * @param response SIP response message
 * @param response_len Length of response
 * @return 0 on success, -1 on failure
 */
int uac_process_response(const char *response, size_t response_len);

/**
 * Get current call state
 * @return Current UAC call state
 */
uac_call_state_t uac_get_state(void);

/**
 * Get call state as string
 * @param state Call state enum
 * @return String representation of state
 */
const char* uac_state_to_string(uac_call_state_t state);

/**
 * Reset UAC to IDLE state (clears any stuck call state)
 * Useful for error recovery and bulk testing
 */
void uac_reset_state(void);

#endif // UAC_H
