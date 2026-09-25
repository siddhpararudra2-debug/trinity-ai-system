# Vendored Google Fonts (latin subset)

Self-hosted woff2 files so `next build` never contacts `fonts.googleapis.com` /
`fonts.gstatic.com`. This removes the intermittent CI failure where Turbopack's
build-time font fetch failed and surfaced as
`Module not found: Can't resolve '@vercel/turbopack-next/internal/font/google/font'`.

- **Source**: [fonts.google.com](https://fonts.google.com) — Google Fonts CSS API v2
  (`https://fonts.googleapis.com/css2?...`, fetched with a Chrome User-Agent so the
  response is woff2), latin (`/* latin */`) blocks only, unicode-range `U+0000-00FF, …`
- **License**: SIL Open Font License 1.1 (OFL-1.1) — see
  <https://fonts.google.com/about>
- **Used by**: `app/globals.css` (`@font-face` rules at the top of the file +
  `--font-*` custom properties in `:root`)

## Families and weights

| File | Family | Weight | Notes |
| --- | --- | --- | --- |
| `space-grotesk-400.woff2` | Space Grotesk | 400 | latin subset |
| `space-grotesk-500.woff2` | Space Grotesk | 500 | latin subset |
| `space-grotesk-600.woff2` | Space Grotesk | 600 | latin subset |
| `space-grotesk-700.woff2` | Space Grotesk | 700 | latin subset |
| `inter-400.woff2` | Inter | 400 | latin subset |
| `inter-500.woff2` | Inter | 500 | latin subset |
| `inter-600.woff2` | Inter | 600 | latin subset |
| `jetbrains-mono-400.woff2` | JetBrains Mono | 400 | latin subset |
| `jetbrains-mono-600.woff2` | JetBrains Mono | 600 | latin subset |
| `ibm-plex-mono-400.woff2` | IBM Plex Mono | 400 | latin subset |
| `ibm-plex-mono-500.woff2` | IBM Plex Mono | 500 | latin subset |
| `ibm-plex-mono-600.woff2` | IBM Plex Mono | 600 | latin subset |

> Note: Google Fonts serves a **single variable woff2 per family** for Space Grotesk,
> Inter and JetBrains Mono — every requested weight of those families points at the
> same latin URL, so the per-weight files are byte-identical copies of that one
> variable file (this is exactly what Google's own css2 CSS declares; each
> `@font-face` keeps its per-weight `font-weight` descriptor so the `wght` axis
> resolves to the right instance). IBM Plex Mono ships distinct static files per
> weight. Files are kept one-per-weight so filenames map 1:1 to the `@font-face`
> rules and to the weights the app uses.

## Regeneration

Fetch the css2 CSS with a Chrome User-Agent (that is what returns woff2 URLs), then
download only the `/* latin */` block of each weight:

```powershell
$ua = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
$dir = "frontend/public/fonts"

# 1) CSS (this network step is a maintenance action only — never part of npm run build)
curl.exe -A $ua -o space-grotesk.css "https://fonts.googleapis.com/css2?family=Space+Grotesk:wght@400;500;600;700&display=swap"
curl.exe -A $ua -o inter.css         "https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600&display=swap"
curl.exe -A $ua -o jetbrains-mono.css "https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;600&display=swap"
curl.exe -A $ua -o ibm-plex-mono.css  "https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;500;600&display=swap"

# 2) For each /* latin */ block (unicode-range contains U+0000-00FF), download:
#      <family-slug>-<weight>.woff2   e.g. space-grotesk-400.woff2
#    from the url(https://fonts.gstatic.com/...) inside that block.
```

When adding a weight, also add the matching `@font-face` rule in
`frontend/app/globals.css`.
