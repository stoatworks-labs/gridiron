# Test fixtures

A deliberately awkward sponsor pack, because the easy cases were never the problem.

| file | what it is there to catch |
|---|---|
| `alpha_round.png` | a mark drawn to its own edges — must not be trimmed away |
| `bravo_wide.png` | a 4:1 wordmark — must claim two cells |
| `charlie.jpg` | no alpha channel at all |
| `delta_anim.gif` | four frames — animated content in a cell |
| `echo_outlined.svg` | vector done right: text converted to outlines |
| `foxtrot_livetext.svg` | **vector done wrong**: live `<text>`, which nanosvg drops silently. Parses clean, draws nothing where the sponsor's name should be. The plugin must say so out loud. |
| `golf_padded.png` | an 8:1 mark centred in a square canvas — raw aspect says 1, the truth is 8 |

`not-an-image.txt` is not a mistake: the scanner must ignore it, and the test
counts on exactly seven supported files being found beside it.

`golf_padded.png` and `foxtrot_livetext.svg` are the two that matter. Both are
exactly what turns up in a real sponsor pack, and both fail silently if nothing
is looking for them.
