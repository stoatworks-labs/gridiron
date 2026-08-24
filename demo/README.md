# Demo logo pack

A folder to point gridiron at. Pick any file inside it — the plugin reads the
directory around it.

It is deliberately a *mixed* pack, because a real sponsor board is one and the
awkward cases are the whole point:

| group | files | what they exercise |
|---|---|---|
| Resolume | `resolume-avenue.svg` `resolume-wire.svg` `resolume-arena.svg` | vector logos, rasterised at cell size |
| Stoatworks | `stoatworks-mark.png` `stoatworks-lockup.png` `stoatworks-stoat.svg` | the 881x209 lockup is a 4.2:1 wordmark and claims two cells |
| App graphics | `app-*.png` | 16:9 panels with no alpha at all |
| Invented sponsors | `sponsor-*.png` | roundels and wordmarks across a wide spread of aspects |

Two files are there specifically to fail if nothing is watching:

- **`sponsor-safe-area-51-padded.png`** and **`sponsor-codec-red-padded.png`** are
  drawn into square canvases with a third of the frame as air. Their raw aspect
  says 1; the artwork is nearer 5:1 and 1:1 respectively. Set **Fit** to
  **Trim margins** and watch them jump to the same optical size as everything
  else — that is the difference the mode exists for.

## The invented sponsors are invented

`sponsor-*.png` are made up. Any resemblance to a real company is accidental,
and the names are puns on the trade — Colour Bars & Grill, Haze & Confused,
Genlock & Key, Gobo Getters, Par Can Do, Truss Issues, Codec Red, Bit Depth
Charge, Safe Area 51.

## The Resolume logos

`resolume-*.svg` are Resolume's product marks — Avenue, Wire, and the Arena 7
icon — supplied by this repository's author and included only as sample content
for a demo wall.

**They are Resolume's trademarks, not ours, and their presence here implies no
affiliation with or endorsement by Resolume.** Gridiron is an independent
third-party plugin. If you are redistributing gridiron or using it commercially,
supply your own logos rather than these.

They also happen to be a useful test: all three are exported as outlines rather
than live text, which is exactly what an SVG logo needs to be for gridiron to
draw it. Each drops one `clipPath` that the vector parser ignores, which is
harmless for these three — the stripe motif still stops at the letterforms — but
is reported in the plugin's About field, as it should be.

## Live text will not draw

Worth repeating here, because it is the failure that costs someone a sponsor's
name on a wall: an SVG whose wordmark is **live `<text>`** parses without error
and draws nothing. Convert text to outlines before exporting. See the main
README.
