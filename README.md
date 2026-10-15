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

Percent follows the familiar calculator convention: with a pending `+` or `−`
it takes that percentage of the running total (`200 + 10 % =` gives `220`),
while with `×`, `÷`, or `^` — or on its own — it simply divides by 100.

Everything works from the keyboard too:

- `0-9`, `.`, `+`, `-`, `*` or `x`, `/`, `^`, and `%` enter digits and operators.
  The numpad enters digits whether or not NumLock is on.
- `Enter` or `=` calculates.
- `Backspace` deletes the last digit.
- `C` or `Escape` clears.
- `S` toggles the sign.
- `V` replaces the number on screen with its square root. A negative number is an error.
- `Ctrl+C` or `Super+C` copies the result.
- `Ctrl+V` or `Super+V` pastes a number.

Colors follow the current Omarchy theme (`~/.local/state/omarchy/current/theme/colors.toml`)
and re-tint live when the theme changes. Text follows the desktop text size —
`omarchy display text size`, or GNOME's `text-scaling-factor`.

## Requirements

- Qt 6: `qt6-base`, `qt6-declarative`
- `xdg-desktop-portal` and a portal backend

The iA Writer Mono font is bundled under the SIL Open Font License 1.1; see
`fonts/OFL.txt`. The font is copyright Information Architects Inc. and based on
IBM Plex, copyright IBM Corp.
