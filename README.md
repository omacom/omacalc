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

Percent follows the familiar calculator convention: with a pending `+` or `−`
it takes that percentage of the running total (`200 + 10 % =` gives `220`),
while with `×` or `÷` — or on its own — it simply divides by 100.

Everything works from the keyboard too:

- `0-9`, `.`, `+`, `-`, `*`, `/`, and `%` enter digits and operators.
- `Enter` or `=` calculates.
- `Backspace` deletes the last digit.
- `C` or `Escape` clears.
- `S` toggles the sign.
- `R` toggles Reverse Polish Notation mode.
- `Ctrl+C` or `Super+C` copies the result.
- `Ctrl+V` or `Super+V` pastes a number.

Click **RPN** in the display (or press `R`) to switch to stack-based entry.
Numbers go on the stack with `ENT`, and operators apply immediately to the
top two values: `3 ENT 4 +` is 7. Empty `ENT` duplicates the top of the
stack, `↕` swaps the top two values, and backspace with no current entry
drops the top. The last chosen mode is remembered.

Colors follow the current Omarchy theme (`~/.local/state/omarchy/current/theme/colors.toml`)
and re-tint live when the theme changes. Text follows the desktop text size —
`omarchy display text size`, or GNOME's `text-scaling-factor`.

## Requirements

- Qt 6: `qt6-base`, `qt6-declarative`
- `xdg-desktop-portal` and a portal backend

The iA Writer Mono font is bundled under the SIL Open Font License 1.1; see
`fonts/OFL.txt`. The font is copyright Information Architects Inc. and based on
IBM Plex, copyright IBM Corp.
