/*
 * rebuildDerivedColumns() against a real grid, under the mock in mock_apps_script.js.
 *
 *   node GoogleSheets/test/run_rebuild_tests.js
 *
 * This is the backfill repair: when the offline queue delivers readings for instants a
 * four-hour average already covered, every derived value from the insertion point down
 * has to be recomputed from the rows as they now stand. It is the one part of the script
 * that rewrites history, so it is the one part where a mistake is silent - the sheet
 * still looks plausible, it is just wrong about what data existed.
 *
 * Quality is the FILL on E:F plus a note on E, not a column of its own, which is what
 * quality() below reads.
 */

const path = require('path');
const { makeSheet, makeSpreadsheet, loadScript } = require('./mock_apps_script.js');

const SCRIPT = path.join(__dirname, '..', 'post_tilt.gs');

const MIN = 60000;
const T0 = Date.UTC(2026, 7, 1, 6, 0, 0); /* 08:00 Europe/Zagreb */

const ss = makeSpreadsheet({});
const ctx = loadScript(SCRIPT, ss);
const K = ctx.readConst;

const COLUMNS = K('DATA_COLUMN_COUNT');
const NO_FILL = K('NO_FILL_COLOR');
const COMPLETE_FILL = K('AVERAGE_COMPLETE_COLOR');
const INCOMPLETE_FILL = K('AVERAGE_INCOMPLETE_COLOR');
const GAP_FILL = K('DATA_GAP_COLOR');
const NEW_DAY_FILL = K('NEW_DAY_COLOR');

/* Logging opens the spreadsheet; capture the events instead. */
const events = [];
ctx.safeLogEvent = (level, wine, code, message, details) =>
  events.push({ level, wine, code, message, details });

let failures = 0;

function check(name, actual, expected) {
  const ok = JSON.stringify(actual) === JSON.stringify(expected);
  if (!ok) failures++;
  console.log(`  ${ok ? 'PASS' : 'FAIL'}  ${name}`);
  if (!ok) console.log(`        expected ${JSON.stringify(expected)}\n        actual   ${JSON.stringify(actual)}`);
}

function checkThat(name, condition, detail = '') {
  if (!condition) failures++;
  console.log(`  ${condition ? 'PASS' : 'FAIL'}  ${name}${condition ? '' : '  ' + detail}`);
}

/* A wine sheet whose data rows are the given capture times, ascending. */
function sheetOf(times, sgAt, tempAt) {
  const sheet = makeSheet('Test Wine');
  sheet.getRange(1, 1).setValue('Test Wine');
  sheet.getRange(2, 1, 1, COLUMNS).setValues([K('WINE_SHEET_HEADERS')]);

  times.forEach((t, i) => {
    sheet.getRange(3 + i, 1, 1, 3).setValues([[
      new Date(t),
      sgAt ? sgAt(t, i) : 1.0900,
      tempAt ? tempAt(t, i) : 20
    ]]);
  });

  return sheet;
}

function rowOf(sheet, t) {
  for (let r = 3; r <= sheet.getLastRow(); r++) {
    const v = sheet.getRange(r, 1).getValue();
    if (v instanceof Date && v.getTime() === t) return r;
  }
  return -1;
}

/* What a row now says. Quality is the fill on E, so that is where it is read from. */
function derived(sheet, row) {
  return {
    rollingSG: sheet.getRange(row, 5).getValue(),
    rollingTempC: sheet.getRange(row, 6).getValue(),
    prevDaySG: sheet.getRange(row, 7).getValue(),
    prevDayTempC: sheet.getRange(row, 8).getValue(),
    qualityFill: sheet.getRange(row, 5).getBackground(),
    qualityNote: sheet.getRange(row, 5).getNotes()[0][0],
    qualityWeight: sheet.getRange(row, 5).getFontWeight(),
    rowFill: sheet.getRange(row, 1).getBackground(),
    rowNote: sheet.getRange(row, 1).getNotes()[0][0]
  };
}

