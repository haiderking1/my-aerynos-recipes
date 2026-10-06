# my-aerynos-recipes

Personal [boulder](https://github.com/AerynOS/os-tools) recipes for [AerynOS](https://aerynos.com),
for software that is not in the official repos.

| Package | What it is |
|---|---|
| `audio-panel` | Small GTK4 sound menu for Waybar: output/input devices with volume sliders (C, my own) |
| `cursor-bin` | [Cursor](https://cursor.com) editor, repackaged from the official `.deb` |
| `zen-browser-bin` | [Zen Browser](https://zen-browser.app), repackaged from the official tarball |
| `t3code-nightly-bin` | [T3 Code](https://github.com/pingdotgg/t3code) desktop app, nightly builds, repackaged from the official `.deb` |
| `font-maple-mono-nf` | [Maple Mono NF](https://github.com/subframe7536/maple-font) font |

## Build and install

```sh
cd <package>
boulder build stone.yaml

# serve the result from a local moss repo
mkdir -p ~/.cache/local_repo/x86_64
cp *.stone ~/.cache/local_repo/x86_64/
moss index ~/.cache/local_repo/x86_64/

# once:
sudo moss repo add local file://$HOME/.cache/local_repo/x86_64/stone.index -p 100

sudo moss repo update
sudo moss install <package>
```

## Updates

Packages with an `autoupdate.sh` (Zen, Cursor, T3 Code) check upstream for a new release, update the recipe,
rebuild and re-index the local repo. T3 Code nightlies are picked up at most once a day. Run them from a systemd user timer and new versions arrive with
`sudo moss sync -u` like everything else. Needs `jq`.

## License

Recipes and `audio-panel` are MIT. Packaged software keeps its upstream license
(Cursor is proprietary: the recipe only points at Cursor's official download, nothing is redistributed).
