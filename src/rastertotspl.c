/*
 * rastertotspl.c - CUPS filter: CUPS/PWG raster -> TSPL for the iDPRT SP410
 *                  family of 203-dpi direct-thermal label printers.
 *
 * This is an independent, clean-room implementation written from the public
 * TSPL command reference and black-box observation of printer behaviour.  It
 * contains no code from, and is not derived from, the vendor's closed-source
 * "raster-tspl" filter.
 *
 * Usage (as invoked by cupsd):
 *   sp410-rastertotspl job-id user title copies options [file]
 *
 * Copyright 2026 The sp410-cups-driver contributors.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cups/cups.h>
#include <cups/raster.h>

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dither.h"
#include "settings.h"
#include "tspl.h"

#ifndef SP410_VERSION
#  define SP410_VERSION "0.1.0"
#endif

static volatile sig_atomic_t g_canceled = 0;

static void
on_sigterm(int sig)
{
  (void)sig;
  g_canceled = 1;
}

/* ---- pixel unpacking --------------------------------------------------- */

static inline unsigned
sample1(const uint8_t *line, unsigned bits, unsigned x)
{
  switch (bits)
  {
    case 1:  return ((line[x >> 3] >> (7 - (x & 7))) & 1) ? 255 : 0;
    case 2:  return ((line[x >> 2] >> (6 - 2 * (x & 3))) & 3) * 85;
    case 4:  return ((line[x >> 1] >> ((x & 1) ? 0 : 4)) & 15) * 17;
    case 8:  return line[x];
    case 16:
    {
      uint16_t v;
      memcpy(&v, line + 2 * (size_t)x, 2);
      return v >> 8;
    }
  }
  return 0;
}

static inline unsigned
channel(const uint8_t *line, unsigned bits, unsigned nch, unsigned x, unsigned c)
{
  if (bits == 16)
  {
    uint16_t v;
    memcpy(&v, line + 2 * ((size_t)x * nch + c), 2);
    return v >> 8;
  }
  return line[(size_t)x * nch + c];
}

typedef enum { SRC_INK, SRC_LUMA, SRC_RGB, SRC_CMYK } src_kind_t;

/* Decide how to interpret a page; returns -1 if unsupported. */
static int
classify(const cups_page_header2_t *h, src_kind_t *kind, unsigned *nch)
{
  unsigned bits = h->cupsBitsPerColor;

  switch (h->cupsColorSpace)
  {
    case CUPS_CSPACE_K:
      *kind = SRC_INK;  *nch = 1; break;
    case CUPS_CSPACE_W:
    case CUPS_CSPACE_SW:
      *kind = SRC_LUMA; *nch = 1; break;
    case CUPS_CSPACE_RGB:
    case CUPS_CSPACE_SRGB:
    case CUPS_CSPACE_ADOBERGB:
      *kind = SRC_RGB;  *nch = 3; break;
    case CUPS_CSPACE_CMYK:
      *kind = SRC_CMYK; *nch = 4; break;
    default:
      return -1;
  }

  if (*nch == 1)
    return (bits == 1 || bits == 2 || bits == 4 || bits == 8 || bits == 16) ? 0 : -1;

  /* Multi-channel: only chunky 8/16-bit is supported. */
  if (h->cupsColorOrder != CUPS_ORDER_CHUNKED || (bits != 8 && bits != 16))
    return -1;
  return 0;
}

