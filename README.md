# Led Control Board
Status: PCB designed, DRC clean, awaiting fabrication.
This is a board to control up to 20, 16x16 LED matrix panels using xLights.

## Project Purpose

### Why I Built This
One of my close friends, Ruben, wanted to build a big LED panel project with me so we could display text, animations, and more.
After this board is fully working, I also want to use it in a deadmau5-style head build for Comic-Con.

### What It Does
- Raspberry Pi Pico (RP2040) drives WS2812 LED panels.
- 74HCT245 shifts data from 3.3V logic to 5V logic.
- The board includes sync in/out so multiple boards can be chained.
- Animations can be played from microSD. TODO: Verify full playback workflow in firmware bring-up.
- LED power comes from an external regulated 5V PSU; the board is not intended to carry full LED current for large panel loads.

## Hardware Overview

### Controller + Interfaces
- Main MCU/module: Raspberry Pi Pico (RP2040)
- Level shifting: 74HCT245 (3.3V logic in, 5V logic out)
- LED outputs: 8x JST-XH 3-pin (2.50 mm pitch) connectors (GND / DATA / +5V)
- Power: External regulated 5V PSU. NOTE: The board can route +5V to outputs, but high LED current should be delivered with separate power wiring if needed. TODO: Confirm safe current path limits in assembled testing.

### Key Components
| Reference | Component | Role | Notes |
| A1 | RaspberryPi_Pico | MCU | Runs LED animation/control firmware |
| U1 | 74HCT245 | Shift Pico 3.3V data to 5V logic | Uses Pico 3V3 on the logic side |
| J1 | SD_READER1 | Provides storage for animation data | TODO: Confirm final file format/workflow |

## Repository Layout

```
/
├── README.md
├── JOURNAL.md
├── gerber.zip
├── schematic.pdf
├── cart.png
└── src/
    ├── LedScreen.kicad_sch
    ├── LedScreen.kicad_pcb
    ├── LedScreen.kicad_pro
    └── LedScreen.wrl
```

## Manufacturing Artifacts
- `gerber.zip` - JLCPCB archive
- `schematic.pdf` - Export from KiCad
- `cart.png` - Screenshot of JLCPCB checkout/cart.
- `src/LedScreen.wrl` - Exported 3D model from KiCad.

## Firmware / Software
The Pico runs my own firmware in the `arduino_firmware/` folder.
Install the Earle Philhower Arduino-Pico core, select `Raspberry Pi Pico`, then upload the sketch from `arduino_firmware/`.
If you need manual USB mass-storage mode, hold `BOOTSEL` while connecting USB before upload.

I also included an xLights example for controlling this board.
TODO: Document how xLights sends data to the Pico (USB serial / E1.31 via adapter / other).

xLights configuration depends on the final data path used between xLights and the Pico.
Practical recommendation: start with up to 3 daisy-chained panels per output lane while validating signal integrity and power behavior.
With 8 output lanes, that targets up to 24 panels total in a chained layout. TODO: Verify this guideline under real load/cable conditions.
Use an external 5V PSU sized for the actual panel count and brightness.

## Bring-Up and Validation
- TODO: Real bring-up procedure.
- TODO: Real validation tests performed.
- TODO: Known issues / limitations.
- TODO: First power-on checklist (check 5V rail, check 3V3, verify one lane).

## Bill of Materials (BOM)
Interactive BOM (HTML): [bom/ibom.html](bom/ibom.html)

### Quick Component Summary
| Ref | Component | Purpose |
| --- | --- | --- |
| `A1` | Raspberry Pi Pico 1 | Main controller for LED data/control logic |
| `U1` | 74HCT245 | Shifts Pico 3.3V logic to 5V-compatible signals |
| `J1` | MicroSD reader | Stores animation/content data for playback |
| `JST-XH 2.50 mm pitch 3-pin outputs` | LED data connectors | Distributes LED signal to panel chains |

Power note: LED panel load is powered from an external 5V supply; this board is not intended to carry the full panel current through onboard traces.

### Procurement List (Fill In)
| Item | Quantity | Supplier Link | Notes |
| --- | --- | --- | --- |
| Raspberry Pi Pico 1 | TODO | TODO | TODO |
| 74HCT245 | TODO | TODO | TODO |
| MicroSD reader module/connector | TODO | TODO | TODO |
| JST-XH 2.50 mm pitch 3-pin connectors | TODO | TODO | TODO |

## Images
- TODO: Schematic screenshot.
- TODO: PCB render (3D).
- TODO: Assembled board photo.
- TODO: Wiring photo.

## Acknowledgements
- TODO: Any references, libraries, or people who helped.

## License
- Hardware: CERN-OHL-S (TODO confirm)
- Firmware/Software: MIT (TODO confirm)
