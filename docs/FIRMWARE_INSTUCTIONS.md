Things to note:
Ill first explain updating the firmware via the sd card. then via BOOTSEL. then via the andriod ide.

Putting new code on the board is not requered to update the LED animations.

To update LED aniamtions Please read [The animation Instuctions](ANIMATIONS_INSTUCTIONS.md)


The sd card reads for a file named [firmware.uf2](../examples/example_firmware.uf2). You can build this file from the adruino ide or find the latest in the [Repo's Releases page](https://github.com/Nickdev8/LedScreen/releases)

The other way is to hold down the BOOTSEL button while pluggin the usb of the pico into a laptop. and you should see a new drive apear. put the .uf2 file in there and next time you plug it in without holding the BOOTSEL button. it should load the new code.

The last option is to plug the Pico in with BOOTSEL pressed and uploading the sketch directly from the Adruino IDE. you would need to have the right libraries and board installed for this. linked here:
