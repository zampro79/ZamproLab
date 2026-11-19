# Lua I²C master

`zam.i2c` is available when Lua is enabled; it does not initialize the bus at boot. Each execution may acquire one bus with `zam.i2c.setup(sda, scl, frequency)` at 100000 or 400000 Hz. Classic examples use GPIO21 SDA and GPIO22 SCL. On S3 use allowed GPIO4 and GPIO5; S3 has not been physically validated. Check wiring, 3.3 V levels and suitable external pull-ups.

`zam.i2c.scan()` returns responding 7-bit addresses from `0x08` to `0x77`. It only supports a 100 kHz bus: at 400 kHz it reports an error because ESP-IDF 5.5.3 probe sets 100 kHz. Reads and writes support both frequencies. An ACK does not identify a device.

`zam.i2c.write(address, data)` sends a binary string; `zam.i2c.read(address, length)` returns one; `zam.i2c.write_read(address, data, length)` performs a combined transaction with repeated START. Data can contain NUL. Lengths are 1–256 bytes; addresses are 7-bit, without shifting or the R/W bit. Driver errors and timeouts raise Lua errors. Probe timeout is 20 ms per address and transfer timeout is 100 ms. STOP is checked between probes, not during a transaction.

The program owns the bus and pins; GPIO and I²C cannot acquire the same pin. Cleanup releases device, bus and pins after completion, STOP, error or OOM. Failed release quarantines pins until reboot. Copy `07_i2c_scanner.lua` from Samples to UserFS before running it.

For a correctly identified and wired BME280, read its ID register:

```lua
zam.i2c.setup(21, 22, 100000)
local id = zam.i2c.write_read(0x76, string.char(0xD0), 1)
print(string.format("ID: 0x%02X", string.byte(id)))
```

Use the detected address. The [Bosch datasheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf) defines read-only register `0xD0` and ID `0x60`. An I²C ACK is not device identification; do not send unverified display commands. See BME280 (omitted from this source candidate) and SSD1306 (not included in this source candidate).
