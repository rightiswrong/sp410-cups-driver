# Contributing

Thanks for helping make the SP410 work everywhere.

## Most valuable right now: hardware reports

If you own an SP410, SP410BT or SP420, please open an issue with:

1. `lsusb` line for the printer and the printer's line from `sudo /usr/lib/cups/backend/usb`
2. `sp410ctl status` and `sp410ctl info` output
3. Whether `sp410ctl test-label` printed, and whether `lp -d <queue> /usr/share/cups/data/testprint` did
4. Distro, architecture (`dpkg --print-architecture`) and media used (size, gap/black mark)
5. Anything that answers an [open question](docs/REVERSE_ENGINEERING.md#open-questions)

## Clean-room rules (please read before any reverse engineering)

* Do not commit vendor binaries, PPDs, scripts or decompiled/disassembled code.
  `vendor/`, `vendor-inspect/` and `compare-out/` are git-ignored on purpose.
* Contribute *findings in your own words* to `docs/REVERSE_ENGINEERING.md`.
* Don't submit code you wrote while reading disassembly of the same function —
  describe the behaviour in an issue instead and let someone else implement it.

## Code

* C99, no dependencies beyond libcups and libm. 2-space indent, CUPS-style
  braces (see existing files). Build must stay warning-free with `-Werror`.
* Never call `setlocale()` in the filter (TSPL needs `.` decimals).
* PPDs are generated: edit `ppd/gen_ppd.py`, run `make ppd`, commit both.
  If you add an option, parse it in `src/settings.c`, document it in the
  README option table and add a test.
* Every behaviour change needs a case in `tests/run_tests.py`. Prefer exact
  dot comparisons (`assert_same_dots`) over statistics where the input allows.
* Run `make check` (and ideally the sanitizer build from the README) before
  opening a pull request. Update `CHANGELOG.md` and, if you add/rename files,
  `MANIFEST.md`.

## Commit messages

Imperative summary line ≤ 72 characters, blank line, body explaining *why*.
Reference the open-question number when a change settles one.
