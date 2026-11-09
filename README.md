# Sykerö Track Work Time

Pebble watchapp companion to the [trackworktime](https://github.com/Sykero-Software/trackworktime)
Android app. Control your work-time tracking from the wrist: pick a task to clock
in (or switch tasks), stop tracking, and see the current state — all without
opening the phone.

Talks to `fi.sykero.trackworktime` via **PebbleKit Android 2**. The
[Sykerö TimeStyle](https://github.com/Sykero-Software/TimeStylePebble) watchface
shows the same tracking status as a strip on your watchface.

**[Install from the Pebble appstore →](https://apps.repebble.com/syker-track-work-time_2c694894e79e439cb8db3033)**

## What it does

- **Task list** — shows your tasks with per-task worked time and percentages
  (% of the task's budget and % of today's total). Select a task to start or
  switch tracking.
- **Status + Stop while tracking** — the top row collapses into a combined row
  showing the active task, total worked time, and the estimated end-of-workday
  time (auto-pause / flexi aware, pushed from the phone). Selecting it stops
  tracking.
- **Robust offline behaviour** — the menu always renders at least a status row
  (loading / error / no tasks), so the app never shows a blank screen when the
  phone or trackworktime isn't responding. Commands auto-retry a few times.
- **Clay config page** — one toggle, *Return to watchface after selecting a task
  or Stop* (default on), persisted on the watch.

## Development with JailBee

This project is developed extensively using [JailBee](https://jailbee.gisgro.io/)
for isolated development environments. With JailBee and its Linux/Incus
prerequisites installed, run from this repository on the **host**:

```sh
jailbee init
jailbee base build          # one-time toolchain image; rebuild after SDK changes
jailbee new dev
jailbee tmux dev
```

Development normally happens with Claude Code inside the container. Before
creating it, enable `agents.claude.enabled` and `agents.claude.autostart`
in your host's `~/.config/jailbee/global.yaml`; `jailbee tmux dev` then attaches to
the session running Claude. Ask Claude to make changes and run the project's
builds and tests.

The committed [configuration](.jailbee/config.yaml) and
[installer](.jailbee/install.d/45-pebble-sdk.sh) provide Node.js 24,
pebble-tool 5.1.2, Pebble SDK 4.44 and a headless emulator; no parent
repository is required. Building the image installs the SDK under the
[Pebble developer terms](https://developer.repebble.com/legal/sdk-license/index.html).
Keep personal agent/IDE settings and credentials in JailBee's host-local config.
Commit inside the container, then run `jailbee git pull dev` on the host;
publish from the host, not the container.

## Building & running

```sh
pebble build                          # build for all targetPlatforms
pebble install --emulator diorite     # install on an emulator (NOT basalt)
pebble install --cloudpebble app.pbw  # install to a paired phone via the cloud relay
```

The build re-injects `companionApp` into the bundled `.pbw` (`wscript` hook) and
patches `pebble-clay` for the flint/gabbro platforms (see `wscript` for the why).

## Project layout

```
src/c/main.c       watchapp UI (MenuLayer) + PebbleKit message handling
src/pkjs/          PebbleKit JS: Clay config (config.js) + glue (index.js)
package.json       UUID, platforms, companionApp, message keys
wscript            build hooks (companionApp re-inject, Clay platform patch)
```

App name: **Sykerö Track Work Time** · UUID `2B5F824D-533E-40EC-8B77-AE3E28B45B18`.

## Documentation

Full SDK docs, tutorials, and API reference: <https://developer.repebble.com>

## Support

Questions, feedback or bug reports: <pebble.trackworktime@sykero.fi>

Browse all Sykerö Software apps on the Pebble appstore:
<https://apps.repebble.com/apps/dev/syker-software_9f6c9c6e9ce88af6a0db953e>
