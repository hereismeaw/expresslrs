#pragma once

// ============================================================
// ConnexInsights — top-level integration header
//
// Include this single header from tx_main.cpp / rx_main.cpp to
// pull in all custom sub-GHz functionality.  The entire module
// compiles to nothing when FEATURE_SUBGHZ_ONLY_LR1121 == 0.
// ============================================================

#include <stdint.h>
#include <stdbool.h>

#if FEATURE_SUBGHZ_ONLY_LR1121

#include "RFProfile.h"
#include "RFPath.h"
#include "MBBStateMachine.h"
#include "AuditLog.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Global singletons (defined in ConnexInsights.cpp)
// ---------------------------------------------------------------------------
extern RFProfile_t   g_activeRFProfile;
extern RFPath_t      g_rfPaths[RF_PATH_COUNT];
extern MBBContext_t  g_mbbCtx;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Call once at firmware boot, before the main loop starts.
// Loads the compiled-in RF profile, initializes both paths, starts audit log.
// Returns false if the profile fails validation (should halt / enter failsafe).
bool ConnexInsights_Init(void);

// Call every packet interval from the TX/RX main loop ISR-safe context.
// Drives the MBB state machine and refreshes link-health from radio stats.
void ConnexInsights_Tick(void);

// ---------------------------------------------------------------------------
// RF path management
// ---------------------------------------------------------------------------

// Return the currently active RF path.
RFPath_t *ConnexInsights_GetActivePath(void);

// Return the candidate path (or NULL if no transition in progress).
RFPath_t *ConnexInsights_GetCandidatePath(void);

// Update link-health for a specific path from the radio driver callback.
void ConnexInsights_UpdateHealth(rfpath_id_e path_id,
                                  uint8_t lq, int16_t rssi, int8_t snr, uint8_t pkt_loss);

// ---------------------------------------------------------------------------
// Zone / transition control
// ---------------------------------------------------------------------------

// Request a zone transition (called by Lua handler or auto-threshold).
bool ConnexInsights_RequestTransition(rfzone_name_e target_zone);

// Abort any in-progress transition.
void ConnexInsights_AbortTransition(void);

// Return current MBB state (for telemetry reporting).
mbb_state_e ConnexInsights_GetMBBState(void);

// ---------------------------------------------------------------------------
// Frequency validation
// ---------------------------------------------------------------------------

// Wrapper around RFProfile_IsExcluded — always call this before setting a
// frequency on either LR1121 instance to enforce the exclusion mask.
bool ConnexInsights_IsFreqAllowed(uint32_t freq_mhz);

// ---------------------------------------------------------------------------
// Telemetry helpers
// ---------------------------------------------------------------------------

// Serialize the current state into a compact telemetry buffer (max 16 bytes).
// Returns number of bytes written.  Format matches CRSF custom params 0x74-0x7A.
uint8_t ConnexInsights_SerializeTelemetry(uint8_t *buf, uint8_t max_len);

// Return config hash for the active profile (reported in telemetry / audit).
uint32_t ConnexInsights_GetConfigHash(void);

#ifdef __cplusplus
}
#endif

#else // FEATURE_SUBGHZ_ONLY_LR1121 == 0

// Null stubs so code that guards with #if still compiles cleanly
static inline bool     ConnexInsights_Init(void)                          { return true; }
static inline void     ConnexInsights_Tick(void)                          {}
static inline void     ConnexInsights_UpdateHealth(uint8_t,uint8_t,int16_t,int8_t,uint8_t) {}
static inline bool     ConnexInsights_RequestTransition(uint8_t)          { return false; }
static inline void     ConnexInsights_AbortTransition(void)               {}
static inline bool     ConnexInsights_IsFreqAllowed(uint32_t)             { return true; }
static inline uint8_t  ConnexInsights_SerializeTelemetry(uint8_t*,uint8_t){ return 0; }
static inline uint32_t ConnexInsights_GetConfigHash(void)                 { return 0; }

#endif // FEATURE_SUBGHZ_ONLY_LR1121
