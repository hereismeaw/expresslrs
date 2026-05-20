#pragma once
#if !defined TARGET_NATIVE
#include <Arduino.h>
#endif

// Used to XOR with OtaCrcInitializer and macSeed to reduce compatibility with previous versions.
// It should be incremented when the OTA packet structure is modified.
#define OTA_VERSION_ID      4

#define UNDEF_PIN (-1)

/// General Features ///
#define LED_MAX_BRIGHTNESS 50 //0..255 for max led brightness

/////////////////////////

#define WORD_ALIGNED_ATTR __attribute__((aligned(4)))
#define WORD_PADDED(size) (((size)+3) & ~3)

#undef ICACHE_RAM_ATTR //fix to allow both esp32 and esp8266 to use ICACHE_RAM_ATTR for mapping to IRAM
#define ICACHE_RAM_ATTR IRAM_ATTR

#if defined(TARGET_NATIVE)
#define IRAM_ATTR
#include "native.h"
#endif

// ============================================================
// ConnexInsights Sub-GHz custom target feature flags
// These are set by the target .ini / compile_profile.py output.
// Defaults here are intentionally 0 so normal upstream targets
// are completely unaffected.
// ============================================================
#ifndef FEATURE_SUBGHZ_ONLY_LR1121
#define FEATURE_SUBGHZ_ONLY_LR1121          0
#endif
#ifndef FEATURE_CUSTOM_150_960_PROFILE
#define FEATURE_CUSTOM_150_960_PROFILE      0
#endif
#ifndef FEATURE_DUAL_LR1121_MAKE_BEFORE_BREAK
#define FEATURE_DUAL_LR1121_MAKE_BEFORE_BREAK 0
#endif
#ifndef FEATURE_REGULATORY_EXCLUSION_MASK
#define FEATURE_REGULATORY_EXCLUSION_MASK   0
#endif
#ifndef FEATURE_RF_PROFILE_AUDIT_LOG
#define FEATURE_RF_PROFILE_AUDIT_LOG        0
#endif

// Compile-time assertion: custom sub-GHz targets must never define 2.4 GHz domain
#if FEATURE_SUBGHZ_ONLY_LR1121 && defined(Regulatory_Domain_ISM_2400)
#error "FEATURE_SUBGHZ_ONLY_LR1121 and Regulatory_Domain_ISM_2400 are mutually exclusive."
#endif
#if FEATURE_SUBGHZ_ONLY_LR1121 && defined(RADIO_SX128X)
#error "FEATURE_SUBGHZ_ONLY_LR1121 requires RADIO_LR1121, not RADIO_SX128X."
#endif
// ============================================================

/*
 * Features
 * define features based on pins before defining pins as UNDEF_PIN
 */

// Using these DEBUG_* imply that no SerialIO will be used so the output is readable
#if !defined(DEBUG_CRSF_NO_OUTPUT) && defined(TARGET_RX) && (defined(DEBUG_RCVR_LINKSTATS) || defined(DEBUG_RX_SCOREBOARD) || defined(DEBUG_RCVR_SIGNAL_STATS))
#define DEBUG_CRSF_NO_OUTPUT
#endif

#if defined(DEBUG_CRSF_NO_OUTPUT)
#define OPT_CRSF_RCVR_NO_SERIAL true
#elif defined(TARGET_RX)
extern bool pwmSerialDefined;

#define OPT_CRSF_RCVR_NO_SERIAL (GPIO_PIN_RCSIGNAL_RX == UNDEF_PIN && GPIO_PIN_RCSIGNAL_TX == UNDEF_PIN && !pwmSerialDefined)
#else
#define OPT_CRSF_RCVR_NO_SERIAL false
#endif

#if defined(RADIO_SX128X)
#define Regulatory_Domain_ISM_2400 1
// ISM 2400 band is in use => undefine other requlatory domain defines
#undef Regulatory_Domain_AU_915
#undef Regulatory_Domain_EU_868
#undef Regulatory_Domain_IN_866
#undef Regulatory_Domain_FCC_915
#undef Regulatory_Domain_AU_433
#undef Regulatory_Domain_EU_433
#undef Regulatory_Domain_US_433
#undef Regulatory_Domain_US_433_WIDE

#elif defined(RADIO_SX127X) || defined(RADIO_LR1121)
#if !(defined(Regulatory_Domain_AU_915) || defined(Regulatory_Domain_FCC_915) || \
        defined(Regulatory_Domain_EU_868) || defined(Regulatory_Domain_IN_866) || \
        defined(Regulatory_Domain_AU_433) || defined(Regulatory_Domain_EU_433) || \
        defined(Regulatory_Domain_US_433) || defined(Regulatory_Domain_US_433_WIDE) || \
        defined(Regulatory_Domain_Custom150_960) || \
        defined(UNIT_TEST))
#error "Regulatory_Domain is not defined for 900MHz device. Check user_defines.txt!"
#endif
#else
#error "Either RADIO_SX127X, RADIO_LR1121 or RADIO_SX128X must be defined!"
#endif

#if defined(PLATFORM_ESP32)
#include <soc/uart_pins.h>
#endif
#if !defined(U0RXD_GPIO_NUM)
#define U0RXD_GPIO_NUM (3)
#endif
#if !defined(U0TXD_GPIO_NUM)
#define U0TXD_GPIO_NUM (1)
#endif

#include "hardware.h"
