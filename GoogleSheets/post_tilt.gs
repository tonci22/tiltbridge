/*
 * Set this to your own spreadsheet's id - the long string between /d/ and /edit
 * in its URL. The copy running in the Apps Script editor holds the real value;
 * it is deliberately not committed here.
 */
const SPREADSHEET_ID =
  'YOUR_SPREADSHEET_ID_HERE';

const TIME_ZONE = 'Europe/Zagreb';

/*
 * TiltBridge normally submits approximately every 10 minutes.
 *
 * 10 = save every submission
 * 20 = save approximately every 20 minutes
 * 30 = save approximately every 30 minutes
 * 60 = save approximately every hour
 */
const DEFAULT_LOG_INTERVAL_MINUTES = 10;

/* Rolling SG and temperature average period. */
const ROLLING_AVERAGE_HOURS = 4;

/* Write one rolling-average result every four hours. */
const AVERAGE_OUTPUT_INTERVAL_HOURS = 4;

/*
 * Mark a wine MISSING after no incoming reading for this long.
 *
 * Under the schemaVersion 2 batch protocol this is measured against the reading's
 * CAPTURE time, not the moment the request arrived. TiltBridge's default offline-queue
 * snapshot interval is 30 minutes, so the newest capture in a batch is routinely
 * 30+ minutes old on arrival and a threshold of 30 would flap every wine to MISSING.
 *
 * Keep this comfortably above the device's snapshot interval. 60 tolerates the 30-minute
 * default plus one missed snapshot. If you shorten the snapshot interval on the device,
 * this can come down with it; if you lengthen it, this must go up.
 */
const MISSING_READING_MINUTES = 60;

/* Check missing readings automatically every 15 minutes. */
const MISSING_CHECK_INTERVAL_MINUTES = 15;

/*
 * A TiltBridge request waits up to six seconds for another
 * wine request to finish its spreadsheet work.
 */
const WEBAPP_LOCK_WAIT_MS = 6000;

/*
 * Background checks are not limited by TiltBridge's HTTP timeout,
 * so they can wait longer for the spreadsheet lock.
 */
const BACKGROUND_LOCK_WAIT_MS = 30000;

/* Prefix used for durable error events waiting to be written. */
const PENDING_LOG_PREFIX = 'pending-system-log-';

const TITLE_COLOR = 'yellow';
const HEADER_COLOR = 'orange';
const NEW_DAY_COLOR = '#d9ead3';
const STATUS_OK_COLOR = '#d9ead3';
const STATUS_MISSING_COLOR = '#f4cccc';
const STATUS_NO_DATA_COLOR = '#fff2cc';
const DATA_GAP_COLOR = '#f4cccc';
const AVERAGE_COMPLETE_COLOR = '#d9ead3';
const AVERAGE_INCOMPLETE_COLOR = '#fff2cc';
const AVERAGE_INSUFFICIENT_COLOR = '#f4cccc';

/* Quality limits for each four-hour average window. */
const AVERAGE_COMPLETE_MAX_GAP_MINUTES = 30;
const AVERAGE_INCOMPLETE_MAX_GAP_MINUTES = 60;

/*
 * ---------------------------------------------------------------------------
 * Backfill repair
 * ---------------------------------------------------------------------------
 *
 * Columns E..I, the new-day shading and the average-quality note are derived
 * values: each is a pure function of columns A, B and C over a window of rows.
 * They used to be computed once, when the row was appended, from whatever rows
 * happened to be in the sheet at that moment - and never revisited.
 *
 * That is wrong the moment TiltBridge's offline queue exists. A reading that
 * failed to upload is kept on the device and delivered later, so rows arrive
 * for instants that are ALREADY covered by a four-hour average written earlier.
 * The queue had done its job, the data was complete, and the sheet still said
 * 'INCOMPLETE - 1h 30m gap' forever.
 *
 * So after a batch that inserted rows anywhere other than the end of the sheet,
 * every derived value from the insertion point down is recomputed from the rows
 * as they now stand. See rebuildDerivedColumns().
 *
 * Context needed above the first rebuilt row: the whole rolling window (4h)
 * plus the whole previous calendar day. A calendar day starts at most 48h
 * before any instant on the day after it, so 48h + the rolling window always
 * covers both, with no timezone arithmetic.
 */
const REBUILD_CONTEXT_LOOKBACK_MS =
  (48 + ROLLING_AVERAGE_HOURS) *
  60 * 60 * 1000;

/*
 * The firmware's schemaVersion 2 HTTP timeout is 15 seconds and the rebuild
 * runs inside the request, under the lock, so the in-request pass is capped.
 * A 24-hour outage at the ten-minute logging interval backfills ~144 rows, so
 * 600 covers roughly four days of catch-up without going near the timeout.
 *
 * Anything past the cap is queued as a pending rebuild and finished by the
 * background trigger, which has no HTTP timeout to respect.
 */
const REBUILD_MAX_ROWS_PER_REQUEST = 600;

/* The background pass is bounded only by the six-minute execution limit. */
const REBUILD_MAX_ROWS_PER_BACKGROUND_PASS = 4000;

/* Stop starting new rebuild work after this much of a background run. */
const REBUILD_BACKGROUND_BUDGET_MS = 240000;

/*
 * A second guard on the background loop. Each pass advances a sheet's pending
 * start by up to REBUILD_MAX_ROWS_PER_BACKGROUND_PASS rows, so the work always
 * terminates; this only bounds how much of it one trigger run takes on.
 */
const REBUILD_MAX_BACKGROUND_PASSES = 20;

/* Script Properties prefix for a sheet with a rebuild still owed to it. */
const PENDING_REBUILD_PREFIX = 'pending-derived-rebuild-';

/* Cell notes this script owns and may therefore clear on a rebuild. */
const DATA_GAP_NOTE_PREFIX = 'DATA GAP:';

/*
 * getBackgrounds() reports an unfilled cell as '#ffffff' and never as null, so
 * the rebuild writes that same value back rather than null. It keeps the
 * before/after comparison honest - a rebuild that changes nothing writes
 * nothing - and it is what sortWineSheetByCaptureTime() already round-trips.
 */
const NO_FILL_COLOR = '#ffffff';

const SYSTEM_LOG_SHEET = 'System Log';
const MONITORING_SHEET = 'Monitoring';

/* Monitoring layout: A..H as before, plus I for the wine's current device. */
const MONITORING_COLUMN_COUNT = 9;

/*
 * Wine sheet layout, thirteen columns:
 *
 *   A  Date and time         capture time, never the upload time
 *   B  SG                    final calibrated, smoothed value
 *   C  Temperature °C
 *   D  (blank visual separator)
 *   E  4-hour avg SG
 *   F  4-hour avg °C
 *   G  Average quality
 *   H  Previous day avg SG
 *   I  Previous day avg °C
 *   J  (blank visual separator)
 *   K  Raw SG            \  schemaVersion 2 only. The single-reading payload
 *   L  Signal dBm         |  carries none of these, so they stay blank there.
 *   M  Record id         /
 *
 * A..I keep the positions they had in every previous layout, so the charts,
 * the rolling averages, the daily averages, the new-day shading and the gap
 * detection all continue to address the same columns.
 *
 * WHAT THE DIAGNOSTIC BLOCK DROPPED, AND WHY
 *
 * The nineteen-column layout wrote every field the device sends. In practice
 * that is a wall of numbers nobody reads, so K..M now carry only what a column
 * can answer that a note cannot:
 *
 * - Smoothed SG is gone. Column B already IS the smoothed value - the payload's
 *   SG is calibrate(smooth(raw)) - so the two differ only by the calibration
 *   polynomial, and are identical on the default identity coefficients.
 *   Raw SG stays, because that one genuinely differs: it is pre-smoothing.
 *
 * - The five RSSI columns collapse to one. Column L is the average dBm, which
 *   is the only one worth charting or scanning down; the reading's own dBm and
 *   the min/max/sample-count live in a note on that cell, so the detail is one
 *   hover away instead of four columns wide.
 *
 * - Device (MAC) is gone from the rows. Repeating an unchanging 17-character
 *   address on every row says nothing. The current device per wine is shown on
 *   the Monitoring tab, and a CHANGE is recorded where a change actually
 *   matters: a note on that row's Record id cell plus a DEVICE_CHANGED entry
 *   in System Log.
 *
 * Nothing here is lost to the audit trail: '_processed_ids' still records every
 * record id with its wine, sheet, row, capture time, receipt time and outcome.
 */
const DATA_COLUMN_COUNT = 13;

const WINE_SHEET_HEADERS = [
  'Date and time',
  'SG',
  'Temperature °C',
  '',
  '4-hour avg SG',
  '4-hour avg °C',
  'Average quality',
  'Previous day avg SG',
  'Previous day avg °C',
  '',
  'Raw SG',
  'Signal dBm',
  'Record id'
];

/* Script Properties prefix holding the device last seen for a wine sheet. */
const WINE_DEVICE_PREFIX = 'wine-device-';

/*
 * Chart geometry, all derived from the layout so it cannot drift out of step
 * with it. Hardcoding the anchor is what put the charts on top of the data
 * when the table widened from nine columns to nineteen.
 *
 * HORIZONTAL: setPosition() places a chart's top-left corner at the anchor
 * cell, so a chart only ever extends right and down. Anchoring one clear
 * column past the last data column therefore makes horizontal overlap
 * impossible regardless of CHART_WIDTH_PIXELS.
 *
 * VERTICAL: a chart CHART_HEIGHT_PIXELS tall covers
 * ceil(height / row height) rows from its anchor. At the Google Sheets
 * default row height of 21px a 360px chart covers 18 rows, so anchoring the
 * SG chart at row 2 fills rows 2..19 and the temperature chart starts at
 * row 21, leaving one whole row of clearance.
 */
const CHART_WIDTH_PIXELS = 800;
const CHART_HEIGHT_PIXELS = 360;
const DEFAULT_ROW_HEIGHT_PIXELS = 21;

/* One blank column between the data and the charts. */
const CHART_COLUMN =
  DATA_COLUMN_COUNT + 2;

const CHART_ROW_SPAN = Math.ceil(
  CHART_HEIGHT_PIXELS /
  DEFAULT_ROW_HEIGHT_PIXELS
);

const SG_CHART_ROW = 2;

const TEMP_CHART_ROW =
  SG_CHART_ROW + CHART_ROW_SPAN + 1;

/*
 * Bump this whenever prepareSheet() must run again over already-prepared sheets.
 *
 * ensureSheetPrepared() skips prepareSheet() once the property
 * 'sheet-layout-<LAYOUT_VERSION>-<sheetId>' is '1', so a change to prepareSheet alone has
 * no effect on existing sheets - the version string is what invalidates that key.
 *
 * v15 exists because the diagnostic block narrowed from K-S to K-M. Every sheet has to be
 * re-prepared once so the headers, the number formats, the column widths, the title merge and
 * the chart anchor all follow the new width. Re-preparing an up-to-date sheet is
 * non-destructive - migrateLegacyLayoutIfNeeded returns early when the headers already match,
 * and prepareSheet only clears the D and J separator columns. A sheet on an OLDER layout is
 * reset to its raw readings instead; there is no in-place widening or narrowing any more.
 */
const LAYOUT_VERSION =
  'wine-layout-v15-narrow-diagnostics';


/*
 * ---------------------------------------------------------------------------
 * Enhanced queued mode (schemaVersion 2) settings
 * ---------------------------------------------------------------------------
 *
 * WHERE recordId IS STORED, AND WHY
 *
 * In two places, deliberately:
 *
 * - column M of the wine sheet, next to the row it identifies, so a row can
 *   be traced back to the exact device record that produced it;
 * - the hidden '_processed_ids' sheet, which remains the single source of
 *   truth for de-duplication.
 *
 * The wine sheet cannot be the dedup index on its own: a batch may span
 * several wine sheets, ids also have to survive for readings that were never
 * written as rows (throttled, rejected), and scanning every wine sheet on
 * every request would be far more expensive than reading one flat id list.
 *
 * '_processed_ids' stores the id together with the wine, the sheet name, the
 * row number, the capture time, the receipt time and the outcome, so ids that
 * produced no row are still accounted for.
 *
 * Of the schemaVersion 2 payload, 'mac' (a duplicate of deviceId), 'Comment' (always empty
 * in this phase) and 'SG_Smoothed' (column B already carries the smoothed value) are not
 * written anywhere. 'deviceId' is recorded per wine rather than per row - see
 * noteDeviceChange() - and the RSSI aggregates live in the note on column L.
 */
const PROCESSED_IDS_SHEET = '_processed_ids';

/* Keep at most this many processed record ids. Oldest rows are trimmed. */
const PROCESSED_ID_RETENTION = 20000;

/*
 * Safety cap for one request. The firmware default is 20 readings per batch.
 * Readings beyond this cap are left unacknowledged, so the device simply
 * resends them in the next batch.
 */
const BATCH_MAX_READINGS = 40;

/*
 * A batch performs up to BATCH_MAX_READINGS times the spreadsheet work of a
 * single reading, so it waits longer for the lock than the single-reading
 * path does. The firmware's schemaVersion 2 HTTP timeout is 15 seconds, so a
 * 10-second lock wait still leaves room for the write itself.
 *
 * WEBAPP_LOCK_WAIT_MS is deliberately left at 6000 for the single-reading
 * path so that its behaviour and its log messages are unchanged.
 */
const WEBAPP_BATCH_LOCK_WAIT_MS = 10000;


/*
 * RUN THIS FUNCTION MANUALLY ONCE AFTER PASTING THE CODE.
 *
 * It:
 * - formats existing wine sheets,
 * - creates or refreshes both charts,
 * - creates Monitoring and System Log tabs,
 * - installs the automatic missing-reading check,
 * - flags transmission gaps and average quality,
 * - runs the first status check.
 *
 * If you are upgrading a spreadsheet that already holds readings, run
 * rebuildAllDerivedColumns() once as well. Earlier versions computed each
 * four-hour average once, when its row was written, so windows the offline
 * queue completed after the fact are still marked INCOMPLETE.
 */
function initialSetup() {
  const lock = LockService.getScriptLock();

  if (!lock.tryLock(BACKGROUND_LOCK_WAIT_MS)) {
    queuePendingLog(
      'ERROR',
      'System',
      'INITIAL_SETUP_BUSY',
      'Initial setup could not acquire the spreadsheet lock.',
      {
        waitMilliseconds: BACKGROUND_LOCK_WAIT_MS
      }
    );

    throw new Error(
      'Initial setup could not acquire the spreadsheet lock.'
    );
  }

  try {
    const spreadsheet =
      SpreadsheetApp.openById(SPREADSHEET_ID);

    ensureSpreadsheetTimeZone(spreadsheet);
    ensureSystemLogSheet(spreadsheet);
    ensureMonitoringSheet(spreadsheet);
    flushPendingLogs(spreadsheet);

    spreadsheet.getSheets().forEach(function (sheet) {
      if (!isWineSheet(sheet)) {
        return;
      }

      const wineName = getWineNameFromSheet(sheet);

      /* prepareSheet() rebuilds the charts as part of the layout. */
      prepareSheet(sheet, wineName);
      markSheetPrepared(sheet);
    });

    installMissingReadingTrigger();

    safeLogEvent(
      'INFO',
      'System',
      'INITIAL_SETUP_COMPLETE',
      'Initial setup completed successfully.',
      {
        logIntervalMinutes:
          DEFAULT_LOG_INTERVAL_MINUTES,
        rollingAverageHours:
          ROLLING_AVERAGE_HOURS,
        averageOutputIntervalHours:
          AVERAGE_OUTPUT_INTERVAL_HOURS,
        completeAverageMaxGapMinutes:
          AVERAGE_COMPLETE_MAX_GAP_MINUTES,
        incompleteAverageMaxGapMinutes:
          AVERAGE_INCOMPLETE_MAX_GAP_MINUTES,
        missingReadingMinutes:
          MISSING_READING_MINUTES,
        missingCheckMinutes:
          MISSING_CHECK_INTERVAL_MINUTES,
        webappLockWaitMilliseconds:
          WEBAPP_LOCK_WAIT_MS
      }
    );

  } finally {
    lock.releaseLock();
  }

  /* Run after releasing the setup lock. */
  checkForMissingReadings();
}

function installMissingReadingTrigger() {
  ScriptApp.getProjectTriggers().forEach(function (trigger) {
    if (
      trigger.getHandlerFunction() ===
      'checkForMissingReadings'
    ) {
      ScriptApp.deleteTrigger(trigger);
    }
  });

  ScriptApp
    .newTrigger('checkForMissingReadings')
    .timeBased()
    .everyMinutes(MISSING_CHECK_INTERVAL_MINUTES)
    .create();
}


/*
 * Optional manual tool: immediately writes all queued errors to System Log.
 */
function flushPendingLogsNow() {
  const lock = LockService.getScriptLock();

  if (!lock.tryLock(BACKGROUND_LOCK_WAIT_MS)) {
    throw new Error(
      'Could not acquire the spreadsheet lock to flush pending logs.'
    );
  }

  try {
    const spreadsheet =
      SpreadsheetApp.openById(SPREADSHEET_ID);

    ensureSpreadsheetTimeZone(spreadsheet);
    flushPendingLogs(spreadsheet);

  } finally {
    lock.releaseLock();
  }
}


