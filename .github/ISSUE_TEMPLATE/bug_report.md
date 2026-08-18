---
name: Bug report
about: Something renders wrong, crashes, or performs badly
labels: bug
---

## What happens

<!-- What you see, and what you expected instead. A screenshot or a short
     video is worth far more than a description for rendering bugs. -->

## Setup

- OS + version:
- GPU + driver version:
- Linux `.so` or Windows `version.dll`:
- Build: development (`make`) or shipping (`make deploy`)?
- **Antialiasing**: on/off, and which mode
- Other graphics settings you changed from default:

<!-- GPU/driver/AA are genuinely load-bearing here: some issues reproduce only
     on particular drivers, and AA changes which code paths run. -->

## Panel state

<!-- Open the panel (Ctrl+Shift+F11 toggles it) and note anything not at its
     default, especially under Local light and Performance. "Restore defaults"
     is a quick way to check whether a setting is involved. -->

## Does it happen with the injector removed?

<!-- Remove the .so / .dll and confirm the behaviour is gone. This separates
     injector bugs from the base game's own. -->

## Log

<!-- Run from a terminal and paste the [shadowmap] lines around the problem. -->
