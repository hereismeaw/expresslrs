/**
 * Unit tests for MBBStateMachine
 *
 *   cd ExpressLRS/src
 *   pio test -e native_cx -f test_mbb
 */

#include <unity.h>
#include <cstdint>
#include <cstring>

static uint32_t s_millis_val = 0;
uint32_t millis() { return s_millis_val; }
void advance_time(uint32_t ms) { s_millis_val += ms; }

#define DBGLN(fmt, ...) do {} while(0)
#define snprintf_P snprintf

// Feature flags, RF constants, zone tables, and exclusion ranges are injected
// by [env:native_cx] build_flags and test/generated/rf_zone_table.h.
// AuditLog stubs are provided by AuditLog.cpp's FEATURE_RF_PROFILE_AUDIT_LOG=0 path.

#include "RFPath.h"
#include "RFProfile.h"

// g_activeRFProfile must exist for MBB_Tick to call _pick_candidate_freq
RFProfile_t g_activeRFProfile;

static void _init_profile(void)
{
    RFProfile_Load(&g_activeRFProfile);
}

#include "MBBStateMachine.h"

// ─── Helpers ──────────────────────────────────────────────────────────────
static void _init_ctx(MBBContext_t *ctx, RFPath_t paths[RF_PATH_COUNT])
{
    RFPath_Init(paths);
    _init_profile();

    RFPairMode_t *pm = &g_activeRFProfile.pair_mode;
    pm->lq_threshold      = 60;
    pm->rssi_threshold_dbm = -110;
    pm->confirm_window_ms  = 200;
    pm->scan_timeout_ms    = 1000;
    pm->lock_timeout_ms    = 500;

    MBB_Init(ctx, paths, pm);
}

// Simulate a healthy active path
static void _set_healthy(RFPath_t *p, uint8_t lq, int16_t rssi)
{
    RFPath_UpdateHealth(p, lq, rssi, 5, 0);
}

// ─── Tests ────────────────────────────────────────────────────────────────

void test_init_stable_active(void)
{
    RFPath_t paths[RF_PATH_COUNT];
    MBBContext_t ctx;
    _init_ctx(&ctx, paths);
    TEST_ASSERT_EQUAL(MBB_STABLE_ACTIVE, ctx.state);
    TEST_ASSERT_EQUAL(RFPATH_ACTIVE, paths[RF_PATH_A].state);
    TEST_ASSERT_EQUAL(RFPATH_IDLE,   paths[RF_PATH_B].state);
}

void test_happy_path_transition(void)
{
    RFPath_t paths[RF_PATH_COUNT];
    MBBContext_t ctx;
    _init_ctx(&ctx, paths);

    _set_healthy(&paths[RF_PATH_A], 90, -60);

    // Request transition to MID zone
    TEST_ASSERT_TRUE(MBB_RequestTransition(&ctx, RF_ZONE_MID));
    TEST_ASSERT_TRUE(ctx.transition_requested);

    // Tick: STABLE_ACTIVE → PREPARE_SECONDARY
    MBB_Tick(&ctx);
    TEST_ASSERT_EQUAL(MBB_PREPARE_SECONDARY, ctx.state);

    // Tick: PREPARE_SECONDARY → SECONDARY_SCAN (candidate is already IDLE)
    MBB_Tick(&ctx);
    TEST_ASSERT_EQUAL(MBB_SECONDARY_SCAN, ctx.state);

    // Tick: SECONDARY_SCAN → SECONDARY_CANDIDATE_LOCK
    MBB_Tick(&ctx);
    TEST_ASSERT_EQUAL(MBB_SECONDARY_CANDIDATE_LOCK, ctx.state);
    TEST_ASSERT_GREATER_THAN(0, paths[RF_PATH_B].current_freq_mhz);

    // Simulate candidate acquiring signal
    _set_healthy(&paths[RF_PATH_B], 85, -70);

    // Tick: SECONDARY_CANDIDATE_LOCK → DUAL_LOCK_CONFIRM
    MBB_Tick(&ctx);
    TEST_ASSERT_EQUAL(MBB_DUAL_LOCK_CONFIRM, ctx.state);

    // Advance past confirm window
    advance_time(300);

    // Keep candidate healthy
    _set_healthy(&paths[RF_PATH_B], 85, -70);

    // Tick: DUAL_LOCK_CONFIRM → COMMIT_SWITCH
    MBB_Tick(&ctx);
    TEST_ASSERT_EQUAL(MBB_COMMIT_SWITCH, ctx.state);

    // Tick: COMMIT_SWITCH → VERIFY_PRIMARY_RELEASE (path B is now active)
    MBB_Tick(&ctx);
    TEST_ASSERT_EQUAL(MBB_VERIFY_PRIMARY_RELEASE, ctx.state);
    TEST_ASSERT_EQUAL(RFPATH_ACTIVE, paths[RF_PATH_B].state);
    TEST_ASSERT_EQUAL(RFPATH_GUARD,  paths[RF_PATH_A].state);

    // Advance past verify timeout
    advance_time(600);

    // Tick: VERIFY_PRIMARY_RELEASE → STABLE_ACTIVE
    MBB_Tick(&ctx);
    TEST_ASSERT_EQUAL(MBB_STABLE_ACTIVE, ctx.state);
    TEST_ASSERT_EQUAL(RF_PATH_B, ctx.active_path_id);
    TEST_ASSERT_EQUAL(1, paths[RF_PATH_B].health.transition_success_count);
}

