// LVGL 9 configuration for the dock. Only values that differ from lv_conf_template.h defaults.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

// LVGL allocates from PSRAM (src/lv_mem_psram.c); strings and sprintf come from the C library.
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CUSTOM   // src/lv_mem_psram.c
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_DEF_REFR_PERIOD 20
#define LV_USE_OS LV_OS_NONE
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

#define LV_DRAW_SW_SUPPORT_ARGB8888 1
#define LV_USE_DRAW_SW_ASM LV_DRAW_SW_ASM_NONE

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16

#define LV_USE_CANVAS 1
#define LV_USE_LABEL 1
#define LV_USE_BUTTON 1
#define LV_USE_LIST 1
#define LV_USE_IMAGE 1
#define LV_LABEL_LONG_TXT_HINT 1

// Not used by the dock
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_USE_CHART 0
#define LV_USE_CALENDAR 0
#define LV_USE_KEYBOARD 0
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPAN 0
#define LV_USE_WIN 0
#define LV_USE_TEXTAREA 1
#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0

#endif
