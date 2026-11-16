# Hardware and wiring

## What was measured

The investigated PTT T65-TDK keypad has two independent contact banks. A key
closes one row contact to the row common and one column contact to the column
common; row and column wires are not shorted to each other as they would be in
a conventional matrix.

| T65 pin | Measured function |
|---:|---|
| 1 | Column common |
| 6 | C1 |
| 7 | C2 |
| 11 | C3 |
| 9 | Row common |
| 8 | R1 |
| 10 | R2 |
| 12 | R3 |
| 13 | R4 |

Pins 2-5 have not been identified and this project assigns them no function.
The mapping above is based on one measured assembly. T65 production variants,
repairs, replacement boards or wire colours may differ.

## Verify before connecting

Disconnect the keypad from the telephone. With a multimeter in continuity or
low-resistance mode:

1. Confirm that no unpressed selection contact is connected to either common.
2. Hold key `1`: pin 1 should connect only to pin 6, and pin 9 only to pin 8.
3. Check every key against the row/column table below.
4. Confirm that the row and column commons remain electrically separate inside
   the keypad.
5. Confirm that none of these wires remains connected to the telephone line,
   transformer, DTMF IC, oscillator or another powered circuit.

| | C1 / pin 6 | C2 / pin 7 | C3 / pin 11 |
|---|---:|---:|---:|
| R1 / pin 8 | 1 | 2 | 3 |
| R2 / pin 10 | 4 | 5 | 6 |
| R3 / pin 12 | 7 | 8 | 9 |
| R4 / pin 13 | `*` | 0 | `#` |

> [!CAUTION]
> The original telephone/DTMF electronics must be electrically disconnected,
> not merely switched off. Telephone-line voltages and legacy circuitry can
> damage the ESP32 and create a safety hazard.

## ESP32 connection

Connect T65 pins 1 and 9 to ESP32 GND. Connect each of the seven selection
contacts to a separate native GPIO. The component configures them as inputs
with internal pull-ups and treats LOW as active. No external supply is applied
to the contacts and no DTMF decoder is used.

The README pin selection is valid for a classic `esp32dev` board definition.
For other boards, consult its schematic. Avoid pins used by flash, PSRAM, USB,
boot strapping or onboard peripherals. ESPHome rejects pins that cannot provide
the required pull-up/input mode, but it cannot know every carrier-board wiring
conflict.
