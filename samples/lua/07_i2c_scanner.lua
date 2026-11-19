-- Goal: find devices responding on the I2C bus.
-- Lua concepts: tables, loops, conditions and formatting.
-- Hardware: ESP32 Classic, SDA GPIO21, SCL GPIO22, external 3.3 V pull-ups.
-- Check wiring, voltages and resistors before RUN.
-- Firmware: Lua ON with zam.i2c. ESP32-S3 requires different allowed pins.
-- Ends on its own; STOP is checked between addresses.
if type(zam.i2c) ~= "table" then
    print("This example requires firmware with zam.i2c.")
    return
end
print("=== ZAMPROLAB I2C SCANNER ===")
zam.i2c.setup(21, 22, 100000)
local devices = zam.i2c.scan()
if #devices == 0 then
    print("No devices found")
else
    for _, address in ipairs(devices) do
        print(string.format("Device found: 0x%02X", address))
    end
end
print("Scan complete")