/* An average point is a row that got a quality fill; unmarked rows are plain. */
function isAveragePoint(sheet, row) {
  return derived(sheet, row).qualityFill !== NO_FILL;
}


console.log('\nA: a four-hour window whose hole the queue filled in later');
{
  /* Live rows every 10 min for 2h50, then a 70-minute hole, then 4h00. */
  const live = [];
  for (let m = 0; m <= 170; m += 10) live.push(T0 + m * MIN);
  live.push(T0 + 240 * MIN);

  const backfill = [];
  for (let m = 180; m <= 230; m += 10) backfill.push(T0 + m * MIN);

  const sheet = sheetOf(live.concat(backfill).sort((a, b) => a - b));

  /* What the append path left on the 4h row before the backfill arrived. */
  const anchorRow = rowOf(sheet, T0 + 240 * MIN);
  sheet.getRange(anchorRow, 5, 1, 2).setBackground(K('AVERAGE_INSUFFICIENT_COLOR'));
  sheet.getRange(anchorRow, 5).setNote(
    'Largest interval without a saved reading inside this 4-hour window: 1h 10m. Readings used: 19.');
  sheet.getRange(anchorRow, 1, 1, 3).setBackground(GAP_FILL);
  sheet.getRange(anchorRow, 1).setNote('DATA GAP: 1h 10m since the previous captured TiltBridge reading.');

  const outcome = ctx.rebuildDerivedColumns(sheet, T0 + 180 * MIN, 600);

  checkThat('rebuild reported a change', outcome.changed);
  check('average points rewritten', outcome.averagePointCount, 1);

  const d = derived(sheet, anchorRow);
  check('quality is now COMPLETE', d.qualityFill, COMPLETE_FILL);
  check('and the average cells are bold', d.qualityWeight, 'bold');
  check('average SG is populated', typeof d.rollingSG, 'number');
  check('average temp is populated', typeof d.rollingTempC, 'number');
  check('stale DATA GAP note cleared', d.rowNote, '');
  check('stale DATA GAP fill cleared', d.rowFill, NO_FILL);
  check('the note explains the full window', d.qualityNote,
    'Largest interval without a saved reading inside this 4-hour window: 10 min. Readings used: 25.');
}


console.log('\nB: rerunning the rebuild writes nothing');
{
  const all = [];
  for (let m = 0; m <= 480; m += 10) all.push(T0 + m * MIN);
  const sheet = sheetOf(all);

  const first = ctx.rebuildDerivedColumns(sheet, T0, 600);
  checkThat('first pass changed something', first.changed);
  check('two average points over eight hours', first.averagePointCount, 2);

  const second = ctx.rebuildDerivedColumns(sheet, T0, 600);
  check('second pass is a no-op', second.changed, false);
}


console.log('\nC: the average point moves onto the backfilled row');
{
  /*
   * Live rows stop at 2h50 and resume at 4h20, so the append path put the four-hour
   * point on the 4h20 row. The queue then delivers 3h00..4h10.
   */
  const live = [];
  for (let m = 0; m <= 170; m += 10) live.push(T0 + m * MIN);
  for (let m = 260; m <= 300; m += 10) live.push(T0 + m * MIN);

  const backfill = [];
  for (let m = 180; m <= 250; m += 10) backfill.push(T0 + m * MIN);

  const sheet = sheetOf(live.concat(backfill).sort((a, b) => a - b));

  const oldAnchorRow = rowOf(sheet, T0 + 260 * MIN);
  sheet.getRange(oldAnchorRow, 5, 1, 2).setValues([[1.09, 20]]).setBackground(INCOMPLETE_FILL);

  ctx.rebuildDerivedColumns(sheet, T0 + 180 * MIN, 600);

  const newAnchorRow = rowOf(sheet, T0 + 240 * MIN);
  check('the 4h00 row is now the average point', derived(sheet, newAnchorRow).qualityFill, COMPLETE_FILL);
  check('the 4h20 row is no longer one', isAveragePoint(sheet, oldAnchorRow), false);
  check('its stale average value was cleared', derived(sheet, oldAnchorRow).rollingSG, '');
  check('and its bold with it', derived(sheet, oldAnchorRow).qualityWeight, 'normal');
}


