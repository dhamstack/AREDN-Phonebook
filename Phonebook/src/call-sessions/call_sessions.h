// call_manager/call_sessions.h
#ifndef CALL_SESSIONS_H
#define CALL_SESSIONS_H

#include "../common.h" 

// Call Session Management Prototypes
CallSession* find_call_session_by_callid(const char *call_id);
CallSession* create_call_session();
void terminate_call_session(CallSession *session);
void init_call_sessions();
int get_active_call_count(); 

#endif // CALL_SESSIONS_H