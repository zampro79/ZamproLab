-- Goal: send SOS with an external LED.
-- Lua concepts: functions, tables and loops.
-- Hardware: ESP32 Classic, external LED and suitable resistor on GPIO18
-- to GND, active HIGH; check pinout and 3.3 V wiring.
-- On ESP32-S3 choose an allowed, verified pin: GPIO18 is not allowed.
-- Firmware: Lua ON and zam.gpio; does not use the onboard LED.
-- Ends on its own or with STOP; cleanup releases the pin.
if type(zam.gpio) ~= "table" or type(zam.gpio.mode) ~= "function" or type(zam.gpio.write) ~= "function" then
    print("This example requires firmware with zam.gpio.")
    return
end
local circuito_verificato = false -- Set true only after checking the circuit.
if not circuito_verificato then
    print("Check LED, resistor, GPIO18 and GND; then set circuito_verificato.")
    return
end
local led = 18
local segnali = {1, 1, 1, 3, 3, 3, 1, 1, 1}
zam.gpio.mode(led, "output")
for _, durata in ipairs(segnali) do
    zam.gpio.write(led, true)
    zam.sleep(durata * 200)
    zam.gpio.write(led, false)
    zam.sleep(200)
end
print("SOS complete")
