## What changed

<!-- One or two sentences. Link the issue this closes, if any. -->

## Why

<!-- The behaviour before, the behaviour after, and why the new behaviour is correct. -->

## How it was tested

- [ ] `ctest --preset msvc-x64-release --output-on-failure` passes
- [ ] Strict build passes (`SHADOWDPI_WARNINGS_AS_ERRORS=ON`,
      `SHADOWDPI_REQUIRE_WINDIVERT_RUNTIME=ON`)
- [ ] Added or updated tests for the changed behaviour
- [ ] Manually exercised on a real connection, if this touches the packet path

## Docs

- [ ] `README.md` and `docs/` still match the code
- [ ] `CHANGELOG.md` updated under `Unreleased` if user-visible

## Notes for reviewers

<!-- Tradeoffs, follow-up work, or anything you are unsure about. -->