static void
line_to_ink(const uint8_t *line, unsigned width, unsigned bits,
            src_kind_t kind, unsigned nch, uint8_t *ink)
{
  unsigned x;

  switch (kind)
  {
    case SRC_INK:
      for (x = 0; x < width; x ++)
        ink[x] = (uint8_t)sample1(line, bits, x);
      break;
    case SRC_LUMA:
      for (x = 0; x < width; x ++)
        ink[x] = (uint8_t)(255 - sample1(line, bits, x));
      break;
    case SRC_RGB:
      for (x = 0; x < width; x ++)
      {
        unsigned r = channel(line, bits, nch, x, 0);
        unsigned g = channel(line, bits, nch, x, 1);
        unsigned b = channel(line, bits, nch, x, 2);
        unsigned y = (299 * r + 587 * g + 114 * b + 500) / 1000;
        ink[x] = (uint8_t)(255 - y);
      }
      break;
    case SRC_CMYK:
      for (x = 0; x < width; x ++)
      {
        unsigned c = channel(line, bits, nch, x, 0);
        unsigned m = channel(line, bits, nch, x, 1);
        unsigned y = channel(line, bits, nch, x, 2);
        unsigned k = channel(line, bits, nch, x, 3);
        unsigned v = (300 * c + 590 * m + 110 * y + 500) / 1000 + k;
        ink[x] = (uint8_t)(v > 255 ? 255 : v);
      }
      break;
  }
}

/* ---- resolution fix-up ------------------------------------------------- */

/*
 * Box-filter resample of an ink plane.  Used only when the incoming raster
 * is not 203 dpi (e.g. a driverless/IPP pipeline that rendered at 300 dpi).
 */
static uint8_t *
resample(const uint8_t *src, int sw, int sh, int dw, int dh)
{
  uint8_t *dst;
  int      x, y;
  double   fx = (double)sw / dw, fy = (double)sh / dh;

  if ((dst = malloc((size_t)dw * (size_t)dh)) == NULL)
    return NULL;

  for (y = 0; y < dh; y ++)
  {
    int y0 = (int)floor(y * fy), y1 = (int)ceil((y + 1) * fy);

    if (y1 <= y0) y1 = y0 + 1;
    if (y1 > sh)  y1 = sh;

    for (x = 0; x < dw; x ++)
    {
      int      x0 = (int)floor(x * fx), x1 = (int)ceil((x + 1) * fx);
      unsigned sum = 0, n = 0;
      int      yy, xx;

      if (x1 <= x0) x1 = x0 + 1;
      if (x1 > sw)  x1 = sw;

      for (yy = y0; yy < y1; yy ++)
        for (xx = x0; xx < x1; xx ++)
        {
          sum += src[(size_t)yy * sw + xx];
          n ++;
        }
      dst[(size_t)y * dw + x] = (uint8_t)(n ? (sum + n / 2) / n : 0);
    }
  }
  return dst;
}

/* ---- main --------------------------------------------------------------- */

