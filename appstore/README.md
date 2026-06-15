# Pebble appstore listing — Sykerö Track Work Time

Listing material for the Pebble appstore, kept under version control so it is
ready to upload. **Watchapp** listing (requires the full asset set).

## Text fields (this directory)

| File | Field |
|---|---|
| `title.txt` | Listing title |
| `description.txt` | Listing description (EN) |
| `category.txt` | Appstore category |
| `source_url.txt` | Source code URL |
| `support_email.txt` | Support email |

## Assets (not text, not tracked here)

- **Large icon** 144×144 PNG and **small icon** 80×80 PNG — **required**.
- **Marketing banner** 720×320 PNG — **required** for a watchapp.
- **Screenshots** — ≥1 per supported platform, **unframed**, native resolution.

Banner + icons are auto-composed by the superrepo's
`scripts/gen-appstore-assets.py`; screenshots by
`scripts/pebble-appstore-screenshots.sh`. Generated output is gitignored under
the superrepo `appstore/` (regenerable).

## Status

Drafted, awaiting manual upload to the Pebble appstore.
