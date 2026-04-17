![Blender Render](RENDER.png)
![irl pcb](PXL_20260321_124728446.MP.jpg)
# Led Control Board
## To understand how to run
To update the code on the MCU, I would sudgest reading [The firmware instuctions](FIRMWARE_INSTUCTIONS.md).
And to update the Animations on the display, read [updating animatnios instuctions](ANIMATIONS_INSTUCTIONS.md)

## Project Purpose

### Why I Built This
One of my close friends, Ruben, wanted to build a big LED panel project with me so we could display text, animations, and more.
After this board is fully working, I also want to use it in a deadmau5-style head build for Comic-Con. Shown in the video below

### What??
- Raspberry Pi Pico (RP2040) drives WS2812 LED panels.
- 74HCT245 shifts data from 3.3V logic to 5V logic.
- The board includes sync in/out so multiple boards can be chained.
- Animations can be played from an microSD card.
- LED power comes from an external regulated 5V PSU; the board is not intended to carry full LED current for large panel loads.

## Hardware Overview
- Main MCU/module: Raspberry Pi Pico (RP2040)
- The Power should come from an external regulated 5V PSU. 
- The board can route +5V to outputs, but high LED current should be delivered with separate power wiring if needed. For smaller Panel counts, the board is capable of connecting the 5V pin of the Panel output pins. Bridge the LED Power pins to do so.

## Repository Layout
```
/
├── README.md
├── gerber.zip
├── Gerber/
├── arduino_firmware/
├── bom/
└── src/
    ├── LedScreen.kicad_sch
    ├── LedScreen.kicad_pcb
    ├── LedScreen.kicad_pro
    └── LedScreen.wrl
```

## Special Files
- `gerber.zip` - JLCPCB archive
- `schematic.pdf` - Export from KiCad
- `cart.png` - Screenshot of JLCPCB checkout/cart.
- `src/LedScreen.wrl` - Exported 3D model from KiCad.

## Firmware / Software
The Pico runs my own firmware in the `arduino_firmware/` folder.
To Install:
1. Install the Earle Philhower Arduino-Pico core, 
2. Select `Raspberry Pi Pico`, 
3. Then upload the sketch from `arduino_firmware/`.
- If you need manual USB mass-storage mode, hold `BOOTSEL` while connecting USB before upload.

### SD Card Playback
- The Pico firmware looks for animation files in the SD card root and in `/animations`.
- Supported raw file extensions are `.rgb`, `.raw`, and `.bin`.
- Raw files are played at `20 FPS` and must be a whole number of frames where each frame is `30 LEDs * 3 bytes = 90 bytes`.
- Raw frame byte order is `R, G, B` for LED 0, then `R, G, B` for LED 1, and so on.
- Files are played in alphabetical order. With one file on the card, playback wraps back to that file.
- USB serial frame streaming still works and temporarily overrides SD playback while data is arriving.

#### Optional `.lsa` Header Format
If you want per-file frame-rate and loop control, use a `.lsa` file with this 16-byte header before the raw RGB frame data:

```text
0x00-0x03  "LSA1"
0x04-0x05  LED count, little-endian (`30`)
0x06-0x07  FPS, little-endian
0x08-0x0B  Frame count, little-endian
0x0C       Flags (`bit 0 = loop this file`)
0x0D-0x0F  Reserved
```

## Note
I also included an xLights example for controlling this board.
xLights configuration depends on the final data path used between xLights and the Pico.

Start with up to 3 daisy-chained panels per output lane while validating signal integrity and power behavior.

With 8 output lanes, that targets up to 24 panels total in a chained layout.
Use an external 5V PSU sized for the actual panel count and brightness.

## Bill of Materials
Interactive BOM: [cdn.nickesselman.nl](https://cdn.nickesselman.nl/ledpanel/ibom.html)

## Acknowledgements
- Ruben helped me brainstorm the idea and do the math on the power consumption
- My Dad for helping me figuer out the limits of the Pi Pico, And telling stories of Previous projects he did as a teenager my age

## License
- Hardware: CERN-OHL-S
- Firmware/Software: MIT License

Inspiration:
https://github.com/user-attachments/assets/429253ec-16dd-4379-a7f1-76a112b9e6f4

src/
![jclpcb cart](jlcMyOrders.png)
![gerbersalt text](gerbers.png)
![pcb](pcb.png)
![ascem](scem.png)
![alt text](jlcMyOrders.png)
