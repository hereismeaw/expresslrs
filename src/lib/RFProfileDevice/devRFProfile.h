#pragma once

// ============================================================
// devRFProfile — CRSF device for RF zone / transition control
//
// Only compiled when FEATURE_CUSTOM_150_960_PROFILE is active.
//
// Design:
//   - Writable configuration (zone pair, policy, guard band) is
//     exposed as standard CRSF selection/number parameters.
//   - Read-only status (MBB state, active path, LQ, hash) is
//     pushed as a periodic custom telemetry frame (0x29 ELRS info
//     extended payload, tag 0xCX).
//   - The Lua companion script reads status from telemetry and
//     writes config via parameter write.
// ============================================================

#include "device.h"

#if FEATURE_CUSTOM_150_960_PROFILE

extern device_t RFProfile_device;

// Custom telemetry frame tag for RF status payload.
// Sent inside a CRSF_FRAMETYPE_ELRS_STATUS (0x2E) extended frame.
#define CX_TELEMETRY_TAG_RF_STATUS  0xC0

#endif // FEATURE_CUSTOM_150_960_PROFILE
