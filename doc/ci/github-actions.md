# GitHub Actions CI

This document describes the GitHub Actions workflows in `.github/workflows/`.

## What it does

The repository uses three workflow entrypoints:

- `Smoke`
  - runs smoke tests on pull requests targeting `devel`
  - runs the same smoke jobs on pushes to `devel`
- `RPM Build`
  - validates the RPM build path in a Fedora container on pull requests targeting `devel`
  - runs again on pushes to `devel`
- `Regression`
  - runs heavier jobs only when dispatched manually

There is also a `PR Commands` workflow that turns pull request comments into
workflow dispatches.

## Workflows

### `smoke.yml`

Runs one smoke job on `ubuntu-latest`.

Each job:

1. checks out the requested ref
2. runs `autogen.sh`
3. installs the development libraries needed for the default shipped configure path
4. runs `configure --prefix=/usr/local`
5. builds Gluster
6. installs it
7. runs the smoke selection with `run-tests.sh`
8. uploads regression output and Gluster logs when available

The smoke selection is:

- `tests/basic/*.t`
- `tests/basic/afr/*.t`
- `tests/basic/distribute/*.t`
- `tests/bugs/fb*.t`
- `tests/features/brick-min-free-space.t`

### `rpm-build.yml`

Runs the packaging validation path in a Fedora container on `ubuntu-latest`:

1. checks out the requested ref
2. installs Fedora build dependencies from `glusterfs.spec`
3. runs `autogen.sh`
4. runs `configure --prefix=/usr/local`
5. runs `make dist`
6. runs `make -C extras/LinuxRPM glusterrpms_without_autogen`
7. uploads generated RPMs when present

### `regression.yml`

Runs only by manual dispatch and currently supports one mode:

- `full`

`full` runs local `run-tests.sh` on `ubuntu-latest`.

## PR comment commands

`pr-commands.yml` accepts these pull request comments:

- `/recheck smoke`
  - dispatches `smoke.yml` against the PR head SHA
- `/run regression`
  - dispatches `regression.yml` with `mode=full` against the PR head SHA

Only trusted users can trigger these commands:

- `/recheck smoke`
  - `OWNER`, `MEMBER`, or `COLLABORATOR`
- `/run regression`
  - `OWNER` or `MEMBER`

## Trigger policy

- Pull requests to `devel`
  - run smoke and RPM validation automatically
- Pushes to `devel`
  - run smoke and RPM validation automatically
- Full regression
  - manual only

## Runner requirements

The workflows use GitHub-hosted runners:

- `ubuntu-latest` for build, smoke, and manual regression
- a Fedora container inside `ubuntu-latest` for RPM validation

These jobs still require:

- `sudo` for install and test steps on Ubuntu jobs
- local daemon startup and mount operations required by `run-tests.sh`

## Current scope

These workflows cover the GitHub-hosted subset of the old CI model:

- automatic smoke on pull requests
- automatic smoke and RPM validation on pushes to `devel`
- comment-triggered smoke rechecks
- comment-triggered manual full regression

They do not attempt to recreate the former dedicated multi-host regression
hardware inside GitHub-hosted Actions.
