const { makeSheet, loadScript } = require('./harness');

const ctx = loadScript();
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

const V13 = ['Date and time','SG','Temperature °C','','4-hour avg SG','4-hour avg °C',
  'Average quality','Previous day avg SG','Previous day avg °C','','Raw SG','Signal dBm','Record id'];


console.log('\n== A: layout detection ==');
{
  check('the current headers are recognised', ctx.isCurrentWineLayout(V13), true);

  /* Anything else must be rejected so prepareSheet resets rather than misaligns. */
  const older = V13.slice(); older[11] = 'Smoothed SG';
  check('an older layout is rejected', ctx.isCurrentWineLayout(older), false);
  check('a truncated header row is rejected', ctx.isCurrentWineLayout(V13.slice(0, 9)), false);
  check('an empty header row is rejected', ctx.isCurrentWineLayout([]), false);
}


console.log('\n== B: row values are the 13 the layout declares ==');
{
  const measurement = {
    captureDate: new Date(), sg: 1.0901, tempC: 22.5,
    sgRaw: 1.0899, sgSmoothed: 1.0900,
    rssi: -61, rssiAvg: -62, rssiMin: -70, rssiMax: -55, rssiSamples: 12,
    deviceId: 'E2:F3:CC:64:39:6D', recordId: 'rec-1'
  };
  const row = ctx.buildMeasurementRowValues(measurement, measurement.captureDate, ctx.emptyAverageSet());

  check('thirteen values', row.length, 13);
  check('K is Raw SG', row[10], 1.0899);
  check('L is the RSSI average', row[11], -62);
  check('M is the record id', row[12], 'rec-1');
  check('smoothed SG appears nowhere', row.includes(1.0900), false);
  check('MAC appears nowhere', row.includes('E2:F3:CC:64:39:6D'), false);
  check('min/max/samples appear nowhere', [row.includes(-70), row.includes(-55), row.includes(12)], [false, false, false]);
}


console.log('\n== C: the signal column and its note ==');
{
  const full = { rssi: -61, rssiAvg: -62, rssiMin: -70, rssiMax: -55, rssiSamples: 12 };
  check('value is the average', ctx.signalValue(full), -62);
  check('note carries the detail', ctx.buildSignalNote(full),
    'This reading: -61 dBm. Range over the window: -70 to -55 dBm. Samples: 12.');

  /* Single-reading path: no aggregates at all. */
  const bare = { rssi: '', rssiAvg: '', rssiMin: '', rssiMax: '', rssiSamples: '' };
  check('blank when nothing was sent', ctx.signalValue(bare), '');
  check('no note when nothing was sent', ctx.buildSignalNote(bare), '');

  /* A device that sent its own dBm but no window aggregate. */
  const partial = { rssi: -58, rssiAvg: '', rssiMin: '', rssiMax: '', rssiSamples: '' };
  check('falls back to the reading dBm', ctx.signalValue(partial), -58);
  check('note mentions only what exists', ctx.buildSignalNote(partial), 'This reading: -58 dBm.');
}


console.log('\n== D: the device is recorded once, and again only on change ==');
{
  const sheet = makeSheet('Test Wine');
  const state = { sheet, wineName: 'Test Wine' };
  const spreadsheet = { getUrl: () => 'https://example/', getSheetId: () => 1 };
  const when = new Date(Date.UTC(2026, 7, 1, 6, 0, 0));

  ctx.__events.length = 0;

  ctx.noteDeviceChange(spreadsheet, state, { deviceId: 'AA:BB:CC:DD:EE:01' }, 3, when);
  check('first device recorded silently', ctx.__events.length, 0);
  check('and is readable as the wine\'s device', ctx.getStoredDeviceId(sheet), 'AA:BB:CC:DD:EE:01');
  check('no note on the first row', sheet.getRange(3, 13).getNotes()[0][0], '');

  ctx.noteDeviceChange(spreadsheet, state, { deviceId: 'AA:BB:CC:DD:EE:01' }, 4, when);
  check('same device again writes nothing', ctx.__events.length, 0);
  check('still no note', sheet.getRange(4, 13).getNotes()[0][0], '');

  ctx.noteDeviceChange(spreadsheet, state, { deviceId: 'AA:BB:CC:DD:EE:02' }, 5, when);
  check('a change is logged once', ctx.__events.length, 1);
  check('logged as DEVICE_CHANGED', ctx.__events[0].code, 'DEVICE_CHANGED');
  check('log names both devices',
    [ctx.__events[0].details.previousDevice, ctx.__events[0].details.currentDevice],
    ['AA:BB:CC:DD:EE:01', 'AA:BB:CC:DD:EE:02']);
  check('note lands on the record-id column, not column A',
    sheet.getRange(5, 13).getNotes()[0][0],
    'Device changed to AA:BB:CC:DD:EE:02 (previously AA:BB:CC:DD:EE:01).');
  check('column A note left alone for the gap marker', sheet.getRange(5, 1).getNotes()[0][0], '');
  check('stored device now updated', ctx.getStoredDeviceId(sheet), 'AA:BB:CC:DD:EE:02');

  ctx.noteDeviceChange(spreadsheet, state, { deviceId: '' }, 6, when);
  check('an empty device id changes nothing', ctx.getStoredDeviceId(sheet), 'AA:BB:CC:DD:EE:02');
}


console.log(failures === 0 ? '\nALL PASS\n' : '\n' + failures + ' FAILURE(S)\n');
process.exit(failures === 0 ? 0 : 1);
