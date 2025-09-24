# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

AREDN-Phonebook1 is a SIP proxy server designed for AREDN (Amateur Radio Emergency Data Network) mesh networks. The server provides phonebook functionality by fetching CSV phonebook data from AREDN mesh servers and managing SIP user registrations and call routing.

## Build System

This project uses OpenWRT's build system to create `.ipk` packages for AREDN routers.

### Local Development Commands

```bash
# Navigate to the Phonebook directory
cd Phonebook

# Manual compilation for testing (requires OpenWRT SDK setup)
make defconfig
make package/phonebook/compile V=s
```

### GitHub Actions Build

The project uses GitHub Actions to automatically build `.ipk` packages:
- Triggered on tag pushes (format: `*.*.*`) and pull requests
- Builds for `ath79/generic` and `x86/64` architectures
- Uses OpenWRT SDK 23.05.3
- Output: `.ipk` files attached to GitHub releases

## Architecture

### Core Components

The application follows a modular C architecture with these main modules:

- **main.c**: Entry point, UDP socket handling, thread coordination
- **sip_core/**: SIP message processing, REGISTER/INVITE handling, call routing
- **phonebook_fetcher/**: Downloads CSV phonebook from mesh servers
- **user_manager/**: Manages registered SIP users and authentication
- **call-sessions/**: Tracks active call sessions and routing
- **status_updater/**: Updates user active/inactive status based on phonebook
- **csv_processor/**: Parses phonebook CSV files
- **config_loader/**: Loads configuration from `/etc/sipserver.conf`
- **file_utils/**: File I/O utilities
- **log_manager/**: Centralized logging system

### Threading Model

The application uses a multi-threaded architecture:
- Main thread: SIP message processing via UDP socket
- Phonebook fetcher thread: Periodic CSV downloads
- Status updater thread: User status management
- Thread synchronization via mutexes and condition variables

### Configuration

Runtime configuration is handled via `/etc/sipserver.conf`:
- SIP handler process priority
- Phonebook fetch intervals
- Status update intervals
- Multiple phonebook server definitions

## File Structure

```
Phonebook/
├── Makefile              # OpenWRT package build configuration
├── src/                  # C source code
│   ├── main.c           # Application entry point
│   ├── common.h         # Shared headers and constants
│   └── [modules]/       # Modular components (*.c + *.h)
└── files/
    └── etc/
        ├── init.d/      # OpenWRT init scripts
        └── sipserver.conf # Default configuration
```

## Key Constants

Defined in `Phonebook/src/common.h`:
- `SIP_PORT`: 5060 (standard SIP port)
- `MAX_SIP_MSG_LEN`: 2048 bytes
- `MAX_REGISTERED_USERS`: Configurable limit
- CSV field length limits for phonebook entries

## Development Notes

- The application is designed for OpenWRT/AREDN embedded environments
- Uses POSIX threading and standard C libraries
- No external dependencies beyond system libraries
- Phonebook data format: CSV with FirstName,Name,Callsign,PhoneNumber fields
- SIP implementation handles basic REGISTER and INVITE messages for call routing