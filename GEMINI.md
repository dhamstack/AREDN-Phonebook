# Gemini Project Context: AREDN Phonebook

## Project Overview

This project contains the source code for the **AREDN Phonebook**, an emergency-resilient, multi-threaded SIP proxy server designed for [Amateur Radio Emergency Data Network (AREDN)](https://www.arednmesh.org/) mesh networks.

The application is written in **C** and is intended to run on resource-constrained OpenWRT-based routers. Its primary purpose is to provide a centralized, self-healing phonebook directory and call routing service that is robust enough for emergency communications. It automatically fetches phonebook data from other mesh nodes, manages SIP user registrations, and provides a web-accessible XML phonebook for SIP phones.

Key architectural features include:
*   **Multi-threaded C Architecture**: A main thread for SIP processing, and background threads for phonebook fetching, status updates, and a "passive safety" system that monitors and recovers hung threads.
*   **Resilience and Self-Healing**: Designed to survive power outages, with features like an "emergency boot" that loads a cached phonebook, flash-friendly file operations to preserve router memory, and automatic cleanup of stale call sessions.
*   **OpenWRT Integration**: Deployed as an `.ipk` package with a standard `init.d` service script for management and a `/etc/sipserver.conf` file for configuration.

## Building and Running

The primary method for building this project is through the **OpenWRT SDK**, which is automated via GitHub Actions.

### CI/CD Build (Preferred Method)

The project is built and packaged using GitHub Actions, as defined in `.github/workflows/`.

*   **Release Builds (`build-ipk.yml`)**: Triggered by pushing a version tag (e.g., `v1.2.3`). This workflow builds `.ipk` packages for multiple architectures (`ath79`, `x86`, `ipq40xx`), creates a GitHub Release, and attaches the packages.
*   **Development Builds (`dev-build.yml`)**: Triggered by pushing to the `development` branch or by pushing tags like `dev-*` or `test-*`. This builds an `.ipk` for the `ath79` architecture and uploads it as a temporary build artifact.

### Local Development Build

To build locally, you must have the appropriate OpenWRT SDK set up. The process is defined in `Phonebook/Makefile`.

1.  **Navigate to the SDK directory.**
2.  **Copy the project source** into the `package/` directory of the SDK.
3.  **Run the build commands:**
    ```bash
    # From within the SDK root directory
    make defconfig
    make package/aredn-phonebook/compile V=s
    ```
    The resulting `.ipk` file will be in the `sdk/bin/packages/` directory.

### Running the Service

On an OpenWRT device where the `.ipk` is installed, the application runs as a service.

*   **Service Management**:
    ```bash
    /etc/init.d/AREDN-Phonebook start
    /etc/init.d/AREDN-Phonebook stop
    /etc/init.d/AREDN-Phonebook restart
    /etc/init.d/AREDN-Phonebook status
    ```
*   **Configuration**: The service is configured via `/etc/sipserver.conf`.
*   **Logs**: Logs can be viewed using the `logread` command on the router (e.g., `logread | grep "AREDN-Phonebook"`).

## Development Conventions

*   **Language**: The project is written entirely in C.
*   **Modularity**: The code is organized into modules based on functionality (e.g., `sip_core`, `phonebook_fetcher`, `passive_safety`). Each module has its own `.c` and `.h` file.
*   **Concurrency**: The application is heavily multi-threaded and uses `pthread` mutexes and condition variables for synchronization. Pay close attention to thread safety when modifying shared data structures like `registered_users` and `call_sessions`.
*   **Logging**: A custom logging system (`log_manager`) is used. Use the `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`, and `LOG_DEBUG` macros for output.
*   **Commit Style**: Commits appear to follow the [Conventional Commits](https://www.conventionalcommits.org/) specification (e.g., `fix(ci): ...`, `feat(...): ...`).
*   **Testing**: The repository contains workflows for build verification (`minimal-test.yml`, `test-build.yml`) but does not appear to have a dedicated unit testing framework for the C code itself. Testing is primarily done through build success and manual verification.
