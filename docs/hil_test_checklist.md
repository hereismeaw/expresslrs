# ConnexInsights HIL (Hardware-in-the-Loop) Test Checklist

**Target hardware:** Nomad Dual-LR1121 TX + DBR4 Dual-LR1121 RX  
**Regulatory scope:** NBTC/Army/Air Force authorized sandbox, 151–959 MHz  
**Profile:** NBTC_SANDBOX_001 (hash 0xBAF9DCBE)

---

## 1. Pre-flight — Build and Flash

- [ ] Run `compile_profile.py` and verify output hash matches `build_flags.txt`
- [ ] Build `Nomad_Dual_LR1121_TX` and `DBR4_Dual_LR1121_RX` with `pio run`
- [ ] Flash both devices; confirm no boot errors on serial monitor
- [ ] Serial log shows `[CX] Profile loaded: NBTC_SANDBOX_001  hash=0xBAF9DCBE`
- [ ] Serial log shows `[CX] Init complete. Active=A Candidate=B  MBB=STABLE_ACTIVE`

---

## 2. Frequency Compliance

- [ ] Confirm LR1121 instance 0 (Path A) operating frequency is within LOW zone (151–450 MHz)
- [ ] Confirm LR1121 instance 1 (Path B) initial frequency is within MID or HIGH zone
- [ ] Probe with spectrum analyser: no emission in DTV band 494–694 MHz
- [ ] Probe: no emission in 868 MHz ISM window (864–872 MHz)
- [ ] Probe: no emission in 915 MHz ISM window (911–919 MHz)
- [ ] Probe: no emission above 959 MHz (no 2.4 GHz leak)

---

## 3. Baseline Link Quality (Path A active, Path B idle)

- [ ] TX and RX bind and reach `connected` state
- [ ] Uplink LQ ≥ 95% at 3 m bench range
- [ ] RSSI within expected range for power level and distance
- [ ] CRSF link-stats frame delivered to FC at 5 Hz
- [ ] ConnexInsights telemetry params visible in Lua menu (zone, MBB state, hash)
- [ ] Lua shows active path = A, candidate = none

---

## 4. AuditLog Integrity

- [ ] UART output contains `AUDIT:BOOT`, `AUDIT:PROFILE_LOADED`, `AUDIT:ZONE_SEL` entries
- [ ] Each AuditLog entry includes timestamp, event code, and profile hash
- [ ] No `AUDIT:PROFILE_REJECTED` in normal boot sequence
- [ ] Force invalid hash by reflashing with patched binary → confirm `[CX] FATAL` and `radioFailed` state

---

## 5. Health Update Feed (ConnexInsights_UpdateHealth)

- [ ] At steady-state, Path A health fields reflect live link (non-zero LQ, negative RSSI)
- [ ] Path B health is 0/0 while RFPATH_IDLE (not active)
- [ ] Induce RF degradation (attenuator): Path A LQ visible decreasing in telemetry
- [ ] Confirm telemetry byte [3] (LQ_A) and byte [4] (RSSI_A) track hardware link stats

---

## 6. Make-Before-Break Transition — Threshold Triggered

- [ ] Start with Path A (LOW zone) active at stable LQ
- [ ] Insert 20 dB attenuator on Path A antenna to force LQ below threshold (< 70%)
- [ ] Confirm MBB state machine progresses: `STABLE → PREPARE → SCAN → CANDIDATE_LOCK → DUAL_LOCK_CONFIRM → COMMIT_SWITCH → STABLE`
- [ ] During transition: Path A must not lose carrier for > 1 CRSF packet interval
- [ ] After commit: active path = B, zone = HIGH, old zone A goes to RFPATH_GUARD then RFPATH_IDLE
- [ ] Remove attenuator: system stays on Path B (no spontaneous rollback)
- [ ] AuditLog contains transition record with reason, zones, and timestamps

---

## 7. Make-Before-Break Transition — Lua Commanded

- [ ] From Lua menu, request transition to MID zone
- [ ] MBB state progresses through full sequence as in section 6
- [ ] Transition completes within 5 s of command
- [ ] AuditLog reason code = `TRANSITION_COMMANDED`

---

## 8. Rollback and Failsafe

- [ ] Start MBB transition; block Path B signal before `DUAL_LOCK_CONFIRM` completes
- [ ] Confirm rollback to `STABLE_ACTIVE` (original Path A) within `scan_timeout_ms`
- [ ] Lua shows MBB state = `STABLE_ACTIVE`, active path unchanged
- [ ] AuditLog contains `ROLLBACK` entry with reason = `MBB_REASON_SCAN_TIMEOUT`
- [ ] Block ALL RF: confirm `FAILSAFE_HOLD` state and FC reports failsafe on CRSF
- [ ] Restore RF: confirm recovery to `STABLE_ACTIVE` without manual reset

---

## 9. Multi-Transition Stress Test

- [ ] Perform 10 consecutive commanded transitions (LOW→HIGH→LOW...) at 30 s intervals
- [ ] Zero dropped CRSF packets during any transition window
- [ ] Zero `FAILSAFE_HOLD` events
- [ ] AuditLog transition count matches expected (10 entries)
- [ ] Path health counters (`transition_success_count`) increment correctly each cycle

---

## 10. Regulatory Seal

- [ ] Spectrum analyser session screenshot saved (no emissions outside 151–959 MHz, no DTV, no ISM overlap)
- [ ] `rf_profile_manifest.json` snapshot archived with test session
- [ ] AuditLog exported and attached to test report
- [ ] Legal profile ID and config hash recorded: `NBTC_SANDBOX_001` / `0xBAF9DCBE`
- [ ] Sign-off: Engineer _____________ Date _____________
