#!/usr/bin/env bash
# Rebuilds cursor-bin into the local moss repo when a new Cursor release is out.
# Afterwards `sudo moss sync -u` picks it up along with everything else.
set -euo pipefail

RECIPE_DIR="${RECIPE_DIR:-$HOME/aeryn-recipes/cursor-bin}"
REPO_DIR="${REPO_DIR:-$HOME/.cache/local_repo/x86_64}"

cd "$RECIPE_DIR"

release_json="$(curl -fsSL 'https://www.cursor.com/api/download?platform=linux-x64&releaseTrack=stable')"
latest="$(jq -r .version <<<"$release_json")"
url="$(jq -r .debUrl <<<"$release_json")"

current="$(sed -nE 's/^version *: *"?([^"]+)"?/\1/p' stone.yaml)"

if [[ "$latest" == "$current" ]]; then
    echo "cursor-bin is up to date ($current)"
    exit 0
fi

echo "Updating cursor-bin: $current -> $latest"
cp stone.yaml stone.yaml.bak
trap 'mv -f stone.yaml.bak stone.yaml; echo "Update failed, recipe restored" >&2' ERR

# Cursor publishes no checksum; boulder downloads the deb and records its sha256
boulder recipe update -y --ver "$latest" -u "$url"

rm -f ./*.stone
boulder build -y stone.yaml

rm -f "$REPO_DIR"/cursor-bin-*.stone
cp cursor-bin-*.stone "$REPO_DIR"/
moss index "$REPO_DIR"

trap - ERR
rm -f stone.yaml.bak
echo "Built cursor-bin $latest, run 'sudo moss sync -u' to install it"

# Publish the updated recipe. The package is already built, so a failed push only warns.
if git add -- . && git commit -q -m "cursor-bin: Update to $latest" -- . && git push -q; then
    echo "Pushed cursor-bin $latest to GitHub"
else
    echo "warning: could not push the cursor-bin update to GitHub" >&2
fi