function doGet() {
  return ContentService
    .createTextOutput(
      'TiltBridge Google Sheets endpoint is online.'
    )
    .setMimeType(ContentService.MimeType.TEXT);
}


function doPost(e) {
  const lock = LockService.getScriptLock();

  let rawPayload = '';
  let wineForLog = 'Unknown Wine';
  let tiltData;
  const receivedAt = new Date();

  /*
   * Parse the request before trying to acquire the spreadsheet lock.
   * This lets a lock-timeout log identify the correct wine.
   */
  if (
    !e ||
    !e.postData ||
    !e.postData.contents
  ) {
    queuePendingLog(
      'ERROR',
      wineForLog,
      'NO_POST_DATA',
      'No POST data was received.',
      {}
    );

    return jsonResponse({
      status: 'error',
      code: 'NO_POST_DATA',
      message: 'No POST data was received.'
    });
  }

  rawPayload = e.postData.contents;

  try {
    tiltData = JSON.parse(rawPayload);
  } catch (parseError) {
    queuePendingLog(
      'ERROR',
      wineForLog,
      'INVALID_JSON',
      'TiltBridge sent data that was not valid JSON.',
      {
        payload: truncateText(rawPayload, 1500),
        parseError:
          parseError && parseError.message
            ? parseError.message
            : String(parseError)
      }
    );

    return jsonResponse({
      status: 'error',
      code: 'INVALID_JSON',
      message:
        'TiltBridge sent data that was not valid JSON.'
    });
  }

  /*
   * Enhanced queued mode. TiltBridge firmware with "Enhanced Google Sheets
   * mode" enabled posts a batch of queued readings instead of one live
   * reading, and expects acceptedRecordIds back. Everything below this branch
   * is the original single-reading path and is reached unchanged by any
   * request that does not declare schemaVersion 2.
   */
  if (
    tiltData &&
    Number(tiltData.schemaVersion) === 2
  ) {
    return handleBatchPost(
      tiltData,
      rawPayload,
      receivedAt
    );
  }

  wineForLog = firstPresent(
    tiltData.Beer,
    tiltData.Color,
    'Unknown Wine'
  );

  /*
   * Capture the preceding incoming reading before advancing it, then advance
   * it to this request's time. Done before the lock so that a lock timeout
   * cannot produce a false missing-reading warning. The batch path uses the
   * same helper with the readings' capture times.
   */
  const wineContexts =
    snapshotIncomingReadings([{
      wineName: wineForLog,
      captureDate: receivedAt
    }]);

  if (!lock.tryLock(WEBAPP_LOCK_WAIT_MS)) {
    queuePendingLog(
      'ERROR',
      wineForLog,
      'WEBAPP_BUSY',
      'The spreadsheet lock could not be acquired within 6 seconds.',
      {
        waitMilliseconds: WEBAPP_LOCK_WAIT_MS,
        receivedAt:
          Utilities.formatDate(
            receivedAt,
            TIME_ZONE,
            'dd.MM.yyyy HH:mm:ss'
          ),
        payload: truncateText(rawPayload, 1500)
      }
    );

    return jsonResponse({
      status: 'error',
      code: 'WEBAPP_BUSY',
      message:
        'The spreadsheet lock could not be acquired within 6 seconds.'
    });
  }

  try {
    const spreadsheet =
      SpreadsheetApp.openById(SPREADSHEET_ID);

    ensureSpreadsheetTimeZone(spreadsheet);

    /*
     * Write any errors that were queued while the spreadsheet was busy.
     */
    flushPendingLogs(spreadsheet);

    const sg = toNumber(tiltData.SG);
    const tempF = toNumber(tiltData.Temp);

    if (sg === '') {
      throw codedError(
        'INVALID_SG',
        'The SG field was missing or was not numeric.'
      );
    }

    if (tempF === '') {
      throw codedError(
        'INVALID_TEMPERATURE',
        'The temperature field was missing or was not numeric.'
      );
    }

    /*
     * From here the single-reading path is a thin caller over exactly the
     * same code the batch path uses. The one difference is the capture time:
     * this payload carries none, so the receipt time is the best available
     * approximation and is passed as the capture time.
     */
    const state = getWineSheetState(
      spreadsheet,
      {},
      wineContexts,
      wineForLog,
      receivedAt
    );

    const outcome = appendMeasurementRow(
      spreadsheet,
      state,
      buildLegacyMeasurement(
        tiltData,
        receivedAt,
        sg,
        tempF
      )
    );

    return createSuccessResponse(
      spreadsheet,
      state.sheet,
      outcome.rowNumber !== null,
      outcome.rowNumber === null
        ? 'LOG_INTERVAL_NOT_REACHED'
        : 'READING_SAVED'
    );

  } catch (error) {
    const code =
      error && error.code
        ? error.code
        : 'POST_ERROR';

    const message =
      error && error.message
        ? error.message
        : String(error);

    /*
     * The lock is owned here, so writing directly to System Log is safe.
     */
    safeLogEvent(
      'ERROR',
      wineForLog,
      code,
      message,
      {
        stack:
          error && error.stack
            ? error.stack
            : '',
        payload:
          truncateText(rawPayload, 1500)
      }
    );

    return jsonResponse({
      status: 'error',
      code: code,
      message: message
    });

  } finally {
    lock.releaseLock();
  }
}

/*
 * ===========================================================================
 * Enhanced queued mode (schemaVersion 2)
 * ===========================================================================
 *
 * Request body:
 *
 *   { schemaVersion: 2, deviceName, Email, tzOffset,
 *     readings: [ { recordId, deviceId, mac, Beer, Color, Temp (F), SG,
 *                   SG_Raw, SG_Smoothed, RSSI, RSSI_Avg, RSSI_Min, RSSI_Max,
 *                   RSSI_Samples, CapturedAtUtc, TimestampValid,
 *                   UptimeMsAtCapture, Comment }, ... ] }
 *
 * Response body:
 *
 *   { status, code, logged, savedRows, acceptedRecordIds, doclongurl }
 *
 * The device deletes a queued record only when its id comes back inside
 * acceptedRecordIds. Delivery is therefore at-least-once and duplicate
 * suppression happens here, keyed on recordId via '_processed_ids'.
 *
 * Everything in this section is additive. No function used by the
 * single-reading path is modified.
 */
function handleBatchPost(
  body,
  rawPayload,
  receivedAt
) {
  const lock = LockService.getScriptLock();

  const deviceName = firstPresent(
    body.deviceName,
    'Unknown TiltBridge'
  );

  /*
   * body.Email and body.tzOffset are accepted for protocol completeness but
   * are not used here: the spreadsheet is fixed by SPREADSHEET_ID and every
   * date is rendered in TIME_ZONE. CapturedAtUtc is authoritative.
   */

  if (!Array.isArray(body.readings)) {
    queuePendingLog(
      'ERROR',
      'System',
      'BATCH_NO_READINGS',
      'A schemaVersion 2 request did not contain a readings array.',
      {
        deviceName: deviceName,
        payload: truncateText(rawPayload, 1500)
      }
    );

    return jsonResponse({
      status: 'error',
      code: 'BATCH_NO_READINGS',
      message:
        'A schemaVersion 2 request did not contain a readings array.',
      acceptedRecordIds: []
    });
  }

  const readings =
    body.readings.slice(0, BATCH_MAX_READINGS);

  const droppedReadingCount =
    body.readings.length - readings.length;

  /*
   * Read each wine's previous incoming timestamp before advancing it, exactly
   * as the single-reading path does, then advance it to the NEWEST capture
   * time in this batch. A backlog upload is therefore never mistaken for a
   * transmission gap, and the 15-minute missing-reading check keeps working
   * off a timestamp this path keeps updating.
   */
  const wineContexts =
    snapshotBatchIncomingReadings(readings);

  if (!lock.tryLock(WEBAPP_BATCH_LOCK_WAIT_MS)) {
    queuePendingLog(
      'ERROR',
      'System',
      'WEBAPP_BUSY',
      'The spreadsheet lock could not be acquired for a queued batch.',
      {
        deviceName: deviceName,
        readingCount: readings.length,
        waitMilliseconds:
          WEBAPP_BATCH_LOCK_WAIT_MS,
        receivedAt:
          Utilities.formatDate(
            receivedAt,
            TIME_ZONE,
            'dd.MM.yyyy HH:mm:ss'
          ),
        payload: truncateText(rawPayload, 1500)
      }
    );

    /*
     * Nothing was persisted, so no record id is acknowledged and the device
     * resends this batch unchanged with the same ids.
     */
    return jsonResponse({
      status: 'error',
      code: 'WEBAPP_BUSY',
      message:
        'The spreadsheet lock could not be acquired for a queued batch.',
      acceptedRecordIds: []
    });
  }

  /*
   * Declared outside the try so that a fatal error part way through a batch
   * can still acknowledge the rows that did land.
   */
  const acceptedRecordIds = [];

  let spreadsheet = null;
  let lastTouchedSheet = null;
  let savedRowCount = 0;

  try {
    spreadsheet =
      SpreadsheetApp.openById(SPREADSHEET_ID);

    ensureSpreadsheetTimeZone(spreadsheet);

    /*
     * Write any errors that were queued while the spreadsheet was busy.
     */
    flushPendingLogs(spreadsheet);

    const store =
      openProcessedIdStore(spreadsheet);

    const sheetStates = {};

    if (droppedReadingCount > 0) {
      safeLogEvent(
        'WARNING',
        'System',
        'BATCH_TRUNCATED',
        'A queued batch contained more readings than this script processes in one request.',
        {
          deviceName: deviceName,
          receivedReadings:
            body.readings.length,
          processedReadings: readings.length,
          maximumReadings: BATCH_MAX_READINGS
        }
      );
    }

    for (
      let i = 0;
      i < readings.length;
      i++
    ) {
      const reading = readings[i] || {};

      const recordId =
        String(
          firstPresent(reading.recordId, '')
        ).trim();

      if (!recordId) {
        safeLogEvent(
          'WARNING',
          'System',
          'MISSING_RECORD_ID',
          'A queued reading arrived without a recordId and cannot be de-duplicated.',
          {
            deviceName: deviceName,
            position: i,
            reading:
              truncateText(
                safeJsonStringify(reading),
                1000
              )
          }
        );

        continue;
      }

      /*
       * Already stored, most likely by a request whose acknowledgement was
       * lost in transit. No second row is written, but the id IS returned so
       * the device finally drops the record.
       */
      if (store.known.has(recordId)) {
        acceptedRecordIds.push(recordId);
        continue;
      }

      const wineForLog =
        resolveWineName(reading);

      try {
        const sg = toNumber(reading.SG);
        const tempF = toNumber(reading.Temp);

        if (
          sg === '' ||
          tempF === ''
        ) {
          /*
           * A structurally broken reading can never become valid, so leaving
           * it unacknowledged would pin a device queue slot forever. It is
           * recorded as rejected and acknowledged, and never written.
           */
          safeLogEvent(
            'ERROR',
            wineForLog,
            'INVALID_READING',
            'A queued reading was discarded because SG or temperature was missing or not numeric.',
            {
              deviceName: deviceName,
              recordId: recordId,
              sg: reading.SG,
              temp: reading.Temp
            }
          );

          recordProcessedId(
            store,
            recordId,
            wineForLog,
            '',
            '',
            null,
            'REJECTED_INVALID'
          );

          store.known.add(recordId);
          acceptedRecordIds.push(recordId);

          continue;
        }

        const state = getWineSheetState(
          spreadsheet,
          sheetStates,
          wineContexts,
          wineForLog,
          receivedAt
        );

        const outcome = appendMeasurementRow(
          spreadsheet,
          state,
          buildBatchMeasurement(
            reading,
            recordId,
            sg,
            tempF
          )
        );

        recordProcessedId(
          store,
          recordId,
          wineForLog,
          state.sheetName,
          outcome.rowNumber === null
            ? ''
            : outcome.rowNumber,
          outcome.captureDate,
          outcome.result
        );

        store.known.add(recordId);
        acceptedRecordIds.push(recordId);

        lastTouchedSheet = state.sheet;

        if (outcome.rowNumber !== null) {
          savedRowCount++;
        }

      } catch (readingError) {
        /*
         * The id is deliberately left out of acceptedRecordIds. The device
         * keeps the record queued and resends it with the same id.
         */
        safeLogEvent(
          'ERROR',
          wineForLog,
          readingError && readingError.code
            ? readingError.code
            : 'BATCH_ROW_ERROR',
          readingError && readingError.message
            ? readingError.message
            : String(readingError),
          {
            deviceName: deviceName,
            recordId: recordId,
            stack:
              readingError && readingError.stack
                ? readingError.stack
                : '',
            reading:
              truncateText(
                safeJsonStringify(reading),
                1000
              )
          }
        );
      }
    }

    flushTimestampWarnings(
      spreadsheet,
      sheetStates,
      deviceName
    );

    /*
     * Still inside the lock: a backlog can land under rows that were logged
     * earlier with later capture times, so each sheet this batch touched is
     * put back into chronological order before the response goes out.
     */
    sortAppendedSheets(
      spreadsheet,
      sheetStates,
      deviceName
    );

    trimProcessedIdStore(
      store,
      PROCESSED_ID_RETENTION
    );

    /*
     * 'partial' whenever something was not acknowledged. The firmware treats
     * both the same and acts only on acceptedRecordIds, but it makes the
     * difference visible when testing the endpoint by hand.
     */
    return jsonResponse({
      status:
        acceptedRecordIds.length ===
        body.readings.length
          ? 'ok'
          : 'partial',
      code: 'BATCH_PROCESSED',
      logged: savedRowCount > 0,
      savedRows: savedRowCount,
      acceptedRecordIds: acceptedRecordIds,
      doclongurl:
        buildBatchDocumentUrl(
          spreadsheet,
          lastTouchedSheet
        )
    });

  } catch (error) {
    const code =
      error && error.code
        ? error.code
        : 'BATCH_POST_ERROR';

    const message =
      error && error.message
        ? error.message
        : String(error);

    /*
     * The lock is owned here, so writing directly to System Log is safe.
     */
    safeLogEvent(
      'ERROR',
      'System',
      code,
      message,
      {
        deviceName: deviceName,
        stack:
          error && error.stack
            ? error.stack
            : '',
        acknowledgedRecords:
          acceptedRecordIds.length,
        payload:
          truncateText(rawPayload, 1500)
      }
    );

    /*
     * Rows written before the failure are already committed and their ids are
     * already in '_processed_ids', so acknowledging them here avoids a pointless
     * resend. Everything else stays queued on the device.
     */
    /*
     * The firmware's response buffer is a fixed 2048 bytes and truncation
     * there reads as a malformed reply, so the message is kept short. Twenty
     * record ids plus the document URL already use about 600 bytes.
     */
    return jsonResponse({
      status: 'partial',
      code: code,
      message: truncateText(message, 300),
      logged: savedRowCount > 0,
      savedRows: savedRowCount,
      acceptedRecordIds: acceptedRecordIds,
      doclongurl:
        buildBatchDocumentUrl(
          spreadsheet,
          lastTouchedSheet
        )
    });

  } finally {
    lock.releaseLock();
  }
}


/*
 * Reads every wine's previous incoming timestamp, then advances it to the
 * newest capture time present in this batch.
 *
 * Two things matter here:
 *
 * - the previous value must be read BEFORE it is advanced, because it is the
 *   reference used for transmission-gap detection;
 * - it is advanced to the newest CAPTURE time, never to the upload time, and
 *   never backwards. Never backwards matters when a user switches from the
 *   single-reading path, where the stored value is a receipt time that can be
 *   newer than the oldest queued capture.
 *
 * Run before the lock is taken, mirroring the single-reading path, so that a
 * lock timeout cannot produce a false missing-reading warning.
 */
function snapshotBatchIncomingReadings(readings) {
  return snapshotIncomingReadings(
    readings
      .filter(function (reading) {
        return Boolean(reading);
      })
      .map(function (reading) {
        return {
          wineName:
            resolveWineName(reading),
          captureDate:
            parseCapturedAtUtc(reading)
        };
      })
  );
}


/*
 * Reads every wine's previous incoming timestamp, then advances it to the
 * newest capture time supplied.
 *
 * Two things matter here:
 *
 * - the previous value must be read BEFORE it is advanced, because it is the
 *   reference used for transmission-gap detection;
 * - it is advanced to the newest CAPTURE time, never to the upload time, and
 *   never backwards. Never backwards matters when a user turns enhanced mode
 *   off again, where the stored value would otherwise be rewound by a receipt
 *   time older than the newest capture already recorded.
 *
 * Both paths call this before taking the lock, so a lock timeout cannot
 * produce a false missing-reading warning.
 *
 * entries: [{ wineName, captureDate }], where captureDate may be null.
 */
