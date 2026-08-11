const { makeSheet, loadScript } = require('./harness');

const ctx = loadScript();
const MIN = 60000;
const T0 = Date.UTC(2026, 7, 1, 6, 0, 0); /* 08:00 local */

let failures = 0;

function check(name, actual, expected) {
  const ok = JSON.stringify(actual) === JSON.stringify(expected);
  if (!ok) {
    failures++;
    console.log('  FAIL ' + name + '\n    expected ' + JSON.stringify(expected) + '\n    actual   ' + JSON.stringify(actual));
  } else {
    console.log('  ok   ' + name + '  ' + JSON.stringify(actual));
  }
}

function checkTrue(name, actual) {
  check(name, !!actual, true);
}

/* Builds a sheet whose data rows are the given capture times, ascending. */
function sheetOf(times, sgAt, tempAt) {
  const sheet = makeSheet('Test Wine');
  sheet.getRange(1, 1).setValues([['Test Wine']]);
  sheet.getRange(2, 1, 1, 13).setValues([new Array(13).fill('h')]);

  times.forEach((t, i) => {
    const row = 3 + i;
    sheet.getRange(row, 1, 1, 3).setValues([[
      new Date(t),
      sgAt ? sgAt(t, i) : 1.0900,
      tempAt ? tempAt(t, i) : 20
    ]]);
  });

  return sheet;
}

function rowOf(sheet, t) {
  const last = sheet.getLastRow();
  for (let r = 3; r <= last; r++) {
    const v = sheet.getRange(r, 1).getValue();
    if (v instanceof Date && v.getTime() === t) return r;
  }
  return -1;
}

function derived(sheet, row) {
  return {
    E: sheet.getRange(row, 5).getValue(),
    F: sheet.getRange(row, 6).getValue(),
    G: sheet.getRange(row, 7).getValue(),
    H: sheet.getRange(row, 8).getValue(),
    I: sheet.getRange(row, 9).getValue(),
    gBg: sheet.getRange(row, 7).getBackgrounds()[0][0],
    aBg: sheet.getRange(row, 1).getBackgrounds()[0][0],
    aNote: sheet.getRange(row, 1).getNotes()[0][0]
  };
}


console.log('\n== A: a four-hour window whose hole the queue filled in later ==');
{
  /* Live rows every 10 min for 2h50, then a 70-minute hole, then 4h00. */
  const live = [];
  for (let m = 0; m <= 170; m += 10) live.push(T0 + m * MIN);
  live.push(T0 + 240 * MIN);

  const backfill = [];
  for (let m = 180; m <= 230; m += 10) backfill.push(T0 + m * MIN);

  const all = live.concat(backfill).sort((a, b) => a - b);
  const sheet = sheetOf(all);

  /* What the old code left on the 4h row before the backfill arrived. */
  const anchorRow = rowOf(sheet, T0 + 240 * MIN);
  sheet.getRange(anchorRow, 7).setValues([['INSUFFICIENT DATA — 1h 10m gap']]);
  sheet.getRange(anchorRow, 7).setBackground('#f4cccc');
  sheet.getRange(anchorRow, 7).setFontWeight('bold');
  sheet.getRange(anchorRow, 7).setNote('Largest interval without a saved reading inside this 4-hour window: 1h 10m. Readings used: 19.');
  sheet.getRange(anchorRow, 1, 1, 3).setBackground('#f4cccc');
  sheet.getRange(anchorRow, 1).setNote('DATA GAP: 1h 10m since the previous captured TiltBridge reading.');

  const outcome = ctx.rebuildDerivedColumns(sheet, T0 + 180 * MIN, 600);

  checkTrue('rebuild reported a change', outcome.changed);
  check('average points rewritten', outcome.averagePointCount, 1);

  const d = derived(sheet, anchorRow);
  check('quality is now COMPLETE', d.G, 'COMPLETE');
  check('quality fill is the complete colour', d.gBg, '#d9ead3');
  check('average SG is populated', typeof d.E, 'number');
  check('average temp is populated', typeof d.F, 'number');
  check('stale DATA GAP note cleared', d.aNote, '');
  check('stale DATA GAP fill cleared', d.aBg, '#ffffff');
  check('note explains the full window', sheet.getRange(anchorRow, 7).getNotes()[0][0],
    'Largest interval without a saved reading inside this 4-hour window: 10 min. Readings used: 25.');
}


