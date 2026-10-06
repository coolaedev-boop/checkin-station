// ============================================================================
//  DASHBOARD SCRIPT: runs on your SCHOOL account (a standalone project at
//  script.google.com). Serves Dashboard.html to school accounts only and pulls
//  the data from the station script on your personal Gmail.
//
//  Deploy it as a web app:
//    Execute as: Me    Who has access: Anyone within <your school>
//
//  First time: fill in the three constants, then run testConnection() from the
//  editor. If that logs data, deploy. If it errors, the log says why.
//  After ANY edit: Deploy > Manage deployments > edit > New version > Deploy.
// ============================================================================

const STATION_URL     = 'https://script.google.com/macros/s/PASTE-STATION-ID/exec';  // the station script's /exec URL
const DASHBOARD_TOKEN = 'change-me-to-a-different-long-string';  // must match DASHBOARD_TOKEN in the station script

// Who can open the dashboard. Google only shows this script a viewer's email when
// they're in the same Google Workspace as you, so other domains get turned away.
const DASHBOARD_DOMAINS = ['newtonstudents.org', 'STAFF-DOMAIN-HERE'];

const CACHE_SECONDS = 10;   // viewers share one fetch per 10 s (the page refreshes every 10 s)

function viewerAllowed_() {
  let email = '';
  try {
    email = String(Session.getActiveUser().getEmail() || '').toLowerCase();
  } catch (err) {
    email = '';
  }
  return email !== '' && DASHBOARD_DOMAINS.some(d => email.endsWith('@' + d.toLowerCase()));
}

function doGet() {
  if (!viewerAllowed_()) {
    return HtmlService.createHtmlOutput(
      '<p style="font:16px/1.5 system-ui,sans-serif;max-width:32rem;margin:48px auto;padding:0 16px">' +
      'Sign in with your Newton County Schools account to see the equipment dashboard.</p>')
      .setTitle('Equipment check-in')
      .addMetaTag('viewport', 'width=device-width, initial-scale=1');
  }
  return HtmlService.createHtmlOutputFromFile('Dashboard')
    .setTitle('Equipment check-in')
    .addMetaTag('viewport', 'width=device-width, initial-scale=1');
}

// Called by Dashboard.html through google.script.run.
function getDashboard() {
  if (!viewerAllowed_()) throw new Error('Sign in with your school account to see the dashboard.');

  const cache = CacheService.getScriptCache();
  const cached = cache.get('dashboard');
  if (cached) return JSON.parse(cached);

  const data = fetchStation_();
  try {
    cache.put('dashboard', JSON.stringify(data), CACHE_SECONDS);
  } catch (err) {
    // over the cache's 100 KB limit: just skip caching
  }
  return data;
}

function fetchStation_() {
  const url = STATION_URL + '?action=dashboard&token=' + encodeURIComponent(DASHBOARD_TOKEN);
  const res = UrlFetchApp.fetch(url, { muteHttpExceptions: true, followRedirects: true });
  let data;
  try {
    data = JSON.parse(res.getContentText());
  } catch (err) {
    throw new Error('The station script sent a web page, not data (HTTP ' + res.getResponseCode() +
                    '). Check STATION_URL, and that the station script is deployed to Anyone.');
  }
  if (!data.ok) {
    throw new Error(data.error === 'forbidden'
      ? 'DASHBOARD_TOKEN does not match the station script.'
      : 'Station script error: ' + data.error);
  }
  delete data.ok;
  return data;
}

// Run this from the editor once. It asks for permissions, then checks every link
// in the chain: your email is allowed, the school lets this script fetch outside
// data, and the station script answers with the right token.
function testConnection() {
  Logger.log('Your email: ' + Session.getActiveUser().getEmail() +
             (viewerAllowed_() ? ' (allowed)' : ' (NOT in DASHBOARD_DOMAINS)'));
  const data = fetchStation_();
  Logger.log('OK: ' + data.totalItems + ' items, ' + data.out.length + ' out, ' +
             data.logCount + ' log rows, station ' + (data.station ? 'has reported' : 'has not reported yet'));
}