static int
process_page(cups_raster_t *ras, const cups_page_header2_t *h,
             const sp410_settings_t *s, int page)
{
  src_kind_t        kind;
  unsigned          nch, y;
  unsigned          sw = h->cupsWidth, sh = h->cupsHeight;
  unsigned          xdpi = h->HWResolution[0] ? h->HWResolution[0] : SP410_DPI;
  unsigned          ydpi = h->HWResolution[1] ? h->HWResolution[1] : SP410_DPI;
  uint8_t          *line = NULL, *ink = NULL, *bits = NULL, *label = NULL;
  int               w, hgt, lw, lh, rc = -1, copies;
  size_t            stride, lstride;
  double            wmm, hmm;
  tspl_page_stats_t st;

  if (classify(h, &kind, &nch))
  {
    fprintf(stderr, "ERROR: Unsupported raster format on page %d "
            "(colorspace %u, %u bits, order %u)\n", page,
            h->cupsColorSpace, h->cupsBitsPerColor, h->cupsColorOrder);
    return -1;
  }
  if (!sw || !sh || !h->cupsBytesPerLine || sw > 20000 || sh > 100000)
  {
    fprintf(stderr, "ERROR: Bad raster geometry on page %d (%ux%u)\n", page, sw, sh);
    return -1;
  }

  /* Physical label size in mm, from the page size in points. */
  wmm = (h->cupsPageSize[0] > 0 ? h->cupsPageSize[0] : (float)h->PageSize[0]) * 25.4 / 72.0;
  hmm = (h->cupsPageSize[1] > 0 ? h->cupsPageSize[1] : (float)h->PageSize[1]) * 25.4 / 72.0;
  if (wmm <= 0 || hmm <= 0)
  {
    /* Fall back to the pixel geometry. */
    wmm = sw * 25.4 / xdpi;
    hmm = sh * 25.4 / ydpi;
  }

  fprintf(stderr, "DEBUG: Page %d: %ux%u px @ %ux%u dpi, %u bpc, cspace %u, "
          "label %.1fx%.1f mm\n", page, sw, sh, xdpi, ydpi,
          h->cupsBitsPerColor, h->cupsColorSpace, wmm, hmm);

  if ((line = malloc(h->cupsBytesPerLine)) == NULL ||
      (ink = malloc((size_t)sw * sh)) == NULL)
  {
    fputs("ERROR: Out of memory reading page\n", stderr);
    goto done;
  }

  for (y = 0; y < sh; y ++)
  {
    if (g_canceled)
      goto done;
    if (cupsRasterReadPixels(ras, line, h->cupsBytesPerLine) != h->cupsBytesPerLine)
    {
      fprintf(stderr, "ERROR: Short raster data on page %d at line %u\n", page, y);
      goto done;
    }
    line_to_ink(line, sw, h->cupsBitsPerColor, kind, nch, ink + (size_t)y * sw);
  }

  w   = (int)sw;
  hgt = (int)sh;

  if (abs((int)xdpi - SP410_DPI) > 1 || abs((int)ydpi - SP410_DPI) > 1)
  {
    int      nw = (int)lround((double)sw * SP410_DPI / xdpi);
    int      nh = (int)lround((double)sh * SP410_DPI / ydpi);
    uint8_t *r;

    fprintf(stderr, "INFO: Resampling page %d from %ux%u dpi to %d dpi\n",
            page, xdpi, ydpi, SP410_DPI);
    if (nw < 1) nw = 1;
    if (nh < 1) nh = 1;
    if ((r = resample(ink, w, hgt, nw, nh)) == NULL)
    {
      fputs("ERROR: Out of memory resampling page\n", stderr);
      goto done;
    }
    free(ink);
    ink = r;
    w   = nw;
    hgt = nh;
  }

  /* Halftone in source geometry. */
  stride = ((size_t)w + 7) / 8;
  if ((bits = malloc(stride * (size_t)hgt)) == NULL ||
      dither_page(s->dither, s->threshold, ink, w, hgt, bits, stride))
  {
    fputs("ERROR: Out of memory while halftoning\n", stderr);
    goto done;
  }

  /* Clip to the print head and apply the user shift. */
  lw = w   < s->max_width_dots  ? w   : s->max_width_dots;
  lh = hgt < s->max_length_dots ? hgt : s->max_length_dots;
  if (lw < w)
    fprintf(stderr, "WARNING: Page %d is %d dots wide; the print head is %d dots "
            "(%d mm). The right edge will be clipped.\n", page, w,
            s->max_width_dots, s->max_width_dots / SP410_DOTS_PER_MM);
  if (lh < hgt)
    fprintf(stderr, "WARNING: Page %d is %d dots long; clipping to %d dots.\n",
            page, hgt, s->max_length_dots);
  if (wmm > s->max_width_dots / (double)SP410_DOTS_PER_MM)
    wmm = s->max_width_dots / (double)SP410_DOTS_PER_MM;
  if (hmm > s->max_length_dots / (double)SP410_DOTS_PER_MM)
    hmm = s->max_length_dots / (double)SP410_DOTS_PER_MM;

  lstride = ((size_t)lw + 7) / 8;
  if ((label = calloc(lstride, (size_t)lh)) == NULL)
  {
    fputs("ERROR: Out of memory composing label\n", stderr);
    goto done;
  }

  for (int ly = 0; ly < lh; ly ++)
  {
    int sy = ly - s->shift_y_dots;

    if (sy < 0 || sy >= hgt)
      continue;
    for (int lx = 0; lx < lw; lx ++)
    {
      int sx = lx - s->shift_x_dots;

      if (sx < 0 || sx >= w)
        continue;
      if (bits[(size_t)sy * stride + (sx >> 3)] & (0x80 >> (sx & 7)))
        label[(size_t)ly * lstride + (lx >> 3)] |= (uint8_t)(0x80 >> (lx & 7));
    }
  }

  copies = h->NumCopies > 1 ? (int)h->NumCopies : 1;

  if (tspl_write_page(stdout, s, wmm, hmm, label, lw, lh, lstride, copies, &st))
  {
    fprintf(stderr, "ERROR: Unable to write page %d to the printer: %s\n",
            page, strerror(errno));
    goto done;
  }

  fprintf(stderr, "DEBUG: Page %d: %d band(s), %zu bitmap bytes, %zu bytes total\n",
          page, st.bands, st.bitmap_bytes, st.total_bytes);
  fprintf(stderr, "PAGE: %d %d\n", page, copies);
  rc = 0;

done:
  free(label);
  free(bits);
  free(ink);
  free(line);
  return rc;
}

