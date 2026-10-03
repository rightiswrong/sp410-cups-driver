/*
 * ppdcheck.c - Parse the driver PPDs with libcups and sanity-check them.
 *
 * cupstestppd is not available everywhere (and is deprecated), so this uses
 * the same parser cupsd uses, plus driver-specific checks: every option the
 * filter reads exists, every default is a real choice, every page size has a
 * PaperDimension/ImageableArea, and the cupsFilter line names our filter.
 *
 * Copyright 2026 The sp410-cups-driver contributors.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <cups/ppd.h>

#include <stdio.h>
#include <string.h>

static const char *const required[] =
{
  "PageSize", "Resolution", "ColorModel", "Darkness", "PrintSpeed",
  "PrintDirection", "MediaTracking", "GapHeight", "GapOffset", "TearOff",
  "TearOffset", "Dither", "Threshold", "ShiftX", "ShiftY"
};

static int
check(const char *path)
{
  ppd_file_t   *ppd;
  ppd_status_t  st;
  int           line, errors = 0, i, found_filter = 0;

  if ((ppd = ppdOpenFile(path)) == NULL)
  {
    st = ppdLastError(&line);
    printf("FAIL %s: %s on line %d\n", path, ppdErrorString(st), line);
    return 1;
  }

  for (i = 0; i < (int)(sizeof(required) / sizeof(required[0])); i ++)
  {
    ppd_option_t *o = ppdFindOption(ppd, required[i]);

    if (!o)
    {
      printf("FAIL %s: missing option %s\n", path, required[i]);
      errors ++;
      continue;
    }
    if (!ppdFindChoice(o, o->defchoice))
    {
      printf("FAIL %s: %s default \"%s\" is not a choice\n", path, o->keyword,
             o->defchoice);
      errors ++;
    }
    for (int c = 0; c < o->num_choices; c ++)
    {
      ppdMarkDefaults(ppd);
      if (ppdMarkOption(ppd, o->keyword, o->choices[c].choice) < 0 ||
          !ppdIsMarked(ppd, o->keyword, o->choices[c].choice))
      {
        printf("FAIL %s: cannot mark %s=%s\n", path, o->keyword,
               o->choices[c].choice);
        errors ++;
      }
    }
  }

  for (i = 0; i < ppd->num_sizes; i ++)
  {
    ppd_size_t *s = ppd->sizes + i;

    if (!strcmp(s->name, "Custom"))
      continue;
    if (s->width <= 0 || s->length <= 0 || s->right <= s->left || s->top <= s->bottom)
    {
      printf("FAIL %s: page size %s lacks dimensions/imageable area\n", path, s->name);
      errors ++;
    }
    if (s->width > 306.2f)
    {
      printf("FAIL %s: page size %s wider than the 108 mm head\n", path, s->name);
      errors ++;
    }
  }

  if (!ppd->variable_sizes)
  {
    printf("FAIL %s: custom page sizes not enabled\n", path);
    errors ++;
  }

  for (i = 0; i < ppd->num_filters; i ++)
    if (strstr(ppd->filters[i], "application/vnd.cups-raster") &&
        strstr(ppd->filters[i], "sp410-rastertotspl"))
      found_filter = 1;
  if (!found_filter)
  {
    printf("FAIL %s: cupsFilter does not reference sp410-rastertotspl\n", path);
    errors ++;
  }

  if (!errors)
    printf("ok   %s: %d sizes, %d groups, model \"%s\"\n", path, ppd->num_sizes,
           ppd->num_groups, ppd->modelname);
  ppdClose(ppd);
  return errors ? 1 : 0;
}

int
main(int argc, char *argv[])
{
  int bad = 0;

  if (argc < 2)
  {
    fputs("usage: ppdcheck file.ppd ...\n", stderr);
    return 2;
  }
  for (int i = 1; i < argc; i ++)
    bad |= check(argv[i]);
  return bad;
}
