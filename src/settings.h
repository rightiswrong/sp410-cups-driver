/*
 * settings.h - Job/printer settings for the SP410 TSPL filter.
 *
 * Copyright 2026 The sp410-cups-driver contributors.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SP410_SETTINGS_H
#define SP410_SETTINGS_H

#include <cups/cups.h>

/* Print-head geometry of the iDPRT SP410 (203 dpi = 8 dots/mm, 108 mm max). */
#define SP410_DPI               203
#define SP410_DOTS_PER_MM       8
#define SP410_MAX_WIDTH_DOTS    864     /* 108 mm * 8 dots/mm */
#define SP410_MAX_LENGTH_DOTS   2400    /* 300 mm * 8 dots/mm (spec sheet) */

typedef enum
{
  MEDIA_GAP = 0,        /* die-cut labels with gaps (TSPL GAP)        */
  MEDIA_BLACKMARK,      /* labels/tags with black marks (TSPL BLINE)  */
  MEDIA_CONTINUOUS      /* continuous roll (TSPL GAP 0,0)             */
} media_tracking_t;

typedef enum
{
  DITHER_AUTO = 0,      /* threshold for line art, Atkinson for gray fills */
  DITHER_THRESHOLD,
  DITHER_ATKINSON,
  DITHER_FLOYD,
  DITHER_ORDERED
} dither_mode_t;

typedef struct
{
  int               darkness;       /* TSPL DENSITY 0..15, -1 = printer default */
  int               speed;          /* TSPL SPEED ips 1..6, -1 = printer default */
  media_tracking_t  media;
  double            gap_mm;         /* gap / black-mark height                 */
  double            gap_offset_mm;  /* gap / black-mark offset                 */
  int               direction;      /* TSPL DIRECTION 0 or 1                   */
  int               tear;           /* SET TEAR ON (1) / OFF (0)               */
  double            tear_offset_mm; /* TSPL OFFSET, 0 = don't send             */
  int               shift_x_dots;   /* horizontal image shift, dots            */
  int               shift_y_dots;   /* vertical image shift, dots              */
  dither_mode_t     dither;
  int               threshold;      /* ink level 1..254 at which a dot prints  */
  int               max_width_dots;
  int               max_length_dots;
  int               band_merge_rows;/* merge blank runs shorter than this      */
} sp410_settings_t;

void        settings_defaults(sp410_settings_t *s);
int         settings_load(sp410_settings_t *s, int num_options,
                          cups_option_t *options, const char *ppd_path);
const char *dither_name(dither_mode_t d);

#endif /* SP410_SETTINGS_H */
