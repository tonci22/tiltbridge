# TiltBridge documentation

Start here.

| Document | What it is for |
|---|---|
| [DEVELOPMENT.md](DEVELOPMENT.md) | Build, flash, serial capture, HTTP endpoints, acceptance tests. **Read §5.1 before ever running `uploadfs`** — it destroys the device configuration. |
| [KNOWN_ISSUES.md](KNOWN_ISSUES.md) | Open bugs with file:line, one unexplained behaviour, and a **"do not re-investigate"** list of theories already disproven. **Read before diagnosing anything.** |
| [APPS_SCRIPT.md](APPS_SCRIPT.md) | The Google Sheets side: deploying `post_tilt.gs`, the wine sheet layout, the constant that must track the device's push interval, and how to test it without deploying. |
| [phase1/](phase1/) | Design specs and the implementation log for the reliability/offline-queue work. `STATUS.md` there is a point-in-time record and has since drifted — trust the code and KNOWN_ISSUES.md over it. |

## The short version

```bash
PIO=~/.platformio/penv/bin/pio               # not on PATH

$PIO run -e esp32_headless                   # compile (fastest check)
$PIO run -e esp32_headless --target upload   # firmware only - preserves config
node GoogleSheets/test/run_tests.js          # Apps Script checks

curl -s http://<device>/api/version/         # confirm what is actually running
curl -s http://<device>/api/queue/           # queuedReadings must be 0 before uploadfs
```

Never combine `--target upload` with `--target buildfs --target uploadfs`; PlatformIO
reorders and dedupes them and the firmware write silently disappears.

## Two habits worth keeping

The [verification discipline](KNOWN_ISSUES.md#verification-discipline) section exists because
every bug *introduced* during recent work had passed a test first. The two that cost the most:

- **A single sample can pass by coincidence.** Anything involving a clock must deliberately
  cross the boundary the code could be wrong about.
- **A green indicator is only evidence if you have seen it go red.** Two "healthy" counters
  were reported for 25 hours while the mechanism that set them could never fire.
