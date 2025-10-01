#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

// Lightweight HTTP POST client for remote reporting
// Uses pure POSIX sockets - no external dependencies

/**
 * Send JSON data via HTTP POST
 * @param url Full URL (e.g., "http://collector.local.mesh:5000/ingest")
 * @param json_data JSON string to send
 * @return 0 on success, -1 on error
 */
int http_post_json(const char *url, const char *json_data);

#endif // HTTP_CLIENT_H
