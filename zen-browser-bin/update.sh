# Zen Browser: latest stable GitHub release, with GitHub's sha256
latest() {
    local asset=zen.linux-x86_64.tar.xz json tag
    json=$(curl -fsSL https://api.github.com/repos/zen-browser/desktop/releases/latest)
    tag=$(jq -r .tag_name <<<"$json")
    echo "$tag" \
        "https://github.com/zen-browser/desktop/releases/download/$tag/$asset" \
        "$(jq -r --arg n "$asset" '.assets[] | select(.name == $n) | .digest // "" | ltrimstr("sha256:")' <<<"$json")"
}
