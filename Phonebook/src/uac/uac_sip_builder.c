// uac_sip_builder.c - SIP Message Builder for UAC
#include "uac.h"
#include "../common.h"

#define MODULE_NAME "UAC_BUILDER"

// Build INVITE message
int uac_build_invite(char *buffer, size_t buffer_size, uac_call_t *call,
                     const char *local_ip, int local_port) {
    LOG_DEBUG("[UAC_BUILDER] Building INVITE message");

    if (!buffer || !call || !local_ip) {
        LOG_ERROR("[UAC_BUILDER] Invalid parameters to uac_build_invite");
        return -1;
    }

    LOG_DEBUG("[UAC_BUILDER] INVITE params - target: %s, local: %s:%d, Call-ID: %s",
              call->target_number, local_ip, local_port, call->call_id);

    // Build proper SDP with multiple codecs for better Yealink compatibility
    // Use standard RTP port range (Yealink phones often reject low ports like 10000)
    char sdp[1024];
    snprintf(sdp, sizeof(sdp),
        "v=0\r\n"
        "o=%s %ld %ld IN IP4 %s\r\n"
        "s=AREDN UAC Test Call\r\n"
        "c=IN IP4 %s\r\n"
        "t=0 0\r\n"
        "m=audio 16384 RTP/AVP 8 0 101\r\n"
        "a=rtpmap:8 PCMA/8000\r\n"
        "a=rtpmap:0 PCMU/8000\r\n"
        "a=rtpmap:101 telephone-event/8000\r\n"
        "a=fmtp:101 0-15\r\n"
        "a=ptime:20\r\n"
        "a=sendrecv\r\n",
        UAC_PHONE_NUMBER, (long)time(NULL), (long)time(NULL), local_ip, local_ip);

    int content_length = strlen(sdp);
    LOG_DEBUG("[UAC_BUILDER] SDP body created (%d bytes)", content_length);

    int written = snprintf(buffer, buffer_size,
        "INVITE sip:%s@localnode.local.mesh:5060 SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:%d;branch=%s\r\n"
        "From: <sip:%s@%s:%d>;tag=%s\r\n"
        "To: <sip:%s@localnode.local.mesh:5060>\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %d INVITE\r\n"
        "Contact: <sip:%s@%s:%d>\r\n"
        "Max-Forwards: 70\r\n"
        "User-Agent: AREDN-Phonebook-UAC/1.0\r\n"
        "Content-Type: application/sdp\r\n"
        "Content-Length: %d\r\n"
        "\r\n"
        "%s",
        call->target_number,
        local_ip, local_port, call->via_branch,
        UAC_PHONE_NUMBER, local_ip, local_port, call->from_tag,
        call->target_number,
        call->call_id,
        call->cseq,
        UAC_PHONE_NUMBER, local_ip, local_port,
        content_length,
        sdp);

    if (written >= buffer_size) {
        LOG_ERROR("[UAC_BUILDER] INVITE message truncated (needed %d bytes, have %zu)", written, buffer_size);
        return -1;
    }

    LOG_DEBUG("[UAC_BUILDER] ✓ Built INVITE message (%d bytes)", written);
    return 0;
}

// Build ACK message
int uac_build_ack(char *buffer, size_t buffer_size, uac_call_t *call,
                  const char *local_ip, int local_port) {
    LOG_DEBUG("[UAC_BUILDER] Building ACK message");

    if (!buffer || !call || !local_ip) {
        LOG_ERROR("[UAC_BUILDER] Invalid parameters to uac_build_ack");
        return -1;
    }

    LOG_DEBUG("[UAC_BUILDER] ACK params - target: %s, Call-ID: %s, To-tag: %s",
              call->target_number, call->call_id, call->to_tag);

    int written = snprintf(buffer, buffer_size,
        "ACK sip:%s@localnode.local.mesh:5060 SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:%d;branch=z9hG4bK%ld\r\n"
        "From: <sip:%s@%s:%d>;tag=%s\r\n"
        "To: <sip:%s@localnode.local.mesh:5060>;tag=%s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %d ACK\r\n"
        "Max-Forwards: 70\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        call->target_number,
        local_ip, local_port, random(),
        UAC_PHONE_NUMBER, local_ip, local_port, call->from_tag,
        call->target_number, call->to_tag,
        call->call_id,
        call->cseq);

    if (written >= buffer_size) {
        LOG_ERROR("[UAC_BUILDER] ACK message truncated (needed %d bytes, have %zu)", written, buffer_size);
        return -1;
    }

    LOG_DEBUG("[UAC_BUILDER] ✓ Built ACK message (%d bytes)", written);
    return 0;
}

// Build BYE message
int uac_build_bye(char *buffer, size_t buffer_size, uac_call_t *call,
                  const char *local_ip, int local_port) {
    LOG_DEBUG("[UAC_BUILDER] Building BYE message");

    if (!buffer || !call || !local_ip) {
        LOG_ERROR("[UAC_BUILDER] Invalid parameters to uac_build_bye");
        return -1;
    }

    LOG_DEBUG("[UAC_BUILDER] BYE params - target: %s, Call-ID: %s, CSeq: %d",
              call->target_number, call->call_id, call->cseq);

    int written = snprintf(buffer, buffer_size,
        "BYE sip:%s@localnode.local.mesh:5060 SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:%d;branch=z9hG4bK%ld\r\n"
        "From: <sip:%s@%s:%d>;tag=%s\r\n"
        "To: <sip:%s@localnode.local.mesh:5060>;tag=%s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %d BYE\r\n"
        "Max-Forwards: 70\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        call->target_number,
        local_ip, local_port, random(),
        UAC_PHONE_NUMBER, local_ip, local_port, call->from_tag,
        call->target_number, call->to_tag,
        call->call_id,
        call->cseq);

    if (written >= buffer_size) {
        LOG_ERROR("[UAC_BUILDER] BYE message truncated (needed %d bytes, have %zu)", written, buffer_size);
        return -1;
    }

    LOG_DEBUG("[UAC_BUILDER] ✓ Built BYE message (%d bytes)", written);
    return 0;
}
