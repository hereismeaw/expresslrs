/**
 * Unit tests for RFProfile, RFPath, and AuditLog
 * Run under the UNIT_TEST native target (no hardware needed).
 *
 *   cd ExpressLRS/src
 *   pio test -e native_cx -f test_rfprofile
 */

#include <unity.h>
#include <cstdint>
#include <cstring>

// Provide millis() stub for native
static uint32_t s_millis = 0;
uint32_t millis() { return s_millis; }
void delay(uint32_t ms) { s_millis += ms; }

// Stub DBGLN
#define DBGLN(fmt, ...) do {} while(0)

// Feature flags, RF constants, zone tables, and exclusion ranges are injected
// by [env:native_cx] build_flags and test/generated/rf_zone_table.h.

#include "RFProfile.h"
#include "RFPath.h"

// ─── RFProfile tests ──────────────────────────────────────────────────────

void test_is_excluded_dtv_range(void)
{
    TEST_ASSERT_TRUE(RFProfile_IsExcluded(498));
    TEST_ASSERT_TRUE(RFProfile_IsExcluded(550));
    TEST_ASSERT_TRUE(RFProfile_IsExcluded(690));
}

void test_is_excluded_868(void)
{
    TEST_ASSERT_TRUE(RFProfile_IsExcluded(868));
}

void test_is_excluded_915(void)
{
    TEST_ASSERT_TRUE(RFProfile_IsExcluded(915));
}

void test_is_not_excluded_low_zone_freq(void)
{
    TEST_ASSERT_FALSE(RFProfile_IsExcluded(200));
    TEST_ASSERT_FALSE(RFProfile_IsExcluded(300));
    TEST_ASSERT_FALSE(RFProfile_IsExcluded(400));
}

void test_is_not_excluded_high_zone_safe_freq(void)
{
    TEST_ASSERT_FALSE(RFProfile_IsExcluded(800));
    TEST_ASSERT_FALSE(RFProfile_IsExcluded(850));
    TEST_ASSERT_FALSE(RFProfile_IsExcluded(950));
}

void test_out_of_range_excluded(void)
{
    TEST_ASSERT_TRUE(RFProfile_IsExcluded(100));   // below min
    TEST_ASSERT_TRUE(RFProfile_IsExcluded(1000));  // above max
    TEST_ASSERT_TRUE(RFProfile_IsExcluded(2400));  // 2.4G
}

void test_profile_validate_zero_hash_fails(void)
{
    RFProfile_t bad = {};
    bad.config_hash = 0;
    char reason[64] = {};
    TEST_ASSERT_FALSE(RFProfile_Validate(&bad, reason));
    TEST_ASSERT_NOT_EQUAL(0, strlen(reason));
}

void test_profile_load_succeeds(void)
{
    RFProfile_t profile;
    TEST_ASSERT_TRUE(RFProfile_Load(&profile));
    TEST_ASSERT_EQUAL(RF_PROFILE_CONFIG_HASH, profile.config_hash);
}

void test_profile_get_zone_low(void)
{
    RFProfile_t profile;
    RFProfile_Load(&profile);
    const RFZone_t *z = RFProfile_GetZone(&profile, RF_ZONE_LOW);
    TEST_ASSERT_NOT_NULL(z);
    TEST_ASSERT_EQUAL(RF_ZONE_LOW, z->name);
    TEST_ASSERT_GREATER_THAN(0, z->freq_count);
}

void test_profile_get_zone_invalid(void)
{
    RFProfile_t profile;
    RFProfile_Load(&profile);
    TEST_ASSERT_NULL(RFProfile_GetZone(&profile, RF_ZONE_INVALID));
}

// ─── RFPath tests ─────────────────────────────────────────────────────────

void test_rfpath_init(void)
{
    RFPath_t paths[RF_PATH_COUNT];
    RFPath_Init(paths);
    TEST_ASSERT_EQUAL(RFPATH_DISABLED, paths[RF_PATH_A].state);
    TEST_ASSERT_EQUAL(RFPATH_DISABLED, paths[RF_PATH_B].state);
    TEST_ASSERT_EQUAL(RF_PATH_A, paths[RF_PATH_A].path_id);
    TEST_ASSERT_EQUAL(RF_PATH_B, paths[RF_PATH_B].path_id);
}

void test_rfpath_set_state(void)
{
    RFPath_t paths[RF_PATH_COUNT];
    RFPath_Init(paths);
    RFPath_SetState(&paths[RF_PATH_A], RFPATH_ACTIVE, "test");
    TEST_ASSERT_EQUAL(RFPATH_ACTIVE, paths[RF_PATH_A].state);
}

void test_rfpath_is_active(void)
{
    RFPath_t p = {};
    p.state = RFPATH_ACTIVE;
    TEST_ASSERT_TRUE(RFPath_IsActive(&p));
    p.state = RFPATH_IDLE;
    TEST_ASSERT_FALSE(RFPath_IsActive(&p));
}

void test_rfpath_update_health(void)
{
    RFPath_t p = {};
    RFPath_UpdateHealth(&p, 85, -75, 8, 2);
    TEST_ASSERT_EQUAL(85, p.health.lq);
    TEST_ASSERT_EQUAL(-75, p.health.rssi_dbm);
    TEST_ASSERT_EQUAL(8, p.health.snr_db);
    TEST_ASSERT_EQUAL(2, p.health.pkt_loss_pct);
}

void test_rfpath_state_name(void)
{
    TEST_ASSERT_EQUAL_STRING("ACTIVE", RFPath_StateName(RFPATH_ACTIVE));
    TEST_ASSERT_EQUAL_STRING("IDLE",   RFPath_StateName(RFPATH_IDLE));
    TEST_ASSERT_EQUAL_STRING("FAILED", RFPath_StateName(RFPATH_FAILED));
}

// ─── Entry point ──────────────────────────────────────────────────────────
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_is_excluded_dtv_range);
    RUN_TEST(test_is_excluded_868);
    RUN_TEST(test_is_excluded_915);
    RUN_TEST(test_is_not_excluded_low_zone_freq);
    RUN_TEST(test_is_not_excluded_high_zone_safe_freq);
    RUN_TEST(test_out_of_range_excluded);
    RUN_TEST(test_profile_validate_zero_hash_fails);
    RUN_TEST(test_profile_load_succeeds);
    RUN_TEST(test_profile_get_zone_low);
    RUN_TEST(test_profile_get_zone_invalid);
    RUN_TEST(test_rfpath_init);
    RUN_TEST(test_rfpath_set_state);
    RUN_TEST(test_rfpath_is_active);
    RUN_TEST(test_rfpath_update_health);
    RUN_TEST(test_rfpath_state_name);

    return UNITY_END();
}
