# Animation files

Copy `.lsa` animation files into the root of a FAT32-formatted SD card.

On each boot:

1. Every file named `startup*.lsa` plays once in case-insensitive alphabetical order.
2. All other `.lsa` files play in case-insensitive alphabetical order.
3. After the final normal animation, the normal playlist starts again.

Examples:

- `startup.lsa`
- `startup_1.lsa`
- `01_intro.lsa`
- `02_logo.lsa`

Startup files are not included in the repeating playlist. If there are no startup files, normal playback begins immediately. If there are no normal files, the panel returns to its idle error indication after the startup files finish.

Generate `.lsa` files from xLights FSEQ exports with [`scripts/fseq_to_lsa.py`](../scripts/fseq_to_lsa.py), or convert videos with [`scripts/mp4_to_lsa.py`](../scripts/mp4_to_lsa.py). The xLights project is in [`xlights_project/`](../xlights_project/).

The active firmware reads animations only from the SD card. USB live preview and GIF playback are not supported.

To change the board size, update [`arduino_firmware/Main/Config.h`](../arduino_firmware/Main/Config.h) and rebuild the firmware.
