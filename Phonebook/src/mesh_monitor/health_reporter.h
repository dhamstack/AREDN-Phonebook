#ifndef HEALTH_REPORTER_H
#define HEALTH_REPORTER_H

/**
 * Export network monitoring data to JSON file
 *
 * @param filepath Path to output JSON file (e.g., /tmp/meshmon_network.json)
 * @return 0 on success, -1 on failure
 */
int export_network_to_json(const char *filepath);

#endif // HEALTH_REPORTER_H
