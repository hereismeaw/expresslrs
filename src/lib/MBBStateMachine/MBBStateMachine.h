#pragma once

// ============================================================
// MBBStateMachine — make-before-break dual-path transition
//
// Primary invariant: the active (primary) RF path MUST continue
// carrying CRSF control packets throughout any transition until
// the new path has been confirmed and committed.
//
// All state transitions are written to AuditLog.
// No transition may leave both paths in an undefined state.
// ============================================================

#include <stdint.h>
#include <stdbool.h>
#include "RFPath.h"
#include "RFProfile.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// MBB top-level states
// ---------------------------------------------------------------------------
typedef enum mbb_state_e : uint8_t {
    MBB_STABLE_ACTIVE           = 0,
    MBB_PREPARE_SECONDARY       = 1,
    MBB_SECONDARY_SCAN          = 2,
    MBB_SECONDARY_CANDIDATE_LOCK = 3,
    MBB_DUAL_LOCK_CONFIRM       = 4,
    MBB_COMMIT_SWITCH           = 5,
    MBB_VERIFY_PRIMARY_RELEASE  = 6,
    MBB_ROLLBACK                = 7,
    MBB_FAILSAFE_HOLD           = 8,
    MBB_STATE_COUNT
} mbb_state_e;

// ---------------------------------------------------------------------------
// Rollback / failsafe reason codes (logged to AuditLog)
// ---------------------------------------------------------------------------
typedef enum mbb_reason_e : uint8_t {
    MBB_REASON_NONE              = 0,
    MBB_REASON_SCAN_TIMEOUT      = 1,
    MBB_REASON_NO_VALID_FREQ     = 2,
    MBB_REASON_THRESHOLD_FAIL    = 3,
    MBB_REASON_LOCK_TIMEOUT      = 4,
    MBB_REASON_PRIMARY_DEGRADED  = 5,
    MBB_REASON_RELEASE_TIMEOUT   = 6,
    MBB_REASON_PRIMARY_FAILED    = 7,
    MBB_REASON_COMMANDED_ABORT   = 8,
} mbb_reason_e;

// ---------------------------------------------------------------------------
// Context passed into every MBB tick
// ---------------------------------------------------------------------------
typedef struct {
    RFPath_t      *paths;            // [RF_PATH_COUNT] — both paths
    RFPairMode_t  *pair_mode;
    rfpath_id_e    active_path_id;   // which path is currently ACTIVE
    rfpath_id_e    candidate_path_id; // which path is being prepared
    rfzone_name_e  target_zone;      // zone the candidate should use
    mbb_state_e    state;
    mbb_reason_e   last_reason;
    uint32_t       state_entered_ms;
    uint32_t       confirm_start_ms; // when DUAL_LOCK_CONFIRM entered
    bool           transition_requested; // set by Lua/CLI
    bool           transition_aborted;
} MBBContext_t;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Initialize context.  Both paths should be set up before calling this.
void MBB_Init(MBBContext_t *ctx, RFPath_t *paths, RFPairMode_t *pair_mode);

// Call this every packet interval from the TX/RX main loop.
// Returns true if state changed this tick.
bool MBB_Tick(MBBContext_t *ctx);

// Request a controlled zone transition (e.g. from Lua).
// zone: the target zone for the secondary path.
// Returns false if a transition is already in progress or the profile rejects it.
bool MBB_RequestTransition(MBBContext_t *ctx, rfzone_name_e zone);

// Abort any in-progress transition and return to STABLE_ACTIVE or FAILSAFE_HOLD.
void MBB_AbortTransition(MBBContext_t *ctx, mbb_reason_e reason);

// Return a human-readable state name.
const char *MBB_StateName(mbb_state_e state);

// Return a human-readable reason name.
const char *MBB_ReasonName(mbb_reason_e reason);

// Convenience: is the system in a fully stable state?
static inline bool MBB_IsStable(const MBBContext_t *ctx)
{
    return ctx->state == MBB_STABLE_ACTIVE;
}

// Convenience: is the system in failsafe?
static inline bool MBB_IsFailsafe(const MBBContext_t *ctx)
{
    return ctx->state == MBB_FAILSAFE_HOLD;
}

#ifdef __cplusplus
}
#endif
