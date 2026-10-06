# Cursor: stable release from Cursor's download API (no checksum published)
latest() {
    curl -fsSL 'https://www.cursor.com/api/download?platform=linux-x64&releaseTrack=stable' \
        | jq -r '"\(.version) \(.debUrl)"'
}
