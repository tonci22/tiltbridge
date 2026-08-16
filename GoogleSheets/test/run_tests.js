/*
 * Assertions over the real post_tilt.gs, run under the mock in mock_apps_script.js.
 *
 *   node GoogleSheets/test/run_tests.js
 *
 * These cover the things that are cheap to get wrong and expensive to discover on a
 * live sheet: the column layout agreeing with itself, and the quality thresholds
 * tracking the configured upload interval instead of a hardcoded cadence.
 */

const path = require('path');
const { makeSheet, makeSpreadsheet, loadScript } = require('./mock_apps_script.js');

const SCRIPT = path.join(__dirname, '..', 'post_tilt.gs');

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

const ss = makeSpreadsheet({ rose: makeSheet('rose') });
const ctx = loadScript(SCRIPT, ss);
const K = ctx.readConst;

console.log('\nLayout self-consistency');

const columnCount = K('DATA_COLUMN_COUNT');
const headers = K('WINE_SHEET_HEADERS');

check('headers length matches DATA_COLUMN_COUNT', headers.length, columnCount);
checkThat('charts anchor clear of the data', K('CHART_COLUMN') > columnCount,
  `CHART_COLUMN=${K('CHART_COLUMN')} DATA_COLUMN_COUNT=${columnCount}`);
checkThat('no average-quality column remains',
  !headers.some(h => String(h).toLowerCase().includes('quality')),
  JSON.stringify(headers));

/*
 * The row builder and the layout must agree. They are edited in different places and a
 * mismatch writes every value one column left or right of where the headers say.
 */
const row = ctx.buildMeasurementRowValues(
  { sg: 1.0880, tempC: 23.1, sgRaw: 1.0885, recordId: 'ABC-1', rssiAverage: -70 },
  new Date('2026-08-16T10:00:00Z'),
  {
    rollingSG: 1.0879, rollingTempC: 23.0, quality: 'COMPLETE',
    previousDaySG: 1.0900, previousDayTempC: 22.8
  }
);
check('buildMeasurementRowValues width', row.length, columnCount);
checkThat('quality text is not written into a cell',
  !row.some(v => String(v).includes('COMPLETE')), JSON.stringify(row));

/* The separator columns must actually be blank in a built row. */
const separators = headers
  .map((h, i) => (h === '' ? i : -1))
  .filter(i => i >= 0);
checkThat('separator columns are blank in a built row',
  separators.every(i => row[i] === ''),
  `separators at ${separators} -> ${separators.map(i => JSON.stringify(row[i]))}`);

console.log('\nQuality thresholds track the configured cadence');

const interval = K('EXPECTED_READING_INTERVAL_MINUTES');
const complete = K('AVERAGE_COMPLETE_MAX_GAP_MINUTES');
const incomplete = K('AVERAGE_INCOMPLETE_MAX_GAP_MINUTES');
const missing = K('MISSING_READING_MINUTES');

console.log(`  (interval ${interval} min -> COMPLETE<=${complete}, INCOMPLETE<=${incomplete}, MISSING>${missing})`);

const quality = gap =>
  ctx.classifyRollingAssessment({ maxGapMinutes: gap, sg: 1.08, tempC: 23, readingCount: 8 }).quality;

/*
 * The bug this guards: with absolute thresholds, a PERFECT window at the configured
 * interval sat exactly on the COMPLETE boundary, so jitter reported INCOMPLETE and one
 * missed reading reported INSUFFICIENT DATA. Perfect data must be comfortably COMPLETE.
 */
checkThat('a perfect window is COMPLETE', quality(interval) === 'COMPLETE', quality(interval));
checkThat('jitter of 25% is still COMPLETE',
  quality(Math.round(interval * 1.25)) === 'COMPLETE', quality(Math.round(interval * 1.25)));
checkThat('one missed reading is at worst INCOMPLETE',
  quality(interval * 2).startsWith('INCOMPLETE'), quality(interval * 2));
checkThat('a four-times gap is INSUFFICIENT',
  quality(interval * 4).startsWith('INSUFFICIENT'), quality(interval * 4));
checkThat('MISSING tolerates two consecutive misses', missing > interval * 2,
  `missing=${missing} interval=${interval}`);
checkThat('thresholds are ordered', complete < incomplete && incomplete < interval * 4,
  `${complete} < ${incomplete}`);

console.log(`\n${failures === 0 ? 'All checks passed.' : failures + ' CHECK(S) FAILED.'}\n`);
process.exit(failures === 0 ? 0 : 1);
