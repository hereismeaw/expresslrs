#include "MBBStateMachine.h"
#include "logging.h"

#if FEATURE_DUAL_LR1121_MAKE_BEFORE_BREAK

#include "AuditLog.h"
#include "RFProfile.h"
#include "FHSS.h"

// ---------------------------------------------------------------------------
// External active RF profile (singleton in firmware)
// ---------------------------------------------------------------------------
extern RFProfile_t g_activeRFProfile;

// ---------------------------------------------------------------------------
// State and reason name tables
// ---------------------------------------------------------------------------
static const char *const s_state_names[] = {
    "STABLE_ACTIVE",
    "PREPARE_SECONDARY",
    "SECONDARY_SCAN",
    "SECONDARY_CANDIDATE_LOCK",
    "DUAL_LOCK_CONFIRM",
    "COMMIT_SWITCH",
    "VERIFY_PRIMARY_RELEASE",
    "ROLLBACK",
    "FAILSAFE_HOLD",
};

static const char *const s_reason_names[] = {
    "NONE",
    "SCAN_TIMEOUT",
    "NO_VALID_FREQ",
    "THRESHOLD_FAIL",
    "LOCK_TIMEOUT",
    "PRIMARY_DEGRADED",
    "RELEASE_TIMEOUT",
    "PRIMARY_FAILED",
    "COMMANDED_ABORT",
};

const char *MBB_StateName(mbb_state_e state)
{
    if ((uint8_t)state < MBB_STATE_COUNT) return s_state_names[(uint8_t)state];
    return "UNKNOWN";
}

