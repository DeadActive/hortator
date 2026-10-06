# Versions of the drum firmware

The drum firmware is numbered `MAJOR.MINOR.PATCH`, starting at **0.5.0** (2026-10-06). The number lives in one
place, the file `VERSION` at the top of the repository; `CHANGELOG.md` says what each version changed.

## What changes which number

- **PATCH** (0.5.0 → 0.5.1): fixes only. No new feature, no new control; projects and settings stay as they are.
- **MINOR** (0.5.1 → 0.6.0): a new feature or control, or a new project format that still loads the older
  projects (e.g. FDR7 for the song chain). Most roadmap steps are minors.
- **MAJOR** (0.x → 1.0.0, 1.x → 2.0.0): projects or settings saved by the previous version no longer load, or the
  controls change so much that old habits break. 1.0.0 is also the version the user declares finished and stable;
  until then the major stays 0.

A merge with both fixes and a feature is a minor. Upstream Felucca's own numbers (its `v1.0.2` tags, `--release
X.Y` builds) are separate and unchanged.

## Where it shows

- The FM-1: MENU → ABOUT shows `DRUM-<version>`; the USB console (`status`, the greeting) prints it too.
- The local web installer (`build/site`): `drum-<version>+<commit>`, with `-dirty` when the source had
  uncommitted changes. Between versions, builds keep the last version and differ by the commit.
- Git: the local tag `drum-v<version>` on the commit that set it (upstream's `v*` tags are a different line).

## Releasing a version (when work is merged to `main`)

1. Add a line per user-visible change under `## Unreleased` in `CHANGELOG.md`.
2. `python3 tools/version.py bump patch` (or `minor` / `major`): writes `VERSION`, dates the notes.
3. Commit `VERSION` and `CHANGELOG.md`, then `git tag drum-v<version>`.
4. `DRUM_PACKAGE=1 ./build.sh`: the package and `build/site` carry the new number; the host suite checks it
   (`tools/version.py check build`).
