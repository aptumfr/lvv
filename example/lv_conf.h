/**
 * @file lv_conf.h
 * LVGL configuration for lv:: C++ bindings testing
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 32

/*=========================
   STDLIB WRAPPER SETTINGS
 *=========================*/
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

#define LV_STDINT_INCLUDE       <stdint.h>
#define LV_STDDEF_INCLUDE       <stddef.h>
#define LV_STDBOOL_INCLUDE      <stdbool.h>
#define LV_INTTYPES_INCLUDE     <inttypes.h>
#define LV_LIMITS_INCLUDE       <limits.h>
#define LV_STDARG_INCLUDE       <stdarg.h>

/*====================
   HAL SETTINGS
 *====================*/
#define LV_DEF_REFR_PERIOD  16      /* ~60 FPS for smoother cursor */
#define LV_DPI_DEF 130

/*=================
 * OPERATING SYSTEM
 *=================*/
#define LV_USE_OS   LV_OS_NONE

/*=====================
 * RENDERING CONFIG
 *=====================*/
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_DRAW_BUF_ALIGN 4

/* Enable complex gradients (linear at angles, radial, conical) */
#define LV_USE_DRAW_SW_COMPLEX_GRADIENTS 1

/* Max gradient color stops (default 2, needs 8 for rainbow gradients) */
#define LV_GRADIENT_MAX_STOPS 8

/*=======================
 * FEATURE CONFIGURATION
 *=======================*/

/* Observer - REQUIRED for State<T> */
#define LV_USE_OBSERVER 1

/* Flex and Grid layouts */
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/*==================
 * WIDGETS
 *==================*/
#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BUTTON     1
#define LV_USE_BUTTONMATRIX 1
#define LV_USE_CALENDAR   1
#define LV_USE_CANVAS     1
#define LV_USE_CHART      1
#define LV_USE_CHECKBOX   1
#define LV_USE_DROPDOWN   1
#define LV_USE_IMAGE      1
#define LV_USE_IMAGEBUTTON 1
#define LV_USE_KEYBOARD   1
#define LV_USE_LABEL      1
#define LV_USE_LED        1
#define LV_USE_LINE       1
#define LV_USE_LIST       1
#define LV_USE_MENU       1
#define LV_USE_MSGBOX     1
#define LV_USE_ROLLER     1
#define LV_USE_SCALE      1
#define LV_USE_SLIDER     1
#define LV_USE_SPAN       1
#define LV_USE_SPINBOX    1
#define LV_USE_SPINNER    1
#define LV_USE_SWITCH     1
#define LV_USE_TEXTAREA   1
#define LV_USE_TABLE      1
#define LV_USE_TABVIEW    1
#define LV_USE_TILEVIEW   1
#define LV_USE_WIN        1

/* OpenGL ES driver (for 3D texture support) */
#define LV_USE_OPENGLES   1

/* 3D texture widget (requires LV_USE_OPENGLES) */
#define LV_USE_3DTEXTURE  1

/*==================
 * THEMES
 *==================*/
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 0
#define LV_THEME_DEFAULT_GROW 1
#define LV_THEME_DEFAULT_TRANSITION_TIME 80

/*==================
 * FONTS
 *==================*/
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_26 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0

#define LV_FONT_DEFAULT &lv_font_montserrat_14
/* Large generated fonts for demos (e.g. nxp smartwatch) */
#define LV_FONT_FMT_TXT_LARGE 1

/*==================
 * TEXT SETTINGS
 *==================*/
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_)]}"
#define LV_TXT_LINE_BREAK_LONG_LEN 0
#define LV_TXT_LINE_BREAK_LONG_PRE_MIN_LEN 3
#define LV_TXT_LINE_BREAK_LONG_POST_MIN_LEN 3

/* BiDirectional text support (for Arabic, Hebrew, etc.) */
#define LV_USE_BIDI 1
#if LV_USE_BIDI
    #define LV_BIDI_BASE_DIR_DEF LV_BASE_DIR_AUTO
#endif

/* Arabic/Persian character processing for ligatures */
#define LV_USE_ARABIC_PERSIAN_CHARS 1

/*==================
 *  DISPLAY DRIVERS
 *==================*/

/* Native X11 driver - recommended for Linux desktop */
#define LV_USE_X11 1

/* SDL driver - cross-platform simulator */
#define LV_USE_SDL 0
#if LV_USE_SDL
    #define LV_SDL_INCLUDE_PATH <SDL2/SDL.h>
    #define LV_SDL_RENDER_MODE  LV_DISPLAY_RENDER_MODE_DIRECT
    #define LV_SDL_BUF_COUNT    1
    #define LV_SDL_FULLSCREEN   0
    #define LV_SDL_DIRECT_EXIT  1
    #define LV_SDL_MOUSEWHEEL_MODE LV_SDL_MOUSEWHEEL_MODE_ENCODER
#endif

/*==================
 * LOGGING
 *==================*/
#define LV_USE_LOG 1
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF 1
#endif

/*==================
 * ASSERTS
 *==================*/
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

/*==================
 * DEBUG
 *==================*/
#define LV_USE_REFR_DEBUG       0
#define LV_USE_LAYER_DEBUG      0
#define LV_USE_PARALLEL_DRAW_DEBUG 0

/*==================
 * OTHERS
 *==================*/
#define LV_USE_TRANSLATION 1

#define LV_USE_OBJ_PROPERTY 0
#define LV_USE_OBJ_PROPERTY_NAME 0
#define LV_USE_OBJ_NAME 1

#define LV_USE_SNAPSHOT 1
#define LV_USE_SYSMON 0
#define LV_USE_PROFILER 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

#define LV_USE_MONKEY 0
#define LV_USE_GRIDNAV 0
#define LV_USE_FRAGMENT 1
#define LV_USE_IMGFONT 0
#define LV_USE_IME_PINYIN 0
#define LV_USE_FILE_EXPLORER 0

#define LV_USE_FREETYPE 0
#define LV_USE_TINY_TTF 1
#if LV_USE_TINY_TTF
    #define LV_TINY_TTF_FILE_SUPPORT 1
    #define LV_TINY_TTF_CACHE_GLYPH_CNT 128
    #define LV_TINY_TTF_CACHE_KERNING_CNT 256
#endif

/* File system - needed for TTF file loading */
#define LV_USE_FS_POSIX 1
#if LV_USE_FS_POSIX
    #define LV_FS_POSIX_LETTER 'A'
    #define LV_FS_POSIX_PATH ""
    #define LV_FS_POSIX_CACHE_SIZE 0
#endif

/*==================
 * THORVG / VECTOR GRAPHICS
 *==================*/
#define LV_USE_FLOAT 1
#define LV_USE_MATRIX 1
#define LV_USE_VECTOR_GRAPHIC 1
#define LV_USE_THORVG 1
#define LV_USE_THORVG_INTERNAL 1
#define LV_USE_THORVG_EXTERNAL 0

/* Lottie animation widget (requires ThorVG) */
#define LV_USE_LOTTIE 1

/* SVG support */
#define LV_USE_SVG 1

#define LV_USE_RLOTTIE 0
#define LV_USE_FFMPEG 0

#define LV_BUILD_EXAMPLES 0

#endif /*LV_CONF_H*/
