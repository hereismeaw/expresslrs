#pragma once

// ============================================================
// Nomad Dual-Band TX Module — Dual LR1121 custom target
// Sub-GHz only: 151–959 MHz
// 2.4 GHz operation: DISABLED at compile time
// Authorized: NBTC / Army / Air Force RF sandbox
// ============================================================

#define TARGET_NAME "Nomad_DualLR1121_TX"
#define DEVICE_NAME "Nomad Dual LR1121 TX"

// Both radio instances use LR1121 — SX1280 is not present on this hardware
#define RADIO_LR1121
#define RADIO_LR1121_DUAL  // signals dual-instance support

// Sub-GHz profile feature flags (generated values injected by build system)
#ifndef FEATURE_SUBGHZ_ONLY_LR1121
#define FEATURE_SUBGHZ_ONLY_LR1121          1
#endif
#ifndef FEATURE_CUSTOM_150_960_PROFILE
#define FEATURE_CUSTOM_150_960_PROFILE      1
#endif
#ifndef FEATURE_DUAL_LR1121_MAKE_BEFORE_BREAK
#define FEATURE_DUAL_LR1121_MAKE_BEFORE_BREAK 1
#endif
#ifndef FEATURE_REGULATORY_EXCLUSION_MASK
#define FEATURE_REGULATORY_EXCLUSION_MASK   1
#endif
#ifndef FEATURE_RF_PROFILE_AUDIT_LOG
#define FEATURE_RF_PROFILE_AUDIT_LOG        1
#endif

// Safety assertion: 2.4 GHz MUST NOT be active on this target
#if defined(Regulatory_Domain_ISM_2400) || defined(RADIO_SX128X)
#error "2.4 GHz regulatory domain must not be defined for the Nomad Dual LR1121 TX target."
#endif

// SPI — Path A (primary, LR1121 instance 0)
#define GPIO_PIN_SCK_A        18
#define GPIO_PIN_MISO_A       19
#define GPIO_PIN_MOSI_A       23
#define GPIO_PIN_NSS_A         5
#define GPIO_PIN_DIO1_A        4
#define GPIO_PIN_NRST_A       27
#define GPIO_PIN_BUSY_A       26

// SPI — Path B (secondary, LR1121 instance 1)
// Shares MOSI/MISO/SCK; separate NSS and DIO
#define GPIO_PIN_NSS_B        25
#define GPIO_PIN_DIO1_B       33
#define GPIO_PIN_NRST_B       32
#define GPIO_PIN_BUSY_B       35

// PA enable lines (active-high)
#define GPIO_PIN_PA_ENABLE_A  21
#define GPIO_PIN_PA_ENABLE_B  22

// RF switch (path select) — reserved for future use
#define GPIO_PIN_RF_SWITCH    UNDEF_PIN

// LED
#define GPIO_PIN_LED_GREEN    2
#define GPIO_PIN_LED_RED      UNDEF_PIN

// UART to EdgeTX (CRSF protocol)
#define GPIO_PIN_RCSIGNAL_TX  1
#define GPIO_PIN_RCSIGNAL_RX  3

// Default power table index (references PA calibration table)
#define RF_DEFAULT_PA_PROFILE 0

// Hardware RF limits (enforced at compile time; exclusion_generator also validates)
#define RF_HW_MIN_MHZ 151U
#define RF_HW_MAX_MHZ 959U