function snapshotIncomingReadings(entries) {
  const contexts = {};

  entries.forEach(function (entry) {
    if (!entry) {
      return;
    }

    const key = 'wine:' + entry.wineName;

    if (!contexts[key]) {
      contexts[key] = {
        wineName: entry.wineName,
        previousIncomingDate:
          getStoredLastIncomingReading(
            entry.wineName
          ),
        newestCaptureDate: null
      };
    }

    if (!entry.captureDate) {
      return;
    }

    const context = contexts[key];

    if (
      !context.newestCaptureDate ||
      entry.captureDate.getTime() >
        context.newestCaptureDate.getTime()
    ) {
      context.newestCaptureDate =
        entry.captureDate;
    }
  });

  Object.keys(contexts).forEach(function (key) {
    const context = contexts[key];

    if (context.newestCaptureDate) {
      rememberIncomingReadingIfNewer(
        context.wineName,
        context.newestCaptureDate
      );
    }
  });

  return contexts;
}


function resolveWineName(reading) {
  return firstPresent(
    reading.Beer,
    reading.Color,
    'Unknown Wine'
  );
}


/*
 * Advance the remembered incoming timestamp only when the supplied date is
 * newer. rememberIncomingReading() stays unconditional for any caller that
 * genuinely wants to overwrite.
 */
function rememberIncomingReadingIfNewer(
  wineName,
  date
) {
  if (
    !date ||
    isNaN(date.getTime())
  ) {
    return;
  }

  const stored =
    getStoredLastIncomingReading(wineName);

  if (
    stored &&
    stored.getTime() >= date.getTime()
  ) {
    return;
  }

  rememberIncomingReading(wineName, date);
}


/*
 * Returns the reading's capture time, or null when the device had no
 * trustworthy clock. The 'Z' suffix parses as UTC and Sheets renders the
 * resulting Date in the spreadsheet timezone, so no offset arithmetic is
 * applied here and none should ever be added.
 */
function parseCapturedAtUtc(reading) {
  if (!reading) {
    return null;
  }

  if (
    reading.TimestampValid === false ||
    reading.TimestampValid === 'false'
  ) {
    return null;
  }

  const text = firstPresent(
    reading.CapturedAtUtc,
    ''
  );

  if (!text) {
    return null;
  }

  const parsed = new Date(String(text));

  if (isNaN(parsed.getTime())) {
    return null;
  }

  return parsed;
}


/*
 * Per-wine working state, shared by both paths.
 *
 * A batch may span several wines, so the sheet, its tail dates and its next
 * free row are resolved once per wine and then advanced in memory. Keeping
 * the dates in memory is what lets the rolling and daily averages treat each
 * appended row's own capture time as "now" without re-reading the sheet tail
 * for every row. The single-reading path passes a throwaway {} for
 * sheetStates and simply gets a one-entry cache.
 */
function getWineSheetState(
  spreadsheet,
  sheetStates,
  wineContexts,
  wineName,
  receivedAt
) {
  const key = 'wine:' + wineName;

  if (sheetStates[key]) {
    return sheetStates[key];
  }

  const sheetName =
    sanitizeSheetName(wineName);

  let sheet =
    spreadsheet.getSheetByName(sheetName);

  if (!sheet) {
    sheet = spreadsheet.insertSheet(sheetName);
    prepareSheet(sheet, wineName);
    markSheetPrepared(sheet);

    safeLogEvent(
      'INFO',
      wineName,
      'NEW_WINE_SHEET_CREATED',
      'A new wine sheet was created with the standard layout and both charts.',
      {
        sheetName: sheetName
      }
    );

  } else {
    ensureSheetPrepared(
      sheet,
      wineName
    );
  }

  handleReadingRecovery(
    spreadsheet,
    sheet,
    wineName,
    receivedAt
  );

  const context =
    wineContexts[key] || {};

  const lastAverageOutputDate =
    getLastAverageOutputDate(sheet);

  const state = {
    wineName: wineName,
    sheetName: sheetName,
    sheet: sheet,

    /* Reference for the first gap check of this request. */
    previousIncomingDate:
      context.previousIncomingDate || null,

    /* Capture time of the newest row actually written to the sheet. */
    lastSavedCaptureDate:
      getLastMeasurementDate(sheet),

    /*
     * Capture time of the previous reading seen in this request, saved or
     * not. This is the transmission-gap reference: a reading the logging
     * interval declined to store still proves the device transmitted.
     */
    lastCaptureSeen: null,

    lastAverageOutputDate:
      lastAverageOutputDate,

    firstMeasurementDate:
      lastAverageOutputDate
        ? null
        : getFirstMeasurementDate(sheet),

    untimestampedRecordIds: [],

    /* Rows this request appended; drives the post-batch ordering check. */
    rowsAppended: 0,

    /* Data rows start at row 3, below the title and the header. */
    nextRow: Math.max(
      3,
      sheet.getLastRow() + 1
    )
  };

  sheetStates[key] = state;

  return state;
}


/*
 * THE row-writing function. Both the single-reading path and the batch path
 * go through here, so gap detection, the logging-interval throttle, the
 * rolling four-hour average, the previous-day average and the new-day shading
 * exist exactly once.
 *
 * measurement: {
 *   captureDate,   Date, or null when the device had no trustworthy clock
 *   sg, tempC,     required, temperature already converted to Celsius
 *   sgRaw,
 *   rssi, rssiAvg, rssiMin, rssiMax, rssiSamples,
 *   deviceId, recordId, uptimeMsAtCapture
 * }
 *
 * Everything after tempC is optional and written as '' when absent, which is
 * exactly what the single-reading payload produces.
 *
 * Returns { rowNumber, captureDate, result }, where rowNumber is null when
 * the logging interval deliberately skipped the reading and result is one of
 * SAVED, SKIPPED_LOG_INTERVAL, NO_VALID_TIMESTAMP.
 *
 * Throwing from here leaves a batch record id out of acceptedRecordIds, which
 * is how a genuine write failure gets retried by the device.
 */
function appendMeasurementRow(
  spreadsheet,
  state,
  measurement
) {
  const captureDate = measurement.captureDate;

  if (!captureDate) {
    return appendUntimestampedRow(
      state,
      measurement
    );
  }

  /*
   * Gaps are measured between consecutive CAPTURES, so a backlog that was
   * captured on schedule and uploaded hours later is not a gap. On the
   * single-reading path the capture time is the receipt time, which makes
   * this identical to the original request-to-request comparison.
   */
  const gapReferenceDate =
    state.lastCaptureSeen ||
    state.previousIncomingDate ||
    state.lastSavedCaptureDate;

  const incomingGapMinutes =
    gapReferenceDate
      ? Math.max(
          0,
          (
            captureDate.getTime() -
            gapReferenceDate.getTime()
          ) / 60000
        )
      : 0;

  const hasIncomingDataGap =
    Boolean(gapReferenceDate) &&
    incomingGapMinutes > MISSING_READING_MINUTES;

  state.lastCaptureSeen = captureDate;

  /*
   * The logging-interval throttle compares capture times. Comparing receipt
   * times would make every reading in a backlog look simultaneous and would
   * discard everything after the first row.
   *
   * A reading after a real transmission gap is always saved, even when the
   * configured normal spreadsheet logging interval has not elapsed.
   */
  if (
    !hasIncomingDataGap &&
    !shouldLogMeasurement(
      state.lastSavedCaptureDate,
      captureDate,
      DEFAULT_LOG_INTERVAL_MINUTES
    )
  ) {
    return {
      rowNumber: null,
      captureDate: captureDate,
      result: 'SKIPPED_LOG_INTERVAL'
    };
  }

  const averages = emptyAverageSet();

  let averageQualityColor = null;
  let averageQualityNote = '';

  /*
   * Raw readings continue at the normal logging interval.
   * Average columns are populated only once every four hours, measured on
   * capture times so a backlog produces its four-hour points where they
   * actually belong.
   */
  if (
    shouldWriteStateAverage(
      state,
      captureDate,
      AVERAGE_OUTPUT_INTERVAL_HOURS
    )
  ) {
    const classified =
      classifyRollingAssessment(
        calculateRollingAverageAssessment(
          state.sheet,
          captureDate,
          ROLLING_AVERAGE_HOURS,
          measurement.sg,
          measurement.tempC
        )
      );

    averages.rollingSG = classified.rollingSG;
    averages.rollingTempC = classified.rollingTempC;
    averages.quality = classified.quality;
    averageQualityColor = classified.color;
    averageQualityNote = classified.note;

    state.lastAverageOutputDate = captureDate;
  }

  const firstReadingOfDay =
    isDifferentCalendarDay(
      state.lastSavedCaptureDate,
      captureDate,
      TIME_ZONE
    );

  if (
    firstReadingOfDay &&
    state.lastSavedCaptureDate
  ) {
    const previousDateKey =
      getPreviousCalendarDateKey(
        captureDate,
        TIME_ZONE
      );

    const previousDayAverages =
      calculateDailyAverages(
        state.sheet,
        previousDateKey,
        TIME_ZONE
      );

    averages.previousDaySG =
      previousDayAverages.sg;

    averages.previousDayTempC =
      previousDayAverages.tempC;
  }

  const newRow = state.nextRow;

  /*
   * Column A is the reading's own capture time, never the upload time.
   * new Date(CapturedAtUtc) already carries the correct instant, and Sheets
   * renders it in the spreadsheet timezone.
   */
  state.sheet
    .getRange(newRow, 1, 1, DATA_COLUMN_COUNT)
    .setValues([
      buildMeasurementRowValues(
        measurement,
        captureDate,
        averages
      )
    ]);

  state.sheet
    .getRange(newRow, 1, 1, DATA_COLUMN_COUNT)
    .setBackground(null);

  if (firstReadingOfDay) {
    state.sheet
      .getRange(newRow, 1, 1, DATA_COLUMN_COUNT)
      .setBackground(NEW_DAY_COLOR);
  }

  if (hasIncomingDataGap) {
    const gapText =
      formatDurationMinutes(incomingGapMinutes);

    state.sheet
      .getRange(newRow, 1, 1, 3)
      .setBackground(DATA_GAP_COLOR);

    state.sheet
      .getRange(newRow, 1)
      .setNote(
        'DATA GAP: ' +
        gapText +
        ' since the previous captured TiltBridge reading.'
      );

    safeLogEvent(
      'WARNING',
      state.wineName,
      'DATA_GAP',
      'A reading was captured after a data gap of ' +
        gapText +
        '.',
      {
        gapMinutes: incomingGapMinutes,
        recordId: measurement.recordId || '',
        previousCapturedReading:
          Utilities.formatDate(
            gapReferenceDate,
            TIME_ZONE,
            'dd.MM.yyyy HH:mm:ss'
          ),
        currentCapturedReading:
          Utilities.formatDate(
            captureDate,
            TIME_ZONE,
            'dd.MM.yyyy HH:mm:ss'
          ),
        thresholdMinutes:
          MISSING_READING_MINUTES,
        sheetUrl:
          spreadsheet.getUrl() +
          '#gid=' +
          state.sheet.getSheetId()
      }
    );
  }

  if (averages.quality) {
    state.sheet
      .getRange(newRow, 7)
      .setBackground(averageQualityColor)
      .setFontWeight('bold')
      .setNote(averageQualityNote);
  }

  /* The RSSI detail that used to occupy four columns of its own. */
  const signalNote = buildSignalNote(measurement);

  if (signalNote) {
    state.sheet
      .getRange(newRow, 12)
      .setNote(signalNote);
  }

  noteDeviceChange(
    spreadsheet,
    state,
    measurement,
    newRow,
    captureDate
  );

  state.nextRow = newRow + 1;
  state.rowsAppended++;
  state.lastSavedCaptureDate = captureDate;

  if (!state.firstMeasurementDate) {
    state.firstMeasurementDate = captureDate;
  }

  return {
    rowNumber: newRow,
    captureDate: captureDate,
    result: 'SAVED'
  };
}


/*
 * A reading captured while the device had no trustworthy clock.
 *
 * Column A is left empty on purpose. Every derived calculation in this script
 * keys off column A, and all of the backwards scans skip rows whose column A
 * does not parse as a date, so this row is automatically excluded from the
 * rolling averages, the daily averages and the new-day shading instead of
 * poisoning them with a fabricated time.
 *
 * The row is still acknowledged by the caller so the device stops retrying it.
 */
function appendUntimestampedRow(
  state,
  measurement
) {
  const newRow = state.nextRow;

  state.sheet
    .getRange(newRow, 1, 1, DATA_COLUMN_COUNT)
    .setValues([
      buildMeasurementRowValues(
        measurement,
        null,
        emptyAverageSet()
      )
    ]);

  state.sheet
    .getRange(newRow, 1, 1, DATA_COLUMN_COUNT)
    .setBackground(null);

  const uptimeMs = toNumber(
    measurement.uptimeMsAtCapture
  );

  const uptimeText =
    uptimeMs === ''
      ? 'unknown'
      : formatDurationMinutes(
          uptimeMs / 60000
        );

  state.sheet
    .getRange(newRow, 1)
    .setNote(
      'NO VALID TIMESTAMP: the TiltBridge clock was not trustworthy when ' +
      'this reading was captured, so no capture time could be recorded. ' +
      'Device uptime at capture: ' +
      uptimeText +
      '. Record id: ' +
      (measurement.recordId || 'none') +
      '. This row is excluded from all average calculations.'
    );

  state.nextRow = newRow + 1;
  state.rowsAppended++;

  state.untimestampedRecordIds.push(
    measurement.recordId || '(no record id)'
  );

  return {
    rowNumber: newRow,
    captureDate: null,
    result: 'NO_VALID_TIMESTAMP'
  };
}


/*
 * The thirteen cell values of one data row, in layout order. The single place
 * that knows the column order, so changing the layout again means editing
 * WINE_SHEET_HEADERS and this function together.
 */
function buildMeasurementRowValues(
  measurement,
  captureDate,
  averages
) {
  return [
    captureDate || '',
    measurement.sg,
    measurement.tempC,
    '',
    averages.rollingSG,
    averages.rollingTempC,
    averages.quality,
    averages.previousDaySG,
    averages.previousDayTempC,
    '',
    valueOrBlank(measurement.sgRaw),
    signalValue(measurement),
    valueOrBlank(measurement.recordId)
  ];
}


/*
 * The one number that goes in column L.
 *
 * The average is the honest summary of a scan window and the only one worth
 * charting. A device that sent no aggregate at all still has its own reading's
 * dBm, so that stands in rather than leaving the column blank.
 */
function signalValue(measurement) {
  if (
    measurement.rssiAvg !== undefined &&
    measurement.rssiAvg !== null &&
    measurement.rssiAvg !== ''
  ) {
    return measurement.rssiAvg;
  }

  return valueOrBlank(measurement.rssi);
}


/*
 * The detail that used to be four extra columns, as a note on the signal cell.
 * Returns '' when the payload carried nothing worth expanding on, in which case
 * no note is written at all.
 */
function buildSignalNote(measurement) {
  const parts = [];

  if (
    measurement.rssi !== undefined &&
    measurement.rssi !== null &&
    measurement.rssi !== ''
  ) {
    parts.push(
      'This reading: ' + measurement.rssi + ' dBm.'
    );
  }

  const hasRange =
    measurement.rssiMin !== '' &&
    measurement.rssiMin !== undefined &&
    measurement.rssiMin !== null &&
    measurement.rssiMax !== '' &&
    measurement.rssiMax !== undefined &&
    measurement.rssiMax !== null;

  if (hasRange) {
    parts.push(
      'Range over the window: ' +
      measurement.rssiMin +
      ' to ' +
      measurement.rssiMax +
      ' dBm.'
    );
  }

  if (
    measurement.rssiSamples !== '' &&
    measurement.rssiSamples !== undefined &&
    measurement.rssiSamples !== null
  ) {
    parts.push(
      'Samples: ' + measurement.rssiSamples + '.'
    );
  }

  return parts.join(' ');
}


/*
 * The MAC no longer has a column, so a CHANGE has to be recorded where a change
 * is what matters, rather than by repeating an unchanging value on every row.
 *
 * Three places, each answering a different question:
 *
 * - a note on this row's Record id cell     "which Tilt produced this row?"
 * - a DEVICE_CHANGED entry in System Log    "when did it change, and from what?"
 * - the Monitoring tab's Device column      "what is on this wine right now?"
 *
 * The first reading a wine ever sees is recorded silently: there is no change
 * to report, and a note on the first row of every sheet would be noise.
 *
 * The note goes on column M rather than column A on purpose - column A's note
 * is owned by the data-gap marker, which rebuildDerivedColumns() clears and
 * rewrites, and two owners of one note is how notes get lost.
 */
function noteDeviceChange(
  spreadsheet,
  state,
  measurement,
  rowNumber,
  captureDate
) {
  const deviceId = String(
    measurement.deviceId === undefined ||
    measurement.deviceId === null
      ? ''
      : measurement.deviceId
  ).trim();

  if (!deviceId) {
    return;
  }

  const properties =
    PropertiesService.getScriptProperties();

  const key =
    WINE_DEVICE_PREFIX +
    state.sheet.getSheetId();

  const previousDeviceId =
    properties.getProperty(key);

  if (previousDeviceId === deviceId) {
    return;
  }

  properties.setProperty(key, deviceId);

  /* First device ever seen for this wine: nothing changed, so nothing to say. */
  if (!previousDeviceId) {
    return;
  }

  state.sheet
    .getRange(rowNumber, DATA_COLUMN_COUNT)
    .setNote(
      'Device changed to ' +
      deviceId +
      ' (previously ' +
      previousDeviceId +
      ').'
    );

  safeLogEvent(
    'WARNING',
    state.wineName,
    'DEVICE_CHANGED',
    'Readings for this wine are now arriving from a different Tilt.',
    {
      previousDevice: previousDeviceId,
      currentDevice: deviceId,
      changedAt:
        Utilities.formatDate(
          captureDate,
          TIME_ZONE,
          'dd.MM.yyyy HH:mm:ss'
        ),
      row: rowNumber,
      sheetUrl:
        spreadsheet.getUrl() +
        '#gid=' +
        state.sheet.getSheetId()
    }
  );
}