int
main(int argc, char *argv[])
{
  int                 fd = 0, page = 0, num_options, status = 0;
  cups_option_t      *options = NULL;
  cups_raster_t      *ras;
  cups_page_header2_t header;
  sp410_settings_t    s;
  struct sigaction    sa;

  if (argc == 2 && !strcmp(argv[1], "--version"))
  {
    puts("sp410-rastertotspl " SP410_VERSION);
    return 0;
  }

  if (argc < 6 || argc > 7)
  {
    fprintf(stderr, "Usage: %s job-id user title copies options [file]\n", argv[0]);
    return 1;
  }

  if (argc == 7 && (fd = open(argv[6], O_RDONLY)) < 0)
  {
    fprintf(stderr, "ERROR: Unable to open raster file \"%s\": %s\n",
            argv[6], strerror(errno));
    return 1;
  }

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_sigterm;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGTERM, &sa, NULL);

  settings_defaults(&s);
  num_options = cupsParseOptions(argv[5], 0, &options);
  settings_load(&s, num_options, options, getenv("PPD"));
  cupsFreeOptions(num_options, options);

  fprintf(stderr, "DEBUG: sp410-rastertotspl %s: darkness=%d speed=%d media=%d "
          "gap=%.1f/%.1f dir=%d tear=%d dither=%s threshold=%d shift=%d,%d\n",
          SP410_VERSION, s.darkness, s.speed, (int)s.media, s.gap_mm,
          s.gap_offset_mm, s.direction, s.tear, dither_name(s.dither),
          s.threshold, s.shift_x_dots, s.shift_y_dots);

  if ((ras = cupsRasterOpen(fd, CUPS_RASTER_READ)) == NULL)
  {
    fputs("ERROR: Unable to read raster stream\n", stderr);
    if (fd)
      close(fd);
    return 1;
  }

  while (!g_canceled && cupsRasterReadHeader2(ras, &header))
  {
    page ++;
    fprintf(stderr, "INFO: Printing label %d\n", page);
    if (process_page(ras, &header, &s, page))
    {
      if (!g_canceled)
        status = 1;
      break;
    }
  }

  cupsRasterClose(ras);
  if (fd)
    close(fd);

  if (g_canceled)
  {
    fputs("INFO: Job canceled\n", stderr);
    return 0;
  }

  if (page == 0 && status == 0)
  {
    fputs("ERROR: No pages found\n", stderr);
    return 1;
  }

  if (status == 0)
    fputs("INFO: Ready to print.\n", stderr);
  return status;
}
