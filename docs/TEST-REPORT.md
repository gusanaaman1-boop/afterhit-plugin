# AFTERHIT 1.0.0 — test and validation record

2026-09-26, macOS 26 on Apple M5, JUCE 9 (857aab9c), Release build.

| Check | Result | Raw output |
|---|---|---|
| DSP measurement suite (`AfterHitTests`) | **59 / 59 pass** | [test-dsp-output.txt](test-dsp-output.txt) |
| Host-contract suite (`AfterHitHostTests`) | **58 / 58 pass** | [test-host-output.txt](test-host-output.txt) |
| pluginval 1.0.4, strictness 10 — VST3 | **SUCCESS** | [pluginval-output.txt](pluginval-output.txt) |
| pluginval 1.0.4, strictness 10 — AU | **SUCCESS** | same |
| `auval -v aufx Afht Naam` | **AU VALIDATION SUCCEEDED** | [auval-output.txt](auval-output.txt) |
| Deployment target (x86_64 slice) | macOS 10.13 | `otool -l` |
| DSP + host suites under **ASan + UBSan** (Debug) | **59 / 59, 58 / 58, no reports** | `cmake -B build-san -DAH_SANITIZERS=ON` |
| **Windows** (GitHub Actions, VS 2022 / MSVC, x64) | **59 / 59 + 58 / 58 pass**; installer built, installed, verified on disk, uninstalled clean | [run 36300498741](https://github.com/gusanaaman1-boop/afterhit-plugin/actions/runs/36300498741) |

What the suites cover, mapped to spec section 7:

1. **Neutrality** — bit-exact pass-through at HIT 0 / SPACE 0 / OUTPUT 0,
   mono + stereo, 5 block sizes, 3 sample rates, extreme TAIL/GATE/AFTER,
   values over full scale; also through the processor with the Neutral preset
   and after automating HIT back to 0; bypass exact after its 10 ms crossfade.
2. **Post-hit timing** — AFTER 25 ms: wet −240 dBFS (exact zero) for 24 ms,
   first wet at 28 ms; AFTER 0/10/60/100 ms land where set; dry onset on the
   input sample; a one-sample click excites the reverb.
3. **TAIL/GATE independence** — GATE off: TAIL 0.5 → 2 s stretches the decay
   3.3×; TAIL 0.15 s decays 30 dB in 0.11 s; TAIL 2 s: GATE 10/40/70/100 %
   → 800/220/120/60 ms; GATE 70 %: TAIL 1 s vs 4 s close within 10 ms of
   each other; return gain never steps (max 0.004/sample); no HF burst at the close.
4. **Retrigger** — old tail ducked 43 dB under the second attack, smooth,
   new tail after AFTER; 32-hit roll at 1/16: +0.0 dB over one hit (defaults),
   +0.2 dB with TAIL 6 s; GATE off + TAIL 6 s: +4.3 dB (bounded).
5. **Detector** — silence, −20 dBFS noise and a 200 Hz tone (10 s each, up to
   sensitivity 100 %) give at most one onset; −45 dBFS hit ignored at 0 %,
   caught at 100 %; L-then-R hit 3 ms apart = one event; 6 kicks = 6; 1/32
   roll at 140 BPM = 16; same count at 44.1/48/96 kHz; output bit-identical
   for block sizes 1/13/4096.
6. **Host edges** — every control randomly automated per block at 44.1/48/96 kHz:
   finite and bounded; mono at block size 1; exact silence 10.9 s after a hit
   with TAIL 6 s and both tanks asleep; tempo 120 → 140 BPM glides 250 → 214 ms;
   host tempo reaches the engine, no tempo → 120 BPM flagged; state save/load of
   every parameter + drawer state; a re-sent program number after load does not
   overwrite the restored values; garbage state ignored; **zero allocations in
   processBlock** (5 block sizes, editor open, automation, preset changes);
   20 editor open/close cycles while processing.
7. **Level** — see DESIGN.md: no limiter; full HIT + SPACE on a −0.1 dBFS
   kick peaks at +11.3 dBFS and the clip indicator latches.

Editor: 640 × 226 closed, 640 × 302 with ADVANCED open; the four 60 px dials
keep identical bounds either way; default read-outs 20% / 28% / 1.0 s / 1/8;
edited preset shown with `*`.

## Cubase 15 smoke test (macOS)

Cubase 15.0.20 (Apple Silicon) scanned AFTERHIT.vst3 (confirmed in
`vst3plugins.xml`). The plug-in ran as insert 1 on an audio track
("02_hit-kick") at its normal size (640 × 226 + Cubase's 47 px header): all
four labels and values legible, no scrolling, the strip showing the real
captured kick hits, GATE reading 1/8 = 240 ms from the project's 125 BPM, the
OUT clip latch lit on a hot track. Screenshot taken by the agent during the
session; the ADVANCED open/close check inside Cubase was **not** completed
(the user was working in Cubase at the time, so the agent stopped).

## Listening renders

`renders/` (made with `AfterHitRender` from the samples in `Make Music/`):
kick / snare / flam / sidestick / Tech House loop at the defaults, plus
Tight Kick (kick), Short Snare (snare), Big Fill (flam), Perc Room and
Dub Hit (loop), and Neutral (snare, should null against the source).
The agent cannot listen; these are for the owner's ears. Objective checks on
them: onset counts match the material (the sidestick sample's second onset
is a real second strike at +44 ms); the snare's wet is silent for 25 ms.

## Windows — what the CI found

The first two Windows runs failed one check the Mac passed: after a long
tail the output never reached exact silence (on run 2 it sat at 0.1). Cause:
the fractional delay reads (pre-delay, modulated tank lines) could round the
index onto the buffer size — one past the end. The Mac read zeros there by
luck; Windows read garbage into the feedback loop. A local ASan build
reproduced it as a heap-buffer-overflow; fixed in commit "Fix out-of-bounds
read…", ASan-clean, Windows green on the next run. Recursive states are also
snapped to zero below −400 dB so x86 flush-to-zero cannot stall a one-pole.

## Not tested

- **Cubase on Windows**: no Windows machine here — CI proves build, tests and
  installer, not a DAW session.
- Offline bounce inside Cubase: covered by the engine's offline renderer and
  `getTailLengthSeconds` (TAIL × 1.2 + AFTER + 0.1 s), not by a Cubase export.
