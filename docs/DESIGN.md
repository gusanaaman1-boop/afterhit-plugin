# AFTERHIT — design notes

## Audio path

**Before (a normal reverb on a drum):** the hit and the reverb begin together,
so the reverb's early energy smears the attack, and on a busy part the tails
pile into a wash.

**After (AFTERHIT):**

```
in ──┬──────────────────────── dry ── HIT (attack emphasis) ─────────────┐
     ├─ linked detector ── onset ─┬─ send window (40 ms hold + 25 ms rel)  │
     │                            ├─ return gate: duck → wait AFTER → open │
     │                            │                → hold → GATE close     │
     │                            └─ choke old tank / route to fresh tank  │
     └─ 4 ms look-back ─ 60 Hz HP ─ × send window ─ pre-delay (AFTER−4 ms) │
          ─ 4 all-pass diffuser ─ early reflections + 2 × 16-line FDN      │
          ─ TONE low-pass ─ WIDTH (M/S) ─ × SPACE × return gate ──────── (+) ─ OUTPUT ─ bypass xfade ─ out
```

1. **Detector.** Stereo-linked power (L²+R²)/2. A fast envelope (0.3 ms attack,
   8 ms release) must rise over a slow one (10 ms / 12 ms) by 11 → 4.5 dB
   (SENSITIVITY 0 → 100 %) and cross an absolute floor of −34 → −66 dBFS.
   Hysteresis (must fall under half the rise before re-arming) and an adaptive
   refractory (≥ 15 ms, ends once the hit falls 6 dB from its peak, ≤ 50 ms).
2. **HIT.** A fast peak follower and a slow follower *of it*. Their ratio is
   above 1 only during an attack; that ratio (saturating at 2×) × HIT × 6 dB
   is the gain, smoothed 1 ms. At HIT 0 the dry path has no arithmetic at all.
3. **Send.** A 4 ms look-back tap feeds the reverb, so the part of the hit
   before the detector fired is captured — a single-sample click still
   excites a tail. The send only opens per event, so continuous material
   between hits does not wash the room.
4. **AFTER.** Implemented as pre-delay on the *send*, not a delay on the
   reverb output, so moving AFTER never bends a ringing tail's pitch. The
   return gate stays closed for AFTER after each onset, which also ducks any
   older tail under the new attack.
5. **Reverb.** Input diffuser, six early-reflection taps per side, and two
   16-line FDNs (Hadamard feedback, 19–58 ms lines, two lines slowly
   modulated). TAIL sets the low-band T60 exactly; TONE sets how much faster
   the band above 3 kHz decays (0.2–0.9 × T60) plus a wet low-pass
   (3–18 kHz). With GATE on, a new hit moves the send to the other tank and
   chokes the old tail's tank to T60 0.12 s — smoothly, never cleared. Tanks
   whose output falls below −140 dBFS go to sleep (exact zero, no CPU).
6. **GATE.** After the wet opens it holds, then closes with a critically
   damped two-pole fall (τ = fade/5, fade = 35 % of the window, 12–90 ms).
   GATE 0 = OFF: the room follows TAIL naturally.

## Control mappings

| Control | Law |
|---|---|
| HIT | linear: 0–100 % → 0–6 dB of attack emphasis |
| SPACE | wet gain = 6 × s² (s = 0..1). 10 % is 40 dB under 100 %. Default 28 %: a real snare's tail peaks ~7–8 dB under its hit, a short clap ~19 dB under |
| TAIL | 0.15–6 s, skewed so the knob centre is 0.95 s (log-like). Wet level partly compensated: × T60^−¼ |
| GATE | ≤ 0.5 % OFF; 0.5–40 %: 3000 → 250 ms (log); 40–100 %: 250 → 80 ms (log) — the *reference* window at 120 BPM. SYNC off plays the reference in ms. SYNC on snaps reference/500 ms to the nearest of 1 bar, 1/2, 1/4., 1/4, 1/8., 1/8, 1/16., 1/16, 1/32 (log distance) and plays it at host tempo. The snap depends only on the knob, so a stationary knob never changes division with tempo or transport. Default 40 % = 1/8 |
| AFTER | 0–100 ms. Below 4 ms the wet starts at ~4–7 ms (the look-back) |
| WIDTH | M/S on the wet only, 0–150 % |
| OUTPUT | −12…+6 dB after the dry/wet sum. No limiter anywhere |

## Measurements (M5 Mac, 2026-09-26)

- **Latency:** 0 samples reported, 0 measured (dry onset lands on the input sample).
- **CPU:** one stereo instance, 48 kHz, 512-sample blocks, drum loop:
  0.3–0.4 % of one core (~280× real time). Silence costs less (tanks sleep).
- **Neutral** (HIT 0, SPACE 0, OUTPUT 0): output == input bit for bit, mono and
  stereo, block sizes 1–4096, 44.1/48/96 kHz, any TAIL/GATE/AFTER.
- **Real snare** (`81_Snare_11_SP.wav`, HIT 0): wet at −138 dBFS (24-bit floor)
  for the first 25 ms, −50 dBFS at 25 ms, then blooms.
- **Loud input:** a −0.1 dBFS kick + clap at HIT 100 / SPACE 100 peaks at about
  +11.3 dBFS (a full-level room on a long kick). At the default settings a −1.5 dBFS kick sample peaks at +0.2 dBFS
  (HIT 20 % ≈ +1.2 dB on the attack). This is honest gain — nothing is limited;
  the OUT meter latches its clip indicator. Trim OUTPUT on hot material.

## Behaviour notes and limitations

- Transport play/stop does not touch the reverb; only a new sample rate
  (prepareToPlay) clears it. A seek does not reset the tail.
- The timing strip draws the **measured** capture of the last detected hit
  (dry peak, wet output) on a warped time axis, plus a faint model of the
  current settings so a knob move shows at once. In silence it simply holds.
- SYNC has no host tempo → 120 BPM, shown as "120 BPM (no host)" in the drawer.
- Rolls faster than ~1/32 at 140 BPM with long-bodied claps may merge hits at
  low SENSITIVITY — raise SENSITIVITY for dense rolls.
- GATE OFF with a 6 s TAIL on a fast roll builds up naturally (+4 dB measured
  over one hit) — that is the physics of a long room, not a runaway.
- Not code-signed / notarised (paid certificates are out of scope); macOS may
  ask for right-click → Open for the standalone. DAW-loaded plug-ins load.
- No AAX. No Windows binary has been built yet (see TEST-REPORT).
