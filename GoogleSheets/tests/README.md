# Apps Script tests

`post_tilt.gs` runs on Google's servers, so these run it under Node's `vm` with a fake of the
Sheets API — a grid carrying values, backgrounds, notes and font weights, which is what lets the
tests assert on the things the script actually uses as per-row state.

```bash
cd GoogleSheets/tests
cp ../post_tilt.gs post_tilt.js     # the harness loads post_tilt.js
node test_rebuild.js                # backfill repair: averages, quality, day averages, gaps
node test_layout.js                 # 13-column layout, the 19 -> 13 migration, device tracking
```

Both print `ALL PASS` and exit non-zero on failure.

`post_tilt.js` is a copy, not the source of truth — edit `../post_tilt.gs` and copy it again.
