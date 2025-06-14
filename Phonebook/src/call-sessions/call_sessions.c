include "call_sessions.h"
#include "../common.h" // For logging macros

#define MODULE_NAME "SESSION"

CallSession* find_call_session_by_callid(const char *call_id) {
    for (int i = 0; i < MAX_CALL_SESSIONS; i++) {
        if (call_sessions[i].in_use &&
            strcmp(call_sessions[i].call_id, call_id) == 0) {
            return &call_sessions[i];
        }
    }
    return NULL;
}

CallSession* create_call_session() {
    for (int i = 0; i < MAX_CALL_SESSIONS; i++) {
        if (!call_sessions[i].in_use) {
            call_sessions[i].in_use = 1;
            call_sessions[i].state = CALL_STATE_FREE;
            memset(call_sessions[i].call_id, 0, sizeof(call_sessions[i].call_id));
            memset(call_sessions[i].cseq, 0, sizeof(call_sessions[i].cseq));
            memset(call_sessions[i].from_tag, 0, sizeof(call_sessions[i].from_tag));
            memset(call_sessions[i].to_tag, 0, sizeof(call_sessions[i].to_tag));
            memset(&call_sessions[i].caller_addr, 0, sizeof(struct sockaddr_in));
            memset(&call_sessions[i].callee_addr, 0, sizeof(struct sockaddr_in));
            memset(&call_sessions[i].original_caller_addr, 0, sizeof(struct sockaddr_in));
            LOG_DEBUG("Call Sessions: Created new call session at index %d.", i);
            return &call_sessions[i];
        }
    }
    LOG_WARN("Call Sessions: Max call sessions reached (%d), cannot create new session.",
                MAX_CALL_SESSIONS);
    return NULL;
}

void terminate_call_session(CallSession *session) {
    if (session && session->in_use) {
        LOG_INFO("Call Sessions: Terminating call session Call-ID: %s", session->call_id);
        session->in_use = 0;
        session->state = CALL_STATE_FREE;
        memset(session->call_id, 0, sizeof(session->call_id));
        memset(session->cseq, 0, sizeof(session->cseq));
        memset(session->from_tag, 0, sizeof(session->from_tag));
        memset(session->to_tag, 0, sizeof(session->to_tag));
        memset(&session->caller_addr, 0, sizeof(struct sockaddr_in));
        memset(&session->callee_addr, 0, sizeof(struct sockaddr_in));
        memset(&session->original_caller_addr, 0, sizeof(struct sockaddr_in));
    }
}

void init_call_sessions() {
    for (int i = 0; i < MAX_CALL_SESSIONS; i++) {
        call_sessions[i].in_use = 0;
        call_sessions[i].state = CALL_STATE_FREE;
    }
    LOG_INFO("Initialized call session table (max %d sessions).",
                MAX_CALL_SESSIONS);
}