# T3 Code: newest nightly pre-release on GitHub, with GitHub's sha256.
# Nightlies land several times a day, so update at most once a day.
MIN_AGE_HOURS=20

latest() {
    local json version asset
    json=$(curl -fsSL 'https://api.github.com/repos/pingdotgg/t3code/releases?per_page=30' \
        | jq '[.[] | select(.prerelease and (.tag_name | test("nightly")))] | sort_by(.published_at) | last')
    version=$(jq -r '.tag_name | ltrimstr("v")' <<<"$json")
    asset="T3-Code-$version-amd64.deb"
    echo "$version" \
        "https://github.com/pingdotgg/t3code/releases/download/v$version/$asset" \
        "$(jq -r --arg n "$asset" '.assets[] | select(.name == $n) | .digest // "" | ltrimstr("sha256:")' <<<"$json")"
}
