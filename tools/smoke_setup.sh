#!/usr/bin/env bash
# One-time setup for tools/smoke_test.py. Run inside `nix develop`.
# Hosts the mod on the Source SDK Base 2013 dedicated server (the TF2 dedicated server's engine is
# ABI-incompatible with SDK-built game DLLs: it crashes freeing KeyValues at map activate).
set -euo pipefail
ds=${SRCDS_DIR:-$HOME/srcds}
repo=$(cd "$(dirname "$0")/.." && pwd)

fetch() {  # steamcmd sometimes fails the first attempt with "Missing configuration"
    for _ in 1 2 3; do steamcmd +force_install_dir "$1" +login anonymous +app_update "$2" validate +quit | grep -q "fully installed" && return; done
    echo "steamcmd failed for app $2" >&2; return 1
}
fetch "$ds/sdkbase" 244310   # Source SDK Base 2013 Dedicated Server: engine
fetch "$ds/tf2" 232250       # TF2 Dedicated Server: TF content (maps, models, VPKs)

# SDK base ships a 64-bit engine but only a 32-bit launcher; TF2's launcher just loads bin/linux64/dedicated_srv.so.
cp "$ds/tf2/srcds_linux64" "$ds/sdkbase/"
mkdir -p "$ds/sdkbase/.steam/sdk64" && cp "$HOME/.local/share/Steam/linux64/steamclient.so" "$ds/sdkbase/.steam/sdk64/"
# SDK-built game DLLs ask for libtier0.so, soundemittersystem.so, ...; the dedicated server names them *_srv.so.
(cd "$ds/sdkbase/bin/linux64" && for f in *_srv.so; do [ -e "${f%_srv.so}.so" ] || ln -s "$f" "${f%_srv.so}.so"; done)

# Test mod dir: our build + config, gameinfo pointed at the downloaded content instead of Steam app IDs.
t=$ds/mod_tf_test
mkdir -p "$t/bin/linux64"
for d in cfg resource materials steam.inf; do ln -sfn "$repo/game/mod_tf/$d" "$t/$d"; done
for f in server client; do ln -sfn "$repo/game/mod_tf/bin/linux64/$f.so" "$t/bin/linux64/$f.so"; done
ln -sfn "$repo/game/mod_tf/bin/linux64/server.so" "$t/bin/linux64/server_srv.so"
sed -e "s#|appid_440|#$ds/tf2/#g" -e "s#|appid_243750|#$ds/sdkbase/#g" "$repo/game/mod_tf/gameinfo.txt" > "$t/gameinfo.txt"
echo "ready: python3 tools/smoke_test.py"
