# Moonlight NL

This is a personal macOS fork of **Moonlight**, the open source GameStream/Sunshine client, built for streaming from a MacBook to a Windows PC running [Vibepollo](https://github.com/LizardByte/Sunshine) (a Sunshine/Apollo fork).

The original project, build instructions, and everything else about upstream Moonlight live at **[moonlight-stream/moonlight-qt](https://github.com/moonlight-stream/moonlight-qt)**. Nothing below is upstream functionality — it's everything added on top for this one machine's setup. See that repo for the base feature set, supported platforms, and build requirements.

## What's different here

### Branding
- Renamed to "Moonlight NL" with its own bundle ID, so it can be installed and run side by side with a stock Moonlight install.
- Darker header/toolbar color than the background, home screen title reads "Moonlight NL".

### Mac-specific input
- **Cmd/Ctrl swap**: sends Control when you press Command and the Windows key when you press Control, matching the physical key position between Mac and Windows keyboards. Toggleable live with `Ctrl+Alt+Shift+W`.
- **Trackpad scroll speed**: a configurable scroll-speed multiplier (Settings), plus an opt-in **tick-based scrolling** mode for apps (like Rhino3D) that count scroll *events* rather than scroll *magnitude* — toggle with `Ctrl+Alt+Shift+R`. Amount-based scaling is the default; most apps (and Rhino's own `zoom.scale` setting) handle magnitude fine on their own.

### Wi-Fi stability
- Optional **AWDL disable during streaming** (Settings → Advanced): macOS periodically scans for AirDrop/AirPlay/Handoff peers over AWDL, which can cause brief Wi-Fi stutter. When enabled, AWDL is turned off right as a stream starts — before any stream traffic begins, not after the window is already open — and restored when the stream ends. Requires a one-time passwordless `sudo` rule for `ifconfig awdl0 down`/`up` (e.g. in `/etc/sudoers.d/`); otherwise this silently has no effect.

### On-screen hotkey feedback
Every hotkey toggle now shows visible feedback instead of acting silently:
- A brief **toast** in the bottom-right corner naming what just changed and its new ON/OFF state.
- A **persistent list** of every hotkey and its current status, toggled with `Ctrl+Alt+Shift+H`, top-right corner.
- A **battery indicator** (see below), top-right, flush to the corner above the hotkey list.
- A one-time **reminder** of how to release the mouse, shown automatically whenever running in windowed mode (see below), bottom-right.

Current hotkeys (all `Ctrl+Alt+Shift+<key>`):

| Key | Action |
| --- | --- |
| H | Toggle this hotkey list |
| Q | Quit |
| D | Minimize |
| E | Quit and exit host app |
| X | Toggle full screen |
| V | Paste clipboard text |
| S | Toggle performance stats overlay |
| K | Toggle shortcut capture (system keyboard shortcuts go to host vs. stay local) |
| W | Toggle Command/Control key swap |
| Z | Toggle mouse capture |
| M | Toggle absolute mouse mode |
| C | Toggle cursor hiding (only works in absolute mouse mode) |
| L | Toggle pointer region lock |
| R | Toggle tick-based scrolling |
| B | Toggle the battery overlay |

### Run a host command without streaming
Any app configured on the host (Sunshine/Vibepollo) can be launched **without ever opening a stream window**, by piggybacking on the existing per-app launch mechanism: the client sends the same launch request a normal stream would, which is enough for the host to run that app's command, but a `Session`/SDL window is never created.

- **"Run Command (No Stream)"** — right-click any app in the app grid. Useful for any host app set up to run a script rather than an actual game.
- **"Sleep PC"** — right-click a PC in the main grid. Looks for a host app literally named `Sleep` and runs it the same way. Closes any running stream first, so nothing gets cut off mid-session, and sends a best-effort cleanup afterward so the host doesn't keep showing the app as "running" if the command didn't actually succeed.

### Open a session in a window
**"Open in Window"** (or **"Resume in Window"** for an already-running session) — right-click any app. Launches that one session windowed, without touching your persisted full screen/windowed Settings preference; the next normal launch goes back to whatever Settings has configured. Useful for keeping an eye on something without taking over the whole screen.

Since clicking into a windowed stream captures your mouse like any other input method, a small reminder ("`Ctrl+Alt+Shift+Z` releases your mouse from this window") shows automatically for the whole session whenever running windowed.

### Battery overlay
Shows this Mac's battery status while streaming: a small battery icon (outline + a nub, filled to one of four discrete levels — full/half/low/empty — based on the current percentage, not a smooth gauge) plus the percentage itself, top-right corner. A lightning bolt appears inside the icon while charging. On by default (Settings checkbox to turn it off); toggle for just the current session with `Ctrl+Alt+Shift+B`. Refreshes every 15 seconds.

## Building
Same build process as upstream — see [moonlight-stream/moonlight-qt](https://github.com/moonlight-stream/moonlight-qt#building) for Qt/Xcode requirements and build steps. No changes to the build system here.