/* The device currently associated with a wine sheet, or '' if none seen yet. */
function getStoredDeviceId(sheet) {
  return (
    PropertiesService
      .getScriptProperties()
      .getProperty(
        WINE_DEVICE_PREFIX + sheet.getSheetId()
      ) || ''
  );
}


function emptyAverageSet() {
  return {
    rollingSG: '',
    rollingTempC: '',
    quality: '',
    previousDaySG: '',
    previousDayTempC: ''
  };
}


function valueOrBlank(value) {
  return (
    value === undefined ||
    value === null
  )
    ? ''
    : value;
}


/*
 * The single-reading payload carries only Beer, Color, SG, Temp, Comment,
 * Email and tzOffset, so every schemaVersion 2 diagnostic column stays blank
 * and the receipt time stands in for the capture time.
 */
function buildLegacyMeasurement(
  tiltData,
  receivedAt,
  sg,
  tempF
) {
  return {
    captureDate: receivedAt,
    sg: sg,
    tempC: (tempF - 32) * 5 / 9,
    sgRaw: '',
    sgSmoothed: '',
    rssi: '',
    rssiAvg: '',
    rssiMin: '',
    rssiMax: '',
    rssiSamples: '',
    deviceId: firstPresent(
      tiltData.mac,
      tiltData.deviceId,
      ''
    ),
    recordId: '',
    uptimeMsAtCapture: ''
  };
}


/*
 * One schemaVersion 2 reading turned into a measurement. Temp arrives in
 * Fahrenheit as a JSON number and is converted exactly as the single-reading
 * path converts its string.
 */
function buildBatchMeasurement(
  reading,
  recordId,
  sg,
  tempF
) {
  return {
    captureDate:
      parseCapturedAtUtc(reading),
    sg: sg,
    tempC: (tempF - 32) * 5 / 9,
    sgRaw: toNumber(reading.SG_Raw),
    sgSmoothed: toNumber(reading.SG_Smoothed),
    rssi: toNumber(reading.RSSI),
    rssiAvg: toNumber(reading.RSSI_Avg),
    rssiMin: toNumber(reading.RSSI_Min),
    rssiMax: toNumber(reading.RSSI_Max),
    rssiSamples:
      toNumber(reading.RSSI_Samples),
    deviceId: firstPresent(
      reading.deviceId,
      reading.mac,
      ''
    ),
    recordId: recordId,
    uptimeMsAtCapture:
      reading.UptimeMsAtCapture
  };
}


/*
 * One WARNING per wine per request rather than one per row. A device with no
 * clock at all would otherwise write a System Log line for every reading of
 * every batch, every ten minutes.
 */
function flushTimestampWarnings(
  spreadsheet,
  sheetStates,
  deviceName
) {
  Object.keys(sheetStates).forEach(function (key) {
    const state = sheetStates[key];

    if (
      state.untimestampedRecordIds.length === 0
    ) {
      return;
    }

    safeLogEvent(
      'WARNING',
      state.wineName,
      'NO_VALID_TIMESTAMP',
      state.untimestampedRecordIds.length +
        ' reading(s) were saved without a capture time because the TiltBridge clock was not trustworthy. They are excluded from every average and have been acknowledged so the device stops retrying them.',
      {
        deviceName: deviceName,
        recordCount:
          state.untimestampedRecordIds.length,
        recordIds:
          truncateText(
            state.untimestampedRecordIds.join(', '),
            1500
          ),
        sheetUrl:
          spreadsheet.getUrl() +
          '#gid=' +
          state.sheet.getSheetId()
      }
    );
  });
}


/*
 * Whether this capture time is due a four-hour average point.
 *
 * Tracked on the state rather than re-read from the sheet, so that a
 * twenty-row backlog does not re-scan the sheet tail twenty times and so that
 * every row is evaluated against its own capture time.
 */
function shouldWriteStateAverage(
  state,
  captureDate,
  intervalHours
) {
  return isAverageDue(
    state.lastAverageOutputDate
      ? state.lastAverageOutputDate.getTime()
      : null,
    state.firstMeasurementDate
      ? state.firstMeasurementDate.getTime()
      : null,
    captureDate.getTime(),
    intervalHours
  );
}


/*
 * Whether a reading at captureTime is due a four-hour average point.
 *
 * The spacing rule in one place, so the live append path and the rebuild pick
 * the same rows. Both work in milliseconds because the rebuild never has Date
 * objects to hand - it works from the raw column A times it read once.
 *
 * For a new wine, wait until a full window of readings exists before writing
 * the first average.
 */
function isAverageDue(
  lastAverageTime,
  firstMeasurementTime,
  captureTime,
  intervalHours
) {
  const requiredMilliseconds =
    intervalHours * 60 * 60 * 1000;

  if (lastAverageTime === null) {
    if (firstMeasurementTime === null) {
      return false;
    }

    return (
      captureTime - firstMeasurementTime >=
      requiredMilliseconds - 5000
    );
  }

  return (
    captureTime - lastAverageTime >=
    requiredMilliseconds - 5000
  );
}


/*
 * The durable de-duplication store. Held open for the whole batch so the
 * sheet is read once and the append position is tracked in memory.
 */
function openProcessedIdStore(spreadsheet) {
  const sheet =
    ensureProcessedIdSheet(spreadsheet);

  return {
    sheet: sheet,
    nextRow: Math.max(
      2,
      sheet.getLastRow() + 1
    ),
    known: readProcessedIdSet(sheet)
  };
}


function ensureProcessedIdSheet(spreadsheet) {
  let sheet =
    spreadsheet.getSheetByName(
      PROCESSED_IDS_SHEET
    );

  if (sheet) {
    /*
     * Hiding fails when this would be the last visible sheet, which can
     * happen on a brand new spreadsheet where this sheet is created first.
     * Try again while it is still visible.
     */
    if (!sheet.isSheetHidden()) {
      hideProcessedIdSheet(sheet);
    }

    return sheet;
  }

  sheet = spreadsheet.insertSheet(
    PROCESSED_IDS_SHEET
  );

  sheet
    .getRange('A1:G1')
    .setValues([[
      'Record id',
      'Wine',
      'Sheet',
      'Row at append',
      'Captured at',
      'Recorded at',
      'Result'
    ]])
    .setBackground(HEADER_COLOR)
    .setFontWeight('bold');

  sheet.setFrozenRows(1);

  sheet
    .getRange('D:D')
    .setNumberFormat('0');

  sheet
    .getRange('E:E')
    .setNumberFormat(
      'dd.MM.yyyy HH:mm:ss'
    );

  sheet
    .getRange('F:F')
    .setNumberFormat(
      'dd.MM.yyyy HH:mm:ss'
    );

  sheet.setColumnWidth(1, 220);
  sheet.setColumnWidth(2, 180);
  sheet.setColumnWidth(3, 180);
  sheet.setColumnWidth(4, 70);
  sheet.setColumnWidth(5, 175);
  sheet.setColumnWidth(6, 175);
  sheet.setColumnWidth(7, 190);

  hideProcessedIdSheet(sheet);

  return sheet;
}


function hideProcessedIdSheet(sheet) {
  try {
    sheet.hideSheet();
  } catch (hideError) {
    /*
     * Hiding fails only when this would be the last visible sheet. Never
     * worth failing a batch over; ensureProcessedIdSheet() retries later.
     */
    console.error(
      'Could not hide the processed id sheet:',
      hideError
    );
  }
}


function readProcessedIdSet(sheet) {
  const known = new Set();

  const lastRow = sheet.getLastRow();

  if (lastRow < 2) {
    return known;
  }

  const values =
    sheet
      .getRange(2, 1, lastRow - 1, 1)
      .getValues();

  for (let i = 0; i < values.length; i++) {
    const recordId =
      String(values[i][0] || '').trim();

    if (recordId) {
      known.add(recordId);
    }
  }

  return known;
}


/*
 * Written immediately after the data row, per reading, so that a failure can
 * never leave more than a single row without its record id.
 *
 * The stored row number is the row AT APPEND TIME. A later backlog upload can
 * reorder the sheet, so column M of the wine sheet - not this number - is the
 * authoritative way to locate the row a record id produced.
 */
function recordProcessedId(
  store,
  recordId,
  wineName,
  sheetName,
  rowNumber,
  capturedAt,
  result
) {
  const row = store.nextRow;

  store.sheet
    .getRange(row, 1, 1, 7)
    .setValues([[
      recordId,
      wineName,
      sheetName,
      rowNumber,
      capturedAt || '',
      new Date(),
      result
    ]]);

  store.nextRow = row + 1;
}


function trimProcessedIdStore(
  store,
  retention
) {
  const rowCount = store.nextRow - 2;

  if (rowCount <= retention) {
    return;
  }

  const excess = rowCount - retention;

  store.sheet.deleteRows(2, excess);

  store.nextRow -= excess;
}


function buildBatchDocumentUrl(
  spreadsheet,
  sheet
) {
  if (!spreadsheet) {
    return '';
  }

  if (!sheet) {
    return spreadsheet.getUrl();
  }

  return (
    spreadsheet.getUrl() +
    '#gid=' +
    sheet.getSheetId()
  );
}


/*
 * Runs automatically from the installed 15-minute trigger.
 * No email is sent. Results are shown in Monitoring and System Log.
 */
function checkForMissingReadings() {
  const lock = LockService.getScriptLock();

  if (!lock.tryLock(BACKGROUND_LOCK_WAIT_MS)) {
    queuePendingLog(
      'ERROR',
      'System',
      'MONITOR_LOCK_BUSY',
      'The missing-reading check could not acquire the spreadsheet lock.',
      {
        waitMilliseconds: BACKGROUND_LOCK_WAIT_MS
      }
    );

    return;
  }

  try {
    const spreadsheet =
      SpreadsheetApp.openById(SPREADSHEET_ID);

    ensureSpreadsheetTimeZone(spreadsheet);
    ensureMonitoringSheet(spreadsheet);
    flushPendingLogs(spreadsheet);

    /*
     * Finish any derived-value rebuild a device request had to cut short. This
     * runs before the status pass so the Monitoring tab reports on a sheet
     * whose averages are already current.
     */
    flushPendingRebuilds(spreadsheet);

    const now = new Date();
    const rows = [];

    spreadsheet.getSheets().forEach(function (sheet) {
      if (!isWineSheet(sheet)) {
        return;
      }

      const wineName =
        getWineNameFromSheet(sheet);

      const lastDate =
        getLastIncomingReading(
          wineName,
          sheet
        );

      const lastValues =
        getLastMeasurementValues(sheet);

      let status = 'NO DATA';
      let minutesOld = '';

      if (lastDate) {
        minutesOld = Math.max(
          0,
          (now.getTime() - lastDate.getTime()) /
            60000
        );

        if (
          minutesOld >
          MISSING_READING_MINUTES
        ) {
          status = 'MISSING';

          handleMissingReading(
            spreadsheet,
            sheet,
            wineName,
            lastDate,
            minutesOld,
            now
          );

        } else {
          status = 'OK';

          handleReadingRecovery(
            spreadsheet,
            sheet,
            wineName,
            now
          );
        }
      }

      rows.push([
        wineName,
        status,
        lastDate || '',
        minutesOld === ''
          ? ''
          : minutesOld,
        lastValues.sg,
        lastValues.tempC,
        getLastIssue(wineName),
        now,
        getStoredDeviceId(sheet)
      ]);
    });

    writeMonitoringRows(
      spreadsheet,
      rows
    );

  } catch (error) {
    safeLogEvent(
      'ERROR',
      'System',
      'MONITOR_CHECK_ERROR',
      error && error.message
        ? error.message
        : String(error),
      {
        stack:
          error && error.stack
            ? error.stack
            : ''
      }
    );

  } finally {
    lock.releaseLock();
  }
}

function handleMissingReading(
  spreadsheet,
  sheet,
  wineName,
  lastDate,
  minutesOld,
  now
) {
  const properties =
    PropertiesService.getScriptProperties();

  const key =
    getMissingStateKey(sheet);

  const previousState =
    parseJsonSafely(
      properties.getProperty(key),
      {}
    );

  /* Log only when the status first changes to MISSING. */
  if (previousState.active === true) {
    return;
  }

  const message =
    wineName +
    ' has not sent a reading for ' +
    Math.round(minutesOld) +
    ' minutes. Last reading: ' +
    Utilities.formatDate(
      lastDate,
      TIME_ZONE,
      'dd.MM.yyyy HH:mm:ss'
    ) +
    '.';

  safeLogEvent(
    'WARNING',
    wineName,
    'MISSING_READING',
    message,
    {
      minutesOld: minutesOld,
      thresholdMinutes:
        MISSING_READING_MINUTES,
      sheetUrl:
        spreadsheet.getUrl() +
        '#gid=' +
        sheet.getSheetId()
    }
  );

  properties.setProperty(
    key,
    JSON.stringify({
      active: true,
      firstMissingMs: now.getTime()
    })
  );
}


function handleReadingRecovery(
  spreadsheet,
  sheet,
  wineName,
  now
) {
  const properties =
    PropertiesService.getScriptProperties();

  const key =
    getMissingStateKey(sheet);

  const previousState =
    parseJsonSafely(
      properties.getProperty(key),
      {}
    );

  if (previousState.active !== true) {
    return;
  }

  const message =
    wineName +
    ' is sending readings again as of ' +
    Utilities.formatDate(
      now,
      TIME_ZONE,
      'dd.MM.yyyy HH:mm:ss'
    ) +
    '.';

  safeLogEvent(
    'INFO',
    wineName,
    'READING_RECOVERED',
    message,
    {
      sheetUrl:
        spreadsheet.getUrl() +
        '#gid=' +
        sheet.getSheetId()
    }
  );

  properties.deleteProperty(key);
}


function getMissingStateKey(sheet) {
  return (
    'missing-reading-state-' +
    sheet.getSheetId()
  );
}


function ensureSpreadsheetTimeZone(
  spreadsheet
) {
  const properties =
    PropertiesService.getScriptProperties();

  const key =
    'spreadsheet-timezone-' +
    TIME_ZONE;

  if (properties.getProperty(key) !== '1') {
    spreadsheet.setSpreadsheetTimeZone(
      TIME_ZONE
    );

    properties.setProperty(key, '1');
  }
}


function ensureSheetPrepared(
  sheet,
  wineName
) {
  const properties =
    PropertiesService.getScriptProperties();

  const key =
    getSheetLayoutPropertyKey(sheet);

  if (properties.getProperty(key) !== '1') {
    prepareSheet(sheet, wineName);
    properties.setProperty(key, '1');
  }
}


function markSheetPrepared(sheet) {
  PropertiesService
    .getScriptProperties()
    .setProperty(
      getSheetLayoutPropertyKey(sheet),
      '1'
    );
}


function getSheetLayoutPropertyKey(sheet) {
  return (
    'sheet-layout-' +
    LAYOUT_VERSION +
    '-' +
    sheet.getSheetId()
  );
}


