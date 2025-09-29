#!/bin/bash
# Simple test to verify our C code compiles

echo "Testing local compilation of AREDN-Phonebook..."

cd Phonebook/src

# Test basic compilation with gcc
echo "Testing with regular gcc..."
gcc -I. -static \
    main.c \
    call-sessions/call_sessions.c \
    user_manager/user_manager.c \
    phonebook_fetcher/phonebook_fetcher.c \
    sip_core/sip_core.c \
    status_updater/status_updater.c \
    file_utils/file_utils.c \
    csv_processor/csv_processor.c \
    log_manager/log_manager.c \
    config_loader/config_loader.c \
    passive_safety/passive_safety.c \
    -lpthread \
    -o test-aredn-phonebook

if [ $? -eq 0 ]; then
    echo "✅ Local compilation successful!"
    ls -la test-aredn-phonebook
    rm test-aredn-phonebook
else
    echo "❌ Local compilation failed!"
    echo "This indicates source code issues that need to be fixed."
fi