const char *MBB_ReasonName(mbb_reason_e reason)
{
    if ((uint8_t)reason < sizeof(s_reason_names)/sizeof(s_reason_names[0]))
        return s_reason_names[(uint8_t)reason];
    return "UNKNOWN";
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
static void _enter_state(MBBContext_t *ctx, mbb_state_e new_state, mbb_reason_e reason)
{
    mbb_state_e old_state = ctx->state;
    ctx->state            = new_state;
    ctx->last_reason      = reason;
    ctx->state_entered_ms = millis();

    DBGLN("MBB: %s → %s  [%s]  active=%c candidate=%c",
          MBB_StateName(old_state),
          MBB_StateName(new_state),
          MBB_ReasonName(reason),
          (ctx->active_path_id == RF_PATH_A) ? 'A' : 'B',
          (ctx->candidate_path_id == RF_PATH_A) ? 'A' : 'B');

    AuditLog_MBBTransition(old_state, new_state, reason,
                           ctx->active_path_id, ctx->candidate_path_id);
}

static uint32_t _elapsed_ms(MBBContext_t *ctx)
{
    return millis() - ctx->state_entered_ms;
}

static RFPath_t *_active(MBBContext_t *ctx)
{
    return &ctx->paths[ctx->active_path_id];
}

static RFPath_t *_candidate(MBBContext_t *ctx)
{
    return &ctx->paths[ctx->candidate_path_id];
}

// Pick a candidate frequency in the target zone that is not excluded.
// Returns 0 if none found.
static uint32_t _pick_candidate_freq(rfzone_name_e zone)
{
    const RFZone_t *z = RFProfile_GetZone(&g_activeRFProfile, zone);
    if (!z || z->freq_count == 0) return 0;

    // Simple strategy: pick the middle frequency.
    // A production implementation would use the FHSS sequence or LQ history.
    uint16_t idx = z->freq_count / 2;
    uint32_t freq = z->allowed_freq_mhz[idx];

    if (RFProfile_IsExcluded(freq)) return 0;
    return freq;
}

// ---------------------------------------------------------------------------
// State handlers — each returns true if a state change occurred
// ---------------------------------------------------------------------------

static bool _tick_stable_active(MBBContext_t *ctx)
{
    RFPath_t *active = _active(ctx);

    // Check for primary failure
    if (active->state == RFPATH_FAILED)
    {
        _enter_state(ctx, MBB_FAILSAFE_HOLD, MBB_REASON_PRIMARY_FAILED);
        AuditLog_FailsafeActivated(MBB_REASON_PRIMARY_FAILED);
        return true;
    }

    // Auto threshold trigger or explicit request
    bool threshold_crossed = RFPath_IsActive(active) &&
                             active->health.lq < ctx->pair_mode->lq_threshold;
    bool should_transition = ctx->transition_requested || threshold_crossed;

    if (should_transition)
    {
        ctx->transition_requested = false;
        // Candidate is the other path
        ctx->candidate_path_id = (ctx->active_path_id == RF_PATH_A) ? RF_PATH_B : RF_PATH_A;
        _enter_state(ctx, MBB_PREPARE_SECONDARY, MBB_REASON_NONE);
        return true;
    }
    return false;
}

static bool _tick_prepare_secondary(MBBContext_t *ctx)
{
    RFPath_t *cand = _candidate(ctx);

    // Put candidate into IDLE if it isn't already
    if (cand->state == RFPATH_DISABLED || cand->state == RFPATH_FAILED)
    {
        RFPath_SetState(cand, RFPATH_IDLE, "MBB prepare");
        return false;
    }

    if (cand->state == RFPATH_IDLE)
    {
        _enter_state(ctx, MBB_SECONDARY_SCAN, MBB_REASON_NONE);
        return true;
    }

    if (_elapsed_ms(ctx) > ctx->pair_mode->scan_timeout_ms)
    {
        _enter_state(ctx, MBB_ROLLBACK, MBB_REASON_SCAN_TIMEOUT);
        return true;
    }
    return false;
}

static bool _tick_secondary_scan(MBBContext_t *ctx)
{
    RFPath_t *cand = _candidate(ctx);
    RFPath_SetState(cand, RFPATH_SCANNING, "MBB scan");

    uint32_t freq = _pick_candidate_freq(ctx->target_zone);
    if (freq == 0)
    {
        _enter_state(ctx, MBB_ROLLBACK, MBB_REASON_NO_VALID_FREQ);
        return true;
    }

    cand->current_freq_mhz = freq;
    RFPath_SetState(cand, RFPATH_CANDIDATE_LOCK, "candidate freq set");
    _enter_state(ctx, MBB_SECONDARY_CANDIDATE_LOCK, MBB_REASON_NONE);
    return true;
}

static bool _tick_candidate_lock(MBBContext_t *ctx)
{
    RFPath_t *cand   = _candidate(ctx);
    RFPath_t *active = _active(ctx);
    const RFPairMode_t *pm = ctx->pair_mode;

    // Check if primary has also degraded — may need to fast-path commit
    if (active->state == RFPATH_FAILED)
    {
        // Emergency: commit immediately if candidate has any signal
        if (cand->health.lq > 0)
        {
            _enter_state(ctx, MBB_DUAL_LOCK_CONFIRM, MBB_REASON_PRIMARY_DEGRADED);
            ctx->confirm_start_ms = millis();
        }
        else
        {
            _enter_state(ctx, MBB_FAILSAFE_HOLD, MBB_REASON_PRIMARY_FAILED);
            AuditLog_FailsafeActivated(MBB_REASON_PRIMARY_FAILED);
        }
        return true;
    }

    // Normal: check candidate quality thresholds
    bool lq_ok   = cand->health.lq   >= pm->lq_threshold;
    bool rssi_ok = cand->health.rssi_dbm >= pm->rssi_threshold_dbm;

    if (lq_ok && rssi_ok)
    {
        _enter_state(ctx, MBB_DUAL_LOCK_CONFIRM, MBB_REASON_NONE);
        ctx->confirm_start_ms = millis();
        return true;
    }

    if (_elapsed_ms(ctx) > pm->lock_timeout_ms)
    {
        _enter_state(ctx, MBB_ROLLBACK, MBB_REASON_LOCK_TIMEOUT);
        return true;
    }
    return false;
}

static bool _tick_dual_lock_confirm(MBBContext_t *ctx)
{
    RFPath_t *cand   = _candidate(ctx);
    const RFPairMode_t *pm = ctx->pair_mode;

    uint32_t confirm_elapsed = millis() - ctx->confirm_start_ms;
    bool still_good = cand->health.lq >= pm->lq_threshold &&
                      cand->health.rssi_dbm >= pm->rssi_threshold_dbm;

    if (!still_good)
    {
        _enter_state(ctx, MBB_ROLLBACK, MBB_REASON_THRESHOLD_FAIL);
        return true;
    }

    cand->health.candidate_lock_ms = confirm_elapsed;

    if (confirm_elapsed >= pm->confirm_window_ms)
    {
        _enter_state(ctx, MBB_COMMIT_SWITCH, MBB_REASON_NONE);
        return true;
    }
    return false;
}

static bool _tick_commit_switch(MBBContext_t *ctx)
{
    // Atomically swap which path is active.
    // Primary path is kept in GUARD mode briefly during verification.
    RFPath_t *old_active = _active(ctx);
    RFPath_t *new_active = _candidate(ctx);

    RFPath_SetState(new_active, RFPATH_ACTIVE, "MBB commit");
    RFPath_SetState(old_active, RFPATH_GUARD,  "MBB released");

    rfpath_id_e old_id = ctx->active_path_id;
    ctx->active_path_id    = ctx->candidate_path_id;
    ctx->candidate_path_id = old_id;

    new_active->health.transition_success_count++;

    _enter_state(ctx, MBB_VERIFY_PRIMARY_RELEASE, MBB_REASON_NONE);
    return true;
}

static bool _tick_verify_primary_release(MBBContext_t *ctx)
{
    RFPath_t *released = _candidate(ctx); // old active is now candidate slot
    const uint32_t verify_timeout_ms = 500;

    if (_elapsed_ms(ctx) >= verify_timeout_ms)
    {
        // Timeout — something is wrong with the released path
        if (released->state != RFPATH_IDLE && released->state != RFPATH_GUARD)
        {
            DBGLN("MBB: released path still in %s after %u ms",
                  RFPath_StateName(released->state), verify_timeout_ms);
            // Not critical; the new active path is working.  Log and move on.
        }
        RFPath_SetState(released, RFPATH_IDLE, "MBB release verified");
        _enter_state(ctx, MBB_STABLE_ACTIVE, MBB_REASON_NONE);
        return true;
    }
    return false;
}

static bool _tick_rollback(MBBContext_t *ctx)
{
    RFPath_t *cand   = _candidate(ctx);
    RFPath_t *active = _active(ctx);

    RFPath_SetState(cand, RFPATH_IDLE, "MBB rollback");
    cand->health.transition_fail_count++;
    cand->health.rollback_reason = (uint8_t)ctx->last_reason;

    if (active->state == RFPATH_FAILED)
    {
        _enter_state(ctx, MBB_FAILSAFE_HOLD, MBB_REASON_PRIMARY_FAILED);
        AuditLog_FailsafeActivated(MBB_REASON_PRIMARY_FAILED);
    }
    else
    {
        _enter_state(ctx, MBB_STABLE_ACTIVE, MBB_REASON_NONE);
    }
    return true;
}

static bool _tick_failsafe_hold(MBBContext_t *ctx)
{
    // Check both paths — if either recovers, attempt re-lock
    RFPath_t *a = &ctx->paths[RF_PATH_A];
    RFPath_t *b = &ctx->paths[RF_PATH_B];

    const uint32_t rto_ms = 5000; // recovery time objective
    if (_elapsed_ms(ctx) >= rto_ms)
    {
        // After RTO, attempt recovery on whichever path has any signal
        if (a->health.lq > 0 && a->state != RFPATH_FAILED)
        {
            ctx->active_path_id    = RF_PATH_A;
            ctx->candidate_path_id = RF_PATH_B;
            RFPath_SetState(a, RFPATH_ACTIVE, "failsafe recovery");
            _enter_state(ctx, MBB_STABLE_ACTIVE, MBB_REASON_NONE);
            return true;
        }
        if (b->health.lq > 0 && b->state != RFPATH_FAILED)
        {
            ctx->active_path_id    = RF_PATH_B;
            ctx->candidate_path_id = RF_PATH_A;
            RFPath_SetState(b, RFPATH_ACTIVE, "failsafe recovery");
            _enter_state(ctx, MBB_STABLE_ACTIVE, MBB_REASON_NONE);
            return true;
        }
        // Both still failed — stay in FAILSAFE_HOLD; FC failsafe is already triggered
    }
    return false;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void MBB_Init(MBBContext_t *ctx, RFPath_t *paths, RFPairMode_t *pair_mode)
{
    ctx->paths             = paths;
    ctx->pair_mode         = pair_mode;
    ctx->active_path_id    = RF_PATH_A;
    ctx->candidate_path_id = RF_PATH_B;
    ctx->target_zone       = RF_ZONE_LOW;
    ctx->state             = MBB_STABLE_ACTIVE;
    ctx->last_reason       = MBB_REASON_NONE;
    ctx->state_entered_ms  = millis();
    ctx->confirm_start_ms  = 0;
    ctx->transition_requested = false;
    ctx->transition_aborted   = false;

    RFPath_SetState(&paths[RF_PATH_A], RFPATH_ACTIVE, "MBB init");
    RFPath_SetState(&paths[RF_PATH_B], RFPATH_IDLE,   "MBB init");
}

bool MBB_Tick(MBBContext_t *ctx)
{
    if (ctx->transition_aborted)
    {
        ctx->transition_aborted = false;
        MBB_AbortTransition(ctx, MBB_REASON_COMMANDED_ABORT);
        return true;
    }

    switch (ctx->state)
    {
        case MBB_STABLE_ACTIVE:            return _tick_stable_active(ctx);
        case MBB_PREPARE_SECONDARY:        return _tick_prepare_secondary(ctx);
        case MBB_SECONDARY_SCAN:           return _tick_secondary_scan(ctx);
        case MBB_SECONDARY_CANDIDATE_LOCK: return _tick_candidate_lock(ctx);
        case MBB_DUAL_LOCK_CONFIRM:        return _tick_dual_lock_confirm(ctx);
        case MBB_COMMIT_SWITCH:            return _tick_commit_switch(ctx);
        case MBB_VERIFY_PRIMARY_RELEASE:   return _tick_verify_primary_release(ctx);
        case MBB_ROLLBACK:                 return _tick_rollback(ctx);
        case MBB_FAILSAFE_HOLD:            return _tick_failsafe_hold(ctx);
        default:                           return false;
    }
}

bool MBB_RequestTransition(MBBContext_t *ctx, rfzone_name_e zone)
{
    if (ctx->state != MBB_STABLE_ACTIVE) return false;
    if (zone >= RF_ZONE_COUNT) return false;
    if (_pick_candidate_freq(zone) == 0) return false;

    ctx->target_zone            = zone;
    ctx->transition_requested   = true;
    return true;
}

void MBB_AbortTransition(MBBContext_t *ctx, mbb_reason_e reason)
{
    if (ctx->state == MBB_STABLE_ACTIVE || ctx->state == MBB_FAILSAFE_HOLD) return;
    _enter_state(ctx, MBB_ROLLBACK, reason);
}

#endif // FEATURE_DUAL_LR1121_MAKE_BEFORE_BREAK
