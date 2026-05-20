#pragma once

// ============================================================
// RFPath — per-radio-instance state and link-health tracking
// Compiled only when FEATURE_DUAL_LR1121_MAKE_BEFORE_BREAK is active.
// ============================================================

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Path identifier
// ---------------------------------------------------------------------------
typedef enum rfpath_id_e : uint8_t {
    RF_PATH_A = 0,  // LR1121 instance 0 — primary at boot
    RF_PATH_B = 1,  // LR1121 instance 1 — secondary at boot
    RF_PATH_COUNT
} rfpath_id_e;

// ---------------------------------------------------------------------------
// Per-path state machine states
// ---------------------------------------------------------------------------
typedef enum rfpath_state_e : uint8_t {
    RFPATH_DISABLED         = 0,
    RFPATH_IDLE             = 1,  // powered but not transmitting
    RFPATH_SCANNING         = 2,  // scanning candidate frequencies
    RFPATH_CANDIDATE_LOCK   = 3,  // found candidate; confirming stability
    RFPATH_ACTIVE           = 4,  // carrying control packets
    RFPATH_GUARD            = 5,  // post-transition; monitoring old freq
    RFPATH_FAILED           = 6,  // hardware or link failure
} rfpath_state_e;

// ---------------------------------------------------------------------------
// Link-health snapshot (filled every telemetry interval)
// ---------------------------------------------------------------------------
typedef struct {
    uint8_t  lq;               // link quality 0–100 %
    int16_t  rssi_dbm;         // signed dBm (e.g. -80)
    int8_t   snr_db;           // SNR in dB
    uint8_t  pkt_loss_pct;     // 0–100 %
    uint32_t candidate_lock_ms; // how long path has been in CANDIDATE_LOCK
    uint32_t transition_success_count;
    uint32_t transition_fail_count;
    uint8_t  rollback_reason;  // last rollback cause code
} RFPathHealth_t;

// ---------------------------------------------------------------------------
// PA profile reference (index into pa_calibration table)
// ---------------------------------------------------------------------------
typedef struct {
    uint8_t profile_id;
    int8_t  max_power_dbm;
    uint8_t pa_gain_code; // radio-specific PA register value
} PaProfile_t;

// ---------------------------------------------------------------------------
// Per-path descriptor
// ---------------------------------------------------------------------------
typedef struct {
    rfpath_id_e    path_id;
    uint8_t        lr1121_instance; // 0 or 1
    uint8_t        antenna_id;
    uint8_t        pa_profile_id;
    uint32_t       current_freq_mhz;
    rfpath_state_e state;
    RFPathHealth_t health;
    uint32_t       state_entered_ms;  // millis() when state was last entered
} RFPath_t;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Initialize both paths to DISABLED state.
void RFPath_Init(RFPath_t paths[RF_PATH_COUNT]);

// Transition a path to a new state.  Logs every transition via AuditLog.
void RFPath_SetState(RFPath_t *path, rfpath_state_e new_state, const char *reason);

// Update link-health fields from the latest radio report.
void RFPath_UpdateHealth(RFPath_t *path, uint8_t lq, int16_t rssi, int8_t snr, uint8_t pkt_loss);

// Return a human-readable state name (for logging/telemetry).
const char *RFPath_StateName(rfpath_state_e state);

// Return true if the path is in a state that can carry control packets.
static inline bool RFPath_IsActive(const RFPath_t *p) { return p->state == RFPATH_ACTIVE; }

// Return true if the path is healthy enough to consider for transition.
static inline bool RFPath_IsHealthy(const RFPath_t *p, uint8_t lq_min)
{
    return p->health.lq >= lq_min && p->state != RFPATH_FAILED && p->state != RFPATH_DISABLED;
}

#ifdef __cplusplus
}
#endif
