# Architecture

## Overview

`sandbox-benchmark` is split into a Windows GUI configurator and a native runner. The configurator prepares selections and build inputs. The runner executes the selected safe checks and records results without changing behavior in response to detections.

## Configurator

The configurator is a C# WinForms application targeting .NET 10 on Windows. Its role is to:

- load the default profile from disk
- display and edit core profile fields
- display and edit the selected check list
- save profile selections back to disk

The current configurator is intentionally minimal. It edits `profiles/default.json` directly and does not yet orchestrate builds.

## Builder Flow

The intended build flow is:

1. The operator selects checks and options in the configurator.
2. The configurator resolves or writes the selected profile/configuration data.
3. The configurator invokes the runner build pipeline.
4. The build output is written to `artifacts/`.
5. The produced runner is executed separately and writes logs to `logs/`.

This keeps configuration, build, execution, and result collection explicit and inspectable.

## Runner

The runner is a C++17 executable built with CMake and MSVC. Its role is to:

- load the selected profile from disk
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

## Checks Model

Checks are organized as explicit, safe benchmark units. Each check should eventually define:

- a stable identifier
- a human-readable name and description
- inputs or bounds, if any
- result fields produced by execution

Checks remain limited to passive environment or fidelity observation, bounded timing measurement, and bounded user-presence measurement. Unsafe categories are out of scope.

The first environment-fidelity checks are value-only observations. They record processor count, total physical memory, and system drive size without thresholds or interpretation logic.

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

The runner currently loads `profiles/default.json` at runtime and uses its `checks`, `output_directory`, `console_logging_enabled`, and `json_logging_enabled` values directly.

## Artifacts And Logs

`artifacts/` holds generated outputs from the build flow, such as packaged runner binaries and related files. `logs/` holds runtime records produced by the runner, including benchmark results and execution metadata.

The initial JSON log target is still `logs/results.json` through the default profile. It is intentionally simple and local so the project can stabilize the registry, profile-loading, and result contracts before adding real checks.

Artifacts and logs should remain easy to inspect, easy to retain, and easy to compare across runs.
