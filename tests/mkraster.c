/*
 * mkraster.c - Generate CUPS/PWG raster test pages for the filter test suite.
 *
 *   mkraster PATTERN out.ras [expected.pbm]
 *
 * Every pattern is first drawn into an RGB canvas, then encoded into the
 * requested raster format.  The optional PBM (P4, 1 = black) is the exact
 * dot image the filter must produce with Dither=Threshold, Threshold=128,
 * at 203 dpi (multi-page patterns write the first page only).
 *
 * Copyright 2026 The sp410-cups-driver contributors.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cups/raster.h>

#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct
{
  int      w, h;
  uint8_t *rgb;
} canvas_t;

typedef enum { ENC_K1, ENC_K8, ENC_W8, ENC_SW8, ENC_SRGB8 } enc_t;

static canvas_t
canvas_new(int w, int h)
{
  canvas_t c = { w, h, malloc((size_t)w * h * 3) };

  if (!c.rgb)
  {
    perror("malloc");
    exit(2);
  }
  memset(c.rgb, 255, (size_t)w * h * 3);
  return c;
}

static void
fill(canvas_t *c, int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b)
{
  for (int y = y0 < 0 ? 0 : y0; y < y1 && y < c->h; y ++)
    for (int x = x0 < 0 ? 0 : x0; x < x1 && x < c->w; x ++)
    {
      uint8_t *p = c->rgb + ((size_t)y * c->w + x) * 3;
      p[0] = r; p[1] = g; p[2] = b;
    }
}

static unsigned
luma(const uint8_t *p)
{
  return (299u * p[0] + 587u * p[1] + 114u * p[2] + 500u) / 1000u;
}

static void
header(cups_page_header2_t *h, const canvas_t *c, unsigned dpi, enc_t enc,
       unsigned copies)
{
  memset(h, 0, sizeof(*h));
  strcpy(h->MediaClass, "PwgRaster");
  h->HWResolution[0]  = h->HWResolution[1] = dpi;
  h->cupsPageSize[0]  = (float)(c->w * 72.0 / dpi);
  h->cupsPageSize[1]  = (float)(c->h * 72.0 / dpi);
  h->PageSize[0]      = (unsigned)lround(h->cupsPageSize[0]);
  h->PageSize[1]      = (unsigned)lround(h->cupsPageSize[1]);
  h->ImagingBoundingBox[2] = h->PageSize[0];
  h->ImagingBoundingBox[3] = h->PageSize[1];
  h->cupsWidth        = (unsigned)c->w;
  h->cupsHeight       = (unsigned)c->h;
  h->cupsColorOrder   = CUPS_ORDER_CHUNKED;
  h->NumCopies        = copies;

  switch (enc)
  {
    case ENC_K1:
      h->cupsColorSpace = CUPS_CSPACE_K;   h->cupsBitsPerColor = 1;
      h->cupsBitsPerPixel = 1;  h->cupsNumColors = 1; break;
    case ENC_K8:
      h->cupsColorSpace = CUPS_CSPACE_K;   h->cupsBitsPerColor = 8;
      h->cupsBitsPerPixel = 8;  h->cupsNumColors = 1; break;
    case ENC_W8:
      h->cupsColorSpace = CUPS_CSPACE_W;   h->cupsBitsPerColor = 8;
      h->cupsBitsPerPixel = 8;  h->cupsNumColors = 1; break;
    case ENC_SW8:
      h->cupsColorSpace = CUPS_CSPACE_SW;  h->cupsBitsPerColor = 8;
      h->cupsBitsPerPixel = 8;  h->cupsNumColors = 1; break;
    case ENC_SRGB8:
      h->cupsColorSpace = CUPS_CSPACE_SRGB; h->cupsBitsPerColor = 8;
      h->cupsBitsPerPixel = 24; h->cupsNumColors = 3; break;
  }
  h->cupsBytesPerLine = (h->cupsWidth * h->cupsBitsPerPixel + 7) / 8;
}

static int
write_page(cups_raster_t *ras, const canvas_t *c, unsigned dpi, enc_t enc,
           unsigned copies)
{
  cups_page_header2_t h;
  uint8_t            *line;

  header(&h, c, dpi, enc, copies);
  if (!cupsRasterWriteHeader2(ras, &h))
  {
    fputs("mkraster: cupsRasterWriteHeader2 failed\n", stderr);
    return -1;
  }
  line = malloc(h.cupsBytesPerLine);

  for (int y = 0; y < c->h; y ++)
  {
    memset(line, 0, h.cupsBytesPerLine);
    for (int x = 0; x < c->w; x ++)
    {
      const uint8_t *p = c->rgb + ((size_t)y * c->w + x) * 3;
      unsigned       l = luma(p);

      switch (enc)
      {
        case ENC_K1:
          if (255 - l >= 128)
            line[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
          break;
        case ENC_K8:  line[x] = (uint8_t)(255 - l); break;
        case ENC_W8:
        case ENC_SW8: line[x] = (uint8_t)l; break;
        case ENC_SRGB8: memcpy(line + 3 * (size_t)x, p, 3); break;
      }
    }
    if (cupsRasterWritePixels(ras, line, h.cupsBytesPerLine) != h.cupsBytesPerLine)
    {
      fputs("mkraster: short write\n", stderr);
      free(line);
      return -1;
    }
  }
  free(line);
  return 0;
}

/* Expected dot image: P4 PBM, at `dpi` (caller passes 203-dpi canvases). */
static void
write_pbm(const char *path, const canvas_t *c)
{
  FILE *fp = fopen(path, "wb");
  int   wb = (c->w + 7) / 8;

  if (!fp)
  {
    perror(path);
    exit(2);
  }
  fprintf(fp, "P4\n%d %d\n", c->w, c->h);
  for (int y = 0; y < c->h; y ++)
  {
    uint8_t row[2048] = { 0 };

    for (int x = 0; x < c->w; x ++)
      if (255 - luma(c->rgb + ((size_t)y * c->w + x) * 3) >= 128)
        row[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
    fwrite(row, 1, (size_t)wb, fp);
  }
  fclose(fp);
}

/* ---- patterns ----------------------------------------------------------- */

static canvas_t
pat_shipping(int w, int h)
{
  canvas_t c = canvas_new(w, h);
  int      x, i;

  /* 4-dot border */
  fill(&c, 0, 0, w, 4, 0, 0, 0);
  fill(&c, 0, h - 4, w, h, 0, 0, 0);
  fill(&c, 0, 0, 4, h, 0, 0, 0);
  fill(&c, w - 4, 0, w, h, 0, 0, 0);

  /* "address block": rows of text-like dashes */
  for (i = 0; i < 6; i ++)
    for (x = 40; x < w / 2; x += 23)
      fill(&c, x, 60 + i * 30, x + 15 + (i * 7 + x) % 9, 60 + i * 30 + 18, 0, 0, 0);

  /* barcode: bars of 2..5 dots, deliberately not byte aligned */
  for (x = 37, i = 0; x < w - 40; i ++)
  {
    int bw = 2 + (i * 7) % 4, sp = 2 + (i * 5) % 3;
    fill(&c, x, h / 2, x + bw, h / 2 + 160, 0, 0, 0);
    x += bw + sp;
  }

  /* big blank gap, then a solid block at the bottom-right corner (odd edge) */
  fill(&c, w - 101, h - 101, w - 5, h - 5, 0, 0, 0);
  return c;
}

int
main(int argc, char *argv[])
{
  const char    *pat, *out, *pbm;
  int            fd, rc = 0;
  cups_raster_t *ras;
  cups_mode_t    mode = CUPS_RASTER_WRITE;
  canvas_t       c;

  if (argc < 3 || argc > 4)
  {
    fputs("usage: mkraster PATTERN out.ras [expected.pbm]\n"
          "patterns: shipping-k1 shipping-w8 shipping-k8 rgb pwg gray multi\n"
          "          blank wide dpi300 copies odd\n", stderr);
    return 2;
  }
  pat = argv[1];
  out = argv[2];
  pbm = argc == 4 ? argv[3] : NULL;

  if (!strcmp(pat, "pwg"))
    mode = CUPS_RASTER_WRITE_PWG;

  if ((fd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
  {
    perror(out);
    return 2;
  }
  if ((ras = cupsRasterOpen(fd, mode)) == NULL)
  {
    fputs("mkraster: cupsRasterOpen failed\n", stderr);
    return 2;
  }

  if (!strncmp(pat, "shipping-", 9))
  {
    enc_t e = !strcmp(pat + 9, "k1") ? ENC_K1 : !strcmp(pat + 9, "k8") ? ENC_K8 : ENC_W8;
    c = pat_shipping(812, 1218);                       /* 4 x 6 in */
    rc = write_page(ras, &c, 203, e, 1);
  }
  else if (!strcmp(pat, "rgb"))
  {
    c = canvas_new(406, 203);                          /* 2 x 1 in */
    fill(&c, 10, 10, 200, 100, 0, 0, 0);               /* black            */
    fill(&c, 210, 10, 300, 100, 220, 0, 0);            /* red: luma 66     */
    fill(&c, 310, 10, 396, 100, 255, 255, 120);        /* pale yellow      */
    fill(&c, 10, 120, 396, 125, 0, 0, 255);            /* blue line        */
    rc = write_page(ras, &c, 203, ENC_SRGB8, 1);
  }
  else if (!strcmp(pat, "pwg"))
  {
    c = pat_shipping(812, 406);                        /* 4 x 2 in */
    rc = write_page(ras, &c, 203, ENC_SW8, 1);
  }
  else if (!strcmp(pat, "gray"))
  {
    c = canvas_new(812, 609);                          /* 4 x 3 in */
    for (int x = 0; x < 406; x ++)
    {
      uint8_t v = (uint8_t)(255 - x * 255 / 405);
      fill(&c, x, 0, x + 1, 609, v, v, v);             /* gradient left half */
    }
    fill(&c, 450, 50, 780, 150, 0, 0, 0);              /* solid block right  */
    rc = write_page(ras, &c, 203, ENC_W8, 1);
  }
  else if (!strcmp(pat, "multi"))
  {
    canvas_t a = pat_shipping(812, 1218), b = pat_shipping(406, 203),
             d = canvas_new(812, 406);
    rc = write_page(ras, &a, 203, ENC_K1, 1) ||
         write_page(ras, &b, 203, ENC_K1, 1) ||
         write_page(ras, &d, 203, ENC_K1, 1);
    c = a;
    free(b.rgb);
    free(d.rgb);
  }
  else if (!strcmp(pat, "blank"))
  {
    c = canvas_new(812, 406);
    rc = write_page(ras, &c, 203, ENC_K1, 1);
  }
  else if (!strcmp(pat, "wide"))
  {
    c = canvas_new(945, 100);                          /* ~118 mm wide */
    fill(&c, 0, 40, 945, 60, 0, 0, 0);
    rc = write_page(ras, &c, 203, ENC_K1, 1);
  }
  else if (!strcmp(pat, "dpi300"))
  {
    canvas_t hi = canvas_new(1200, 600);               /* 4 x 2 in @300 */
    fill(&hi, 300, 150, 900, 450, 0, 0, 0);
    rc = write_page(ras, &hi, 300, ENC_K1, 1);
    c = hi;
    pbm = NULL;                                        /* no exact PBM */
  }
  else if (!strcmp(pat, "copies"))
  {
    c = pat_shipping(406, 203);
    rc = write_page(ras, &c, 203, ENC_K1, 3);
  }
  else if (!strcmp(pat, "odd"))
  {
    c = canvas_new(457, 254);                          /* 2.25 x 1.25 in */
    fill(&c, 0, 0, 457, 254, 0, 0, 0);                 /* solid, incl. last col */
    rc = write_page(ras, &c, 203, ENC_K1, 1);
  }
  else
  {
    fprintf(stderr, "mkraster: unknown pattern \"%s\"\n", pat);
    return 2;
  }

  cupsRasterClose(ras);
  close(fd);

  if (!rc && pbm)
    write_pbm(pbm, &c);
  free(c.rgb);
  return rc ? 1 : 0;
}
