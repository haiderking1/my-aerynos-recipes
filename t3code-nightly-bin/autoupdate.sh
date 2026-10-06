#!/usr/bin/env bash
# Rebuilds t3code-nightly-bin into the local moss repo when a newer nightly is out.
# Afterwards `sudo moss sync -u` picks it up along with everything else.
set -euo pipefail

RECIPE_DIR="${RECIPE_DIR:-$HOME/aeryn-recipes/t3code-nightly-bin}"
REPO_DIR="${REPO_DIR:-$HOME/.cache/local_repo/x86_64}"
# Nightlies land several times a day; update at most once per this many hours
MIN_AGE_HOURS="${MIN_AGE_HOURS:-20}"

cd "$RECIPE_DIR"

age_hours=$(( ($(date +%s) - $(stat -c %Y stone.yaml)) / 3600 ))
if (( age_hours < MIN_AGE_HOURS )); then
    echo "t3code-nightly-bin was updated ${age_hours}h ago, next check after ${MIN_AGE_HOURS}h"
    exit 0
fi

releases_json="$(curl -fsSL 'https://api.github.com/repos/pingdotgg/t3code/releases?per_page=30')"
release_json="$(jq '[.[] | select(.prerelease and (.tag_name | test("nightly")))]
    | sort_by(.published_at) | last' <<<"$releases_json")"
latest="$(jq -r '.tag_name | ltrimstr("v")' <<<"$release_json")"
asset="T3-Code-$latest-amd64.deb"
digest="$(jq -r --arg name "$asset" \
    '.assets[] | select(.name == $name) | .digest // "" | ltrimstr("sha256:")' <<<"$release_json")"

current="$(sed -nE 's/^version *: *"?([^"]+)"?/\1/p' stone.yaml)"

if [[ "$latest" == "$current" ]]; then
    echo "t3code-nightly-bin is up to date ($current)"
    exit 0
fi

echo "Updating t3code-nightly-bin: $current -> $latest"
cp stone.yaml stone.yaml.bak
trap 'mv -f stone.yaml.bak stone.yaml; echo "Update failed, recipe restored" >&2' ERR

boulder recipe update -y --ver "$latest" \
    -u "https://github.com/pingdotgg/t3code/releases/download/v$latest/$asset"

# Make sure the hash boulder computed matches the one GitHub publishes
if [[ -n "$digest" ]] && ! grep -q "$digest" stone.yaml; then
    echo "sha256 mismatch with GitHub release digest ($digest)" >&2
    false
fi

rm -f ./*.stone
boulder build -y stone.yaml

rm -f "$REPO_DIR"/t3code-nightly-bin-*.stone
cp t3code-nightly-bin-*.stone "$REPO_DIR"/
moss index "$REPO_DIR"

trap - ERR
rm -f stone.yaml.bak
echo "Built t3code-nightly-bin $latest, run 'sudo moss sync -u' to install it"

# Publish the updated recipe. The package is already built, so a failed push only warns.
if git add -- . && git commit -q -m "t3code-nightly-bin: Update to $latest" -- . && git push -q; then
    echo "Pushed t3code-nightly-bin $latest to GitHub"
else
    echo "warning: could not push the t3code-nightly-bin update to GitHub" >&2
fi
