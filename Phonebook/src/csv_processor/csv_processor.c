#define MODULE_NAME "CSV" // Define MODULE_NAME at the top of the file

#include "csv_processor.h"
#include "../common.h" // This includes necessary system headers and core types
#include "../config_loader/config_loader.h" // For g_phonebook_servers_list, g_num_phonebook_servers
#include "../file_utils/file_utils.h"

// Note: Global extern declarations are now in common.h


// Helper functions (static to this file)
static int is_cont(unsigned char c) { return (c & 0xC0) == 0x80; }

// IMPORTANT: The 'static' keyword has been REMOVED here.
// This function is now globally visible as declared in common.h.
void sanitize_utf8(const char *in, char *out, size_t out_sz) {
    size_t o = 0;
    while (*in && o + 1 < out_sz) {
        unsigned char c = (unsigned char)*in;
        size_t len = 1;
        if (c < 0x80) {
        } else if ((c & 0xE0) == 0xC0 && is_cont(in[1])) {
            len = 2;
        } else if ((c & 0xF0) == 0xE0 && is_cont(in[1]) && is_cont(in[2])) {
            len = 3;
        } else if ((c & 0xF8) == 0xF0
                   && is_cont(in[1]) && is_cont(in[2]) && is_cont(in[3])) {
            len = 4;
        } else {
            in++;
            continue;
        }
        if (o + len >= out_sz) break;
        for (size_t i = 0; i < len; i++) out[o++] = *in++;
    }
    out[o] = '\0';
}

static void xml_escape(const char *in, char *out, size_t out_sz) {
    size_t o = 0;
    const unsigned char *p = (const unsigned char*)in;
    while (*p && o + 1 < out_sz) {
        if (*p < 0x80) {
            switch (*p) {
                case '&':  o += snprintf(out+o, out_sz-o, "&amp;");  break;
                case '<':  o += snprintf(out+o, out_sz-o, "&lt;");   break;
                case '>':  o += snprintf(out+o, out_sz-o, "&gt;");   break;
                case '"':  o += snprintf(out+o, out_sz-o, "&quot;"); break;
                default:   out[o++] = *p; break;
            }
            p++;
        } else {
            size_t len = 0; unsigned int cp = 0;
            if ((*p & 0xE0)==0xC0 && is_cont(p[1])) {
                len = 2; cp = ((p[0]&0x1F)<<6)|(p[1]&0x3F);
            } else if ((*p & 0xF0)==0xE0 && is_cont(p[1]) && is_cont(p[2])) {
                len = 3; cp = ((p[0]&0x0F)<<12)|((p[1]&0x3F)<<6)|(p[2]&0x3F);
            } else if ((*p & 0xF8)==0xF0
                       && is_cont(p[1]) && is_cont(p[2]) && is_cont(p[3])) {
                len = 4; cp = ((p[0]&0x07)<<18)
                             | ((p[1]&0x3F)<<12)
                             | ((p[2]&0x3F)<<6)
                             |  (p[3]&0x3F);
            }
            if (len) {
                o += snprintf(out+o, out_sz-o, "&#%u;", cp);
                p += len;
            } else {
                p++;
            }
        }
    }
    out[o] = '\0';
}

int csv_processor_calculate_file_conceptual_hash(const char *filepath, char *output_hash_str, size_t hash_str_len) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        LOG_ERROR("Failed to open '%s' to calculate hash. Error: %s", filepath, strerror(errno));
        return 1;
    }

    unsigned long checksum = 0;
    char buffer[4096];
    size_t bytesRead = 0;

    LOG_DEBUG("Starting hash calculation for '%s'.", filepath);
    while ((bytesRead = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        for (size_t i = 0; i < bytesRead; i++) {
            checksum = (checksum << 1) + (unsigned char)buffer[i];
        }
        LOG_DEBUG("Read %zu bytes for hash calculation.", bytesRead);
    }

    if (ferror(fp)) {
        LOG_ERROR("Error reading file '%s' for hash calculation. Error: %s", filepath, strerror(errno));
        fclose(fp);
        return 1;
    }

    snprintf(output_hash_str, hash_str_len, "%0*lX", (int)(hash_str_len - 1), checksum);
    output_hash_str[hash_str_len - 1] = '\0';

    LOG_DEBUG("Calculated conceptual hash for '%s': %s", filepath, output_hash_str);
    fclose(fp);
    return 0;
}

