# Pebble Lists (Time 2 + iPhone)

A small list app for the Pebble Time 2. You edit lists in a web page on your
iPhone, they sync to the watch, and items you cross out on either one disappear from the watch and are lined through on the phone.
All text is shown in CAPITALS, both on the settings page and on the watch.
There is no toggle. What you type is stored as typed, but the page, the watch
and the Notes export all show it in capitals.

> Status: written but **not yet compiled or run on a watch**. Expect to fix a
> small build error or two on first build, and treat the "Send to watch" step
> as the part most worth testing first (see "Known unknowns").

## What's in the box

| File | Purpose |
|---|---|
| `src/c/main.c` | The watch app: lists menu and items menu (wrapped text, capitals, font size) |
| `src/pkjs/index.js` | Runs inside the Pebble Core app on the iPhone: stores lists, syncs both ways, opens the editor |
| `config.html` | The editor page (also does the Notes export) |
| `package.json`, `wscript` | Project files for the Pebble SDK |

## 1. Host the editor page

The page has to live at a normal web address. Easiest free route: put
`config.html` in a GitHub repo, enable GitHub Pages, and note the address,
e.g. `https://yourname.github.io/pebblelist/config.html`.

Then open `src/pkjs/index.js` and replace `CONFIG_URL` with that address
(it is already set to `https://theguymilly.github.io/PT2List/pebblelist/config.html`).

## 2. Build and install

Use the Pebble SDK from Core Devices (docs: ndocs.repebble.com) or CloudPebble:

```
pebble build
pebble install --phone <your iPhone's IP>
```

With the Pebble Core app on the iPhone, enable the developer connection
described in those docs. The watchapp targets emery (Time 2), plus basalt and
diorite so it also runs on older models.

## 3. Use it

* Pebble Core app → the "Lists" watchapp → Settings: opens the editor. When
  you're done, tap **Send to watch**.
* The editor shows one list at a time, with no buttons for adding or
  reordering. Everything is a gesture:
  * **Pull down** on the page to add an item at the top. Press Return to add
    the next one; press Return on an empty line (or tap away) to finish.
  * **Tap** an item to edit it. A **note** box opens under the text for
    anything you want to keep with that item. Notes stay on the phone: they
    are never sent to the watch. They are shown in small grey text under the
    item on the phone only.
    Tap outside the boxes to save. Clear the text to delete the item.
  * **Hold** an item, then drag, to reorder.
  * **Swipe right** on an item to line it through (swipe right again to undo).
    **Swipe left** to delete it for good (the text turns red as you pass the
    point of no return).
    Lined-through items stay on the phone but are **hidden on the watch**,
    so the watch only shows what is still to do.
  * The order of the list is the priority order shown on the watch.
  * The **left column** lists your lists, and is as wide as the longest name.
    The number on the right is how many items are still to do (it drops as you
    line items through). **Double-tap** a list name to rename it.
    The **ACFT** heading at the top is only a label: it shows the number of
    lists, then the number of items still to do across all of them. It is not
    a list, so it can't be opened, renamed or deleted.
  * Under the title is the list's **description**. Tap it to write a few
    lines about the list (tap "Add description" if it is empty). It is not a
    task, is not counted, and stays on the phone: it is never sent to the
    watch. It is included in the Notes export when "Include notes" is ticked.
  * Tap the **list name** at the top to switch lists (hold and drag to reorder
    them; New / Rename / Delete are at the bottom of that sheet).
  * The **⋯** button opens text size and the Notes export.
* On the watch: pick a list and scroll through its items. The list's name is
  shown in a bar above the items. Long items wrap onto as many lines as they
  need. The number next to each list is how many items are still to do.
* **Double-click Select on an item to delete it.** A single click does nothing,
  so you can't remove one by accident. The item disappears from the watch and
  is lined through on the phone, which you will see the next time you open
  the settings page. There is no undo on the watch: to bring an item back,
  swipe it again on the phone and tap **Send to watch**.
* **Tow moves the aircraft.** On an aircraft list such as `3030 S3A11`,
  completing an item like `TOW M4D` (on the phone or the watch) renames the
  list to `3030 M4D`, so the list always shows where the aircraft is now.
  Undoing the tow on the phone does not change the name back.
* Limits: 10 lists, 20 items each, 63 characters per item.

## Exporting

Open the **⋯** menu. **Export** has two choices:

* **List only:** each list's name and only the items you've checked off
  (lined through). No boxes.
* **Checklist with boxes:** every item, ☐ for not done and ☑ for done, so you
  can see what has and hasn't been done.

Both show a preview. Notes go underneath each item (untick **Include notes**
to leave them out). Then **Share…** (pick Notes or Messages from the iPhone
share sheet), **Copy**, or **Download .txt**.

## Auto-send and fixed screen

* The page can't be zoomed (pinch or double-tap).
* **Auto-send:** in the ⋯ menu, "Send to watch automatically when I leave this
  page" (on by default). If you have unsent changes and switch away from the
  page, it sends them. Sending closes the settings page, so reopen it from the
  Pebble app next time. This is untested on iOS: if it doesn't fire, keep using
  the **Send to watch** button at the bottom.

## Known unknowns (please test)

1. **Settings page on Core's iOS app.** The settings flow uses the classic
   Pebble mechanism (open a URL, return data with `pebblejs://close#…`).
   I couldn't confirm that the Core app's iOS build supports it. If "Send to
   watch" does nothing, tell me what happens and I'll switch the sync method.
2. **Crossing out away from the phone.** The watch retries a few times, then
   gives a double buzz if the phone could not be reached. That item is gone
   from the watch but is not recorded on the phone, so it comes back the next
   time the phone sends its lists to the watch.
3. **Characters.** The built-in watch fonts cover Latin letters. Arabic and
   emoji will not display, and capitals only apply to A–Z.
4. **Fonts.** Watchapps cannot use arbitrary phone fonts; text size is
   selectable (small / medium / large) and text is always capitals. A different
   typeface would mean bundling a font file into the app at build time.
