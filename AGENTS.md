# Repository Operating Rules

This repository is for a Windows-first sandbox benchmark utility. Keep all design and implementation decisions aligned to that goal.

## Core Rules

- Keep the project Windows-first across tooling, UX, build flow, and runtime behavior.
- Implement only transparent benchmark behavior.
- Limit scope to passive environment and fidelity checks, plus selected bounded measurement-only timing and user-presence checks.
- Do not add anti-debug, anti-instrumentation, anti-disassembly, anti-dump, code injection, process killing, stealth, branching on detections, or evasion-oriented behavior changes.
- The runner must always execute the selected checks and log their results.
- Do not branch or alter runtime behavior based on detections. Observations are recorded, not used to evade or suppress execution.
- Do not add check implementations outside the approved safe scope.
- Prefer small, iterative changes over large refactors.
- Update documentation for every change when applicable.
- Before finishing work, run the builds and tests that exist and report the outcome.

## Architecture Expectations

- `configurator/` contains the .NET 10 WinForms configuration and build UI.
- `runner/` contains the C++17 runner built with CMake and MSVC.
- `checks/` defines safe check specifications and metadata, not unsafe capabilities.
- `profiles/` stores saved selections and runner configuration inputs.
- `artifacts/` stores generated build outputs intended for review and use.
- `logs/` stores runner output for later inspection.
- `docs/` holds architecture, roadmap, and other project documentation.

## Change Discipline

- Keep outputs inspectable and easy to reason about.
- Favor explicit configuration and stable logging over implicit behavior.
- If a proposed change would make the tool less transparent or more evasive, do not implement it.
