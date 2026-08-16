# Editor icons (APX-367)

The C++ editor's bitmap icon set from `Content/Images` (origin/main
62b1d00e62a97747a95b16f11a9f9c8bdc1c29a0), used by v2 editor windows:

| File | C++ usage (migration manifest §1) |
| --- | --- |
| `FolderIcon.png` | ProjectBrowserWindow directoryTexture (folder tiles) |
| `FileIcon.png` | ResourceAssets assertTexture (generic asset tile fallback) |
| `LogoSmall.jpeg` | Project manager logo / window icon |
| `minimalist-logo.png` | Empty-project graphic |
| `skore.ico` | Win32 window icon resource |

The vendored stb_image is `STBI_ONLY_PNG`, so the loader embeds RGBA PNG
conversions of the JPEG and ICO: `logo_small.png` and `skore.png` (same
pixels). `scripts/gen-editor-icons-embed.py` regenerates
`editor/content/skore_editor_icons_embed.h` from this directory.
