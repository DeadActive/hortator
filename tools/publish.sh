#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
# Publish a version, built here (docs/VERSIONING.md "Publishing"); nothing is built in the cloud.
#   tools/publish.sh [TAG]      TAG: drum-v<version> (default: drum-v<VERSION.txt>)
# 1. builds TAG exactly, in its own checkout (build/publish/src, a git worktree): the simulator (tools/build_sim.sh)
#    and the firmware package with the site (DRUM_PACKAGE=1 ./build.sh); needs what BUILDING.md lists (Docker on)
# 2. pushes main and TAG to REMOTE (default: origin, github.com/DeadActive/hortator)
# 3. a GitHub release "Hortator <version>" on TAG: hortator-<version>.fwsc, LICENSE, LICENSING.md, LICENSES.zip and
#    the notes of CHANGELOG.md (a release already there: its files replaced)
# 4. GitHub Pages: that build's site (landing page + simulator, web installer, editor) as the next commit of the
#    gh-pages branch (Pages turned on from it the first time)
# Needs gh, logged in.
set -e
cd "$(dirname "$0")/.."
ROOT=$PWD
REMOTE=${REMOTE:-origin}
TAG=${1:-drum-v$(cat VERSION.txt)}
case "$TAG" in drum-v*) ;; *) echo "publish: $TAG is not a drum-v* tag"; exit 1 ;; esac
V=${TAG#drum-v}
git rev-parse -q --verify "refs/tags/$TAG^{commit}" >/dev/null || { echo "publish: no tag $TAG"; exit 1; }
REPO=$(git remote get-url "$REMOTE" | sed -E 's#^(https://github.com/|git@github.com:)##; s#\.git$##')
SHA=$(git rev-parse --short "$TAG^{commit}")
D=$ROOT/build/publish
S=$D/src
W=$D/pages

# 1. the build, of the tag only
git worktree remove --force "$S" 2>/dev/null || rm -rf "$S"
git worktree remove --force "$W" 2>/dev/null || rm -rf "$W"
git worktree prune
mkdir -p "$D"
git worktree add -q --detach "$S" "$TAG"
trap 'cd "$ROOT"; git worktree remove --force "$S" 2>/dev/null; git worktree remove --force "$W" 2>/dev/null; true' EXIT
[ "$(cat "$S/VERSION.txt")" = "$V" ] || { echo "publish: $TAG's VERSION.txt is $(cat "$S/VERSION.txt")"; exit 1; }
(cd "$S" && tools/build_sim.sh && DRUM_PACKAGE=1 ./build.sh)
PKG=$(ls "$S"/build/site/firmware/felucca-drum-"$V"-"$SHA".fwsc 2>/dev/null | head -1)
[ -n "$PKG" ] || { echo "publish: the build made no package felucca-drum-$V-$SHA.fwsc"; exit 1; }
[ -f "$S/build/site/fm1sim.wasm" ] || { echo "publish: the site has no landing page (simulator)"; exit 1; }

mkdir -p "$D/files"
rm -f "$D"/files/*
cp "$PKG" "$D/files/hortator-$V.fwsc"
cp "$S/LICENSE" "$S/LICENSING.md" "$D/files/"
(cd "$S/build/site/firmware" && zip -qr "$D/files/LICENSES.zip" LICENSES)
python3 - "$V" "$REPO" "$S/CHANGELOG.md" > "$D/notes.md" <<'PY'
import re, sys
v, repo, log = sys.argv[1:4]
out, on = [], False
for line in open(log, encoding="utf-8"):
    m = re.match(r"## (\S+)", line)
    if m:
        on = m[1] == v
        continue
    if on:
        out.append(line.rstrip("\n"))
owner, name = repo.split("/")
print("\n".join(out).strip() or "(no notes)")
print()
print(f"Install: https://{owner.lower()}.github.io/{name}/webapp/installer/ (Chrome or Edge, USB), or "
      "tools/fm1_install.py with the .fwsc below. The package holds JieLi SDK files under Apache-2.0: LICENSING.md, "
      "LICENSES.zip.")
PY

# 2. the code
echo "publish: $TAG ($SHA) to $REPO"
git push "$REMOTE" main "refs/tags/$TAG"

# 3. the release
set -- "$D/files/hortator-$V.fwsc" "$D/files/LICENSE" "$D/files/LICENSING.md" "$D/files/LICENSES.zip"
if gh release view "$TAG" -R "$REPO" >/dev/null 2>&1; then
    gh release upload "$TAG" "$@" -R "$REPO" --clobber
    gh release edit "$TAG" -R "$REPO" --title "Hortator $V" --notes-file "$D/notes.md"
else
    gh release create "$TAG" "$@" -R "$REPO" --title "Hortator $V" --notes-file "$D/notes.md" --verify-tag
fi

# 4. the site: the next commit of gh-pages
if git fetch -q "$REMOTE" gh-pages 2>/dev/null; then
    git worktree add -q --detach "$W" FETCH_HEAD
else
    git worktree add -q --detach "$W" "$TAG"
    git -C "$W" checkout -q --orphan gh-pages
fi
git -C "$W" rm -rqf --ignore-unmatch .
cp -R "$S/build/site/." "$W/"
touch "$W/.nojekyll"                                  # served as they are
git -C "$W" add -A
git -C "$W" commit -qm "Hortator $V ($SHA): landing page, installer, editor" || echo "publish: the site is unchanged"
git -C "$W" push -q "$REMOTE" HEAD:refs/heads/gh-pages
gh api "repos/$REPO/pages" >/dev/null 2>&1 ||                 # (a first gh-pages push can turn Pages on itself)
    gh api -X POST "repos/$REPO/pages" -f "source[branch]=gh-pages" -f "source[path]=/" >/dev/null 2>&1 ||
    gh api "repos/$REPO/pages" >/dev/null
echo "publish: release https://github.com/$REPO/releases/tag/$TAG"
echo "publish: site    $(gh api "repos/$REPO/pages" --jq .html_url)"
