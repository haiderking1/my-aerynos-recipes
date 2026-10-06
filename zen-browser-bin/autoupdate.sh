#!/usr/bin/env bash
# Rebuilds zen-browser-bin into the local moss repo when a new Zen release is out.
# Afterwards `sudo moss sync -u` picks it up along with everything else.
set -euo pipefail

RECIPE_DIR="${RECIPE_DIR:-$HOME/aeryn-recipes/zen-browser-bin}"
REPO_DIR="${REPO_DIR:-$HOME/.cache/local_repo/x86_64}"
ASSET="zen.linux-x86_64.tar.xz"

cd "$RECIPE_DIR"

release_json="$(curl -fsSL https://api.github.com/repos/zen-browser/desktop/releases/latest)"
latest="$(jq -r .tag_name <<<"$release_json")"
digest="$(jq -r --arg name "$ASSET" \
    '.assets[] | select(.name == $name) | .digest // "" | ltrimstr("sha256:")' <<<"$release_json")"

current="$(sed -nE 's/^version *: *"?([^"]+)"?/\1/p' stone.yaml)"

if [[ "$latest" == "$current" ]]; then
    echo "zen-browser-bin is up to date ($current)"
    exit 0
fi

echo "Updating zen-browser-bin: $current -> $latest"
cp stone.yaml stone.yaml.bak
trap 'mv -f stone.yaml.bak stone.yaml; echo "Update failed, recipe restored" >&2' ERR

boulder recipe update -y --ver "$latest" \
    -u "https://github.com/zen-browser/desktop/releases/download/$latest/$ASSET"

# Make sure the hash boulder computed matches the one GitHub publishes
if [[ -n "$digest" ]] && ! grep -q "$digest" stone.yaml; then
    echo "sha256 mismatch with GitHub release digest ($digest)" >&2
    false
fi

rm -f ./*.stone
boulder build -y stone.yaml

rm -f "$REPO_DIR"/zen-browser-bin-*.stone
cp zen-browser-bin-*.stone "$REPO_DIR"/
moss index "$REPO_DIR"

trap - ERR
rm -f stone.yaml.bak
echo "Built zen-browser-bin $latest, run 'sudo moss sync -u' to install it"
