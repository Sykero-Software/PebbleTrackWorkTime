var Clay = require('pebble-clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

// Pause the watch's idle auto-exit while the phone config page is open, so the
// app (and PKJS with it) is not killed mid-config, closing the page. Extra
// listeners alongside Clay's own (Pebble supports multiple listeners per event).
Pebble.addEventListener('showConfiguration', function () {
  Pebble.sendAppMessage({ CFG_OPEN: 1 });
});
Pebble.addEventListener('webviewclosed', function () {
  Pebble.sendAppMessage({ CFG_OPEN: 0 });   // resume on close (also on cancel)
});