function prepareSheet(
  sheet,
  wineName
) {
  migrateLegacyLayoutIfNeeded(sheet);

  /*
   * Remove any older title merge before applying this layout's. Widened to the
   * sheet's own width, not the layout's, so a merge left over from a wider
   * layout is fully broken apart before the narrower one is made.
   */
  const titleRange =
    sheet.getRange(1, 1, 1, sheet.getMaxColumns());

  titleRange.breakApart();

  sheet
    .getRange(1, 1, 1, DATA_COLUMN_COUNT)
    .merge();

  sheet
    .getRange('A1')
    .setValue(wineName)
    .setBackground(TITLE_COLOR)
    .setFontSize(14)
    .setFontWeight('bold')
    .setHorizontalAlignment('center');

  sheet
    .getRange(2, 1, 1, DATA_COLUMN_COUNT)
    .setValues([WINE_SHEET_HEADERS])
    .setBackground(HEADER_COLOR)
    .setFontWeight('bold')
    .setHorizontalAlignment('center');

  /* Columns D and J are intentionally empty visual separators. */
  sheet
    .getRange('D:D')
    .clearContent();

  sheet
    .getRange('J:J')
    .clearContent();

  sheet
    .getRange('D2:J2')
    .setBackground(null);

  sheet
    .getRange('D2')
    .setFontWeight('normal');

  sheet
    .getRange('J2')
    .setFontWeight('normal');

  /* D2:J2 above also cleared the header fill on E..I; put it back. */
  sheet
    .getRange('E2:I2')
    .setBackground(HEADER_COLOR);

  sheet.setFrozenRows(2);

  sheet
    .getRange('A:A')
    .setNumberFormat(
      'dd.MM.yyyy HH:mm:ss'
    );

  sheet
    .getRange('B:B')
    .setNumberFormat('0.0000');

  sheet
    .getRange('C:C')
    .setNumberFormat('0.0');

  sheet
    .getRange('E:E')
    .setNumberFormat('0.0000');

  sheet
    .getRange('F:F')
    .setNumberFormat('0.0');

  sheet
    .getRange('G:G')
    .setNumberFormat('@');

  sheet
    .getRange('H:H')
    .setNumberFormat('0.0000');

  sheet
    .getRange('I:I')
    .setNumberFormat('0.0');

  /* Raw SG carries the same four decimals as column B. */
  sheet
    .getRange('K:K')
    .setNumberFormat('0.0000');

  /* Signal is whole negative dBm. */
  sheet
    .getRange('L:L')
    .setNumberFormat('0');

  /* Record id is text, never coerced into a number or a date. */
  sheet
    .getRange('M:M')
    .setNumberFormat('@');

  sheet.setColumnWidth(1, 175);
  sheet.setColumnWidth(2, 90);
  sheet.setColumnWidth(3, 130);
  sheet.setColumnWidth(4, 28);
  sheet.setColumnWidth(5, 135);
  sheet.setColumnWidth(6, 135);
  sheet.setColumnWidth(7, 230);
  sheet.setColumnWidth(8, 155);
  sheet.setColumnWidth(9, 155);
  sheet.setColumnWidth(10, 28);
  sheet.setColumnWidth(11, 95);
  sheet.setColumnWidth(12, 95);
  sheet.setColumnWidth(13, 200);

  /*
   * The charts anchor one clear column past the data, and a sheet can have
   * fewer columns than that anchor - a hand-trimmed sheet, or one created with
   * a narrow grid. setPosition() on a column that does not exist throws, so
   * make sure it exists.
   */
  const missingColumns =
    CHART_COLUMN - sheet.getMaxColumns();

  if (missingColumns > 0) {
    sheet.insertColumnsAfter(
      sheet.getMaxColumns(),
      missingColumns
    );
  }

  /*
   * The charts are part of the layout, so they are (re)built here rather than
   * only from initialSetup(). That is what repositions them when
   * LAYOUT_VERSION changes: ensureSheetPrepared() keys a Script Property on
   * 'sheet-layout-<LAYOUT_VERSION>-<sheetId>', so a version bump makes the key
   * miss exactly once per sheet, prepareSheet() runs, and every later request
   * finds '1' and skips it. Rebuilding charts is expensive and this is the
   * only thing keeping it off the per-request path.
   */
  createOrRefreshCharts(sheet, wineName);
}

function createOrRefreshCharts(
  sheet,
  wineName
) {
  const sgTitle =
    wineName +
    ' – fermentation progress';

  const tempTitle =
    wineName +
    ' – temperature';

  /*
   * Every chart goes, not just the two this function last created.
   *
   * Matching on the title left behind any chart from an older layout, from a
   * wine that was since renamed, or added by hand - and a stale chart stays
   * anchored where it was, which after the layout widened means sitting on top
   * of the data. This DELIBERATELY discards user-added charts on wine sheets:
   * a chart covering the readings is the worse failure of the two, and wine
   * sheets are generated content rather than a place to keep your own work.
   */
  sheet.getCharts().forEach(function (chart) {
    sheet.removeChart(chart);
  });

  const sgChart =
    sheet
      .newChart()
      .setChartType(Charts.ChartType.LINE)
      .addRange(sheet.getRange('A2:A'))
      .addRange(sheet.getRange('B2:B'))
      .addRange(sheet.getRange('E2:E'))
      .setNumHeaders(1)
      .setOption(
        'useFirstColumnAsDomain',
        true
      )
      .setOption('title', sgTitle)
      .setOption(
        'subtitle',
        'Raw SG and rolling 4-hour average, plotted every 4 hours'
      )
      .setOption(
        'legend',
        { position: 'bottom' }
      )
      .setOption(
        'hAxis',
        {
          title: 'Date and time',
          format: 'dd.MM HH:mm',
          slantedText: true,
          slantedTextAngle: 45
        }
      )
      .setOption(
        'vAxis',
        {
          title: 'Specific gravity',
          format: '0.0000'
        }
      )
      .setOption(
        'series',
        {
          0: {
            lineWidth: 1,
            pointSize: 1
          },
          1: {
            lineWidth: 3,
            pointSize: 0
          }
        }
      )
      .setOption('interpolateNulls', true)
      .setOption('width', CHART_WIDTH_PIXELS)
      .setOption('height', CHART_HEIGHT_PIXELS)
      .setPosition(
        SG_CHART_ROW,
        CHART_COLUMN,
        0,
        0
      )
      .build();

  sheet.insertChart(sgChart);

  const tempChart =
    sheet
      .newChart()
      .setChartType(Charts.ChartType.LINE)
      .addRange(sheet.getRange('A2:A'))
      .addRange(sheet.getRange('C2:C'))
      .addRange(sheet.getRange('F2:F'))
      .setNumHeaders(1)
      .setOption(
        'useFirstColumnAsDomain',
        true
      )
      .setOption('title', tempTitle)
      .setOption(
        'subtitle',
        'Raw temperature and rolling 4-hour average, plotted every 4 hours'
      )
      .setOption(
        'legend',
        { position: 'bottom' }
      )
      .setOption(
        'hAxis',
        {
          title: 'Date and time',
          format: 'dd.MM HH:mm',
          slantedText: true,
          slantedTextAngle: 45
        }
      )
      .setOption(
        'vAxis',
        {
          title: 'Temperature °C',
          format: '0.0'
        }
      )
      .setOption(
        'series',
        {
          0: {
            lineWidth: 1,
            pointSize: 1
          },
          1: {
            lineWidth: 3,
            pointSize: 0
          }
        }
      )
      .setOption('interpolateNulls', true)
      .setOption('width', CHART_WIDTH_PIXELS)
      .setOption('height', CHART_HEIGHT_PIXELS)
      .setPosition(
        TEMP_CHART_ROW,
        CHART_COLUMN,
        0,
        0
      )
      .build();

  sheet.insertChart(tempChart);
}


/*
 * Migration from ANY layout that is not the current one.
 *
 * CHOSEN APPROACH: preserve the three raw measurement columns, clear everything
 * else, and log loudly. Lossy by design, not a faithful migration.
 *
 * A, B and C are the only columns every layout this script has ever written
 * agrees on, and they are the only ones that cannot be recomputed. Everything
 * from D rightwards either is derived - E..I are rebuilt from the raw readings
 * as new rows arrive - or is diagnostic detail whose absence costs nothing.
 *
 * Why not "detect and refuse": prepareSheet() would then either leave stale
 * headers over reinterpreted columns, or append current-layout rows underneath
 * old ones. Both silently misalign the sheet, which is exactly the outcome to
 * avoid. A visibly reset sheet is better than a quietly wrong one.
 *
 * So the raw readings (A date, B SG, C °C) are kept, since every layout this
 * script has ever written stores them in those positions, everything from D
 * rightwards is cleared, and a WARNING records that a reset is the cleaner
 * option.
 */
function migrateLegacyLayoutIfNeeded(sheet) {
  const lastRow = sheet.getLastRow();

  /* Empty or brand new sheet: prepareSheet() lays it out from scratch. */
  if (lastRow < 2) {
    return;
  }

  const headers =
    sheet
      .getRange(
        2,
        1,
        1,
        Math.min(
          sheet.getMaxColumns(),
          DATA_COLUMN_COUNT
        )
      )
      .getDisplayValues()[0];

  if (isCurrentWineLayout(headers)) {
    return;
  }

  /* Header row only, no data to preserve or misalign. */
  if (lastRow < 3) {
    return;
  }

  /*
   * The oldest layout this script ever wrote put °F in C and °C in D. Every
   * later one has °C in C.
   */
  const celsiusColumnIndex =
    String(headers[2]).indexOf('°F') !== -1 &&
    String(headers[3]).indexOf('°C') !== -1
      ? 3
      : 2;

  const rowCount = lastRow - 2;

  const readColumnCount = Math.max(
    celsiusColumnIndex + 1,
    3
  );

  const oldData =
    sheet
      .getRange(3, 1, rowCount, readColumnCount)
      .getValues();

  const convertedData =
    oldData.map(function (row) {
      const values = [];

      for (
        let i = 0;
        i < DATA_COLUMN_COUNT;
        i++
      ) {
        values.push('');
      }

      values[0] = row[0];
      values[1] = row[1];

      values[2] =
        row[celsiusColumnIndex] === undefined
          ? ''
          : row[celsiusColumnIndex];

      return values;
    });

  sheet
    .getRange(3, 1, rowCount, DATA_COLUMN_COUNT)
    .clearContent();

  sheet
    .getRange(3, 1, rowCount, DATA_COLUMN_COUNT)
    .setValues(convertedData);

  /*
   * Old quality and gap notes and fills referred to columns that no longer
   * mean the same thing. Only A..C keep their meaning.
   */
  sheet
    .getRange(
      3,
      4,
      rowCount,
      DATA_COLUMN_COUNT - 3
    )
    .setBackground(null)
    .clearNote();

  safeLogEvent(
    'WARNING',
    getWineNameFromSheet(sheet),
    'LAYOUT_WIDENED',
    'This sheet used an older column layout. Its capture times, SG and temperature were kept; all derived and diagnostic columns were cleared and will refill as new readings arrive. Deleting the sheet and letting TiltBridge recreate it gives a cleaner result.',
    {
      sheetName: sheet.getName(),
      migratedRows: rowCount,
      previousHeaders:
        truncateText(headers.join(' | '), 500),
      layoutVersion: LAYOUT_VERSION
    }
  );
}


function isCurrentWineLayout(headers) {
  for (
    let i = 0;
    i < WINE_SHEET_HEADERS.length;
    i++
  ) {
    if (
      String(headers[i] || '').trim() !==
      WINE_SHEET_HEADERS[i]
    ) {
      return false;
    }
  }

  return true;
}

function getStoredLastIncomingReading(wineName) {
  const stored = Number(
    PropertiesService
      .getScriptProperties()
      .getProperty(
        'last-incoming-reading-' +
        safePropertyPart(wineName)
      ) ||
    0
  );

  if (
    Number.isFinite(stored) &&
    stored > 0
  ) {
    return new Date(stored);
  }

  return null;
}


function rememberIncomingReading(
  wineName,
  date
) {
  PropertiesService
    .getScriptProperties()
    .setProperty(
      'last-incoming-reading-' +
      safePropertyPart(wineName),
      String(date.getTime())
    );
}


function getLastIncomingReading(
  wineName,
  sheet
) {
  return (
    getStoredLastIncomingReading(wineName) ||
    getLastMeasurementDate(sheet)
  );
}


/*
 * The newest row that actually carries a capture time.
 *
 * Scans backwards instead of reading only the last row. Rows written with
 * TimestampValid=false have a blank column A, and sortWineSheetByCaptureTime()
 * deliberately parks those at the bottom of the sheet, so the last row is not
 * necessarily the newest measurement. Reading only the last row would return
 * null for any wine that ever produced one untimestamped reading, which would
 * in turn disable the logging-interval throttle and shade every subsequent row
 * as a new day.
 */
function getLastMeasurementDate(sheet) {
  const found =
    findLastMeasurementRow(sheet);

  return found
    ? found.date
    : null;
}


function getLastMeasurementValues(sheet) {
  const found =
    findLastMeasurementRow(sheet);

  if (!found) {
    return {
      sg: '',
      tempC: ''
    };
  }

  const row =
    sheet
      .getRange(found.row, 1, 1, 3)
      .getValues()[0];

  return {
    sg: row[1],
    tempC: row[2]
  };
}


function findLastMeasurementRow(sheet) {
  const lastRow = sheet.getLastRow();

  if (lastRow < 3) {
    return null;
  }

  const chunkSize = 500;
  let endRow = lastRow;

  while (endRow >= 3) {
    const startRow = Math.max(
      3,
      endRow - chunkSize + 1
    );

    const values =
      sheet
        .getRange(
          startRow,
          1,
          endRow - startRow + 1,
          1
        )
        .getValues();

    for (
      let i = values.length - 1;
      i >= 0;
      i--
    ) {
      const time =
        toSortableTime(values[i][0]);

      if (time !== null) {
        return {
          row: startRow + i,
          date: new Date(time)
        };
      }
    }

    endRow = startRow - 1;
  }

  return null;
}


function shouldLogMeasurement(
  previousDate,
  currentDate,
  intervalMinutes
) {
  if (!previousDate) {
    return true;
  }

  if (intervalMinutes <= 10) {
    return true;
  }

  const elapsedMilliseconds =
    currentDate.getTime() -
    previousDate.getTime();

  const requiredMilliseconds =
    intervalMinutes * 60 * 1000;

  return (
    elapsedMilliseconds >=
    requiredMilliseconds - 5000
  );
}


function getFirstMeasurementDate(sheet) {
  const lastRow = sheet.getLastRow();

  if (lastRow < 3) {
    return null;
  }

  const chunkSize = 500;
  let startRow = 3;

  while (startRow <= lastRow) {
    const rowCount = Math.min(
      chunkSize,
      lastRow - startRow + 1
    );

    const values =
      sheet
        .getRange(startRow, 1, rowCount, 1)
        .getValues();

    for (let i = 0; i < values.length; i++) {
      let rowDate = values[i][0];

      if (!(rowDate instanceof Date)) {
        rowDate = new Date(rowDate);
      }

      if (!isNaN(rowDate.getTime())) {
        return rowDate;
      }
    }

    startRow += rowCount;
  }

  return null;
}


function getLastAverageOutputDate(sheet) {
  return getLastAverageOutputDateBefore(
    sheet,
    sheet.getLastRow() + 1
  );
}


/*
 * Capture time of the newest four-hour average point strictly above beforeRow.
 *
 * rebuildDerivedColumns() needs this to continue the average chain from where
 * the untouched part of the sheet left off: the four-hour spacing is measured
 * from the previous average point, so recomputing a tail without that anchor
 * would put the whole chain in a different place.
 */
function getLastAverageOutputDateBefore(
  sheet,
  beforeRow
) {
  const lastRow = Math.min(
    sheet.getLastRow(),
    beforeRow - 1
  );

  if (lastRow < 3) {
    return null;
  }

  const chunkSize = 500;
  let endRow = lastRow;

  while (endRow >= 3) {
    const startRow = Math.max(
      3,
      endRow - chunkSize + 1
    );

    const values =
      sheet
        .getRange(
          startRow,
          1,
          endRow - startRow + 1,
          7
        )
        .getValues();

    for (
      let i = values.length - 1;
      i >= 0;
      i--
    ) {
      const averageSG = values[i][4];
      const averageTempC = values[i][5];
      const averageQuality = values[i][6];

      if (
        averageSG === '' &&
        averageTempC === '' &&
        averageQuality === ''
      ) {
        continue;
      }

      let rowDate = values[i][0];

      if (!(rowDate instanceof Date)) {
        rowDate = new Date(rowDate);
      }

      if (!isNaN(rowDate.getTime())) {
        return rowDate;
      }
    }

    endRow = startRow - 1;
  }

  return null;
}


function calculateRollingAverageAssessment(
  sheet,
  currentDate,
  hours,
  currentSG,
  currentTempC
) {
  const cutoffTime =
    currentDate.getTime() -
    hours * 60 * 60 * 1000;

  let sgTotal = 0;
  let sgCount = 0;
  let tempTotal = 0;
  let tempCount = 0;

  const readingTimes = [
    currentDate.getTime()
  ];

  if (
    currentSG !== '' &&
    currentSG !== null
  ) {
    const value = Number(currentSG);

    if (Number.isFinite(value)) {
      sgTotal += value;
      sgCount++;
    }
  }

  if (
    currentTempC !== '' &&
    currentTempC !== null
  ) {
    const value = Number(currentTempC);

    if (Number.isFinite(value)) {
      tempTotal += value;
      tempCount++;
    }
  }

  const lastRow = sheet.getLastRow();
  const chunkSize = 500;
  let endRow = lastRow;
  let finished = lastRow < 3;

  while (!finished && endRow >= 3) {
    const startRow = Math.max(
      3,
      endRow - chunkSize + 1
    );

    const values =
      sheet
        .getRange(
          startRow,
          1,
          endRow - startRow + 1,
          3
        )
        .getValues();

    for (
      let i = values.length - 1;
      i >= 0;
      i--
    ) {
      let rowDate = values[i][0];

      if (!(rowDate instanceof Date)) {
        rowDate = new Date(rowDate);
      }

      if (isNaN(rowDate.getTime())) {
        continue;
      }

      const rowTime = rowDate.getTime();

      if (rowTime < cutoffTime) {
        finished = true;
        break;
      }

      if (rowTime > currentDate.getTime()) {
        continue;
      }

      readingTimes.push(rowTime);

      const rowSG = Number(values[i][1]);
      const rowTemp = Number(values[i][2]);

      if (
        values[i][1] !== '' &&
        Number.isFinite(rowSG)
      ) {
        sgTotal += rowSG;
        sgCount++;
      }

      if (
        values[i][2] !== '' &&
        Number.isFinite(rowTemp)
      ) {
        tempTotal += rowTemp;
        tempCount++;
      }
    }

    endRow = startRow - 1;
  }

  readingTimes.sort(function (a, b) {
    return a - b;
  });

  let largestGapMilliseconds =
    readingTimes.length > 0
      ? Math.max(0, readingTimes[0] - cutoffTime)
      : hours * 60 * 60 * 1000;

  for (let i = 1; i < readingTimes.length; i++) {
    largestGapMilliseconds = Math.max(
      largestGapMilliseconds,
      readingTimes[i] - readingTimes[i - 1]
    );
  }

  if (readingTimes.length > 0) {
    largestGapMilliseconds = Math.max(
      largestGapMilliseconds,
      currentDate.getTime() -
        readingTimes[readingTimes.length - 1]
    );
  }

  const averages = averageResult(
    sgTotal,
    sgCount,
    tempTotal,
    tempCount
  );

  return {
    sg: averages.sg,
    tempC: averages.tempC,
    maxGapMinutes:
      largestGapMilliseconds / 60000,
    readingCount:
      Math.max(sgCount, tempCount)
  };
}

