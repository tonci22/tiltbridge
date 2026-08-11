/*
 * Minimal fake of the Apps Script surface post_tilt.gs uses, enough to run
 * rebuildDerivedColumns() against a real grid.
 */
const fs = require('fs');
const vm = require('vm');
const path = require('path');

const COLS = 13;

function pad(n, width) {
  let s = String(n);
  while (s.length < width) s = '0' + s;
  return s;
}

/* Fixed +02:00 stands in for Europe/Zagreb; UTC is exact. */
function offsetMinutes(tz) {
  return tz === 'UTC' ? 0 : 120;
}

function formatDate(date, tz, pattern) {
  const shifted = new Date(date.getTime() + offsetMinutes(tz) * 60000);
  const y = shifted.getUTCFullYear();
  const mo = pad(shifted.getUTCMonth() + 1, 2);
  const d = pad(shifted.getUTCDate(), 2);
  const h = pad(shifted.getUTCHours(), 2);
  const mi = pad(shifted.getUTCMinutes(), 2);
  const s = pad(shifted.getUTCSeconds(), 2);

  if (pattern === 'yyyy-MM-dd') return y + '-' + mo + '-' + d;
  if (pattern === 'dd.MM.yyyy HH:mm:ss') return d + '.' + mo + '.' + y + ' ' + h + ':' + mi + ':' + s;
  throw new Error('unsupported pattern ' + pattern);
}

function blankRow() {
  return {
    values: new Array(COLS).fill(''),
    backgrounds: new Array(COLS).fill('#ffffff'),
    notes: new Array(COLS).fill(''),
    fontWeights: new Array(COLS).fill('normal')
  };
}

function makeSheet(name) {
  const rows = [];

  function ensure(rowNumber) {
    while (rows.length < rowNumber) rows.push(blankRow());
  }

  const sheet = {
    _rows: rows,
    getName: () => name,
    getSheetId: () => 1,
    getLastRow: () => {
      for (let i = rows.length - 1; i >= 0; i--) {
        if (rows[i].values.some(v => v !== '' && v !== null && v !== undefined)) return i + 1;
      }
      return 0;
    },
    getMaxColumns: () => Math.max(COLS, ...rows.map(r => r.values.length), 1),
    deleteColumn: (col) => {
      for (const r of rows) {
        for (const f of ['values', 'backgrounds', 'notes', 'fontWeights']) {
          r[f].splice(col - 1, 1);
        }
      }
    },
    getRange: (row, col, numRows, numCols) => {
      numRows = numRows === undefined ? 1 : numRows;
      numCols = numCols === undefined ? 1 : numCols;
      ensure(row + numRows - 1);

      const read = (field) => {
        const out = [];
        for (let r = 0; r < numRows; r++) {
          const line = [];
          for (let c = 0; c < numCols; c++) line.push(rows[row - 1 + r][field][col - 1 + c]);
          out.push(line);
        }
        return out;
      };

      const write = (field, grid) => {
        if (grid.length !== numRows) throw new Error('row count mismatch: ' + grid.length + ' vs ' + numRows);
        for (let r = 0; r < numRows; r++) {
          if (grid[r].length !== numCols) throw new Error('col count mismatch');
          for (let c = 0; c < numCols; c++) rows[row - 1 + r][field][col - 1 + c] = grid[r][c];
        }
        return range;
      };

      const fill = (field, value) => {
        for (let r = 0; r < numRows; r++)
          for (let c = 0; c < numCols; c++) rows[row - 1 + r][field][col - 1 + c] = value;
        return range;
      };

      const range = {
        getValues: () => read('values'),
        getDisplayValues: () => read('values').map(r => r.map(v => v === null || v === undefined ? '' : String(v))),
        getValue: () => read('values')[0][0],
        setValues: (g) => write('values', g),
        getBackgrounds: () => read('backgrounds'),
        setBackgrounds: (g) => write('backgrounds', g),
        setBackground: (v) => fill('backgrounds', v === null ? '#ffffff' : v),
        getNotes: () => read('notes'),
        setNotes: (g) => write('notes', g),
        setNote: (v) => fill('notes', v),
        getFontWeights: () => read('fontWeights'),
        setFontWeights: (g) => write('fontWeights', g),
        setFontWeight: (v) => fill('fontWeights', v)
      };

      return range;
    }
  };

  return sheet;
}

function loadScript() {
  const source = fs.readFileSync(
    path.join(__dirname, 'post_tilt.js'),
    'utf8'
  );

  const properties = {};

  const context = {
    console,
    Utilities: { formatDate },
    PropertiesService: {
      getScriptProperties: () => ({
        getProperty: (k) => (k in properties ? properties[k] : null),
        setProperty: (k, v) => { properties[k] = v; },
        deleteProperty: (k) => { delete properties[k]; },
        getKeys: () => Object.keys(properties)
      })
    },
    SpreadsheetApp: {},
    LockService: {},
    ScriptApp: {},
    ContentService: {},
    __properties: properties,
    __events: []
  };

  vm.createContext(context);
  vm.runInContext(source, context);

  /* Logging touches the spreadsheet; capture it instead. */
  vm.runInContext(
    'safeLogEvent = function (level, wine, code, message, details) {' +
    '  __events.push({ level: level, wine: wine, code: code, message: message, details: details });' +
    '};',
    context
  );

  return context;
}

module.exports = { makeSheet, loadScript, formatDate, COLS, blankRow };
