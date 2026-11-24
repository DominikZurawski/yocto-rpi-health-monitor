# RPi Sentinel requirements

## Functional requirements

| ID | Requirement | Acceptance criterion |
| --- | --- | --- |
| F-01 | Provide a minimal custom image for Raspberry Pi 3. | The image boots from microSD, exposes a local console, and starts Sentinel automatically. |
| F-02 | Sample CPU temperature, RAM usage, and uptime. | Compare readings against the corresponding Linux interfaces, accounting for sampling delay and units. Missing data produces a degraded snapshot. |
| F-03 | Support configurable temperature and RAM thresholds with hysteresis. | Verify activation at the upper threshold, clearance at the lower boundary, and rejection of invalid configurations. Uptime is included as an informational metric. |
| F-04 | Log transitions and publish a local state snapshot. | The journal records alarm and availability transitions. Concurrent readers observe complete previous or new JSON snapshots after initial publication. |
| F-05 | Recover the process and the system through separate watchdogs. | On-device tests demonstrate restart after a crash and a stalled loop, and the start-rate limit after repeated failures. A separate test demonstrates a hardware reset when keepalives cease and automatic service startup after reboot. |

## Non-functional requirements

| ID | Requirement | Acceptance criterion |
| --- | --- | --- |
| NF-01 | Operate offline under a dedicated unprivileged account. | Sampling, alarm evaluation, local publication, and supervision operate with the network disconnected. The service runs with a non-root UID. |
| NF-02 | Limit storage writes and keep the image console-based. | The periodic snapshot and the journal use volatile storage. The package manifest excludes the graphical stack and target development toolchain. |
| NF-03 | Provide host and device verification. | CTest exercises parsers, validation, hysteresis, and failed reads. Device testing covers boot, live metrics, snapshot publication, and both recovery mechanisms. Record the tested image, board model, and outcomes. |
| NF-04 | Rebuild from documented and pinned inputs. | A second build from a clean build directory with the same pinned sources and configuration produces a matching package manifest and a bootable image. Bit-for-bit SD image comparison is a separate check. |

## Scope

The MVP delivers one AArch64 image, a single C service built with CMake,
local configuration and diagnostics, bounded service restart, hardware
watchdog recovery, host unit tests, and Raspberry Pi acceptance tests.

Optional extensions include persistent event history, remote telemetry,
A/B updates with rollback, additional metrics, and a 32-bit build
profile.
