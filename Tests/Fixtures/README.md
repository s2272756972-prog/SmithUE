# API contract fixtures

This directory contains stable JSON fixtures for SmithUE CLI mock tests.

- `api-contract.json` captures representative request/response pairs for the `/api/v1/execute` contract.
- The fixture is intentionally small and additive-only so tests can compare expected shapes without relying on a live editor.

## How to update

1. Start the Unreal Editor with SmithUE enabled.
2. Re-run the real command flow that changed.
3. Capture the live HTTP response payloads.
4. Update `api-contract.json` to keep the same top-level shape unless the API contract really changed.
5. Re-run the CLI mock tests that read this file.

## Notes

- CLI mock tests in `smithue-cli/tests/unit/` reference this file.
- Error samples should keep `error_code` values aligned with the plugin's current enum set.