/*
 * settings.c - Resolve job options + PPD defaults into sp410_settings_t.
 *
 * Precedence (highest first):
 *   1. job options passed in argv[5] (lp -o Darkness=12 ...)
 *   2. the queue's PPD defaults (lpadmin -o Darkness-default=12 rewrites these)
 *   3. built-in defaults below
 *
 * Copyright 2026 The sp410-cups-driver contributors.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "settings.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/*
 * The PPD API is marked deprecated in CUPS 2.x but remains the standard way
 * for a raster driver filter to read its queue's options.  Silence the
 * deprecation warnings locally so the rest of the build can use -Werror.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <cups/ppd.h>

void
settings_defaults(sp410_settings_t *s)
{
  memset(s, 0, sizeof(*s));
  s->darkness        = -1;
  s->speed           = -1;
  s->media           = MEDIA_GAP;
  s->gap_mm          = 2.0;
  s->gap_offset_mm   = 0.0;
  s->direction       = 0;
  s->tear            = 1;
  s->tear_offset_mm  = 0.0;
  s->shift_x_dots    = 0;
  s->shift_y_dots    = 0;
  s->dither          = DITHER_AUTO;
  s->threshold       = 128;
  s->max_width_dots  = SP410_MAX_WIDTH_DOTS;
  s->max_length_dots = SP410_MAX_LENGTH_DOTS;
  s->band_merge_rows = 16;
}

const char *
dither_name(dither_mode_t d)
{
  switch (d)
  {
    case DITHER_AUTO:      return "Auto";
    case DITHER_THRESHOLD: return "Threshold";
    case DITHER_ATKINSON:  return "Atkinson";
    case DITHER_FLOYD:     return "FloydSteinberg";
    case DITHER_ORDERED:   return "Ordered";
  }
  return "?";
}

/* ---- small parsing helpers ------------------------------------------- */

static int
is_default_word(const char *v)
{
  return !strcasecmp(v, "Default") || !strcasecmp(v, "PrinterDefault") ||
         !strcasecmp(v, "None");
}

static int
parse_int(const char *name, const char *v, int lo, int hi, int *out)
{
  char *end;
  long  n;

  errno = 0;
  n = strtol(v, &end, 10);
  if (errno || end == v || *end || n < lo || n > hi)
  {
    fprintf(stderr, "WARNING: Ignoring %s=\"%s\" (expected integer %d..%d)\n",
            name, v, lo, hi);
    return -1;
  }
  *out = (int)n;
  return 0;
}

static int
parse_mm(const char *name, const char *v, double lo, double hi, double *out)
{
  char  *end;
  double d;

  errno = 0;
  d = strtod(v, &end);
  if (end != v && (!strcmp(end, "mm") || !strcmp(end, "MM")))
    end += 2;
  if (errno || end == v || *end || !isfinite(d) || d < lo || d > hi)
  {
    fprintf(stderr, "WARNING: Ignoring %s=\"%s\" (expected %g..%g mm)\n",
            name, v, lo, hi);
    return -1;
  }
  *out = d;
  return 0;
}

static int
parse_bool(const char *v)
{
  if (!strcasecmp(v, "true") || !strcasecmp(v, "on") || !strcasecmp(v, "yes") ||
      !strcmp(v, "1"))
    return 1;
  if (!strcasecmp(v, "false") || !strcasecmp(v, "off") || !strcasecmp(v, "no") ||
      !strcmp(v, "0"))
    return 0;
  return -1;
}

/* ---- option lookup ----------------------------------------------------- */

/*
 * A lookup source is either the job's options or the PPD's defaults; the
 * same parser runs over each in turn so that a malformed job value leaves
 * the queue's configured default in place (rather than the built-in one).
 */
typedef struct
{
  int             num_options;
  cups_option_t  *options;
  ppd_file_t     *ppd;
} lookup_t;

static const char *
lookup(const lookup_t *l, const char *name)
{
  ppd_choice_t *c;

  if (l->ppd)
    return (c = ppdFindMarkedChoice(l->ppd, name)) != NULL ? c->choice : NULL;
  return cupsGetOption(name, l->num_options, l->options);
}

static void apply(sp410_settings_t *s, const lookup_t *l);

