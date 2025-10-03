// uac_sip_parser.c - SIP Response Parser for UAC
#include "uac.h"
#include "../common.h"

#define MODULE_NAME "UAC_PARSER"

// Extract To tag from SIP response
int uac_extract_to_tag(const char *response, char *to_tag_out, size_t out_size) {
    if (!response || !to_tag_out || out_size == 0) {
        LOG_ERROR("Invalid parameters to uac_extract_to_tag");
        return -1;
    }

    // Find To header (case variations: "To:", "t:")
    const char *to_line = strstr(response, "\nTo:");
    if (!to_line) {
        to_line = strstr(response, "\nt:");
    }
    if (!to_line) {
        // Try at beginning of response
        if (strncmp(response, "To:", 3) == 0) {
            to_line = response - 1; // Compensate for \n search
        } else if (strncmp(response, "t:", 2) == 0) {
            to_line = response - 1;
        }
    }

    if (!to_line) {
        LOG_ERROR("No To header found in response");
        return -1;
    }

    // Find tag parameter
    const char *tag_start = strstr(to_line, "tag=");
    if (!tag_start) {
        LOG_DEBUG("No tag in To header (initial INVITE response)");
        to_tag_out[0] = '\0';
        return 0;
    }

    tag_start += 4;  // Skip "tag="

    // Find end of tag (delimiter: semicolon, CRLF, space, or end of string)
    const char *tag_end = strpbrk(tag_start, ";\r\n ");
    if (!tag_end) {
        tag_end = tag_start + strlen(tag_start);
    }

    size_t tag_len = tag_end - tag_start;
    if (tag_len >= out_size) {
        LOG_WARN("To tag truncated (length %zu exceeds buffer %zu)", tag_len, out_size);
        tag_len = out_size - 1;
    }

    strncpy(to_tag_out, tag_start, tag_len);
    to_tag_out[tag_len] = '\0';

    LOG_DEBUG("Extracted To tag: '%s'", to_tag_out);
    return 0;
}
