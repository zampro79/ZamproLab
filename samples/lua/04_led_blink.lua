-- Goal: blink an external LED.
-- Lua concepts: variables, for loops, booleans and functions.
-- Hardware: ESP32 Classic with external LED and suitable resistor on GPIO18,
-- to GND, active HIGH; check board, pinout and 3.3 V wiring.
-- GPIO18 is not allowed on ESP32-S3; check its pinout and allowlist.
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
zam.gpio.mode(led, "output")
for i = 1, 10 do
    zam.gpio.write(led, true)
    print("LED ON", i)
    zam.sleep(500)
    zam.gpio.write(led, false)
    print("LED OFF", i)
    zam.sleep(500)
end
print("Blink complete")