/*
 * Turns a rolling assessment into the five things a four-hour average point
 * puts on a row: the two average values, the quality text, the fill colour for
 * column G and the explanatory note.
 *
 * Shared by the append path and by rebuildDerivedColumns() so that a row
 * written live and the same row recomputed after a backfill cannot disagree
 * about anything except the readings that were actually available.
 */
function classifyRollingAssessment(assessment) {
  const largestGapText =
    formatDurationMinutes(
      assessment.maxGapMinutes
    );

  let rollingSG = '';
  let rollingTempC = '';
  let quality;
  let color;

  if (
    assessment.maxGapMinutes <=
    AVERAGE_COMPLETE_MAX_GAP_MINUTES
  ) {
    rollingSG = assessment.sg;
    rollingTempC = assessment.tempC;
    quality = 'COMPLETE';
    color = AVERAGE_COMPLETE_COLOR;

  } else if (
    assessment.maxGapMinutes <=
    AVERAGE_INCOMPLETE_MAX_GAP_MINUTES
  ) {
    rollingSG = assessment.sg;
    rollingTempC = assessment.tempC;
    quality =
      'INCOMPLETE — ' + largestGapText + ' gap';
    color = AVERAGE_INCOMPLETE_COLOR;

  } else {
    quality =
      'INSUFFICIENT DATA — ' + largestGapText + ' gap';
    color = AVERAGE_INSUFFICIENT_COLOR;
  }

  return {
    rollingSG: rollingSG,
    rollingTempC: rollingTempC,
    quality: quality,
    color: color,
    note:
      'Largest interval without a saved reading inside this ' +
      ROLLING_AVERAGE_HOURS +
      '-hour window: ' +
      largestGapText +
      '. Readings used: ' +
      assessment.readingCount +
      '.'
  };
}


function calculateDailyAverages(
  sheet,
  targetDateKey,
  timezone
) {
  const lastRow = sheet.getLastRow();

  if (lastRow < 3) {
    return {
      sg: '',
      tempC: ''
    };
  }

  let sgTotal = 0;
  let sgCount = 0;
  let tempTotal = 0;
  let tempCount = 0;
  let foundTargetDate = false;

  const chunkSize = 500;
  let endRow = lastRow;

  while (endRow >= 3) {
    const startRow = Math.max(
      3,
      endRow - chunkSize + 1
    );

    const values =
      sheet
        .getRange(
          startRow,
          1,
          endRow - startRow + 1,
          3
        )
        .getValues();

    for (
      let i = values.length - 1;
      i >= 0;
      i--
    ) {
      let rowDate = values[i][0];

      if (!(rowDate instanceof Date)) {
        rowDate = new Date(rowDate);
      }

      if (isNaN(rowDate.getTime())) {
        continue;
      }

      const rowDateKey =
        dateKey(rowDate, timezone);

      if (rowDateKey === targetDateKey) {
        foundTargetDate = true;

        const rowSG = Number(values[i][1]);
        const rowTemp = Number(values[i][2]);

        if (
          values[i][1] !== '' &&
          Number.isFinite(rowSG)
        ) {
          sgTotal += rowSG;
          sgCount++;
        }

        if (
          values[i][2] !== '' &&
          Number.isFinite(rowTemp)
        ) {
          tempTotal += rowTemp;
          tempCount++;
        }

      } else if (
        foundTargetDate &&
        rowDateKey < targetDateKey
      ) {
        return averageResult(
          sgTotal,
          sgCount,
          tempTotal,
          tempCount
        );
      }
    }

    endRow = startRow - 1;
  }

  return averageResult(
    sgTotal,
    sgCount,
    tempTotal,
    tempCount
  );
}


function averageResult(
  sgTotal,
  sgCount,
  tempTotal,
  tempCount
) {
  return {
    sg:
      sgCount > 0
        ? sgTotal / sgCount
        : '',
    tempC:
      tempCount > 0
        ? tempTotal / tempCount
        : ''
  };
}


function formatDurationMinutes(minutes) {
  const roundedMinutes = Math.max(
    0,
    Math.round(Number(minutes) || 0)
  );

  if (roundedMinutes < 60) {
    return roundedMinutes + ' min';
  }

  const hours = Math.floor(
    roundedMinutes / 60
  );

  const remainingMinutes =
    roundedMinutes % 60;

  if (remainingMinutes === 0) {
    return hours + 'h';
  }

  return (
    hours +
    'h ' +
    remainingMinutes +
    'm'
  );
}


function isDifferentCalendarDay(
  previousDate,
  currentDate,
  timezone
) {
  if (!previousDate) {
    return true;
  }

  return (
    dateKey(previousDate, timezone) !==
    dateKey(currentDate, timezone)
  );
}


function dateKey(date, timezone) {
  return Utilities.formatDate(
    date,
    timezone,
    'yyyy-MM-dd'
  );
}


function getPreviousCalendarDateKey(
  currentDate,
  timezone
) {
  const currentDateKey =
    dateKey(currentDate, timezone);

  const parts =
    currentDateKey
      .split('-')
      .map(Number);

  const previousDateUTC =
    new Date(
      Date.UTC(
        parts[0],
        parts[1] - 1,
        parts[2]
      ) -
      24 * 60 * 60 * 1000
    );

  return Utilities.formatDate(
    previousDateUTC,
    'UTC',
    'yyyy-MM-dd'
  );
}


/*
 * Save an error without touching the spreadsheet. Each event gets a
 * unique Script Properties key, so concurrent requests do not overwrite it.
 */
function queuePendingLog(
  level,
  wineName,
  code,
  message,
  details
) {
  try {
    const event = {
      timestampMs: new Date().getTime(),
      level: level,
      wineName: wineName,
      code: code,
      message: message,
      details: details || {}
    };

    const key =
      PENDING_LOG_PREFIX +
      event.timestampMs +
      '-' +
      Utilities.getUuid();

    PropertiesService
      .getScriptProperties()
      .setProperty(
        key,
        JSON.stringify(event)
      );

    if (
      level === 'ERROR' ||
      level === 'WARNING'
    ) {
      setLastIssue(
        wineName,
        code + ': ' + message
      );
    }

  } catch (queueError) {
    console.error(
      'Could not queue System Log event:',
      queueError
    );
  }
}


/*
 * Write all queued events to System Log. Call only while holding the
 * global script lock.
 */
function flushPendingLogs(spreadsheet) {
  const properties =
    PropertiesService.getScriptProperties();

  const allProperties =
    properties.getProperties();

  const pending = [];

  Object.keys(allProperties).forEach(function (key) {
    if (
      key.indexOf(PENDING_LOG_PREFIX) !== 0
    ) {
      return;
    }

    const event = parseJsonSafely(
      allProperties[key],
      null
    );

    if (event) {
      pending.push({
        key: key,
        event: event
      });
    } else {
      pending.push({
        key: key,
        event: {
          timestampMs: new Date().getTime(),
          level: 'ERROR',
          wineName: 'System',
          code: 'PENDING_LOG_PARSE_ERROR',
          message:
            'A queued System Log event could not be parsed.',
          details: {
            propertyKey: key,
            storedValue:
              truncateText(
                allProperties[key],
                1500
              )
          }
        }
      });
    }
  });

  if (pending.length === 0) {
    return;
  }

  pending.sort(function (a, b) {
    return (
      Number(a.event.timestampMs || 0) -
      Number(b.event.timestampMs || 0)
    );
  });

  const rows = pending.map(function (item) {
    const event = item.event;

    if (
      event.level === 'ERROR' ||
      event.level === 'WARNING'
    ) {
      setLastIssue(
        event.wineName || 'Unknown Wine',
        (event.code || 'UNKNOWN') +
          ': ' +
          (event.message || '')
      );
    }

    return [
      new Date(
        Number(event.timestampMs) ||
        new Date().getTime()
      ),
      event.level || 'ERROR',
      event.wineName || 'Unknown Wine',
      event.code || 'UNKNOWN',
      event.message || '',
      truncateText(
        safeJsonStringify(
          event.details || {}
        ),
        5000
      )
    ];
  });

  const sheet =
    ensureSystemLogSheet(spreadsheet);

  const firstRow =
    sheet.getLastRow() + 1;

  sheet
    .getRange(
      firstRow,
      1,
      rows.length,
      6
    )
    .setValues(rows);

  sheet
    .getRange(
      firstRow,
      1,
      rows.length,
      1
    )
    .setNumberFormat(
      'dd.MM.yyyy HH:mm:ss'
    );

  pending.forEach(function (item) {
    properties.deleteProperty(item.key);
  });
}


function ensureSystemLogSheet(spreadsheet) {
  let sheet =
    spreadsheet.getSheetByName(
      SYSTEM_LOG_SHEET
    );

  if (!sheet) {
    sheet = spreadsheet.insertSheet(
      SYSTEM_LOG_SHEET
    );
  }

  sheet
    .getRange('A1:F1')
    .setValues([[
      'Date and time',
      'Level',
      'Wine / component',
      'Error code',
      'Message',
      'Technical details'
    ]])
    .setBackground(HEADER_COLOR)
    .setFontWeight('bold');

  sheet.setFrozenRows(1);

  sheet
    .getRange('A:A')
    .setNumberFormat(
      'dd.MM.yyyy HH:mm:ss'
    );

  sheet.setColumnWidth(1, 175);
  sheet.setColumnWidth(2, 90);
  sheet.setColumnWidth(3, 180);
  sheet.setColumnWidth(4, 210);
  sheet.setColumnWidth(5, 420);
  sheet.setColumnWidth(6, 500);

  return sheet;
}


function safeLogEvent(
  level,
  wineName,
  code,
  message,
  details
) {
  try {
    const spreadsheet =
      SpreadsheetApp.openById(
        SPREADSHEET_ID
      );

    const sheet =
      ensureSystemLogSheet(spreadsheet);

    sheet.appendRow([
      new Date(),
      level,
      wineName,
      code,
      message,
      truncateText(
        safeJsonStringify(details),
        5000
      )
    ]);

    if (
      level === 'ERROR' ||
      level === 'WARNING'
    ) {
      setLastIssue(
        wineName,
        code + ': ' + message
      );
    }

  } catch (loggingError) {
    console.error(
      'Could not write System Log:',
      loggingError
    );
  }
}


function ensureMonitoringSheet(spreadsheet) {
  let sheet =
    spreadsheet.getSheetByName(
      MONITORING_SHEET
    );

  if (!sheet) {
    sheet = spreadsheet.insertSheet(
      MONITORING_SHEET
    );
  }

  sheet
    .getRange('A1:I1')
    .setValues([[
      'Wine',
      'Status',
      'Last reading',
      'Minutes since reading',
      'Last SG',
      'Last temperature °C',
      'Last troubleshooting issue',
      'Last checked',
      /*
       * The device column exists because the MAC left the wine sheets. This is
       * the "mentioned once" home for it: one row per wine, always current.
       */
      'Device'
    ]])
    .setBackground(HEADER_COLOR)
    .setFontWeight('bold');

  /* Clear the old ABV header if this sheet came from an earlier version. */
  sheet
    .getRange('J:J')
    .clearContent()
    .clearFormat();

  sheet.setFrozenRows(1);

  sheet
    .getRange('C:C')
    .setNumberFormat(
      'dd.MM.yyyy HH:mm:ss'
    );

  sheet
    .getRange('D:D')
    .setNumberFormat('0');

  sheet
    .getRange('E:E')
    .setNumberFormat('0.0000');

  sheet
    .getRange('F:F')
    .setNumberFormat('0.0');

  sheet
    .getRange('H:H')
    .setNumberFormat(
      'dd.MM.yyyy HH:mm:ss'
    );

  sheet
    .getRange('I:I')
    .setNumberFormat('@');

  sheet.setColumnWidth(1, 180);
  sheet.setColumnWidth(2, 100);
  sheet.setColumnWidth(3, 175);
  sheet.setColumnWidth(4, 150);
  sheet.setColumnWidth(5, 90);
  sheet.setColumnWidth(6, 145);
  sheet.setColumnWidth(7, 500);
  sheet.setColumnWidth(8, 175);
  sheet.setColumnWidth(9, 155);

  return sheet;
}


function writeMonitoringRows(
  spreadsheet,
  rows
) {
  const sheet =
    ensureMonitoringSheet(spreadsheet);

  const existingRows =
    Math.max(0, sheet.getLastRow() - 1);

  if (existingRows > 0) {
    sheet
      .getRange(
        2,
        1,
        existingRows,
        MONITORING_COLUMN_COUNT
      )
      .clearContent()
      .clearFormat();
  }

  if (rows.length === 0) {
    return;
  }

  rows.sort(function (a, b) {
    return String(a[0]).localeCompare(
      String(b[0])
    );
  });

  sheet
    .getRange(2, 1, rows.length, MONITORING_COLUMN_COUNT)
    .setValues(rows);

  sheet
    .getRange(2, 3, rows.length, 1)
    .setNumberFormat(
      'dd.MM.yyyy HH:mm:ss'
    );

  sheet
    .getRange(2, 4, rows.length, 1)
    .setNumberFormat('0');

  sheet
    .getRange(2, 5, rows.length, 1)
    .setNumberFormat('0.0000');

  sheet
    .getRange(2, 6, rows.length, 1)
    .setNumberFormat('0.0');

  sheet
    .getRange(2, 8, rows.length, 1)
    .setNumberFormat(
      'dd.MM.yyyy HH:mm:ss'
    );

  const backgrounds =
    rows.map(function (row) {
      if (row[1] === 'OK') {
        return [STATUS_OK_COLOR];
      }

      if (row[1] === 'MISSING') {
        return [STATUS_MISSING_COLOR];
      }

      return [STATUS_NO_DATA_COLOR];
    });

  sheet
    .getRange(2, 2, rows.length, 1)
    .setBackgrounds(backgrounds)
    .setFontWeight('bold');
}


function setLastIssue(
  wineName,
  issue
) {
  PropertiesService
    .getScriptProperties()
    .setProperty(
      'last-issue-' +
      safePropertyPart(wineName),
      truncateText(issue, 1000)
    );
}


function getLastIssue(wineName) {
  return (
    PropertiesService
      .getScriptProperties()
      .getProperty(
        'last-issue-' +
        safePropertyPart(wineName)
      ) ||
    ''
  );
}


function isWineSheet(sheet) {
  const name = sheet.getName();

  if (
    name === SYSTEM_LOG_SHEET ||
    name === MONITORING_SHEET ||
    name === PROCESSED_IDS_SHEET
  ) {
    return false;
  }

  if (sheet.getLastRow() < 2) {
    return false;
  }

  return (
    String(
      sheet
        .getRange('A2')
        .getDisplayValue()
    ).trim() === 'Date and time'
  );
}


function getWineNameFromSheet(sheet) {
  return firstPresent(
    sheet.getRange('A1').getDisplayValue(),
    sheet.getName(),
    'Unknown Wine'
  );
}


function createSuccessResponse(
  spreadsheet,
  sheet,
  measurementLogged,
  code
) {
  const sheetUrl =
    spreadsheet.getUrl() +
    '#gid=' +
    sheet.getSheetId();

  return jsonResponse({
    status: 'ok',
    code: code,
    logged: measurementLogged,
    doclongurl: sheetUrl
  });
}


function jsonResponse(object) {
  return ContentService
    .createTextOutput(
      JSON.stringify(object)
    )
    .setMimeType(
      ContentService.MimeType.JSON
    );
}


function codedError(code, message) {
  const error = new Error(message);
  error.code = code;
  return error;
}


function parseJsonSafely(
  text,
  fallback
) {
  if (!text) {
    return fallback;
  }

  try {
    return JSON.parse(text);
  } catch (error) {
    return fallback;
  }
}


function safeJsonStringify(value) {
  try {
    return JSON.stringify(value);
  } catch (error) {
    return String(value);
  }
}


function safePropertyPart(value) {
  return Utilities
    .base64EncodeWebSafe(
      String(value || '')
    )
    .substring(0, 120);
}


function truncateText(
  value,
  maximumLength
) {
  const text = String(value || '');

  if (text.length <= maximumLength) {
    return text;
  }

  return (
    text.substring(
      0,
      maximumLength - 3
    ) +
    '...'
  );
}


function firstPresent() {
  for (
    let i = 0;
    i < arguments.length;
    i++
  ) {
    const value = arguments[i];

    if (
      value !== undefined &&
      value !== null &&
      String(value).trim() !== ''
    ) {
      return value;
    }
  }

  return '';
}