// Helper function to attempt download from a given host/port/path
static int attempt_download(const char* host, const char* port, const char* path) {
    LOG_INFO("Attempting CSV download from %s:%s%s", host, port, path);
    struct addrinfo hints = { .ai_family=AF_UNSPEC, .ai_socktype=SOCK_STREAM },
                    *res, *rp;
    int sock = -1, rv;
    int reuse_addr = 1;

    LOG_DEBUG("Resolving hostname '%s'...", host);
    if ((rv = getaddrinfo(host, port, &hints, &res)) != 0) {
        LOG_INFO("DNS resolution for %s failed: %s", host, gai_strerror(rv));
        return 1;
    }
    LOG_DEBUG("Hostname '%s' resolved. Attempting to connect...", host);

    for (rp = res; rp; rp = rp->ai_next) {
        char ip_str[INET6_ADDRSTRLEN];
        void *addr;
        if (rp->ai_family == AF_INET) {
            addr = &((struct sockaddr_in *)rp->ai_addr)->sin_addr;
        } else { // AF_INET6
            addr = &((struct sockaddr_in6 *)rp->ai_addr)->sin6_addr;
        }
        inet_ntop(rp->ai_family, addr, ip_str, sizeof(ip_str));
        LOG_DEBUG("Trying to connect to IP:Port %s:%s (Family: %d, Type: %d, Protocol: %d)", ip_str, port, rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0) {
            LOG_DEBUG("Failed to create socket for address family %d: %s. Trying next...", rp->ai_family, strerror(errno));
            continue;
        }

        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) < 0) {
            LOG_WARN("setsockopt(SO_REUSEADDR) failed for client socket: %s", strerror(errno));
        }

        struct sockaddr_in client_bind_addr = {0};
        client_bind_addr.sin_family = AF_INET;
        client_bind_addr.sin_addr.s_addr = INADDR_ANY;
        client_bind_addr.sin_port = 0;

        if (bind(sock, (const struct sockaddr *)&client_bind_addr, sizeof(client_bind_addr)) < 0) {
            LOG_ERROR("Failed to bind client socket to ephemeral port: %s", strerror(errno));
            close(sock);
            sock = -1;
            continue;
        }
        LOG_DEBUG("Client socket bound to ephemeral port.");

        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
            LOG_DEBUG("Successfully connected to %s:%s.", ip_str, port);
            break;
        }
        LOG_DEBUG("Connection failed to %s:%s: %s. Trying next address...", ip_str, port, strerror(errno));
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);

    if (sock < 0) {
        LOG_INFO("Could not connect to %s:%s. No usable address found or all connections failed.", host, port);
        return 1;
    }
    LOG_DEBUG("Connection established. Preparing HTTP GET request.");

    char req[512];
    int n_req = snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    if (n_req >= (int)sizeof(req) || n_req < 0) {
        LOG_ERROR("HTTP request string too long or snprintf error, requested size %d, buffer size %zu.", n_req, sizeof(req));
        close(sock);
        return 1;
    }
    ssize_t sent_bytes = send(sock, req, n_req, 0);
    if (sent_bytes < 0) {
        LOG_ERROR("Failed to send HTTP GET request to %s:%s: %s", host, port, strerror(errno));
        close(sock);
        return 1;
    }
    LOG_DEBUG("Sent %zd bytes HTTP GET request:\n%s", sent_bytes, req);

    FILE *fp = fopen(PB_CSV_TEMP_PATH, "wb");
    if (!fp) {
        LOG_ERROR("Failed to open temp file %s for writing: %s", PB_CSV_TEMP_PATH, strerror(errno));
        close(sock);
        return 1;
    }
    LOG_DEBUG("Temporary file '%s' opened for writing downloaded CSV.", PB_CSV_TEMP_PATH);

    char buf[4096];
    ssize_t len_read;
    int http_status_code = 0;
    size_t total_bytes_read = 0;
    bool status_line_read = false;
    char header_buffer[4096] = {0};
    size_t header_buffer_len = 0;

    LOG_DEBUG("Starting HTTP response read loop. Writing to %s.", PB_CSV_PATH);
    while ((len_read = read(sock, buf, sizeof(buf))) > 0) {
        // if (!keep_running) { // REMOVED
        //     LOG_WARN("Download interrupted by shutdown signal. Read %zd bytes.", len_read);
        //     break;
        // }
        LOG_DEBUG("Received %zd bytes from socket.", len_read);

        if (!status_line_read) {
            size_t copy_len = (header_buffer_len + len_read < sizeof(header_buffer)) ? len_read : (sizeof(header_buffer) - header_buffer_len -1);
            memcpy(header_buffer + header_buffer_len, buf, copy_len);
            header_buffer_len += copy_len;
            header_buffer[header_buffer_len] = '\0';

            char *end_of_line = strstr(header_buffer, "\r\n");
            char *body_start = strstr(header_buffer, "\r\n\r\n");

            if (end_of_line && body_start) {
                size_t status_line_len = end_of_line - header_buffer;
                char status_line[256] = {0};
                strncpy(status_line, header_buffer, (status_line_len < sizeof(status_line)-1) ? status_line_len : sizeof(status_line)-1);
                status_line[sizeof(status_line)-1] = '\0';

                LOG_DEBUG("Received complete HTTP Status Line: '%s'", status_line);
                if (sscanf(status_line, "HTTP/%*f %d", &http_status_code) != 1) {
                    LOG_ERROR("Failed to parse HTTP status code from '%s'.", status_line);
                    fclose(fp); close(sock); remove(PB_CSV_PATH); return 1;
                }

                if (http_status_code != 200) {
                    LOG_ERROR("HTTP download failed with status code %d: '%s'.", http_status_code, status_line);
                    fclose(fp); close(sock); remove(PB_CSV_PATH); return 1;
                }
                LOG_DEBUG("Parsed HTTP Status Code: %d. Headers received.", http_status_code);
                status_line_read = true;

                size_t header_len_total = body_start - header_buffer + 4;
                if (len_read > header_len_total) {
                     fwrite(buf + header_len_total, 1, len_read - header_len_total, fp);
                     total_bytes_read += (size_t)(len_read - header_len_total);
                     LOG_DEBUG("Wrote %zu bytes (body part of initial chunk) to CSV. Total: %zu.", (size_t)(len_read - header_len_total), total_bytes_read);
                }
            } else if (header_buffer_len >= sizeof(header_buffer) -1) {
                LOG_ERROR("HTTP header too large or missing end of headers (\\r\\n\\r\\n). Header buffer exhausted.");
                fclose(fp); close(sock); remove(PB_CSV_PATH); return 1;
            } else {
                 LOG_DEBUG("Partial HTTP header received (%zu bytes). Waiting for more data for status line/body split.", header_buffer_len);
            }
        } else {
            fwrite(buf, 1, len_read, fp);
            total_bytes_read += len_read;
            LOG_DEBUG("Appended %zd bytes to CSV. Total: %zu.", len_read, total_bytes_read);
        }
    }
    fclose(fp);
    close(sock);

    if (len_read < 0) {
        LOG_ERROR("Error reading from socket during download: %s", strerror(errno));
        remove(PB_CSV_PATH);
        return 1;
    } else if (total_bytes_read == 0 && http_status_code == 200) {
        LOG_WARN("Downloaded CSV is empty (0 bytes body), despite 200 OK status. File: %s", PB_CSV_PATH);
    } else if (!status_line_read) {
        LOG_ERROR("HTTP response was too short or malformed; no complete status line/headers found. Received %zu bytes.", total_bytes_read);
        remove(PB_CSV_PATH);
        return 1;
    } else if (http_status_code != 200) {
        LOG_ERROR("HTTP download failed: Invalid status code %d. File not saved. Final bytes: %zu.", http_status_code, total_bytes_read);
        remove(PB_CSV_PATH);
        return 1;
    }

    LOG_INFO("CSV downloaded successfully to %s. Total bytes: %zu.", PB_CSV_PATH, total_bytes_read);
    LOG_DEBUG("Finished CSV download process for %s:%s%s.", host, port, path);
    return 0;
}