int
settings_load(sp410_settings_t *s, int num_options, cups_option_t *options,
              const char *ppd_path)
{
  lookup_t from_ppd = { 0, NULL, NULL };
  lookup_t from_job = { num_options, options, NULL };

  if (ppd_path && *ppd_path)
  {
    if ((from_ppd.ppd = ppdOpenFile(ppd_path)) == NULL)
    {
      ppd_status_t st;
      int          line;

      st = ppdLastError(&line);
      fprintf(stderr, "WARNING: Unable to open PPD \"%s\": %s on line %d; "
              "using built-in defaults\n", ppd_path, ppdErrorString(st), line);
    }
    else
    {
      ppdMarkDefaults(from_ppd.ppd);
      apply(s, &from_ppd);
      ppdClose(from_ppd.ppd);
    }
  }

  apply(s, &from_job);
  return 0;
}

static void
apply(sp410_settings_t *s, const lookup_t *l)
{
  const char *v;
  int         i;
  double      d;

  if ((v = lookup(l, "Darkness")) != NULL)
  {
    if (is_default_word(v))
      s->darkness = -1;
    else if (!parse_int("Darkness", v, 0, 15, &i))
      s->darkness = i;
  }

  if ((v = lookup(l, "PrintSpeed")) != NULL)
  {
    if (is_default_word(v))
      s->speed = -1;
    else if (!parse_int("PrintSpeed", v, 1, 6, &i))
      s->speed = i;
  }

  if ((v = lookup(l, "MediaTracking")) != NULL)
  {
    if (!strcasecmp(v, "Gap") || !strcasecmp(v, "NonContinuous") ||
        !strcasecmp(v, "Web"))
      s->media = MEDIA_GAP;
    else if (!strcasecmp(v, "BlackMark") || !strcasecmp(v, "Mark"))
      s->media = MEDIA_BLACKMARK;
    else if (!strcasecmp(v, "Continuous"))
      s->media = MEDIA_CONTINUOUS;
    else
      fprintf(stderr, "WARNING: Ignoring MediaTracking=\"%s\"\n", v);
  }

  if ((v = lookup(l, "GapHeight")) != NULL && !parse_mm("GapHeight", v, 0, 25, &d))
    s->gap_mm = d;
  if ((v = lookup(l, "GapOffset")) != NULL && !parse_mm("GapOffset", v, -25, 25, &d))
    s->gap_offset_mm = d;

  if ((v = lookup(l, "PrintDirection")) != NULL)
  {
    if (!strcasecmp(v, "Normal") || !strcmp(v, "0"))
      s->direction = 0;
    else if (!strcasecmp(v, "Rotate180") || !strcasecmp(v, "Reverse") ||
             !strcmp(v, "1"))
      s->direction = 1;
    else
      fprintf(stderr, "WARNING: Ignoring PrintDirection=\"%s\"\n", v);
  }

  if ((v = lookup(l, "TearOff")) != NULL)
  {
    int b = parse_bool(v);
    if (b < 0)
      fprintf(stderr, "WARNING: Ignoring TearOff=\"%s\"\n", v);
    else
      s->tear = b;
  }

  if ((v = lookup(l, "TearOffset")) != NULL && !parse_mm("TearOffset", v, -10, 10, &d))
    s->tear_offset_mm = d;

  if ((v = lookup(l, "ShiftX")) != NULL && !parse_mm("ShiftX", v, -20, 20, &d))
    s->shift_x_dots = (int)lround(d * SP410_DOTS_PER_MM);
  if ((v = lookup(l, "ShiftY")) != NULL && !parse_mm("ShiftY", v, -20, 20, &d))
    s->shift_y_dots = (int)lround(d * SP410_DOTS_PER_MM);

  if ((v = lookup(l, "Dither")) != NULL)
  {
    if (!strcasecmp(v, "Auto"))
      s->dither = DITHER_AUTO;
    else if (!strcasecmp(v, "Threshold") || !strcasecmp(v, "None"))
      s->dither = DITHER_THRESHOLD;
    else if (!strcasecmp(v, "Atkinson"))
      s->dither = DITHER_ATKINSON;
    else if (!strcasecmp(v, "FloydSteinberg") || !strcasecmp(v, "ErrorDiffusion"))
      s->dither = DITHER_FLOYD;
    else if (!strcasecmp(v, "Ordered") || !strcasecmp(v, "Bayer"))
      s->dither = DITHER_ORDERED;
    else
      fprintf(stderr, "WARNING: Ignoring Dither=\"%s\"\n", v);
  }

  if ((v = lookup(l, "Threshold")) != NULL && !parse_int("Threshold", v, 1, 254, &i))
    s->threshold = i;
}

#pragma GCC diagnostic pop