function toNumber(value) {
  if (
    value === undefined ||
    value === null ||
    value === ''
  ) {
    return '';
  }

  const number = Number(value);

  return Number.isFinite(number)
    ? number
    : '';
}


function sanitizeSheetName(name) {
  const cleaned =
    String(name)
      .replace(
        /[\\\/?*\[\]:]/g,
        '-'
      )
      .trim()
      .substring(0, 100);

  return cleaned || 'Unknown Wine';
}


/*
 * ===========================================================================
 * Chronological ordering after a backlog drain
 * ===========================================================================
 *
 * The device drains its queue FIFO, so a single batch arrives oldest first and
 * appends in order. The sheet as a whole can still end up out of order:
 *
 * - the first batch after enabling enhanced mode appends readings captured
 *   BEFORE the live rows already sitting at the bottom of the sheet;
 * - single-reading rows are stamped with the RECEIPT time while batch rows use
 *   the CAPTURE time, so a sheet written by both paths can interleave;
 * - rows with TimestampValid=false have no sortable key at all.
 *
 * So after every batch, each sheet the batch actually appended to is checked
 * and, only if needed, reordered ascending on column A.
 *
 * Called from handleBatchPost() while the script lock is held, so no other
 * request can append between the ordered-check and the rewrite.
 *
 * The single-reading path deliberately does NOT call this: it appends one row
 * per request in receipt order, which is already ascending by its own capture
 * time, and paying for an ordered-check on every ten-minute check-in would buy
 * nothing.
 */
function sortAppendedSheets(
  spreadsheet,
  sheetStates,
  deviceName
) {
  Object.keys(sheetStates).forEach(function (key) {
    const state = sheetStates[key];

    if (state.rowsAppended === 0) {
      return;
    }

    try {
      const outcome =
        sortWineSheetByCaptureTime(state.sheet);

      if (outcome.reorderedRowCount === 0) {
        return;
      }

      safeLogEvent(
        'INFO',
        state.wineName,
        'ROWS_REORDERED',
        'Rows were reordered so the sheet reads chronologically by capture time after a backlog upload.',
        {
          deviceName: deviceName,
          reorderedRows:
            outcome.reorderedRowCount,
          firstRow: outcome.firstRow,
          lastRow: outcome.lastRow,
          untimestampedRowsMovedToBottom:
            outcome.untimestampedRowCount,
          sheetUrl:
            spreadsheet.getUrl() +
            '#gid=' +
            state.sheet.getSheetId()
        }
      );

      /*
       * A reorder means this batch put rows somewhere other than the end of
       * the sheet, which is exactly the condition under which the derived
       * values below that point were computed without them.
       *
       * outcome.firstRow is the first row the sort moved, so every row from
       * there down is suspect and every row above it is not: the sheet is
       * ascending, so no row that sorted below firstRow can fall inside the
       * rolling window or the previous calendar day of a row above it.
       */
      repairDerivedColumns(
        spreadsheet,
        state.sheet,
        state.wineName,
        toSortableTime(
          state.sheet
            .getRange(outcome.firstRow, 1)
            .getValue()
        ),
        REBUILD_MAX_ROWS_PER_REQUEST,
        deviceName
      );

    } catch (sortError) {
      /*
       * The rows are already written and their ids are already acknowledged,
       * so a failed reorder is cosmetic. It must never turn a successful batch
       * into a retry, which would duplicate rows.
       */
      safeLogEvent(
        'ERROR',
        state.wineName,
        'SORT_FAILED',
        sortError && sortError.message
          ? sortError.message
          : String(sortError),
        {
          deviceName: deviceName,
          stack:
            sortError && sortError.stack
              ? sortError.stack
              : '',
          sheetName: state.sheetName
        }
      );
    }
  });
}


/*
 * Reorders rows 3..lastRow ascending by column A.
 *
 * WHY NOT Range.sort()
 *
 * Range.sort() is documented as sorting "the cells in the given range" and
 * carries no guarantee about per-row backgrounds, notes or font weights, all
 * three of which this sheet uses as real per-row state: new-day shading across
 * the row, the data-gap fill on A..C plus its note on column A, and the
 * average-quality fill, bold and note on column G. It also offers no control
 * over where blank keys land and no guarantee of stability for equal keys.
 *
 * So values, backgrounds, notes and font weights are read together, permuted
 * as one unit, and written back. Anything visual therefore travels with the
 * row that owns it, by construction rather than by trusting a side effect.
 *
 * BLANK COLUMN A (TimestampValid=false) GOES TO THE BOTTOM
 *
 * Such a row has no position in time, so any placement among the timestamped
 * rows would be a fabrication - and placing it mid-sheet is worse than that,
 * because every derived calculation walks backwards from lastRow and stops at
 * the first row older than its window, so a blank row sitting between real
 * readings is an obstacle in the middle of every future scan. At the bottom it
 * is a clearly delimited block of "readings we could not place in time",
 * ordered among themselves by arrival, which is the only ordering information
 * they carry. The cell note on column A already explains each one.
 *
 * Returns { reorderedRowCount, firstRow, lastRow, untimestampedRowCount }.
 */
function sortWineSheetByCaptureTime(sheet) {
  const firstDataRow = 3;
  const lastRow = sheet.getLastRow();

  const nothingToDo = {
    reorderedRowCount: 0,
    firstRow: 0,
    lastRow: 0,
    untimestampedRowCount: 0
  };

  /* A single data row is trivially ordered. */
  if (lastRow <= firstDataRow) {
    return nothingToDo;
  }

  const rowCount =
    lastRow - firstDataRow + 1;

  /*
   * One narrow read of column A decides whether any work is needed at all.
   * An already-ordered sheet, which is the normal case, costs exactly this
   * and writes nothing.
   */
  const dateValues =
    sheet
      .getRange(
        firstDataRow,
        1,
        rowCount,
        1
      )
      .getValues();

  const keys = [];
  let untimestampedRowCount = 0;

  for (let i = 0; i < rowCount; i++) {
    const time =
      toSortableTime(dateValues[i][0]);

    if (time === null) {
      untimestampedRowCount++;
    }

    keys.push({
      index: i,
      time: time
    });
  }

  if (isOrderedByCaptureTime(keys)) {
    return nothingToDo;
  }

  const order =
    keys
      .slice()
      .sort(compareCaptureKeys);

  /*
   * Rows that the sort leaves in place need no rewrite, so only the span from
   * the first moved row to the last moved row is touched. A backlog appended
   * under a handful of live rows therefore rewrites those rows and the backlog,
   * not the whole fermentation history.
   */
  let firstChanged = -1;
  let lastChanged = -1;

  for (let i = 0; i < rowCount; i++) {
    if (order[i].index !== i) {
      if (firstChanged === -1) {
        firstChanged = i;
      }

      lastChanged = i;
    }
  }

  if (firstChanged === -1) {
    return nothingToDo;
  }

  const windowStart =
    firstDataRow + firstChanged;

  const windowRowCount =
    lastChanged - firstChanged + 1;

  const range =
    sheet.getRange(
      windowStart,
      1,
      windowRowCount,
      DATA_COLUMN_COUNT
    );

  const values = range.getValues();
  const backgrounds = range.getBackgrounds();
  const notes = range.getNotes();
  const fontWeights = range.getFontWeights();

  const newValues = [];
  const newBackgrounds = [];
  const newNotes = [];
  const newFontWeights = [];

  for (
    let offset = 0;
    offset < windowRowCount;
    offset++
  ) {
    const source =
      order[firstChanged + offset].index -
      firstChanged;

    newValues.push(values[source]);
    newBackgrounds.push(backgrounds[source]);
    newNotes.push(notes[source]);
    newFontWeights.push(fontWeights[source]);
  }

  range.setValues(newValues);
  range.setBackgrounds(newBackgrounds);
  range.setNotes(newNotes);
  range.setFontWeights(newFontWeights);

  return {
    reorderedRowCount: windowRowCount,
    firstRow: windowStart,
    lastRow:
      windowStart + windowRowCount - 1,
    untimestampedRowCount:
      untimestampedRowCount
  };
}


/*
 * The cheap ordered-check: ascending capture times, with every untimestamped
 * row already below every timestamped one. Equal times are in order.
 */
function isOrderedByCaptureTime(keys) {
  let previousTime = null;
  let seenUntimestamped = false;

  for (let i = 0; i < keys.length; i++) {
    const time = keys[i].time;

    if (time === null) {
      seenUntimestamped = true;
      continue;
    }

    /* A timestamped row below an untimestamped one. */
    if (seenUntimestamped) {
      return false;
    }

    if (
      previousTime !== null &&
      time < previousTime
    ) {
      return false;
    }

    previousTime = time;
  }

  return true;
}


/*
 * Untimestamped rows last, then ascending by time. The index tiebreak makes
 * the ordering stable without relying on the engine's sort being stable, so
 * rows sharing a capture time - and untimestamped rows, which all compare
 * equal - keep the order they were appended in.
 */
function compareCaptureKeys(a, b) {
  if (
    a.time === null &&
    b.time === null
  ) {
    return a.index - b.index;
  }

  if (a.time === null) {
    return 1;
  }

  if (b.time === null) {
    return -1;
  }

  if (a.time !== b.time) {
    return a.time < b.time
      ? -1
      : 1;
  }

  return a.index - b.index;
}


/*
 * Milliseconds for a column A value, or null when the cell holds no usable
 * date. Shared by the sorter and by findLastMeasurementRow() so both agree on
 * exactly which rows count as timestamped.
 */
function toSortableTime(value) {
  if (
    value === '' ||
    value === null ||
    value === undefined
  ) {
    return null;
  }

  const date =
    value instanceof Date
      ? value
      : new Date(value);

  const time = date.getTime();

  return isNaN(time)
    ? null
    : time;
}


/*
 * ===========================================================================
 * Derived-column rebuild (backfill repair)
 * ===========================================================================
 *
 * WHAT IS DERIVED, AND FROM WHAT
 *
 *   E, F  four-hour rolling average SG and °C   \
 *   G     average quality, its fill and its note |  a function of A, B and C
 *   H, I  previous calendar day average SG and °C|  over a window of rows
 *   row fill   new-day shading                  /
 *
 * Columns A, B, C and K..S are facts the device sent and are never touched
 * here. Everything above is recomputed from scratch.
 *
 * ONE EXCEPTION: the DATA GAP fill and note are only ever CLEARED, never
 * added. The live path measures a gap between consecutive readings SEEN,
 * including readings the logging interval declined to store, and a rebuild
 * only sees the stored rows. Clearing a flag whose gap has since been filled
 * is always right; adding one from a coarser view of the same history is not.
 */


/*
 * Run a rebuild and settle whatever it could not finish.
 *
 * Never throws. By the time this runs the rows are already written and their
 * record ids are already acknowledged, so a failed repair is a cosmetic
 * problem; turning it into a failed batch would make the device resend rows
 * that are already in the sheet.
 */
function repairDerivedColumns(
  spreadsheet,
  sheet,
  wineName,
  fromTime,
  maxRows,
  deviceName
) {
  if (
    fromTime === null ||
    fromTime === undefined ||
    !Number.isFinite(Number(fromTime))
  ) {
    return null;
  }

  const sheetName = sheet.getName();

  try {
    const outcome =
      rebuildDerivedColumns(
        sheet,
        Number(fromTime),
        maxRows
      );

    const stillOwedFrom =
      resolvePendingRebuild(
        sheetName,
        wineName,
        Number(fromTime),
        outcome.deferredFromTime
      );

    if (outcome.changed) {
      safeLogEvent(
        'INFO',
        wineName,
        'DERIVED_VALUES_REBUILT',
        'The four-hour averages, their quality, the previous-day averages and the new-day shading were recomputed over rows that a late upload changed.',
        {
          deviceName: deviceName,
          rebuiltRows: outcome.rebuiltRowCount,
          averagePoints: outcome.averagePointCount,
          firstRow: outcome.firstRow,
          lastRow: outcome.lastRow,
          rebuiltFrom:
            Utilities.formatDate(
              new Date(Number(fromTime)),
              TIME_ZONE,
              'dd.MM.yyyy HH:mm:ss'
            ),
          remainingRebuildPending:
            stillOwedFrom === null
              ? 'no'
              : Utilities.formatDate(
                  new Date(stillOwedFrom),
                  TIME_ZONE,
                  'dd.MM.yyyy HH:mm:ss'
                ),
          sheetUrl:
            spreadsheet.getUrl() +
            '#gid=' +
            sheet.getSheetId()
        }
      );
    }

    return outcome;

  } catch (rebuildError) {
    /*
     * Leave the work queued. The background pass retries it with a bigger
     * budget and without an HTTP timeout waiting on the other end.
     */
    writePendingRebuild(
      sheetName,
      wineName,
      Number(fromTime)
    );

    safeLogEvent(
      'ERROR',
      wineName,
      'REBUILD_FAILED',
      rebuildError && rebuildError.message
        ? rebuildError.message
        : String(rebuildError),
      {
        deviceName: deviceName,
        stack:
          rebuildError && rebuildError.stack
            ? rebuildError.stack
            : '',
        sheetName: sheetName
      }
    );

    return null;
  }
}


/*
 * Recomputes every derived value from the first row at or after fromTime down
 * to the last timestamped row, and writes back only if something actually
 * changed.
 *
 * Reads, in order: column A for the whole data area (this decides the geometry
 * of the pass), then B and C over the rebuild window plus its context, then the
 * existing derived values, fills and notes of the rebuild window. Five reads
 * and at most five writes, whatever the window size.
 *
 * Returns
 *   { rebuiltRowCount, averagePointCount, firstRow, lastRow, changed,
 *     deferredFromTime }
 * where deferredFromTime is the capture time the caller must come back for
 * when maxRows cut the window short.
 */
