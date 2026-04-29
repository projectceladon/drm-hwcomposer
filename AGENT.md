# AGENT.md

## OpenCode Scope

This file is the OpenCode/general-agent entry point for `vendor/intel/external/drm-hwcomposer`. Paths are relative to this repo unless explicitly noted.

The comprehensive local guidance lives in `.github/copilot-instructions.md`. Read that file for manifest identity, repo purpose, directory map, build modules, edit workflow, validation, debugging, safety risks, and related repos.

## Priority

- Follow explicit user instructions first.
- For OpenCode/general-agent behavior in this repo, use this file plus `.github/copilot-instructions.md`.
- For GitHub Copilot behavior in this repo, use `.github/copilot-instructions.md`.
- For whole-workspace architecture and cross-repo rules, use the workspace root `.github/copilot-instructions.md`.
- Preserve existing Android, AOSP, Intel, and upstream style when local guidance is absent.

## OpenCode-Specific Workflow

- Build context before editing; do not assume this repo is standalone.
- Keep changes minimal and in this owning repo.
- Do not edit generated files, prebuilts, release snapshots, or binary artifacts unless explicitly requested.
- Do not modify source/build/test files when the task is documentation-only.
- Do not create commits unless explicitly requested.
- Do not run destructive git commands.
