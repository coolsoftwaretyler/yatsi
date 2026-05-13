---
description: Build the native yatsi CLI and run the custom test suite
disable-model-invocation: true
allowed-tools: Bash
---

## Build & Test

Build the native CLI binary and run all custom tests against it.

### Steps

1. Build the CLI (never skip this part, it's trivial and important that we have the latest build, even if it looks like there are no changes in git)

```bash
./build-cli.sh
```

This runs a standard CMake build (not Emscripten) and produces `engine/build-cli/yatsi_cli`.

2. Run the test suite:

```bash
npm test
```

This executes `tsx scripts/run-tests.ts`, which:
- Discovers `.ts` / `.expected` file pairs under `src/custom/tests/`
- Runs each `.ts` file through the `yatsi_cli` binary
- Compares stdout against the `.expected` file
- Reports PASS/FAIL per test with a summary

### When to rebuild

Run `./build-cli.sh` after any change to C++ source files under `engine/src/`. The test runner (`scripts/run-tests.ts`) and test files (`src/custom/tests/`) don't require a rebuild.