function rebuildDerivedColumns(
  sheet,
  fromTime,
  maxRows
) {
  const firstDataRow = 3;
  const lastRow = sheet.getLastRow();

  const nothingToDo = {
    rebuiltRowCount: 0,
    averagePointCount: 0,
    firstRow: 0,
    lastRow: 0,
    changed: false,
    deferredFromTime: null
  };

  if (lastRow < firstDataRow) {
    return nothingToDo;
  }

  const totalRows =
    lastRow - firstDataRow + 1;

  const dateValues =
    sheet
      .getRange(
        firstDataRow,
        1,
        totalRows,
        1
      )
      .getValues();

  const times = [];

  for (let i = 0; i < totalRows; i++) {
    times.push(
      toSortableTime(dateValues[i][0])
    );
  }

  let rewriteStart = -1;

  for (let i = 0; i < totalRows; i++) {
    if (
      times[i] !== null &&
      times[i] >= fromTime
    ) {
      rewriteStart = i;
      break;
    }
  }

  if (rewriteStart === -1) {
    return nothingToDo;
  }

  let rewriteEnd = -1;

  for (
    let i = totalRows - 1;
    i >= rewriteStart;
    i--
  ) {
    if (times[i] !== null) {
      rewriteEnd = i;
      break;
    }
  }

  if (rewriteEnd < rewriteStart) {
    return nothingToDo;
  }

  /*
   * Oldest rows first. The four-hour spacing chains forward from the previous
   * average point, so a suffix cannot be rebuilt before the rows above it are
   * right. Whatever the cap leaves over is handed back to the caller and keeps
   * the values it already has until the background pass reaches it.
   */
  let deferredFromTime = null;

  if (
    rewriteEnd - rewriteStart + 1 >
    maxRows
  ) {
    const cappedEnd =
      rewriteStart + maxRows - 1;

    for (
      let i = cappedEnd + 1;
      i <= rewriteEnd;
      i++
    ) {
      if (times[i] !== null) {
        deferredFromTime = times[i];
        break;
      }
    }

    rewriteEnd = cappedEnd;
  }

  /*
   * Rows read purely as context: they feed the first rebuilt row's rolling
   * window and previous-day average, and are never written to.
   */
  const contextCutoff =
    times[rewriteStart] -
    REBUILD_CONTEXT_LOOKBACK_MS;

  let contextStart = rewriteStart;

  while (contextStart > 0) {
    const previousRowTime =
      times[contextStart - 1];

    if (
      previousRowTime === null ||
      previousRowTime < contextCutoff
    ) {
      break;
    }

    contextStart--;
  }

  const measured =
    sheet
      .getRange(
        firstDataRow + contextStart,
        2,
        rewriteEnd - contextStart + 1,
        2
      )
      .getValues();

  const rewriteStartRow =
    firstDataRow + rewriteStart;

  const rewriteRowCount =
    rewriteEnd - rewriteStart + 1;

  const derivedRange =
    sheet.getRange(
      rewriteStartRow,
      5,
      rewriteRowCount,
      5
    );

  const rowRange =
    sheet.getRange(
      rewriteStartRow,
      1,
      rewriteRowCount,
      DATA_COLUMN_COUNT
    );

  const captureNoteRange =
    sheet.getRange(
      rewriteStartRow,
      1,
      rewriteRowCount,
      1
    );

  const qualityRange =
    sheet.getRange(
      rewriteStartRow,
      7,
      rewriteRowCount,
      1
    );

  const existingDerived =
    derivedRange.getValues();

  const existingBackgrounds =
    rowRange.getBackgrounds();

  const existingCaptureNotes =
    captureNoteRange.getNotes();

  const existingQualityNotes =
    qualityRange.getNotes();

  const existingQualityWeights =
    qualityRange.getFontWeights();

  /*
   * Seed the average chain from the untouched part of the sheet, so rebuilding
   * a tail cannot shift the average points above it.
   */
  const previousAverageDate =
    getLastAverageOutputDateBefore(
      sheet,
      rewriteStartRow
    );

  let lastAverageTime =
    previousAverageDate
      ? previousAverageDate.getTime()
      : null;

  let firstMeasurementTime = null;

  if (lastAverageTime === null) {
    const firstDate =
      getFirstMeasurementDate(sheet);

    firstMeasurementTime =
      firstDate
        ? firstDate.getTime()
        : null;
  }

  /* Newest timestamped row above the window, for the first day comparison. */
  let previousTime = null;

  for (
    let i = rewriteStart - 1;
    i >= 0;
    i--
  ) {
    if (times[i] !== null) {
      previousTime = times[i];
      break;
    }
  }

  const derivedValues = [];
  const backgrounds = [];
  const captureNotes = [];
  const qualityNotes = [];
  const qualityWeights = [];

  let averagePointCount = 0;

  for (
    let offset = 0;
    offset < rewriteRowCount;
    offset++
  ) {
    const index = rewriteStart + offset;
    const rowTime = times[index];

    /*
     * A row with no capture time has no position in time, so it carries no
     * derived value and is copied through untouched.
     */
    if (rowTime === null) {
      derivedValues.push(
        existingDerived[offset].slice()
      );

      backgrounds.push(
        existingBackgrounds[offset].slice()
      );

      captureNotes.push([
        existingCaptureNotes[offset][0]
      ]);

      qualityNotes.push([
        existingQualityNotes[offset][0]
      ]);

      qualityWeights.push([
        existingQualityWeights[offset][0]
      ]);

      continue;
    }

    const averages = emptyAverageSet();

    let qualityColor = null;
    let qualityNote = '';

    if (
      isAverageDue(
        lastAverageTime,
        firstMeasurementTime,
        rowTime,
        AVERAGE_OUTPUT_INTERVAL_HOURS
      )
    ) {
      const classified =
        classifyRollingAssessment(
          rollingAssessmentFromRows(
            times,
            measured,
            contextStart,
            index,
            ROLLING_AVERAGE_HOURS
          )
        );

      averages.rollingSG = classified.rollingSG;
      averages.rollingTempC = classified.rollingTempC;
      averages.quality = classified.quality;
      qualityColor = classified.color;
      qualityNote = classified.note;

      lastAverageTime = rowTime;
      averagePointCount++;
    }

    const firstReadingOfDay =
      previousTime === null ||
      dateKey(
        new Date(previousTime),
        TIME_ZONE
      ) !==
      dateKey(
        new Date(rowTime),
        TIME_ZONE
      );

    if (
      firstReadingOfDay &&
      previousTime !== null
    ) {
      const previousDayAverages =
        dailyAveragesFromRows(
          times,
          measured,
          contextStart,
          index - 1,
          getPreviousCalendarDateKey(
            new Date(rowTime),
            TIME_ZONE
          ),
          TIME_ZONE
        );

      averages.previousDaySG =
        previousDayAverages.sg;

      averages.previousDayTempC =
        previousDayAverages.tempC;
    }

    derivedValues.push([
      averages.rollingSG,
      averages.rollingTempC,
      averages.quality,
      averages.previousDaySG,
      averages.previousDayTempC
    ]);

    /* Whole-row fill first, exactly the order the append path applies it in. */
    const rowBackground = [];

    for (
      let column = 0;
      column < DATA_COLUMN_COUNT;
      column++
    ) {
      rowBackground.push(
        firstReadingOfDay
          ? NEW_DAY_COLOR
          : NO_FILL_COLOR
      );
    }

    const existingCaptureNote =
      existingCaptureNotes[offset][0] || '';

    const ownsCaptureNote =
      existingCaptureNote.indexOf(
        DATA_GAP_NOTE_PREFIX
      ) === 0;

    const stillGapped =
      ownsCaptureNote &&
      previousTime !== null &&
      (rowTime - previousTime) / 60000 >
        MISSING_READING_MINUTES;

    if (stillGapped) {
      rowBackground[0] = DATA_GAP_COLOR;
      rowBackground[1] = DATA_GAP_COLOR;
      rowBackground[2] = DATA_GAP_COLOR;
    }

    captureNotes.push([
      ownsCaptureNote && !stillGapped
        ? ''
        : existingCaptureNote
    ]);

    if (averages.quality) {
      rowBackground[6] = qualityColor;
    }

    backgrounds.push(rowBackground);
    qualityNotes.push([qualityNote]);

    qualityWeights.push([
      averages.quality
        ? 'bold'
        : 'normal'
    ]);

    previousTime = rowTime;
  }

  const changed =
    !gridsEqual(derivedValues, existingDerived) ||
    !gridsEqual(backgrounds, existingBackgrounds) ||
    !gridsEqual(captureNotes, existingCaptureNotes) ||
    !gridsEqual(qualityNotes, existingQualityNotes) ||
    !gridsEqual(qualityWeights, existingQualityWeights);

  if (changed) {
    derivedRange.setValues(derivedValues);
    rowRange.setBackgrounds(backgrounds);
    captureNoteRange.setNotes(captureNotes);
    qualityRange.setNotes(qualityNotes);
    qualityRange.setFontWeights(qualityWeights);
  }

  return {
    rebuiltRowCount: rewriteRowCount,
    averagePointCount: averagePointCount,
    firstRow: rewriteStartRow,
    lastRow:
      rewriteStartRow + rewriteRowCount - 1,
    changed: changed,
    deferredFromTime: deferredFromTime
  };
}


/*
 * calculateRollingAverageAssessment() for a row that is already in the sheet,
 * answered from the arrays the rebuild read once instead of from the sheet.
 *
 * The live version adds the incoming reading separately because it is not a
 * row yet; here it is times[index] and is picked up by the loop, so the two
 * produce the same numbers for the same set of readings.
 */
function rollingAssessmentFromRows(
  times,
  measured,
  contextStart,
  index,
  hours
) {
  const currentTime = times[index];

  const cutoffTime =
    currentTime -
    hours * 60 * 60 * 1000;

  let sgTotal = 0;
  let sgCount = 0;
  let tempTotal = 0;
  let tempCount = 0;

  const readingTimes = [];

  for (let i = index; i >= contextStart; i--) {
    const rowTime = times[i];

    if (rowTime === null) {
      continue;
    }

    if (rowTime < cutoffTime) {
      break;
    }

    if (rowTime > currentTime) {
      continue;
    }

    readingTimes.push(rowTime);

    const rowSG =
      measured[i - contextStart][0];

    const rowTemp =
      measured[i - contextStart][1];

    const sgValue = Number(rowSG);
    const tempValue = Number(rowTemp);

    if (
      rowSG !== '' &&
      Number.isFinite(sgValue)
    ) {
      sgTotal += sgValue;
      sgCount++;
    }

    if (
      rowTemp !== '' &&
      Number.isFinite(tempValue)
    ) {
      tempTotal += tempValue;
      tempCount++;
    }
  }

  readingTimes.sort(function (a, b) {
    return a - b;
  });

  let largestGapMilliseconds =
    readingTimes.length > 0
      ? Math.max(
          0,
          readingTimes[0] - cutoffTime
        )
      : hours * 60 * 60 * 1000;

  for (
    let i = 1;
    i < readingTimes.length;
    i++
  ) {
    largestGapMilliseconds = Math.max(
      largestGapMilliseconds,
      readingTimes[i] - readingTimes[i - 1]
    );
  }

  if (readingTimes.length > 0) {
    largestGapMilliseconds = Math.max(
      largestGapMilliseconds,
      currentTime -
        readingTimes[readingTimes.length - 1]
    );
  }

  const averages = averageResult(
    sgTotal,
    sgCount,
    tempTotal,
    tempCount
  );

  return {
    sg: averages.sg,
    tempC: averages.tempC,
    maxGapMinutes:
      largestGapMilliseconds / 60000,
    readingCount:
      Math.max(sgCount, tempCount)
  };
}


/*
 * calculateDailyAverages() over the rows the rebuild already holds.
 *
 * The context window always reaches at least 48 hours above the first rebuilt
 * row, and a calendar day begins at most 48 hours before any instant on the
 * day after it, so the target day is always fully inside the arrays.
 */
function dailyAveragesFromRows(
  times,
  measured,
  contextStart,
  endIndex,
  targetDateKey,
  timezone
) {
  let sgTotal = 0;
  let sgCount = 0;
  let tempTotal = 0;
  let tempCount = 0;
  let foundTargetDate = false;

  for (
    let i = endIndex;
    i >= contextStart;
    i--
  ) {
    const rowTime = times[i];

    if (rowTime === null) {
      continue;
    }

    const rowDateKey =
      dateKey(
        new Date(rowTime),
        timezone
      );

    if (rowDateKey === targetDateKey) {
      foundTargetDate = true;

      const rowSG =
        measured[i - contextStart][0];

      const rowTemp =
        measured[i - contextStart][1];

      const sgValue = Number(rowSG);
      const tempValue = Number(rowTemp);

      if (
        rowSG !== '' &&
        Number.isFinite(sgValue)
      ) {
        sgTotal += sgValue;
        sgCount++;
      }

      if (
        rowTemp !== '' &&
        Number.isFinite(tempValue)
      ) {
        tempTotal += tempValue;
        tempCount++;
      }

    } else if (
      foundTargetDate &&
      rowDateKey < targetDateKey
    ) {
      break;
    }
  }

  return averageResult(
    sgTotal,
    sgCount,
    tempTotal,
    tempCount
  );
}


/*
 * Cell-by-cell comparison of two same-shaped grids. This is what keeps a
 * rebuild that changes nothing from writing anything at all - which is the
 * normal case for the rows below a small backfill.
 */
function gridsEqual(left, right) {
  if (left.length !== right.length) {
    return false;
  }

  for (
    let row = 0;
    row < left.length;
    row++
  ) {
    if (
      left[row].length !== right[row].length
    ) {
      return false;
    }

    for (
      let column = 0;
      column < left[row].length;
      column++
    ) {
      if (
        left[row][column] !==
        right[row][column]
      ) {
        return false;
      }
    }
  }

  return true;
}


/*
 * ---------------------------------------------------------------------------
 * Rebuild work owed to a sheet
 * ---------------------------------------------------------------------------
 *
 * One Script Property per sheet holding the oldest capture time still to be
 * rebuilt. Written when a device request runs out of its row budget, or when a
 * rebuild throws, and cleared by whichever pass finally covers it.
 */
function getPendingRebuildKey(sheetName) {
  return (
    PENDING_REBUILD_PREFIX + sheetName
  );
}


function readPendingRebuild(sheetName) {
  const stored =
    PropertiesService
      .getScriptProperties()
      .getProperty(
        getPendingRebuildKey(sheetName)
      );

  const parsed =
    parseJsonSafely(stored, null);

  if (
    !parsed ||
    !Number.isFinite(Number(parsed.fromTime))
  ) {
    return null;
  }

  return {
    sheetName: sheetName,
    wineName:
      parsed.wineName || sheetName,
    fromTime: Number(parsed.fromTime)
  };
}


function writePendingRebuild(
  sheetName,
  wineName,
  fromTime
) {
  PropertiesService
    .getScriptProperties()
    .setProperty(
      getPendingRebuildKey(sheetName),
      JSON.stringify({
        sheetName: sheetName,
        wineName: wineName,
        fromTime: Number(fromTime)
      })
    );
}


function deletePendingRebuild(sheetName) {
  PropertiesService
    .getScriptProperties()
    .deleteProperty(
      getPendingRebuildKey(sheetName)
    );
}


/*
 * Records what a finished pass still leaves owed, and returns it.
 *
 * Two things can be outstanding: the tail this pass could not reach, and an
 * older starting point queued earlier that this pass started below. The oldest
 * of the two wins, because a rebuild always works downwards from its start.
 */
function resolvePendingRebuild(
  sheetName,
  wineName,
  coveredFromTime,
  deferredFromTime
) {
  const existing =
    readPendingRebuild(sheetName);

  const candidates = [];

  if (
    existing &&
    existing.fromTime < coveredFromTime
  ) {
    candidates.push(existing.fromTime);
  }

  if (deferredFromTime !== null) {
    candidates.push(deferredFromTime);
  }

  if (candidates.length === 0) {
    if (existing) {
      deletePendingRebuild(sheetName);
    }

    return null;
  }

  const next = Math.min.apply(
    null,
    candidates
  );

  writePendingRebuild(
    sheetName,
    wineName,
    next
  );

  return next;
}


/*
 * Finishes the rebuilds device requests could not afford to finish inline.
 *
 * Called from checkForMissingReadings(), which already holds the spreadsheet
 * lock and, unlike a device request, has no fifteen-second HTTP timeout on the
 * other end. Bounded by wall-clock time so it can never approach the
 * six-minute execution limit.
 *
 * Returns the number of rebuild passes it ran.
 */
function flushPendingRebuilds(spreadsheet) {
  const properties =
    PropertiesService.getScriptProperties();

  const startedAt = Date.now();

  let passCount = 0;

  for (
    let round = 0;
    round < REBUILD_MAX_BACKGROUND_PASSES;
    round++
  ) {
    const keys =
      properties
        .getKeys()
        .filter(function (key) {
          return (
            key.indexOf(
              PENDING_REBUILD_PREFIX
            ) === 0
          );
        });

    if (keys.length === 0) {
      return passCount;
    }

    let workedThisRound = false;

    for (let i = 0; i < keys.length; i++) {
      if (
        Date.now() - startedAt >
        REBUILD_BACKGROUND_BUDGET_MS
      ) {
        return passCount;
      }

      const sheetName =
        keys[i].substring(
          PENDING_REBUILD_PREFIX.length
        );

      const pending =
        readPendingRebuild(sheetName);

      if (!pending) {
        properties.deleteProperty(keys[i]);
        continue;
      }

      const sheet =
        spreadsheet.getSheetByName(sheetName);

      if (!sheet) {
        deletePendingRebuild(sheetName);

        safeLogEvent(
          'WARNING',
          pending.wineName,
          'REBUILD_SHEET_MISSING',
          'A pending derived-value rebuild was dropped because its sheet no longer exists.',
          {
            sheetName: sheetName
          }
        );

        continue;
      }

      repairDerivedColumns(
        spreadsheet,
        sheet,
        pending.wineName,
        pending.fromTime,
        REBUILD_MAX_ROWS_PER_BACKGROUND_PASS,
        'Background rebuild'
      );

      workedThisRound = true;
      passCount++;
    }

    if (!workedThisRound) {
      return passCount;
    }
  }

  return passCount;
}


/*
 * Optional manual tool: recompute every derived value on every wine sheet,
 * from its first reading onwards.
 *
 * Run this once after pasting this version of the script. Sheets written by
 * the previous version still carry four-hour averages and quality marks that
 * were computed before the offline queue delivered the readings they were
 * missing - an 'INCOMPLETE — 1h 30m gap' over a window that is now complete.
 * Nothing else clears those, because nothing else revisits a row once it is
 * written.
 *
 * Safe to run at any time and safe to run twice: it only ever rewrites derived
 * values, and only when recomputing them produces something different.
 *
 * A long history may need more than one run. Whatever a run cannot finish
 * inside its budget stays queued and the fifteen-minute background check
 * carries on with it, so a second run is a convenience, not a requirement.
 */
function rebuildAllDerivedColumns() {
  const lock = LockService.getScriptLock();

  if (!lock.tryLock(BACKGROUND_LOCK_WAIT_MS)) {
    throw new Error(
      'Could not acquire the spreadsheet lock to rebuild derived columns.'
    );
  }

  try {
    const spreadsheet =
      SpreadsheetApp.openById(SPREADSHEET_ID);

    ensureSpreadsheetTimeZone(spreadsheet);
    ensureSystemLogSheet(spreadsheet);
    flushPendingLogs(spreadsheet);

    let queuedSheets = 0;

    spreadsheet.getSheets().forEach(function (sheet) {
      if (!isWineSheet(sheet)) {
        return;
      }

      const firstDate =
        getFirstMeasurementDate(sheet);

      if (!firstDate) {
        return;
      }

      writePendingRebuild(
        sheet.getName(),
        getWineNameFromSheet(sheet),
        firstDate.getTime()
      );

      queuedSheets++;
    });

    const passCount =
      flushPendingRebuilds(spreadsheet);

    const remaining =
      PropertiesService
        .getScriptProperties()
        .getKeys()
        .filter(function (key) {
          return (
            key.indexOf(
              PENDING_REBUILD_PREFIX
            ) === 0
          );
        })
        .length;

    safeLogEvent(
      'INFO',
      'System',
      'FULL_REBUILD_RUN',
      'A full rebuild of the derived columns was run manually.',
      {
        sheetsQueued: queuedSheets,
        rebuildPasses: passCount,
        sheetsStillPending: remaining,
        rowsPerPass:
          REBUILD_MAX_ROWS_PER_BACKGROUND_PASS
      }
    );

  } finally {
    lock.releaseLock();
  }
}
