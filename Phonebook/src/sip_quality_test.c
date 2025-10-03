/*
 * sip_quality_test.c - CLI wrapper for SIP OPTIONS quality testing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sip_quality_lib.h"

typedef struct {
    const char *number;
    const char *ip;
    const char *name;
} phone_entry_t;

static const phone_entry_t default_phones[] = {
    {"441530", "10.197.143.20", "HB9BLA-1 on VM-1 LAN"},
    {"441533", "10.51.55.234",  "HB9BLA-4 on HAP-2 LAN"},
    {"648730", "10.32.73.134",  "HB9TSI remote phone"},
    {NULL, NULL, NULL}
};

void print_usage(const char *prog) {
    printf("Usage: %s [options] <phone_number> [phone_ip]\n", prog);
    printf("   or: %s --test-all\n", prog);
    printf("\nOptions:\n");
    printf("  --test-all          Test all default phones\n");
    printf("  --timeout <ms>      INVITE timeout in ms (default: 5000)\n");
    printf("  --burst <ms>        RTP burst duration in ms (default: 1200)\n");
    printf("  --ptime <ms>        RTP packet interval in ms (default: 40)\n");
    printf("\nDefault phones:\n");
    for (int i = 0; default_phones[i].number; i++) {
        printf("  %s (%s) - %s\n",
               default_phones[i].number,
               default_phones[i].ip,
               default_phones[i].name);
    }
    printf("\nExamples:\n");
    printf("  %s 441530                    # Quick test\n", prog);
    printf("  %s --test-all                # Test all phones\n", prog);
    printf("  %s --timeout 10000 441530    # 10s timeout\n", prog);
}

void print_result(const char *phone_number, const char *phone_ip,
                  const voip_probe_result_t *result) {
    printf("\n=== Phone %s (%s) ===\n", phone_number, phone_ip);
    printf("Status: %s\n", voip_probe_status_str(result->status));

    if (result->status == VOIP_PROBE_SUCCESS) {
        printf("  Media RTT:    %ld ms\n", result->media_rtt_ms);
        printf("  Jitter:       %.2f ms\n", result->jitter_ms);
        printf("  Loss:         %.1f%% (%u/%u packets)\n",
               result->loss_fraction * 100.0,
               result->packets_lost,
               result->packets_sent);
    } else {
        printf("  Reason: %s\n", result->status_reason);
    }
}

const char* lookup_ip(const char *phone_number) {
    for (int i = 0; default_phones[i].number; i++) {
        if (strcmp(default_phones[i].number, phone_number) == 0) {
            return default_phones[i].ip;
        }
    }
    return NULL;
}

int test_one_phone(const char *phone_number, const char *phone_ip,
                   voip_probe_config_t *config) {
    voip_probe_result_t result;

    printf("Testing %s (%s)...\n", phone_number, phone_ip);
    fflush(stdout);

    int rc = test_phone_quality(phone_number, phone_ip, &result, config);

    print_result(phone_number, phone_ip, &result);

    return (result.status == VOIP_PROBE_SUCCESS) ? 0 : 1;
}

int test_all_phones(voip_probe_config_t *config) {
    int failed = 0;

    for (int i = 0; default_phones[i].number; i++) {
        if (test_one_phone(default_phones[i].number,
                          default_phones[i].ip,
                          config) != 0) {
            failed++;
        }
    }

    printf("\n=== Summary ===\n");
    printf("Total: 3 phones, Failed: %d\n", failed);

    return failed;
}

int main(int argc, char *argv[]) {
    voip_probe_config_t config = get_default_config();
    const char *phone_number = NULL;
    const char *phone_ip = NULL;
    int test_all = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--test-all") == 0) {
            test_all = 1;
        } else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
            config.invite_timeout_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--burst") == 0 && i + 1 < argc) {
            config.burst_duration_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--ptime") == 0 && i + 1 < argc) {
            config.rtp_ptime_ms = atoi(argv[++i]);
        } else if (argv[i][0] != '-') {
            if (!phone_number) {
                phone_number = argv[i];
            } else if (!phone_ip) {
                phone_ip = argv[i];
            }
        }
    }

    if (!test_all && !phone_number) {
        fprintf(stderr, "Error: No phone number specified\n\n");
        print_usage(argv[0]);
        return 1;
    }

    if (test_all) {
        return test_all_phones(&config);
    } else {
        if (!phone_ip) {
            phone_ip = lookup_ip(phone_number);
            if (!phone_ip) {
                fprintf(stderr, "Error: Unknown phone number '%s'. Please provide IP address.\n",
                        phone_number);
                return 1;
            }
        }

        return test_one_phone(phone_number, phone_ip, &config);
    }
}
