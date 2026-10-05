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

## Releasing

1. Set the new version in `VERSION`, run `make ppd` (the PPDs carry the
   version), and add the version's section to `CHANGELOG.md`
   (`## [X.Y.Z] - YYYY-MM-DD`). Push to `main` and wait for CI to pass.
2. Either push a tag (`git tag -a vX.Y.Z -m "..." && git push origin vX.Y.Z`),
   or on GitHub open **Actions → CI → Run workflow**, choose `main`, tick
   **publish_release** and run it.
3. CI rebuilds, reruns every test and only then publishes the release:
   `.deb` packages for amd64, arm64 and armhf built on Debian 12, a source
   tarball, `SHA256SUMS.txt`, and notes taken from the changelog. A manual run
   tags the exact commit it built.
