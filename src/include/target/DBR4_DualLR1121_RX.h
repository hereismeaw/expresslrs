#pragma once

// ============================================================
// DBR4 Dual Band Xross Gemini ExpressLRS Receiver
// Dual LR1121 custom target — Sub-GHz only: 151–959 MHz
// 2.4 GHz operation: DISABLED at compile time
// Authorized: NBTC / Army / Air Force RF sandbox
// ============================================================

#define TARGET_NAME "DBR4_DualLR1121_RX"
#define DEVICE_NAME "DBR4 Dual LR1121 RX"

#define RADIO_LR1121
#define RADIO_LR1121_DUAL

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

#if defined(Regulatory_Domain_ISM_2400) || defined(RADIO_SX128X)
#error "2.4 GHz regulatory domain must not be defined for the DBR4 Dual LR1121 RX target."
#endif

// SPI — Path A (LR1121 instance 0)
#define GPIO_PIN_SCK_A       14
#define GPIO_PIN_MISO_A      12
#define GPIO_PIN_MOSI_A      13
#define GPIO_PIN_NSS_A        5
#define GPIO_PIN_DIO1_A       4
#define GPIO_PIN_NRST_A      16
#define GPIO_PIN_BUSY_A      17

// SPI — Path B (LR1121 instance 1)
#define GPIO_PIN_NSS_B       15
#define GPIO_PIN_DIO1_B      18
#define GPIO_PIN_NRST_B      19
#define GPIO_PIN_BUSY_B      21

// PA enables
#define GPIO_PIN_PA_ENABLE_A 22
#define GPIO_PIN_PA_ENABLE_B 23

// LED
#define GPIO_PIN_LED_GREEN    2
#define GPIO_PIN_LED_RED      UNDEF_PIN

// Serial RX/TX to FC (CRSF)
#define GPIO_PIN_RCSIGNAL_TX  1
#define GPIO_PIN_RCSIGNAL_RX  3

#define RF_DEFAULT_PA_PROFILE 0
#define RF_HW_MIN_MHZ 151U
#define RF_HW_MAX_MHZ 959U
