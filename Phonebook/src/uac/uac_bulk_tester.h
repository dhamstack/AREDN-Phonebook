// uac_bulk_tester.h - UAC Bulk Testing Thread
#ifndef UAC_BULK_TESTER_H
#define UAC_BULK_TESTER_H

#include <pthread.h>

/**
 * UAC Bulk Testing Thread
 *
 * Periodically tests all registered users from the phonebook:
 * - Loops through all registered users
 * - For each user, checks if DNS resolves (<phone_number>.local.mesh)
 * - If DNS resolves (node is reachable), triggers UAC test call
 * - Waits for configured interval before next test cycle
 */

/**
 * Start UAC bulk testing thread
 * @return 0 on success, -1 on failure
 */
void *uac_bulk_tester_thread(void *arg);

#endif // UAC_BULK_TESTER_H