console.log('\n== B: rerunning the rebuild writes nothing ==');
{
  const all = [];
  for (let m = 0; m <= 480; m += 10) all.push(T0 + m * MIN);
  const sheet = sheetOf(all);

  const first = ctx.rebuildDerivedColumns(sheet, T0, 600);
  checkTrue('first pass changed something', first.changed);
  check('two average points over eight hours', first.averagePointCount, 2);

  const second = ctx.rebuildDerivedColumns(sheet, T0, 600);
  check('second pass is a no-op', second.changed, false);
}


console.log('\n== C: the average point moves onto the backfilled row ==');
{
  /*
   * Live rows stop at 2h50 and resume at 4h20, so the old code put the
   * four-hour point on the 4h20 row. The queue then delivers 3h00..4h10.
   */
  const live = [];
  for (let m = 0; m <= 170; m += 10) live.push(T0 + m * MIN);
  for (let m = 260; m <= 300; m += 10) live.push(T0 + m * MIN);

  const backfill = [];
  for (let m = 180; m <= 250; m += 10) backfill.push(T0 + m * MIN);

  const sheet = sheetOf(live.concat(backfill).sort((a, b) => a - b));

  const oldAnchorRow = rowOf(sheet, T0 + 260 * MIN);
  sheet.getRange(oldAnchorRow, 7).setValues([['INCOMPLETE — 1h 30m gap']]);
  sheet.getRange(oldAnchorRow, 7).setBackground('#fff2cc');
  sheet.getRange(oldAnchorRow, 5, 1, 2).setValues([[1.09, 20]]);

  ctx.rebuildDerivedColumns(sheet, T0 + 180 * MIN, 600);

  const newAnchorRow = rowOf(sheet, T0 + 240 * MIN);
  check('the 4h00 row is now the average point', derived(sheet, newAnchorRow).G, 'COMPLETE');
  check('the 4h20 row was cleared', derived(sheet, oldAnchorRow).G, '');
  check('its stale average value was cleared', derived(sheet, oldAnchorRow).E, '');
  check('its stale fill was cleared', derived(sheet, oldAnchorRow).gBg, '#ffffff');
}


console.log('\n== D: previous-day average picks up late rows for that day ==');
{
  /*
   * Day one runs 08:00 local to 23:50 local with a hole from 20:00 to 23:50,
   * then day two starts at 00:00. The queue later delivers the hole.
   */
  const dayOneStart = Date.UTC(2026, 7, 1, 6, 0, 0);  /* 08:00 local */
  const times = [];
  for (let m = 0; m <= 12 * 60; m += 10) times.push(dayOneStart + m * MIN);   /* to 20:00 local */
  const backfill = [];
  for (let m = 12 * 60 + 10; m <= 15 * 60 + 50; m += 10) backfill.push(dayOneStart + m * MIN);
  const dayTwo = [];
  for (let m = 16 * 60; m <= 16 * 60 + 120; m += 10) dayTwo.push(dayOneStart + m * MIN);

  /* SG falls steadily, so a day average that misses the tail reads too high. */
  const sgAt = (t) => 1.1 - (t - dayOneStart) / (48 * 60 * MIN);

  const sheet = sheetOf(times.concat(backfill, dayTwo).sort((a, b) => a - b), sgAt);

  const dayTwoFirstRow = rowOf(sheet, dayOneStart + 16 * 60 * MIN);
  check('day two starts at local midnight',
    require('./harness').formatDate(new Date(dayOneStart + 16 * 60 * MIN), 'Europe/Zagreb', 'dd.MM.yyyy HH:mm:ss'),
    '02.08.2026 00:00:00');

  /* Stale previous-day average, computed before the backfill landed. */
  const staleSum = times.reduce((a, t) => a + sgAt(t), 0) / times.length;
  sheet.getRange(dayTwoFirstRow, 8, 1, 2).setValues([[staleSum, 20]]);

  ctx.rebuildDerivedColumns(sheet, dayOneStart + (12 * 60 + 10) * MIN, 600);

  const dayOneAll = times.concat(backfill);
  const trueAverage = dayOneAll.reduce((a, t) => a + sgAt(t), 0) / dayOneAll.length;

  const got = derived(sheet, dayTwoFirstRow).H;
  check('previous-day average now includes the backfilled rows', Math.abs(got - trueAverage) < 1e-12, true);
  check('and it differs from the stale one', Math.abs(got - staleSum) > 1e-9, true);
  check('new-day shading is on the first row of day two', derived(sheet, dayTwoFirstRow).aBg, '#d9ead3');
  check('and not on the row before it', derived(sheet, dayTwoFirstRow - 1).aBg, '#ffffff');
}


