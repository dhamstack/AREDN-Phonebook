#define MODULE_NAME "HEALTH_REPORTER"

#include "health_reporter.h"
#include "mesh_monitor.h"
#include "routing_adapter.h"
#include "../log_manager/log_manager.h"
#include "../common.h"
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

// Get local node name from hostname
static void get_node_name(char *buffer, size_t size) {
    if (gethostname(buffer, size) != 0) {
        strncpy(buffer, "unknown", size - 1);
        buffer[size - 1] = '\0';
    }
}

// Format timestamp in ISO8601 format
static void format_timestamp(time_t t, char *buffer, size_t size) {
    struct tm tm_info;
    gmtime_r(&t, &tm_info);
    strftime(buffer, size, "%Y-%m-%dT%H:%M:%SZ", &tm_info);
}

int export_network_to_json(const char *filepath) {
    if (!filepath) {
        LOG_ERROR("Invalid filepath for network JSON export");
        return -1;
    }

    if (!is_mesh_monitor_enabled()) {
        LOG_DEBUG("Mesh monitor not enabled, skipping network JSON export");
        return -1;
    }

    // Get recent probe results
    probe_result_t probes[PROBE_HISTORY_SIZE];
    int probe_count = get_recent_probes(probes, PROBE_HISTORY_SIZE);

    if (probe_count == 0) {
        LOG_DEBUG("No probe data available yet");
        return -1;
    }

    // Open temporary file for atomic write
    char temp_path[512];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", filepath);

    FILE *fp = fopen(temp_path, "w");
    if (!fp) {
        LOG_ERROR("Failed to open temp file for network JSON: %s", temp_path);
        return -1;
    }

    // Get node name
    char node_name[256];
    get_node_name(node_name, sizeof(node_name));

    // Get current timestamp
    char timestamp[32];
    format_timestamp(time(NULL), timestamp, sizeof(timestamp));

    // Write JSON header
    fprintf(fp, "{\n");
    fprintf(fp, "  \"schema\": \"meshmon.v1\",\n");
    fprintf(fp, "  \"type\": \"network_status\",\n");
    fprintf(fp, "  \"node\": \"%s\",\n", node_name);
    fprintf(fp, "  \"sent_at\": \"%s\",\n", timestamp);
    fprintf(fp, "  \"routing_daemon\": \"%s\",\n", get_routing_daemon_name());
    fprintf(fp, "  \"probe_count\": %d,\n", probe_count);
    fprintf(fp, "  \"probes\": [\n");

    // Write probe results
    for (int i = 0; i < probe_count; i++) {
        probe_result_t *p = &probes[i];

        char probe_time[32];
        format_timestamp(p->timestamp, probe_time, sizeof(probe_time));

        fprintf(fp, "    {\n");
        fprintf(fp, "      \"dst_node\": \"%s\",\n", p->dst_node);
        fprintf(fp, "      \"dst_ip\": \"%s\",\n", p->dst_ip);
        fprintf(fp, "      \"timestamp\": \"%s\",\n", probe_time);
        fprintf(fp, "      \"routing_daemon\": \"%s\",\n", p->routing_daemon);
        fprintf(fp, "      \"rtt_ms_avg\": %.2f,\n", p->rtt_ms_avg);
        fprintf(fp, "      \"jitter_ms\": %.2f,\n", p->jitter_ms);
        fprintf(fp, "      \"loss_pct\": %.2f,\n", p->loss_pct);
        fprintf(fp, "      \"hop_count\": %d,\n", p->hop_count);
        fprintf(fp, "      \"path\": [\n");

        // Write hop information
        for (int h = 0; h < p->hop_count && h < MAX_HOPS; h++) {
            fprintf(fp, "        {\n");
            fprintf(fp, "          \"node\": \"%s\",\n", p->hops[h].node);
            fprintf(fp, "          \"interface\": \"%s\",\n", p->hops[h].interface);
            fprintf(fp, "          \"link_type\": \"%s\",\n", p->hops[h].link_type);
            fprintf(fp, "          \"lq\": %.2f,\n", p->hops[h].lq);
            fprintf(fp, "          \"nlq\": %.2f,\n", p->hops[h].nlq);
            fprintf(fp, "          \"etx\": %.2f,\n", p->hops[h].etx);
            fprintf(fp, "          \"rtt_ms\": %.2f\n", p->hops[h].rtt_ms);
            fprintf(fp, "        }%s\n", (h < p->hop_count - 1) ? "," : "");
        }

        fprintf(fp, "      ]\n");
        fprintf(fp, "    }%s\n", (i < probe_count - 1) ? "," : "");
    }

    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");

    fclose(fp);

    // Atomic rename
    if (rename(temp_path, filepath) != 0) {
        LOG_ERROR("Failed to rename temp network JSON to %s", filepath);
        unlink(temp_path);
        return -1;
    }

    LOG_DEBUG("Network status exported to %s (%d probes)", filepath, probe_count);
    return 0;
}
