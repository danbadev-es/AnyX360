# Inter 4.0

[Inter](https://github.com/rsms/inter) by Rasmus Andersson and the Inter Project Authors, under the
SIL Open Font License 1.1 (`LICENSE.txt`; no Reserved Font Name). AnyX360 embeds it in the games
(`runtime/CMakeLists.txt`) for the settings panel (F2), the performance overlay (F1) and the Kinect
debug window.

`Inter-Regular.ttf` and `Inter-SemiBold.ttf` are subsets of the static fonts in the release's
`extras/ttf` folder (`Inter-4.0.zip`, SHA-256
`ff970a5d4561a04f102a7cb781adbd6ac4e9b6c460914c7a101f15acb7f7d1a4`): Latin (Basic, Latin-1,
Extended-A), punctuation, arrows, ✓ and ●, without hinting or layout features (Dear ImGui's
rasterizer uses neither). Made with fontTools 4.53.1:

```
pyftsubset Inter-<Weight>.ttf --unicodes="U+0020-007E,U+00A0-017F,U+2010-2027,U+2030-203A,U+20AC,U+2122,U+2190-2193,U+2713,U+2715,U+25CF" --layout-features='' --no-hinting --output-file=Inter-<Weight>.ttf
```
