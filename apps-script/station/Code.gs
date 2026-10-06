// ============================================================================
//  STATION SCRIPT: runs on your PERSONAL Gmail, bound to the Google Sheet.
//  Holds all the data and talks to the ESP32. Deploy it as a web app:
//    Execute as: Me    Who has access: Anyone
//  Its /exec URL goes into SCRIPT_URL in config.h AND STATION_URL in the
//  dashboard script (apps-script/dashboard/Code.gs, on your school account).
//
//  API (every call needs a token)
//    GET  ?action=snapshot&token=TOKEN              -> roster + items (ESP32)
//    GET  ?action=dashboard&token=DASHBOARD_TOKEN   -> dashboard data (school dashboard script)
//    POST {type:"event", action:"checkin"|"checkout", barcode, class, person, token}
//    POST {type:"metrics", ...chip health..., token}
//  Browser
//    GET  (no params)  -> a one-line page. The dashboard itself lives on the school account.
//
//  First time: run setup() once from the editor to create the tabs.
//  After ANY edit: Deploy > Manage deployments > edit > New version > Deploy.
// ============================================================================

const TOKEN           = 'change-me-to-a-long-random-string';   // must match API_TOKEN in config.h
const DASHBOARD_TOKEN = 'change-me-to-a-different-long-string'; // must match DASHBOARD_TOKEN in the dashboard script

const TABS = {
  items:   { name: 'Items',   headers: ['Barcode', 'Name', 'Status', 'Holder', 'Updated'] },
  roster:  { name: 'Roster',  headers: ['Class', 'Name'] },
  log:     { name: 'Log',     headers: ['Timestamp', 'Action', 'Barcode', 'Item', 'Class', 'Person', 'Device'] },
  metrics: { name: 'Metrics', headers: ['Timestamp', 'device', 'uptime_s', 'heap_free', 'heap_min', 'heap_max_alloc',
                                        'heap_total', 'psram_free', 'psram_total', 'temp_c', 'rssi', 'cpu_mhz',
                                        'reset_reason', 'pending'] },
};

// ---------- Setup + helpers ----------------------------------------------------

function setup() {
  Object.keys(TABS).forEach(key => sheet_(key));
  // Barcodes are text: stops Sheets from turning 012345 into 12345
  sheet_('items').getRange('A:A').setNumberFormat('@');
  sheet_('log').getRange('C:C').setNumberFormat('@');
}

function sheet_(key) {
  const tab = TABS[key];
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  let sh = ss.getSheetByName(tab.name);
  if (!sh) {
    sh = ss.insertSheet(tab.name);
    sh.appendRow(tab.headers);
    sh.setFrozenRows(1);
  }
  return sh;
}

function rows_(key) {                       // data rows as display text, header skipped
  const sh = sheet_(key);
  const last = sh.getLastRow();
  if (last < 2) return [];
  return sh.getRange(2, 1, last - 1, TABS[key].headers.length).getDisplayValues();
}

function json_(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj)).setMimeType(ContentService.MimeType.JSON);
}

function readItems_() {
  const sh = sheet_('items');
  const last = sh.getLastRow();
  if (last < 2) return [];
  const text = sh.getRange(2, 1, last - 1, 4).getDisplayValues();
  const updated = sh.getRange(2, 5, last - 1, 1).getValues();
  return text
    .map((r, i) => ({
      barcode: r[0].trim(),
      name: r[1].trim() || 'Unnamed item',
      out: r[2].trim().toLowerCase() === 'out',
      holder: r[3].trim(),
      updated: updated[i][0] instanceof Date ? updated[i][0].toISOString() : '',
    }))
    .filter(it => it.barcode);
}

// ---------- Web app entry points ------------------------------------------------