int csv_processor_download_csv(void) {
    for (int i = 0; i < g_num_phonebook_servers; i++) {
        const ConfigurableServer *current_server = &g_phonebook_servers_list[i];
        LOG_INFO("Attempting download from server %d: %s", i + 1, current_server->host);
        if (attempt_download(current_server->host, current_server->port, current_server->path) == 0) {
            LOG_INFO("Download successful from server %s.", current_server->host);
            return 0;
        } else {
            LOG_WARN("Download failed from server %s. Trying next server.", current_server->host);
        }
    }
    LOG_ERROR("All configured phonebook servers failed to provide CSV. Download failed completely.");
    return 1;
}


int csv_processor_convert_csv_to_xml_and_get_path(char *output_path, size_t output_path_len) {
    LOG_INFO("Starting CSV to XML conversion from %s...", PB_CSV_PATH);
    if (access(PB_CSV_PATH, R_OK) != 0) {
        LOG_ERROR("CSV file %s not found or not readable. Error: %s", PB_CSV_PATH, strerror(errno));
        return 1;
    }

    strncpy(output_path, PB_XML_BASE_PATH, output_path_len - 1);
    output_path[output_path_len - 1] = '\0';

    FILE *xml = fopen(output_path, "w");
    if (!xml) {
        LOG_ERROR("Failed to open base xml file for writing '%s'. Error: %s", output_path, strerror(errno));
        return 1;
    }

    FILE *csv = fopen(PB_CSV_PATH, "r");
    if (!csv) {
        LOG_ERROR("Failed to open CSV for reading '%s'. Error: %s", PB_CSV_PATH, strerror(errno));
        fclose(xml);
        remove(output_path);
        return 1;
    }

    fprintf(xml, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<YealinkIPPhoneDirectory>\n");
    char line[2048];
    int ln = 0;
    LOG_DEBUG("Starting CSV parsing loop. Current line number: %d.", ln);
    while (fgets(line, sizeof(line), csv)) {
        // if (!keep_running) { // REMOVED
        //     LOG_WARN("CSV conversion interrupted by shutdown signal.");
        //     break;
        // }
        if (ln++ == 0) {
            LOG_DEBUG("Skipping CSV header row (line %d): '%.*s'", ln, (int)strcspn(line, "\r\n"), line);
            continue;
        }

        int line_len = strcspn(line, "\r\n");
        LOG_DEBUG("Processing line %d: '%.*s...'", ln,
                 (line_len > 30) ? 30 : line_len, line);

        char *cols[5] = {NULL};
        char *p = line;
        for (int i = 0; i < 5; i++) {
            if (i < 4) {
                char *c = strchr(p, ',');
                if (c) {
                    *c = '\0';
                    cols[i] = p;
                    p = c + 1;
                } else {
                    cols[i] = p;
                    p = NULL;
                }
            } else {
                cols[i] = p;
            }
            if (!p && i < 4) {
                LOG_WARN("Line %d has fewer than 5 columns. Missing column %d and subsequent. Line: '%.*s'", ln, i+1, (int)strcspn(line, "\r\n"), line);
                break;
            }
        }

        if (cols[4]) {
            char *e = strchr(cols[4], ',');
            if (e) *e = '\0';
            cols[4][strcspn(cols[4], "\r\n")] = '\0';
        }

        if (!cols[4] || !*cols[4]) {
            LOG_WARN("Skipping line %d due to missing or empty Telephone number (column 5). Line: '%.*s'", ln, (int)strcspn(line, "\r\n"), line);
            continue;
        }

        char s0[MAX_FIRST_NAME_LEN] = {0};
        char s1[MAX_NAME_LEN] = {0};
        char s2[MAX_CALLSIGN_LEN] = {0};
        sanitize_utf8(cols[0] ? cols[0] : "", s0, sizeof(s0));
        sanitize_utf8(cols[1] ? cols[1] : "", s1, sizeof(s1));
        sanitize_utf8(cols[2] ? cols[2] : "", s2, sizeof(s2));

        // Adjust buffer size calculation based on new max lengths, plus overhead for spaces and parentheses
        char full_name_raw[MAX_FIRST_NAME_LEN + MAX_NAME_LEN + MAX_CALLSIGN_LEN + 5]; // +5 for " ()" and null
        if (s0[0] && s1[0] && s2[0]) {
            snprintf(full_name_raw, sizeof(full_name_raw), "%s %s (%s)", s0, s1, s2);
        } else if (s0[0] && s1[0]) {
            snprintf(full_name_raw, sizeof(full_name_raw), "%s %s", s0, s1);
        } else if (s0[0]) {
            strncpy(full_name_raw, s0, sizeof(full_name_raw) - 1);
            full_name_raw[sizeof(full_name_raw) - 1] = '\0';
        } else {
            strncpy(full_name_raw, "Unnamed", sizeof(full_name_raw) - 1);
            full_name_raw[sizeof(full_name_raw) - 1] = '\0';
        }

        // The esc_name buffer size remains generous as XML escaping can greatly expand string length
        char esc_name[MAX_DISPLAY_NAME_LEN * 4 + 32];
        xml_escape(full_name_raw, esc_name, sizeof(esc_name));

        fprintf(xml, "  <DirectoryEntry>\n    <Name>%s</Name>\n    <Telephone>%s</Telephone>\n  </DirectoryEntry>\n",
                esc_name, cols[4]);
        LOG_DEBUG("Added XML entry for Telephone: '%s'", cols[4]);
    }

    if (ferror(csv)) {
        LOG_ERROR("Error reading CSV file '%s' during conversion. Error: %s", PB_CSV_PATH, strerror(errno));
        fclose(csv);
        fclose(xml);
        remove(output_path);
        return 1;
    }

    fclose(csv);
    fflush(xml);
    fsync(fileno(xml));
    fclose(xml);
    LOG_INFO("XML conversion successful. Output: %s.", output_path);
    LOG_DEBUG("Finished CSV to XML conversion process.");
    return 0;
}
