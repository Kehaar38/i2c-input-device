# Third-Party Notices

This repository uses third-party libraries via Arduino cores and Arduino libraries.

## Included notice files

- `licenses/Adafruit_GFX_BSD-2-Clause.txt`
- `licenses/Adafruit_SSD1306_BSD-3-Clause.txt`
- `licenses/Adafruit_BusIO_MIT.txt`
- `licenses/LGPL-2.1.txt`

## Libraries used by this project

### firmware/input_device

- Library: Wire (I2C/TWI)
- Source: Arduino core (MiniCore AVR 3.1.2)
- License: GNU LGPL v2.1 or later (per `Wire.h` header)

### firmware/test_device

- Library: Wire (I2C/TWI)
- Source: ESP32 Arduino core 3.3.7
- License: GNU LGPL v2.1 or later (per `Wire.h` header)

- Library: Adafruit GFX Library 1.12.4
- Source: [Adafruit/Adafruit-GFX-Library](https://github.com/adafruit/Adafruit-GFX-Library)
- License: BSD (see `licenses/Adafruit_GFX_BSD-2-Clause.txt`)

- Library: Adafruit SSD1306 2.5.16
- Source: [Adafruit/Adafruit_SSD1306](https://github.com/adafruit/Adafruit_SSD1306)
- License: BSD (see `licenses/Adafruit_SSD1306_BSD-3-Clause.txt`)

- Dependency: Adafruit BusIO 1.17.4
- Source: [Adafruit/Adafruit_BusIO](https://github.com/adafruit/Adafruit_BusIO)
- License: MIT (see `licenses/Adafruit_BusIO_MIT.txt`)

## Notes

- This repository currently distributes source code and design files; compiled binaries are not distributed.
- If binary distribution starts in the future, include this file and `licenses/` alongside the distributed artifacts.
