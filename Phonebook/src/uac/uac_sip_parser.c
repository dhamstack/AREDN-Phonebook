// uac_sip_parser.c - SIP Response Parser for UAC
#include "uac.h"
#include "../common.h"

#define MODULE_NAME "UAC_PARSER"

// Extract To tag from SIP response
int uac_extract_to_tag(const char *response, char *to_tag_out, size_t out_size) {
    LOG_DEBUG("[UAC_PARSER] Extracting To tag from response");

    if (!response || !to_tag_out || out_size == 0) {
        LOG_ERROR("[UAC_PARSER] Invalid parameters to uac_extract_to_tag");
        return -1;
    }

    LOG_DEBUG("[UAC_PARSER] Searching for To header in response");

    // Find To header (case variations: "To:", "t:")
    const char *to_line = strstr(response, "\nTo:");
    if (!to_line) {
        LOG_DEBUG("[UAC_PARSER] No '\\nTo:' found, trying '\\nt:'");
        to_line = strstr(response, "\nt:");
    }
    if (!to_line) {
        // Try at beginning of response
        if (strncmp(response, "To:", 3) == 0) {
            LOG_DEBUG("[UAC_PARSER] Found 'To:' at start of response");
            to_line = response - 1; // Compensate for \n search
        } else if (strncmp(response, "t:", 2) == 0) {
            LOG_DEBUG("[UAC_PARSER] Found 't:' at start of response");
            to_line = response - 1;
        }
    }

    if (!to_line) {
        LOG_ERROR("[UAC_PARSER] No To header found in response");
        return -1;
    }

    LOG_DEBUG("[UAC_PARSER] To header found, searching for tag parameter");

    // Find tag parameter
    const char *tag_start = strstr(to_line, "tag=");
    if (!tag_start) {
        LOG_DEBUG("[UAC_PARSER] No tag in To header (initial INVITE response)");
        to_tag_out[0] = '\0';
        return 0;
    }

    tag_start += 4;  // Skip "tag="
    LOG_DEBUG("[UAC_PARSER] Tag parameter found, extracting value");

    // Find end of tag (delimiter: semicolon, CRLF, space, or end of string)
    const char *tag_end = strpbrk(tag_start, ";\r\n ");
    if (!tag_end) {
        tag_end = tag_start + strlen(tag_start);
    }

    size_t tag_len = tag_end - tag_start;
    if (tag_len >= out_size) {
        LOG_WARN("[UAC_PARSER] To tag truncated (length %zu exceeds buffer %zu)", tag_len, out_size);
        tag_len = out_size - 1;
    }

    strncpy(to_tag_out, tag_start, tag_len);
    to_tag_out[tag_len] = '\0';

    LOG_DEBUG("[UAC_PARSER] ✓ Extracted To tag: '%s' (%zu bytes)", to_tag_out, tag_len);
    return 0;
}
