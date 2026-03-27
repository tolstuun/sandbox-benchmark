# Roadmap

## Phase 1: Infrastructure

- Establish the repository rules, documentation baseline, and Windows-first build structure.
- Keep the configurator and runner building independently.
- Define the top-level directory responsibilities for checks, profiles, artifacts, and logs.

## Phase 2: Config/Profile Model

- Define a minimal profile schema for selected checks and bounded parameters.
- Load the default profile at runner startup.
- Add configurator support for loading, editing, and saving profiles.
- Decide how the runner receives profile data during build or execution.

## Phase 3: Runner Result Schema And Logging

- Define a stable result schema for run metadata, per-check outputs, timing, and errors.
- Add the first minimal check registry and requested-check execution flow.
- Implement consistent log file naming and output location behavior.
- Make runner logging explicit and deterministic so benchmark output is easy to review.

## Phase 4: First Safe Checks

- Add the first passive environment and fidelity checks.
- Record raw values only for the initial environment-fidelity checks, with no thresholds or detection logic.
- Add the first bounded timing measurements.
- Add the first bounded user-presence checks.
- Keep every check transparent, measurement-only, and free of behavior changes based on observations.

## Phase 5: Configurator Build Integration

- Connect configurator actions to the runner build workflow.
- Stage the built runner and a minimal manifest under `artifacts/`.
- Surface profile selection, build status, and artifact location in the UI.
- Add basic guardrails so only safe, supported benchmark configurations can be produced.
