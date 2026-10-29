# Backgrounds

The art the launcher's dashboard shows behind its Home screen for the selected game, softened by a
slight blur so the dashboard stays in front ([docs/DASHBOARD.md](../docs/DASHBOARD.md)). The images
are the games' publishers' artwork: they are not kept in this repository (git ignores them here)
and are only shipped in the release, cropped and compressed. A game without one shows its cover,
enlarged and blurred.

## Format

- One file per game: `<slug>.jpg` (or `.png`), the same slug as its cover in `covers/`
  (`games/<id>/game.json`, `"cover"`): `gears-of-war-3.jpg`, `minecraft.jpg`.
- 16:9, ideally 3840x2160 (at least 1920x1080). Any other shape is cropped to its centre at 16:9.
- Composition: the avatar stands on the left and the row of covers fills the middle band, so the
  art's subject reads best in the top half and on the right. Wide key art or a game's own
  wallpaper works better than box art.

`tools/package/make_dist.ps1` writes each catalog game's background into the release as a
1920x1080 JPEG (`x360tool background`: its centre at 16:9, scaled up or down with a high-quality
cubic filter; a 1280x800 picture becomes its 1280x720 centre, enlarged). The launcher looks for it in the release's `backgrounds\`
next to `covers\`, or in this folder when it runs from the build.
