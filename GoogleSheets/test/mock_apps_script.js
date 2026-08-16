/*
 * A mock of the Apps Script services post_tilt.gs uses, enough to load the real file
 * and exercise its logic under plain Node.
 *
 * post_tilt.gs cannot be unit-tested any other way: it runs on Google's servers, a
 * firmware build never touches it, and deploying it to find out is a live experiment
 * on a spreadsheet holding real fermentation data.
 *
 * The sheet model is a sparse cell map plus parallel maps for notes, backgrounds, font
 * weights and number formats, because the script uses all of them as real per-row state
 * (new-day shading, data-gap fills, the average-quality fill and its note).
 */

function columnToIndex(letters) {
  return letters
    .split('')
    .reduce((total, ch) => total * 26 + ch.charCodeAt(0) - 64, 0);
}

function makeSheet(name, maxColumns = 30) {
  const values = {}, notes = {}, backgrounds = {}, weights = {}, formats = {}, widths = {};
  const key = (r, c) => r + ':' + c;
  let lastRow = 0;

  const sheet = {
    _values: values, _notes: notes, _backgrounds: backgrounds,
    _weights: weights, _formats: formats, _widths: widths,

    getName: () => name,
    getSheetId: () => 1,
    getMaxColumns: () => maxColumns,
    getMaxRows: () => 5000,
    getLastRow: () => lastRow,
    setFrozenRows() { return sheet; },
    setColumnWidth(c, w) { widths[c] = w; return sheet; },
    getParent: () => null,

    getRange(a, b, c, d) {
      let r1, c1, nr, nc;
      if (typeof a === 'string') {
        // A1 notation: 'D:D', 'E2:H2', 'A1'
        const m = a.match(/^([A-Z]+)(\d*):?([A-Z]*)(\d*)$/);
        if (!m) throw new Error('mock: unsupported A1 range ' + a);
        c1 = columnToIndex(m[1]);
        r1 = m[2] ? Number(m[2]) : 1;
        nc = m[3] ? columnToIndex(m[3]) - c1 + 1 : 1;
        nr = m[4] ? Number(m[4]) - r1 + 1 : (m[2] ? 1 : 5000);
      } else {
        r1 = a; c1 = b; nr = c === undefined ? 1 : c; nc = d === undefined ? 1 : d;
      }

      const grid = (store, fallback) => {
        const out = [];
        for (let i = 0; i < nr; i++) {
          const row = [];
          for (let j = 0; j < nc; j++) row.push(store[key(r1 + i, c1 + j)] ?? fallback);
          out.push(row);
        }
        return out;
      };
      const fill = (store, g) =>
        g.forEach((row, i) => row.forEach((v, j) => { store[key(r1 + i, c1 + j)] = v; }));

      const range = {
        _r1: r1, _c1: c1, _nr: nr, _nc: nc,
        setValues(v) { fill(values, v); lastRow = Math.max(lastRow, r1 + v.length - 1); return range; },
        getValues() { return grid(values, ''); },
        getDisplayValues() { return grid(values, '').map(r => r.map(v => v === '' ? '' : String(v))); },
        getValue() { return values[key(r1, c1)] ?? ''; },
        setValue(v) { values[key(r1, c1)] = v; lastRow = Math.max(lastRow, r1); return range; },
        setNote(n) { notes[key(r1, c1)] = n; return range; },
        getNote() { return notes[key(r1, c1)] ?? ''; },
        setNotes(g) { fill(notes, g); return range; },
        getNotes() { return grid(notes, ''); },
        setBackground(c) { for (let i = 0; i < nr; i++) for (let j = 0; j < nc; j++) backgrounds[key(r1 + i, c1 + j)] = c; return range; },
        setBackgrounds(g) { fill(backgrounds, g); return range; },
        getBackground() { return backgrounds[key(r1, c1)] ?? null; },
        getBackgrounds() { return grid(backgrounds, null); },
        setFontWeight(w) { for (let i = 0; i < nr; i++) for (let j = 0; j < nc; j++) weights[key(r1 + i, c1 + j)] = w; return range; },
        setFontWeights(g) { fill(weights, g); return range; },
        getFontWeight() { return weights[key(r1, c1)] ?? 'normal'; },
        getFontWeights() { return grid(weights, 'normal'); },
        setNumberFormat(f) { for (let j = 0; j < nc; j++) formats[c1 + j] = f; return range; },
        clearContent() {
          for (const k of Object.keys(values)) {
            const [r, c] = k.split(':').map(Number);
            if (c >= c1 && c < c1 + nc && r >= r1 && r < r1 + nr) delete values[k];
          }
          return range;
        },
        clearNote() { return range; },
        merge() { return range; },
        setFontColor() { return range; }, setFontSize() { return range; },
        setFontStyle() { return range; }, setHorizontalAlignment() { return range; },
        setWrap() { return range; }, setBorder() { return range; }
      };
      return range;
    }
  };
  return sheet;
}

function makeSpreadsheet(sheets = {}) {
  const ss = {
    _sheets: sheets,
    getSheetByName: n => sheets[n] ?? null,
    insertSheet(n) { sheets[n] = makeSheet(n); return sheets[n]; },
    getSheets: () => Object.values(sheets),
    getUrl: () => 'https://docs.google.com/spreadsheets/d/mock',
    getId: () => 'mock-id',
    getSpreadsheetTimeZone: () => 'UTC',
    setSpreadsheetTimeZone() { },
    deleteSheet() { }
  };
  return ss;
}

/* Loads the real post_tilt.gs into a VM context wired to the mocks. */
function loadScript(scriptPath, spreadsheet) {
  const fs = require('fs');
  const vm = require('vm');
  const properties = {};

  const context = {
    console,
    SpreadsheetApp: { getActiveSpreadsheet: () => spreadsheet, flush() { } },
    PropertiesService: {
      getScriptProperties: () => ({
        getProperty: k => properties[k] ?? null,
        setProperty: (k, v) => { properties[k] = String(v); },
        deleteProperty: k => { delete properties[k]; },
        getKeys: () => Object.keys(properties)
      })
    },
    LockService: { getScriptLock: () => ({ tryLock: () => true, releaseLock() { } }) },
    Utilities: {
      formatDate: d => new Date(d).toISOString(),
      sleep() { }, getUuid: () => 'mock-uuid'
    },
    ContentService: {
      createTextOutput: t => ({ setMimeType: () => ({ getContent: () => t }) }),
      MimeType: { JSON: 'application/json' }
    },
    Session: { getScriptTimeZone: () => 'UTC' },
    Charts: { ChartType: { LINE: 'LINE' } },
    ScriptApp: { newTrigger: () => ({ timeBased: () => ({ everyMinutes: () => ({ create() { } }) }) }), getProjectTriggers: () => [] },
    _properties: properties
  };
  context.global = context;
  vm.createContext(context);
  vm.runInContext(fs.readFileSync(scriptPath, 'utf8'), context, { filename: scriptPath });

  /*
   * `const` at the top level of a script is script-scoped, not a property of the
   * context object, so constants have to be read by evaluating their name.
   */
  context.readConst = expr => vm.runInContext(expr, context);
  return context;
}

module.exports = { makeSheet, makeSpreadsheet, loadScript, columnToIndex };
