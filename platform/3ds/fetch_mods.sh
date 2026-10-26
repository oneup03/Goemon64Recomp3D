#!/usr/bin/env bash
# fetch_mods.sh <out dir>: the mods built into the executable, as their
# authors released them: each .nrm at a pinned release, checked by SHA-256,
# for mod_bake. None is a submodule: the German translation's repository
# does not rebuild its released mod (its README points to the release
# files), and Redub's release is the author's own build against the symbols
# it was made for. The mods without a release are built from source
# (build_mods.sh, mods.list).
#   Redub (klorfmorf, abyssonym; CC0-1.0): the Japanese cutscene voices.
#   Deutsche Uebersetzung (Mekurukid; MIT): the German translation, a ROM
#   patch alone (platform/3ds/file_patches.cpp applies it).
#   Fast Travel (Killklli; CC0-1.0): the dragon's flight between areas over
#   after 100 frames. Its first release, v1.0.0: v1.0.1 waits for START held
#   instead.
#   Custom Fish (8AE; CC0-1.0): the fishing quest's fish counts, as options.
#   Attack While Moving (sirdankz; no licence stated): the game function that
#   stops the player during a ground attack made to return at once.
#   Intro Lyrics (ZettaHakai; CC0-1.0): the opening song's lyrics as romaji,
#   karaoke, the original or none. Not released: the .nrm its repository
#   keeps, at a pinned commit.
# Camera Snap and BetterCam are not built in: the port's own camera
# (patches) takes their ideas.
set -euo pipefail
out=${1:?out dir}
mkdir -p "$out"
while read -r url sha nrm; do
    [ -n "$url" ] || continue
    if [ ! -f "$out/$nrm" ] || ! echo "$sha  $out/$nrm" | sha256sum -c --quiet 2>/dev/null; then
        curl -fsSL -o "$out/$nrm.part" "$url"
        echo "$sha  $out/$nrm.part" | sha256sum -c --quiet
        mv "$out/$nrm.part" "$out/$nrm"
        echo "fetch_mods: $nrm"
    fi
done <<'LIST'
https://github.com/klorfmorf/MNSGRecompRedubMod/releases/download/1.0.0/redub_mod.nrm baa2815ebad18ceb4f19322cc2d6e780d58631469d0166aea9b4eb6b73c20f12 redub_mod.nrm
https://github.com/Mekurukid/Goemon64-DE/releases/download/v0.8.5-beta/Goemon64-DE-Beta-v0.8.5.nrm ff4e976aeaf7f41a9fabf22e5b7a51dfeb9e40c2587ab0878f275ed4c635079a Goemon64-DE-Beta-v0.8.5.nrm
https://github.com/Killklli/MNSGRecompModFastTravel/releases/download/v1.0.0/mnsg_fast_travel.nrm 2766ef58ac16d5e916cbce2c9b663133bb1d591c10f03105f7db21458c1f4dac mnsg_fast_travel.nrm
https://github.com/8AE/mnsg-custom-fish/releases/download/v1.0.0/custom_fish.mod.nrm 52887d6dd820aa38a6636a37d124daf931d25d6857d6e6f9486a5140c99265e7 custom_fish.mod.nrm
https://github.com/sirdankz/MNSG_AttackWhileMoving/releases/download/MysticalNinjaStarringGoemonRecomp/mnsg_attack_while_moving.nrm bb585e5d99118fe11d0d49d5ac4476717c57a84f29608df8fcaef3904365aa87 mnsg_attack_while_moving.nrm
https://raw.githubusercontent.com/ZettaHakai/MNSGIntroLyrics/6cffcc3126fc2ca3b04fdff469e3601fd9ee2c95/MNSGIntroLyrics.nrm ae537071736ae597771676f4bb39fec1c83d67d2469ef47a9bea4a929096aaa9 MNSGIntroLyrics.nrm
LIST