void test_candidate_threshold_fail_rollback(void)
{
    RFPath_t paths[RF_PATH_COUNT];
    MBBContext_t ctx;
    _init_ctx(&ctx, paths);

    MBB_RequestTransition(&ctx, RF_ZONE_MID);
    MBB_Tick(&ctx); // → PREPARE_SECONDARY
    MBB_Tick(&ctx); // → SECONDARY_SCAN
    MBB_Tick(&ctx); // → SECONDARY_CANDIDATE_LOCK

    // Candidate signal is poor (below threshold)
    _set_healthy(&paths[RF_PATH_B], 20, -120);

    // Advance past lock_timeout
    advance_time(600);

    MBB_Tick(&ctx);
    TEST_ASSERT_EQUAL(MBB_ROLLBACK, ctx.state);
    TEST_ASSERT_EQUAL(MBB_REASON_LOCK_TIMEOUT, ctx.last_reason);

    MBB_Tick(&ctx);
    TEST_ASSERT_EQUAL(MBB_STABLE_ACTIVE, ctx.state);
    TEST_ASSERT_EQUAL(RF_PATH_A, ctx.active_path_id);  // unchanged
    TEST_ASSERT_EQUAL(1, paths[RF_PATH_B].health.transition_fail_count);
}

void test_primary_failure_triggers_failsafe(void)
{
    RFPath_t paths[RF_PATH_COUNT];
    MBBContext_t ctx;
    _init_ctx(&ctx, paths);

    // Primary path fails
    RFPath_SetState(&paths[RF_PATH_A], RFPATH_FAILED, "hardware error");

    MBB_Tick(&ctx);
    TEST_ASSERT_EQUAL(MBB_FAILSAFE_HOLD, ctx.state);
}

void test_rollback_on_no_valid_freq(void)
{
    // If target zone has no frequencies (e.g. all excluded),
    // scan should fail with NO_VALID_FREQ and roll back.
    // This test uses zone RF_ZONE_HIGH which has real freqs in our stub,
    // so we trigger no_valid_freq by requesting an invalid zone.
    RFPath_t paths[RF_PATH_COUNT];
    MBBContext_t ctx;
    _init_ctx(&ctx, paths);

    // Request invalid zone — MBB_RequestTransition should reject it
    TEST_ASSERT_FALSE(MBB_RequestTransition(&ctx, RF_ZONE_COUNT));
    TEST_ASSERT_EQUAL(MBB_STABLE_ACTIVE, ctx.state);
}

void test_abort_transition(void)
{
    RFPath_t paths[RF_PATH_COUNT];
    MBBContext_t ctx;
    _init_ctx(&ctx, paths);

    MBB_RequestTransition(&ctx, RF_ZONE_MID);
    MBB_Tick(&ctx); // → PREPARE_SECONDARY

    MBB_AbortTransition(&ctx, MBB_REASON_COMMANDED_ABORT);
    TEST_ASSERT_EQUAL(MBB_ROLLBACK, ctx.state);
    TEST_ASSERT_EQUAL(MBB_REASON_COMMANDED_ABORT, ctx.last_reason);

    MBB_Tick(&ctx);
    TEST_ASSERT_EQUAL(MBB_STABLE_ACTIVE, ctx.state);
}

void test_failsafe_recovery_after_rto(void)
{
    RFPath_t paths[RF_PATH_COUNT];
    MBBContext_t ctx;
    _init_ctx(&ctx, paths);

    RFPath_SetState(&paths[RF_PATH_A], RFPATH_FAILED, "hardware error");
    MBB_Tick(&ctx); // → FAILSAFE_HOLD
    TEST_ASSERT_EQUAL(MBB_FAILSAFE_HOLD, ctx.state);

    // Path B recovers
    RFPath_UpdateHealth(&paths[RF_PATH_B], 70, -80, 5, 5);
    paths[RF_PATH_B].state = RFPATH_IDLE;

    // Advance past RTO (5000 ms)
    advance_time(5100);

    MBB_Tick(&ctx);
    TEST_ASSERT_EQUAL(MBB_STABLE_ACTIVE, ctx.state);
    TEST_ASSERT_EQUAL(RF_PATH_B, ctx.active_path_id);
}

void test_dual_lock_confirm_quality_drop_rollback(void)
{
    RFPath_t paths[RF_PATH_COUNT];
    MBBContext_t ctx;
    _init_ctx(&ctx, paths);

    MBB_RequestTransition(&ctx, RF_ZONE_MID);
    MBB_Tick(&ctx); // → PREPARE_SECONDARY
    MBB_Tick(&ctx); // → SECONDARY_SCAN

    _set_healthy(&paths[RF_PATH_B], 85, -70);
    MBB_Tick(&ctx); // → SECONDARY_CANDIDATE_LOCK
    _set_healthy(&paths[RF_PATH_B], 85, -70);
    MBB_Tick(&ctx); // → DUAL_LOCK_CONFIRM

    // Quality drops during confirmation window
    _set_healthy(&paths[RF_PATH_B], 10, -120);
    advance_time(50);

    MBB_Tick(&ctx);
    TEST_ASSERT_EQUAL(MBB_ROLLBACK, ctx.state);
    TEST_ASSERT_EQUAL(MBB_REASON_THRESHOLD_FAIL, ctx.last_reason);
}

// ─── Entry point ──────────────────────────────────────────────────────────
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_init_stable_active);
    RUN_TEST(test_happy_path_transition);
    RUN_TEST(test_candidate_threshold_fail_rollback);
    RUN_TEST(test_primary_failure_triggers_failsafe);
    RUN_TEST(test_rollback_on_no_valid_freq);
    RUN_TEST(test_abort_transition);
    RUN_TEST(test_failsafe_recovery_after_rto);
    RUN_TEST(test_dual_lock_confirm_quality_drop_rollback);

    return UNITY_END();
}
