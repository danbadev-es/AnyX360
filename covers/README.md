# Covers

Box art the launcher shows for each game. The images are the games' publishers' artwork: they are
not kept in this repository (git ignores them here) and are only shipped in the release, cropped
to the front cover and compressed, to identify the games AnyX360 plays or will play.

## Format

- One file per game: `<slug>.jpg` (or `.png`). The slug is the game's English title in lower case,
  its words joined by hyphens, without platform, region, edition or IDs: `gears-of-war-3.jpg`,
  `kinect-sports-ultimate-collection.jpg`, `call-of-duty-black-ops-ii.jpg`, `fifa-19.jpg`.
- A full wraparound scan (back, spine and front of the Xbox 360 case, about 1.49:1, any size), or
  the front cover alone (2:3). The launcher and the packaging take the front from a wraparound:
  the right 47.3 % of it.
- The slug is what `games/<id>/game.json` (`"cover"`) and `tools/launcher/upcoming.json` name.

`tools/package/make_dist.ps1` writes each named cover into the release as a 600x900 JPEG
(`x360tool cover`).
