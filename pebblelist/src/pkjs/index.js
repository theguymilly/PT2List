// Phone-side companion: stores the lists, pushes them to the watch,
// opens the editor page, and records items crossed out on the watch.
// Items lined through (on the phone or the watch) are not sent to the watch.

// The settings page is bundled inside this app (page.js, generated from config.html by
// tools/embed.py), so it opens with no internet. If that fails to load, or USE_BUNDLED is
// false, the copy published on GitHub Pages is opened instead.
var CONFIG_URL = 'https://theguymilly.github.io/PT2List/pebblelist/config.html';
var USE_BUNDLED = true;
var PAGE_HTML = '';
try { PAGE_HTML = require('./page'); } catch (e) { PAGE_HTML = ''; }

var STORE_KEY = 'pebblelist_v1';
var MAX_LISTS = 15, MAX_ITEMS = 20, TEXT_LEN = 63, NAME_LEN = 23, NOTE_LEN = 1000, DESC_LEN = 300;

var CMD_RESET = 1, CMD_LIST = 2, CMD_ITEM = 3, CMD_END = 4;
var CMD_CROSS = 10;      // watch -> phone: I crossed this item out

function defaults() {
  return {
    upper: true,
    font: 1,
    lists: [
      { name: 'Groceries', items: [{ t: 'Milk', d: false }, { t: 'Eggs', d: false }, { t: 'Bread', d: false }] },
      { name: 'To do', items: [{ t: 'Call mum', d: false }] }
    ]
  };
}

function normalize(d) {
  var out = { upper: true, font: 1, lists: [] };   // capitals are always on
  var f = d && parseInt(d.font, 10);
  out.font = (f === 0 || f === 1 || f === 2) ? f : 1;
  // page preferences ride along with the lists so they survive without browser storage
  out.group = !!(d && d.group);
  out.task = !!(d && d.task);
  if (d && typeof d.auto === 'boolean') out.auto = d.auto;
  if (d && Array.isArray(d.prio)) {
    out.prio = d.prio.slice(0, 40).map(function (n) {
      return String(n || '').trim().toUpperCase().substring(0, NAME_LEN);
    });
  }
  if (d && d.loc && typeof d.loc === 'object') {
    out.loc = {};
    Object.keys(d.loc).slice(0, 40).forEach(function (k) {
      var r = d.loc[k];
      if (!/^30\d{2}$/.test(k) || !r) return;
      out.loc[k] = {
        cur: String(r.cur || '').substring(0, NAME_LEN),
        hist: (Array.isArray(r.hist) ? r.hist : []).slice(-6).map(function (h) {
          return String(h || '').substring(0, NAME_LEN);
        })
      };
    });
  }
  var lists = (d && d.lists) || [];
  for (var l = 0; l < lists.length && l < MAX_LISTS; l++) {
    var items = [];
    var src = lists[l].items || [];
    for (var i = 0; i < src.length && i < MAX_ITEMS; i++) {
      var t = String(src[i].t || '').replace(/\s+/g, ' ').trim().substring(0, TEXT_LEN);
      if (t) {
        var item = { t: t, d: !!src[i].d };
        var n = String(src[i].n || '').replace(/\r/g, '').trim().substring(0, NOTE_LEN);
        if (n) item.n = n;          // notes stay on the phone; they are never sent to the watch
        items.push(item);
      }
    }
    var entry = {
      name: String(lists[l].name || 'List').trim().substring(0, NAME_LEN) || 'List',
      items: items
    };
    var desc = String(lists[l].desc || '').replace(/\r/g, '').trim().substring(0, DESC_LEN);
    if (desc) entry.desc = desc;      // list description: phone only, never sent to the watch
    out.lists.push(entry);
  }
  return out;
}

function load() {
  try {
    var raw = localStorage.getItem(STORE_KEY);
    if (raw) return normalize(JSON.parse(raw));
  } catch (e) {}
  return defaults();
}

function save(d) {
  try { localStorage.setItem(STORE_KEY, JSON.stringify(d)); } catch (e) {}
}

// ---- sending to the watch -------------------------------------------------
var sendGen = 0;

function sendQueue(queue) {
  var gen = ++sendGen;           // a newer send cancels an older one
  var i = 0;
  function next() {
    if (gen !== sendGen || i >= queue.length) return;
    var tries = 0;
    (function attempt() {
      if (gen !== sendGen) return;
      Pebble.sendAppMessage(queue[i], function () { i++; next(); }, function () {
        if (++tries < 4) setTimeout(attempt, 400);
        else { i++; next(); }
      });
    })();
  }
  next();
}

function sendAll(d) {
  var q = [{ CMD: CMD_RESET, UPPER: d.upper ? 1 : 0, FONT: d.font }];
  d.lists.forEach(function (list, l) {
    q.push({ CMD: CMD_LIST, LIST: l, TEXT: list.name });
    // Items you've lined through are not sent: the watch shows only what is still to do.
    list.items.filter(function (it) { return !it.d; }).forEach(function (it, i) {
      q.push({ CMD: CMD_ITEM, LIST: l, ITEM: i, TEXT: it.t });
    });
  });
  q.push({ CMD: CMD_END });
  sendQueue(q);
}

// ---- events ---------------------------------------------------------------
Pebble.addEventListener('ready', function () {
  sendAll(load());
});

Pebble.addEventListener('appmessage', function (e) {
  var p = e.payload;
  if (p.CMD !== CMD_CROSS) return;
  var d = load();
  var list = d.lists[p.LIST];
  if (!list) return;
  // The watch only knows the items that are still to do, so ITEM counts those only.
  var active = list.items.filter(function (it) { return !it.d; });
  var target = active[p.ITEM];
  if (!target || (p.TEXT && target.t !== p.TEXT)) {       // list changed meanwhile: match by text
    target = null;
    for (var k = 0; k < active.length; k++) {
      if (active[k].t === p.TEXT) { target = active[k]; break; }
    }
  }
  if (target) {
    target.d = true;
    // Completing "TOW M4D" on a list named "3030 S3A11" moves the aircraft: "3030 M4D"
    var tm = /^TOW\s+(.+)$/i.exec(target.t || '');
    var nm = /^(30\d{2})\s+\S/.exec(list.name || '');
    if (tm && nm) {
      var oldName = String(list.name).trim().toUpperCase();
      list.name = (nm[1] + ' ' + tm[1].trim().toUpperCase()).substring(0, NAME_LEN);
      if (d.prio) {      // keep the saved priority order pointing at the renamed list
        d.prio = d.prio.map(function (n) { return n === oldName ? list.name.toUpperCase() : n; });
      }
    }
    save(d);
  }
});

// Builds the address for the settings page: the bundled page with the lists written into it.
function bundledUrl(d) {
  var json = JSON.stringify(d)
    .replace(/</g, '\\u003c').replace(/\u2028/g, '\\u2028').replace(/\u2029/g, '\\u2029');
  var html = PAGE_HTML.replace('/*__PL_INIT__*/', 'window.__PL_INIT__=' + json + ';');
  return 'data:text/html;charset=utf-8,' + encodeURIComponent(html);
}

Pebble.addEventListener('showConfiguration', function () {
  var d = load();
  if (USE_BUNDLED && PAGE_HTML) Pebble.openURL(bundledUrl(d));
  else Pebble.openURL(CONFIG_URL + '#' + encodeURIComponent(JSON.stringify(d)));
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) return;
  var d;
  try { d = JSON.parse(decodeURIComponent(e.response)); }
  catch (err) {
    try { d = JSON.parse(e.response); } catch (err2) { return; }
  }
  d = normalize(d);
  save(d);
  sendAll(d);
});
