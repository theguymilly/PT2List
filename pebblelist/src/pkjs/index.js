// Phone-side companion: stores the lists, pushes them to the watch,
// opens the editor page, and records items crossed out on the watch.
// Items lined through (on the phone or the watch) are not sent to the watch.

// Address where config.html is published (GitHub Pages).
var CONFIG_URL = 'https://theguymilly.github.io/PT2List/pebblelist/config.html';

var STORE_KEY = 'pebblelist_v1';
var MAX_LISTS = 10, MAX_ITEMS = 20, TEXT_LEN = 63, NAME_LEN = 23, NOTE_LEN = 1000;

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
    out.lists.push({
      name: String(lists[l].name || 'List').trim().substring(0, NAME_LEN) || 'List',
      items: items
    });
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
  if (target) { target.d = true; save(d); }
});

Pebble.addEventListener('showConfiguration', function () {
  var d = load();
  Pebble.openURL(CONFIG_URL + '#' + encodeURIComponent(JSON.stringify(d)));
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
