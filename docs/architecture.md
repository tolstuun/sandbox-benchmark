# Architecture

## Overview

`sandbox-benchmark` is split into a Windows GUI configurator and a native runner. The configurator prepares selections and build inputs. The runner executes the selected safe checks and records results without changing behavior in response to detections.

## Configurator

The configurator is a C# WinForms application targeting .NET 10 on Windows. Its role is to:

- load the default profile from disk
- display and edit core profile fields
- display and edit the selected check list
- save profile selections back to disk
- run the local CMake runner build
- generate an embedded-profile source header for the runner build
- copy the built runner and a small manifest into `artifacts/`

The current configurator is intentionally minimal. It edits `profiles/default.json` directly, generates a small embedded-profile header in the runner tree, runs the local runner build commands, and stages the built executable plus a manifest in a timestamped artifact folder.

## Builder Flow

The intended build flow is:

1. The operator selects checks and options in the configurator.
2. The configurator resolves or writes the selected profile/configuration data.
3. The configurator invokes the runner build pipeline with local process execution.
4. On success, the configurator writes a timestamped artifact folder under `artifacts/` containing the runner and `manifest.json`.
5. The produced runner is executed separately and writes logs to `logs/`.

This keeps configuration, build, execution, and result collection explicit and inspectable.

## Runner

The runner is a C++17 executable built with CMake and MSVC. Its role is to:

- load the selected profile from disk
- support `--profile <path>` overrides
- fall back to the embedded profile for normal artifact execution
- resolve requested checks through an internal registry
- execute every selected check
- measure and collect results
- write structured output to logs

The runner must not change execution flow based on detections. It records observations only.

The current runner contract includes a minimal `CheckResult` model with:

- `check_id`
- `status`
- `evidence`
- `started_at`
- `finished_at`

The current status values are:

- `detected`
- `not_detected`
- `error`
- `unsupported`

The current runner flow reads `profiles/default.json`, executes only the requested registered checks, and emits `unsupported` results for unknown requested check IDs. The current registered checks are:

- `demo.runner_start`
- `demo.profile_loaded`
- `env.cpu.logical_processor_count`
- `env.memory.total_physical_mb`
- `env.storage.system_drive_total_gb`
- passive Pafish-derived CPU, generic sandbox, Sandboxie, Wine, QEMU, Bochs, VirtualBox, and VMware checks

## Checks Model

Checks are organized as explicit, safe benchmark units. Each check should eventually define:

- a stable identifier
- a human-readable name and description
- inputs or bounds, if any
- result fields produced by execution

Checks remain limited to passive environment or fidelity observation, bounded timing measurement, and bounded user-presence measurement. Unsafe categories are out of scope.

The current passive Pafish-derived checks only inspect raw system facts and artifacts such as CPUID values, usernames, file paths, registry keys, files, devices, windows, process names, MAC prefixes, and selected WMI strings. They do not alter execution flow.

## Profiles

Profiles represent saved selections for repeatable runs. A profile is expected to capture:

- `profile_id`
- `name`
- `version`
- `checks`
- `output_directory`
- `console_logging_enabled`
- `json_logging_enabled`

Profiles allow the configurator and runner to produce consistent benchmark runs without hidden logic.

The runner currently loads a profile from `--profile <path>` when provided. Otherwise, it uses the embedded profile compiled into the executable. It uses the loaded profile's `checks`, `output_directory`, `console_logging_enabled`, and `json_logging_enabled` values directly.

## Artifacts And Logs

`artifacts/` holds generated outputs from the build flow, including copied runner binaries and per-build `manifest.json` files. `logs/` holds runtime records produced by the runner, including benchmark results and execution metadata.

The initial JSON log target is still `logs/results.json` through the default profile. It is intentionally simple and local so the project can stabilize the registry, profile-loading, and result contracts before adding real checks.

Artifacts and logs should remain easy to inspect, easy to retain, and easy to compare across runs.
