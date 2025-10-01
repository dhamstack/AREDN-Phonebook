#define MODULE_NAME "HTTP_CLIENT"

#include "http_client.h"
#include "../log_manager/log_manager.h"
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define HTTP_TIMEOUT_SEC 10
#define MAX_REQUEST_SIZE 8192

// Parse URL into host, port, path
static int parse_url(const char *url, char *host, int *port, char *path) {
    // Simple parser for http://host:port/path
    if (strncmp(url, "http://", 7) != 0) {
        return -1;
    }

    const char *start = url + 7;
    const char *slash = strchr(start, '/');
    const char *colon = strchr(start, ':');

    // Extract path
    if (slash == NULL) {
        strcpy(path, "/");
        slash = start + strlen(start);
    } else {
        strncpy(path, slash, 255);
        path[255] = '\0';
    }

    // Extract host and port
    if (colon && colon < slash) {
        // Has port
        int host_len = colon - start;
        if (host_len > 255) host_len = 255;
        strncpy(host, start, host_len);
        host[host_len] = '\0';
        *port = atoi(colon + 1);
    } else {
        // Default port 80
        int host_len = slash - start;
        if (host_len > 255) host_len = 255;
        strncpy(host, start, host_len);
        host[host_len] = '\0';
        *port = 80;
    }

    return 0;
}

int http_post_json(const char *url, const char *json_data) {
    char host[256];
    int port;
    char path[256];

    if (!url || !json_data) {
        LOG_ERROR("Invalid arguments to http_post_json");
        return -1;
    }

    if (parse_url(url, host, &port, path) != 0) {
        LOG_ERROR("Invalid URL: %s", url);
        return -1;
    }

    LOG_DEBUG("HTTP POST to %s:%d%s", host, port, path);

    // Create socket (same pattern as OLSR HTTP client)
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        LOG_ERROR("Failed to create socket: %s", strerror(errno));
        return -1;
    }

    // Set timeouts
    struct timeval timeout;
    timeout.tv_sec = HTTP_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    // Resolve hostname
    struct hostent *server = gethostbyname(host);
    if (server == NULL) {
        LOG_ERROR("Failed to resolve host: %s", host);
        close(sockfd);
        return -1;
    }

    // Setup server address
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(port);

    // Connect
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        LOG_ERROR("Failed to connect to %s:%d: %s", host, port, strerror(errno));
        close(sockfd);
        return -1;
    }

    // Build HTTP POST request
    int content_length = strlen(json_data);
    char *request = malloc(MAX_REQUEST_SIZE);
    if (!request) {
        LOG_ERROR("Failed to allocate request buffer");
        close(sockfd);
        return -1;
    }

    int request_len = snprintf(request, MAX_REQUEST_SIZE,
        "POST %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        path, host, content_length, json_data);

    if (request_len >= MAX_REQUEST_SIZE) {
        LOG_ERROR("Request too large for buffer");
        free(request);
        close(sockfd);
        return -1;
    }

    // Send request
    ssize_t sent = send(sockfd, request, request_len, 0);
    free(request);

    if (sent < 0) {
        LOG_ERROR("Failed to send HTTP POST: %s", strerror(errno));
        close(sockfd);
        return -1;
    }

    // Read response (just check for success)
    char response[512];
    ssize_t bytes = recv(sockfd, response, sizeof(response) - 1, 0);
    close(sockfd);

    if (bytes > 0) {
        response[bytes] = '\0';
        // Look for HTTP 200 OK or 202 Accepted
        if (strstr(response, "200") || strstr(response, "202")) {
            LOG_DEBUG("HTTP POST successful");
            return 0;
        }
        LOG_WARN("HTTP POST returned non-200 response");
    }

    // Don't fail if we can't parse response - POST was sent
    return 0;
}
