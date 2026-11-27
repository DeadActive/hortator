# GRIND MACHINE upstream

**Status: Foundation integration in progress.** The GitHub fork and its main commit have been verified against the approved pin. The local preparation history is unrelated to Hortator and must not be pushed; only the prepared file contents are transferred onto the actual fork.

- Upstream: https://github.com/DeadActive/hortator (public, read access confirmed via GitHub connector).
- Approved baseline: [44c9ad4aa57bebf63f412bd552e57143c30b4065](https://github.com/DeadActive/hortator/commit/44c9ad4aa57bebf63f412bd552e57143c30b4065).
- Verified previously: tree `ff6d99c5c5ec82411016f1de02d4e0b5519d563f`; branch `main`. Recheck SHA at actual checkout. No claim that it is the newest release.
- Verified fork: `DaFum/grind-machine`, parent `DeadActive/hortator`; main points to the approved baseline SHA. Integration is scoped to `foundation/pinned-baseline`.
- Lineage: Hortator is derived from Felucca. Preserve upstream Git ancestry and all copyright/SPDX notices. GPL-3.0-only covers the relevant code; not every asset. See [upstream LICENSING.md](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/LICENSING.md).
- No changes to upstream firmware, loaders, assets or hardware have been made in this preparation.

## Resume on a genuine fork

The fork has been created by the user and its parent verified through the GitHub API. Check asset rights before redistribution; a GitHub fork is not a general asset licence. Do not create an unrelated repository in place of a fork.

On an authorized build host, after confirming the fork parent:

```sh
git clone https://github.com/DaFum/grind-machine.git grind-machine-fw
cd grind-machine-fw
git remote add upstream https://github.com/DeadActive/hortator.git
git fetch upstream main --tags
git cat-file -t 44c9ad4aa57bebf63f412bd552e57143c30b4065
git switch --track origin/foundation/pinned-baseline
git rev-parse HEAD
```

Expected: `commit`, followed by the integration branch head; verify the approved pin is its ancestor with `git merge-base --is-ancestor 44c9ad4aa57bebf63f412bd552e57143c30b4065 HEAD`. If the fork's main has advanced, do not reset or force-push it; reconcile the integration branch policy first. The prepared files are integrated via GitHub's Git Data API; the native local checkout is still blocked by DNS. Review the draft PR and follow P01 build and audit tasks on a qualified host. Keep upstream remote read-only; update the pin explicitly in a separate reviewed change, rerunning the baseline gates.
