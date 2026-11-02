# Test fixtures

This directory contains in-game Satisfactory blueprints used as regression
fixtures for Conveyor Lift Auto-Connect Plus.

The fixture blueprints are **test data**, not required by the packaged mod.
They are stored in Git so the exact regression topology does not get lost as
the implementation evolves.

## What to commit

For every fixture blueprint, commit the blueprint file and its matching config
file when present:

- `*.sbp`
- `*.sbpcfg`

Preserve the filenames that Satisfactory created. These files are binary test
assets and should not be edited by hand.

## Installing a fixture locally

Copy the fixture files into the blueprint directory for a Satisfactory save,
then start/reload the game and select the blueprint in the Blueprint build menu.
Blueprint storage is save/session-specific, so the final directory component on
your machine depends on the save being used.

Typical Windows root:

`%LOCALAPPDATA%\\FactoryGame\\Saved\\SaveGames\\blueprints\\<session>/`

If Satisfactory is running while files are copied, reload/restart before assuming
the files were not recognized.

## Regression fixture

`VerticalAutoConnectRegression/` is intended to hold the complete U/D/T/V/R
fixture described in the repository root `TESTING.md`.

When the fixture changes intentionally, update `TESTING.md` in the same commit so
that the binary test data and its expected results remain synchronized.