function doGet(e) {
  const p = (e && e.parameter) || {};
  if (p.action === 'snapshot') {
    if (p.token !== TOKEN) return json_({ ok: false, error: 'forbidden' });
    return json_(Object.assign({ ok: true }, snapshot_()));
  }
  if (p.action === 'dashboard') {
    if (p.token !== DASHBOARD_TOKEN) return json_({ ok: false, error: 'forbidden' });
    return json_(Object.assign({ ok: true }, dashboardData_()));
  }
  return HtmlService.createHtmlOutput(
    '<p style="font:16px/1.5 system-ui,sans-serif;max-width:32rem;margin:48px auto;padding:0 16px">' +
    'This link is for the check-in station. The dashboard has its own link.</p>')
    .setTitle('Equipment check-in')
    .addMetaTag('viewport', 'width=device-width, initial-scale=1');
}

function doPost(e) {
  let data;
  try {
    data = JSON.parse(e.postData.contents);
  } catch (err) {
    return json_({ ok: false, error: 'bad json' });
  }
  if (data.token !== TOKEN) return json_({ ok: false, error: 'forbidden' });

  const lock = LockService.getScriptLock();  // one write at a time
  lock.waitLock(20000);
  try {
    if (data.type === 'event') recordEvent_(data);
    else if (data.type === 'metrics') recordMetrics_(data);
    else return json_({ ok: false, error: 'unknown type' });
  } finally {
    lock.releaseLock();
  }
  return json_({ ok: true });
}

// ---------- ESP32 data ------------------------------------------------------------

function snapshot_() {
  const byClass = new Map();                 // keeps the order classes appear in the sheet
  rows_('roster').forEach(([cls, name]) => {
    cls = cls.trim();
    name = name.trim();
    if (!cls || !name) return;
    if (!byClass.has(cls)) byClass.set(cls, []);
    byClass.get(cls).push(name);
  });
  return {
    roster: Array.from(byClass, ([cls, people]) => ({ class: cls, people })),
    items: readItems_().map(({ barcode, name, out, holder }) => ({ barcode, name, out, holder })),
  };
}

function recordEvent_(d) {
  const when = d.ts ? new Date(d.ts) : new Date();   // device time if it had NTP, else now
  const barcode = String(d.barcode || '');
  const isOut = d.action === 'checkout';
  const holder = isOut ? `${d.person} (${d.class})` : '';

  const items = sheet_('items');
  const codes = rows_('items').map(r => r[0].trim());
  const idx = codes.indexOf(barcode);
  let name = 'Unknown item';
  if (idx >= 0) {
    const row = idx + 2;
    name = items.getRange(row, 2).getDisplayValue() || name;
    items.getRange(row, 3, 1, 3).setValues([[isOut ? 'Out' : 'In', holder, when]]);
  } else {
    items.appendRow([barcode, name, isOut ? 'Out' : 'In', holder, when]);   // rename it in the sheet later
  }

  sheet_('log').appendRow([when, d.action, barcode, name, d.class || '', d.person || '', d.device || '']);
}

function recordMetrics_(d) {
  const fields = TABS.metrics.headers.slice(1);
  sheet_('metrics').appendRow([new Date(), ...fields.map(k => (d[k] ?? ''))]);
}

// ---------- Dashboard data (fetched by the school dashboard script) --------------

function dashboardData_() {
  const items = readItems_();
  const out = items.filter(it => it.out).sort((a, b) => (b.updated > a.updated ? 1 : -1));

  const logSheet = sheet_('log');
  const logLast = logSheet.getLastRow();
  let recent = [];
  if (logLast >= 2) {
    const n = Math.min(30, logLast - 1);
    const start = logLast - n + 1;
    const times = logSheet.getRange(start, 1, n, 1).getValues();
    const text = logSheet.getRange(start, 2, n, 5).getDisplayValues();
    recent = text.map((r, i) => ({
      time: times[i][0] instanceof Date ? times[i][0].toISOString() : '',
      action: r[0], barcode: r[1], item: r[2], cls: r[3], person: r[4],
    })).reverse();
  }

  let station = null;
  const m = sheet_('metrics');
  if (m.getLastRow() >= 2) {
    const headers = TABS.metrics.headers;
    const row = m.getRange(m.getLastRow(), 1, 1, headers.length).getValues()[0];
    station = {};
    headers.forEach((h, i) => (station[h] = row[i] instanceof Date ? row[i].toISOString() : row[i]));
  }

  return { out, totalItems: items.length, recent, logCount: Math.max(0, logLast - 1), station };
}