console.log('\nD: previous-day average picks up late rows for that day');
{
  /*
   * Day one runs 08:00 local to 20:00 local, with 20:10..23:50 arriving late, then day
   * two starts at 00:00 local. The day boundary is a real Europe/Zagreb boundary - see
   * formatDate in the mock.
   */
  const dayOneStart = Date.UTC(2026, 7, 1, 6, 0, 0);  /* 08:00 local */
  const times = [];
  for (let m = 0; m <= 12 * 60; m += 10) times.push(dayOneStart + m * MIN);
  const backfill = [];
  for (let m = 12 * 60 + 10; m <= 15 * 60 + 50; m += 10) backfill.push(dayOneStart + m * MIN);
  const dayTwo = [];
  for (let m = 16 * 60; m <= 16 * 60 + 120; m += 10) dayTwo.push(dayOneStart + m * MIN);

  /* SG falls steadily, so a day average that misses the tail reads too high. */
  const sgAt = (t) => 1.1 - (t - dayOneStart) / (48 * 60 * MIN);

  const sheet = sheetOf(times.concat(backfill, dayTwo).sort((a, b) => a - b), sgAt);

  const dayTwoFirstRow = rowOf(sheet, dayOneStart + 16 * 60 * MIN);
  check('day two starts at local midnight',
    ctx.dateKey(new Date(dayOneStart + 16 * 60 * MIN), K('TIME_ZONE')), '2026-08-02');

  /* The stale previous-day average, as computed before the backfill landed. */
  const staleAverage = times.reduce((a, t) => a + sgAt(t), 0) / times.length;
  sheet.getRange(dayTwoFirstRow, 7, 1, 2).setValues([[staleAverage, 20]]);

  ctx.rebuildDerivedColumns(sheet, dayOneStart + (12 * 60 + 10) * MIN, 600);

  const dayOneAll = times.concat(backfill);
  const trueAverage = dayOneAll.reduce((a, t) => a + sgAt(t), 0) / dayOneAll.length;

  const got = derived(sheet, dayTwoFirstRow).prevDaySG;
  checkThat('previous-day average now includes the backfilled rows',
    Math.abs(got - trueAverage) < 1e-12, `got ${got} want ${trueAverage}`);
  checkThat('and it differs from the stale one', Math.abs(got - staleAverage) > 1e-9);
  check('new-day shading is on the first row of day two',
    derived(sheet, dayTwoFirstRow).rowFill, NEW_DAY_FILL);
  check('and not on the row before it',
    derived(sheet, dayTwoFirstRow - 1).rowFill, NO_FILL);
}


console.log('\nE: a genuinely missing window still reads INCOMPLETE');
{
  const times = [];
  for (let m = 0; m <= 170; m += 10) times.push(T0 + m * MIN);
  /* A 50-minute hole nothing ever fills. */
  for (let m = 220; m <= 300; m += 10) times.push(T0 + m * MIN);

  const sheet = sheetOf(times);
  const outcome = ctx.rebuildDerivedColumns(sheet, T0, 600);

  const anchorRow = rowOf(sheet, T0 + 240 * MIN);
  const d = derived(sheet, anchorRow);

  check('quality reflects the real gap', d.qualityFill, INCOMPLETE_FILL);
  checkThat('and the note names it', d.qualityNote.includes('50 min'), d.qualityNote);
  check('the average value is still written', typeof d.rollingSG, 'number');
  checkThat('something changed', outcome.changed);
}


