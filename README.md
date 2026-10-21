# Atari 2600 Emulator for LilyGO TTGO VGA32

**Compatible with the [ESP32 Bootloader](https://github.com/fg1998/ESP32_Bootloader).**  
**Performance: 55–60 FPS in most cases.**

An Atari 2600 emulator for the **LilyGO TTGO VGA32** board (v1.2 / v1.4), featuring 320×240 VGA output via [FabGL](https://github.com/fdivitto/FabGL) and the **Stella 2014** core (Stella 3.9.3).

Automatically detects cartridge types (2K/4K, **F8**, **F6**, **F4**, **FA**, **E0**, **E7**, F8SC/F6SC/F4SC, FE, 3F, 3E, UA, CV, AR/Supercharger, DPC/Pitfall II, and other Stella-supported mappers). It also detects **PAL vs. NTSC** (property database + scanline analysis + filename) and displays the **overscan** area, which is required by games such as **Enduro**, which render the scoreboard outside the visible 192-line area.

Stable checkpoint: tag **`fw56-ok`**. **FW 67**: Frostbite — TIA gray values `$0C` (ice) and `$0E` (parka) are rendered as distinct shades instead of the same white; `cache.txt` is created if missing (without creating directories). **FW 65**: game list on the fourth line. **FW 64**: LOADING overlay. **FW 62**: compact index. **FW 58**: 16/32 KB ROMs loaded into the heap only once. **FW 56**: RGB222 palette.

## Hardware

- LilyGO TTGO VGA32 (ESP32-WROVER / PICO-D4)
- VGA monitor
- PS/2 keyboard (GPIO interrupt on CLK 33 / DATA 32; FabGL is used for VGA output only)
- FAT32 microSD card
- Headphones/speaker connected to the audio jack (GPIO 25, timer-based DAC at 22,050 Hz, no I2S) — audio is disabled if the SD card driver falls back to MISO=25.

VGA and keyboard pinout (standard board configuration, v1.2 reference):

| Signal | GPIO |
|---|---:|
| VGA R0 / R1 | 21 / 22 |
| VGA G0 / G1 | 18 / 19 |
| VGA B0 / B1 | 4 / 5 |
| HSYNC / VSYNC | 23 / 15 |
| Keyboard DATA | 32 |
| Keyboard CLK | 33 |
| Audio DAC | 25 |
| SD CS / MOSI / SCK | 13 / 12 / 14 |
| SD MISO | 2, fallback 25 |

## SD Card

```text
/
├── bootl.rc          (optional — keyboard pin configuration)
└── 2600/
    ├── Enduro.bin
    ├── Pitfall.bin
    └── ...
```

The firmware attempts to mount the SD card using **MISO=2**. If this fails, it retries using **MISO=25**. Files beginning with `._` (macOS AppleDouble files) and `.DS_Store` are ignored in the `/2600` directory.

The `/2600` directory must already exist on the SD card (the firmware does not create directories). If `cache.txt` does not exist, it is generated at startup. Press **F7** from the help screen (F1) to rebuild the cache after adding or removing games.

### `bootl.rc` (SD card root directory)

```ini
kbddat=32
kbdclk=33
magicb=36
```

If this file is present and readable, `kbddat` and `kbdclk` configure the PS/2 keyboard. Otherwise, the LilyGO defaults (32/33) are used. `magicb` is an additional active-low GPIO that opens the ROM menu — GPIO 36 in this example.

Compatible with the [ESP32_Bootloader](https://github.com/fg1998/ESP32_Bootloader) ecosystem (clears `otadata` at boot to return to the bootloader selector).

## Keyboard Controls

| Key | Function |
|---|---|
| **F1** | Help |
| **F2** | Color / black and white |
| **F3** | Frameskip: **0** = disabled (default); **1** = skip every other frame, retaining collisions in the sprite region; **2** = skip every other frame only during copy clocks. |
| **F4** | SELECT (hold) |
| **F5** | Game RESET (hold) |
| **F6** | Toggle joystick **J1 / J2** (indicator in the bottom-right corner) |
| **F7** | Rebuild the game list cache (`/2600/cache.txt`) — available from the help screen (F1) or the menu |
| **F9** | ROM menu |
| **F10** | Reload the current game |
| **F11** | Game RESET |
| **F12** | Reset the ESP32 |
| **Q A O P** + **Space** | Direction and fire |
| **Arrow keys** + **Space** | Direction and fire (uses the currently selected joystick) |

The ROM menu displays 24 items per page. **Left/Right** changes pages, **Up/Down** changes the selection, **A–Z** jumps to the corresponding letter, and **Enter/Space** loads the selected game. Long filenames are truncated to 39 characters, with a trailing `>` indicating truncation. The last page may contain fewer than 24 games if that is all that remains in the list.

The RAM index stores only the offset, letter, and prefix (16 bytes). Visible filenames are read from `cache.txt` when switching pages, rather than on every cursor movement.

The PS/2 keyboard is read by a GPIO CLK falling-edge ISR, using scan code Set 2 on core 0. FabGL does not use these pins — its ULP cannot coexist with the 80 KB TIA block in DRAM.

## Building and Flashing

[PlatformIO](https://platformio.org/) is required.

```bash
pio run -e vga32 -t upload
```

### Build Environments

- `vga32` — standalone firmware (huge-app partition, flash address `0x10000`).
- `vga32_bootloader` — built for `ota_0` at `0x130000`, for use with the VGA32 bootloader (`pio run -e vga32_bootloader`). Copy `.pio/build/vga32_bootloader/firmware.bin` to the emulator's directory on the SD card, alongside `version.txt`.

The CPU runs at 240 MHz, with Wi-Fi and Bluetooth disabled (Bluetooth RAM is returned to the heap at startup). The TIA buffers (160×256 front + back = 80 KB) reside in internal DRAM; `-mfix-esp32-psram-cache-issue` is not used. ROMs up to **64 KB** are supported.

## Core

Stella 2014 is based on the Stella 3.9.3 libretro snapshot. It provides sufficiently cycle-accurate TIA emulation for illegal HMOVE behavior, RIOT, the 6507 CPU, and the original bankswitch detection logic.

ARM-based cartridges (DPC+, CDF) compile, but are too demanding for the ESP32 without PSRAM. Classic cartridge types (F8/F6/F4/FA/E0/E7 and others) are the primary target.

## License

GPL-3.0 (firmware + FabGL). The Stella 2014 core remains under GPL-2.0-or-later; the combined project is licensed under GPL-3.0.