console.log('\n== E: a genuinely missing window still reads INCOMPLETE ==');
{
  const times = [];
  for (let m = 0; m <= 170; m += 10) times.push(T0 + m * MIN);
  /* A 50-minute hole nothing ever fills. */
  for (let m = 220; m <= 300; m += 10) times.push(T0 + m * MIN);

  const sheet = sheetOf(times);
  const outcome = ctx.rebuildDerivedColumns(sheet, T0, 600);

  const anchorRow = rowOf(sheet, T0 + 240 * MIN);
  check('quality reflects the real gap', derived(sheet, anchorRow).G, 'INCOMPLETE — 50 min gap');
  check('the average value is still written', typeof derived(sheet, anchorRow).E, 'number');
  checkTrue('something changed', outcome.changed);
}


console.log('\n== F: the row cap defers the tail ==');
{
  const times = [];
  for (let m = 0; m <= 1000; m += 10) times.push(T0 + m * MIN);
  const sheet = sheetOf(times);

  const outcome = ctx.rebuildDerivedColumns(sheet, T0, 20);

  check('only the capped rows were rebuilt', outcome.rebuiltRowCount, 20);
  check('the tail is handed back', outcome.deferredFromTime, T0 + 200 * MIN);

  const rest = ctx.rebuildDerivedColumns(sheet, outcome.deferredFromTime, 600);
  checkTrue('the deferred pass finishes the job', rest.changed);
  check('nothing left over', rest.deferredFromTime, null);

  /* The chained result must match a single uncapped pass over the same data. */
  const oneShot = sheetOf(times);
  ctx.rebuildDerivedColumns(oneShot, T0, 600);

  const last = sheet.getLastRow();
  let mismatches = 0;
  for (let r = 3; r <= last; r++) {
    const a = JSON.stringify(sheet.getRange(r, 5, 1, 5).getValues());
    const b = JSON.stringify(oneShot.getRange(r, 5, 1, 5).getValues());
    if (a !== b) mismatches++;
  }
  check('capped-then-resumed equals one uncapped pass', mismatches, 0);
}


console.log('\n== G: untimestamped rows at the bottom are left alone ==');
{
  const times = [];
  for (let m = 0; m <= 300; m += 10) times.push(T0 + m * MIN);
  const sheet = sheetOf(times);

  const blankRowNumber = sheet.getLastRow() + 1;
  sheet.getRange(blankRowNumber, 1, 1, 3).setValues([['', 1.088, 19.5]]);
  sheet.getRange(blankRowNumber, 1).setNote('TiltBridge clock was not trustworthy.');

  ctx.rebuildDerivedColumns(sheet, T0, 600);

  check('no derived value invented for it', derived(sheet, blankRowNumber).G, '');
  check('its own note survives', derived(sheet, blankRowNumber).aNote, 'TiltBridge clock was not trustworthy.');
}


console.log('\n== H: a real gap keeps its flag when the hole is only partly filled ==');
{
  const times = [];
  for (let m = 0; m <= 170; m += 10) times.push(T0 + m * MIN);
  times.push(T0 + 260 * MIN);           /* 90 min after 2h50 */
  const sheet = sheetOf(times);

  const gapRow = rowOf(sheet, T0 + 260 * MIN);
  sheet.getRange(gapRow, 1, 1, 3).setBackground('#f4cccc');
  sheet.getRange(gapRow, 1).setNote('DATA GAP: 1h 30m since the previous captured TiltBridge reading.');

  ctx.rebuildDerivedColumns(sheet, T0, 600);

  check('gap flag kept', derived(sheet, gapRow).aBg, '#f4cccc');
  checkTrue('gap note kept', derived(sheet, gapRow).aNote.indexOf('DATA GAP:') === 0);
}


console.log('\n== I: pending rebuild bookkeeping ==');
{
  ctx.writePendingRebuild('Test Wine', 'Test Wine', 5000);

  check('older pending start survives a later pass',
    ctx.resolvePendingRebuild('Test Wine', 'Test Wine', 9000, null), 5000);

  check('a covered pending entry is cleared',
    ctx.resolvePendingRebuild('Test Wine', 'Test Wine', 5000, null), null);

  check('nothing left in properties',
    Object.keys(ctx.__properties).filter(k => k.indexOf('pending-derived-rebuild-') === 0).length, 0);

  check('a deferred tail is recorded',
    ctx.resolvePendingRebuild('Test Wine', 'Test Wine', 1000, 7000), 7000);

  check('and the oldest of the two wins',
    ctx.resolvePendingRebuild('Test Wine', 'Test Wine', 9000, 8000), 7000);
}


console.log(failures === 0 ? '\nALL PASS\n' : '\n' + failures + ' FAILURE(S)\n');
process.exit(failures === 0 ? 0 : 1);