console.log('\nF: the row cap defers the tail');
{
  const times = [];
  for (let m = 0; m <= 1000; m += 10) times.push(T0 + m * MIN);
  const sheet = sheetOf(times);

  const outcome = ctx.rebuildDerivedColumns(sheet, T0, 20);

  check('only the capped rows were rebuilt', outcome.rebuiltRowCount, 20);
  check('the tail is handed back', outcome.deferredFromTime, T0 + 200 * MIN);

  const rest = ctx.rebuildDerivedColumns(sheet, outcome.deferredFromTime, 600);
  checkThat('the deferred pass finishes the job', rest.changed);
  check('nothing left over', rest.deferredFromTime, null);

  /* The chained result must match a single uncapped pass over the same data. */
  const oneShot = sheetOf(times);
  ctx.rebuildDerivedColumns(oneShot, T0, 600);

  let mismatches = 0;
  for (let r = 3; r <= sheet.getLastRow(); r++) {
    const a = JSON.stringify([
      sheet.getRange(r, 5, 1, 4).getValues(),
      sheet.getRange(r, 5).getBackground()
    ]);
    const b = JSON.stringify([
      oneShot.getRange(r, 5, 1, 4).getValues(),
      oneShot.getRange(r, 5).getBackground()
    ]);
    if (a !== b) mismatches++;
  }
  check('capped-then-resumed equals one uncapped pass', mismatches, 0);
}


console.log('\nG: untimestamped rows at the bottom are left alone');
{
  const times = [];
  for (let m = 0; m <= 300; m += 10) times.push(T0 + m * MIN);
  const sheet = sheetOf(times);

  const blankRow = sheet.getLastRow() + 1;
  sheet.getRange(blankRow, 1, 1, 3).setValues([['', 1.088, 19.5]]);
  sheet.getRange(blankRow, 1).setNote('TiltBridge clock was not trustworthy.');

  ctx.rebuildDerivedColumns(sheet, T0, 600);

  check('no derived value invented for it', derived(sheet, blankRow).rollingSG, '');
  check('not treated as an average point', isAveragePoint(sheet, blankRow), false);
  check('its own note survives', derived(sheet, blankRow).rowNote,
    'TiltBridge clock was not trustworthy.');
}


console.log('\nH: a real gap keeps its flag when the hole is only partly filled');
{
  const times = [];
  for (let m = 0; m <= 170; m += 10) times.push(T0 + m * MIN);
  times.push(T0 + 260 * MIN);           /* 90 min after 2h50 */
  const sheet = sheetOf(times);

  const gapRow = rowOf(sheet, T0 + 260 * MIN);
  sheet.getRange(gapRow, 1, 1, 3).setBackground(GAP_FILL);
  sheet.getRange(gapRow, 1).setNote('DATA GAP: 1h 30m since the previous captured TiltBridge reading.');

  ctx.rebuildDerivedColumns(sheet, T0, 600);

  check('gap flag kept', derived(sheet, gapRow).rowFill, GAP_FILL);
  checkThat('gap note kept',
    derived(sheet, gapRow).rowNote.indexOf(K('DATA_GAP_NOTE_PREFIX')) === 0);
}


console.log('\nI: pending rebuild bookkeeping');
{
  ctx.writePendingRebuild('Test Wine', 'Test Wine', 5000);

  check('an older pending start survives a later pass',
    ctx.resolvePendingRebuild('Test Wine', 'Test Wine', 9000, null), 5000);

  check('a covered pending entry is cleared',
    ctx.resolvePendingRebuild('Test Wine', 'Test Wine', 5000, null), null);

  check('nothing left in properties',
    Object.keys(ctx._properties).filter(k => k.indexOf(K('PENDING_REBUILD_PREFIX')) === 0).length, 0);

  check('a deferred tail is recorded',
    ctx.resolvePendingRebuild('Test Wine', 'Test Wine', 1000, 7000), 7000);

  check('and the oldest of the two wins',
    ctx.resolvePendingRebuild('Test Wine', 'Test Wine', 9000, 8000), 7000);
}


console.log(`\n${failures === 0 ? 'All checks passed.' : failures + ' CHECK(S) FAILED.'}\n`);
process.exit(failures === 0 ? 0 : 1);
