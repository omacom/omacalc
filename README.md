# Omacalc

A dead-simple calculator built with Qt Quick and C++ that automatically follows the Omarchy theme and system dark/light mode.

<img width="800" alt="Omacalc with the Tokyo Night theme" src="screenshots/omacalc.png" />


## Install

Install via the Omarchy Package Repository via the `omacalc` package.

## Usage

The keypad covers the basics: the four arithmetic operators with the usual
precedence (`×` and `÷` before `+` and `−`), decimals, percent, sign toggle,
and backspace. The expression builds up above the display and stays visible
with the result after `=`, so `42 × 3 + 7` reads back exactly as it was entered.
From the keyboard, `^` raises to a power ahead of everything else, so
`2 × 3 ^ 2` gives `18`.

Numbers are grouped with thousands separators as you type, in the display and
the expression line alike, so `1234567.89` reads as `1,234,567.89`. The
fraction is left ungrouped.

Percent follows the familiar calculator convention: with a pending `+` or `−`
it takes that percentage of the running total (`200 + 10 % =` gives `220`),
while with `×`, `÷`, or `^` — or on its own — it simply divides by 100.

Every number is held as an exact fraction, so `0.1 + 0.2 − 0.3` is `0`,
`13373.54 − 13438.77` is `-65.23`, and `1 ÷ 3 × 3` is `1`. Only the display
rounds, to 15 significant digits, half away from zero. A root is exact
whenever its value is (`4 ^ 0.5` is `2`); an irrational one like `2 ^ 0.5`
is carried to about 77 digits, as is a fraction too long to hold, like
`1.0001 ^ 100000`. Results run from `1e-9999` to `9.99999999999999e+9999`;
beyond that, dividing by zero, or a fractional power of a negative number
shows `Error`.

Everything works from the keyboard too:

- `0-9`, `.`, `+`, `-`, `*` or `x`, `/`, `^`, and `%` enter digits and operators.
  The numpad enters digits whether or not NumLock is on.
- `Enter` or `=` calculates.
- `Backspace` deletes the last digit.
- `C` or `Escape` clears.
- `S` toggles the sign.
- `Ctrl+C` or `Super+C` copies the result, without separators.
- `Ctrl+V` or `Super+V` pastes a number, with or without separators.

Colors follow the current Omarchy theme (`~/.local/state/omarchy/current/theme/colors.toml`)
and re-tint live when the theme changes. Text follows the desktop text size —
`omarchy display text size`, or GNOME's `text-scaling-factor`.

## Requirements

- Qt 6: `qt6-base`, `qt6-declarative`
- `gmp` and `mpfr` for exact arithmetic
- `xdg-desktop-portal` and a portal backend

The iA Writer Mono font is bundled under the SIL Open Font License 1.1; see
`fonts/OFL.txt`. The font is copyright Information Architects Inc. and based on
IBM Plex, copyright IBM Corp.

## Development

`bin/build` compiles `build/omacalc` with CMake, and `bin/test` builds and runs the tests offscreen. Building needs `cmake`, `make`, `pkgconf`, and a C++ compiler on top of the packages above. The package itself is built in [omarchy-pkgs](https://github.com/omacom/omarchy-pkgs/tree/master/pkgbuilds/omacalc).

The tests include a fuzzer, `build/omacalc_fuzz`, that types thousands of random calculations and checks every key against an independent model of the calculator. For a longer run with fresh calculations, set `OMACALC_FUZZ_SEQUENCES=1000000 OMACALC_FUZZ_SEED=$RANDOM`.
