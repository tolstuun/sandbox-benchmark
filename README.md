# sandbox-benchmark

`sandbox-benchmark` is a Windows-first utility for transparent sandbox benchmarking. Its purpose is to run clearly selected, measurement-only checks and record what happened without changing behavior based on what is observed.

## Safe Scope

The project is limited to:

- passive environment and fidelity checks
- selected bounded timing measurements
- selected bounded user-presence checks

The project does not include:

- anti-debugging
- anti-instrumentation
- anti-disassembly
- anti-dump behavior
- code injection
- process termination
- stealth techniques
- branching on detections
- evasion-oriented behavior changes

## Architecture

- `configurator/`: C# WinForms application on .NET 10 for selecting checks, managing profiles, and driving local build flow.
- `runner/`: C++17 native runner built with CMake and MSVC. The runner loads a runtime profile, resolves requested checks from an internal registry, executes registered checks, and logs results.
- `checks/`: definitions and organization for safe check categories and metadata.
- `profiles/`: JSON profile data with selected check IDs and logging/output settings.
- `artifacts/`: generated build outputs and packaged runner artifacts.
- `logs/`: runtime logs and result files produced by the runner.
- `docs/`: project documentation, including architecture and roadmap.

## Operating Principles

- The project stays Windows-first.
- The runner always executes the selected checks and records results.
- Observations are logged only; they do not change execution flow or disable checks.
- Changes should be small, reviewable, and documented when they affect project behavior or structure.

## Status

The repository currently contains:

- initial configurator and runner scaffolding
- a minimal profile contract in [profiles/default.json](/C:/dev/sandbox-benchmark/profiles/default.json)
- a minimal runtime profile-loading path from `profiles/default.json`
- a minimal internal check registry and execution pipeline
- a minimal runner result contract with console output and JSON output in `logs/results.json`
- two synthetic demo checks, `demo.runner_start` and `demo.profile_loaded`, to validate registry resolution and execution

No real environment, VM, sandbox, or evasion checks are implemented yet.

See [docs/architecture.md](/C:/dev/sandbox-benchmark/docs/architecture.md) and [docs/roadmap.md](/C:/dev/sandbox-benchmark/docs/roadmap.md) for the planned structure and phased delivery.
