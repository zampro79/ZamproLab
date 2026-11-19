-- Goal: read an external button and display its state.
-- Lua concepts: booleans, conditions and for loops.
-- Hardware: ESP32 Classic, GPIO18 with external pull-down to GND and button
-- to 3.3 V; check component values, pinout and wiring.
-- Do not leave the input floating; input_pullup is unavailable.
-- On ESP32-S3 choose an allowed, verified pin: GPIO18 is not allowed.
-- Firmware: Lua ON and zam.gpio.
-- Ends on its own or with STOP; cleanup releases the pin.
if type(zam.gpio) ~= "table" or type(zam.gpio.mode) ~= "function" or type(zam.gpio.read) ~= "function" then
    print("This example requires firmware with zam.gpio.")
    return
end
local circuito_verificato = false -- Set true only after checking the circuit.
if not circuito_verificato then
    print("Check button and external pull-down on GPIO18; then set circuito_verificato.")
    return
end
local pulsante = 18
zam.gpio.mode(pulsante, "input")
for _ = 1, 30 do
    if zam.gpio.read(pulsante) then print("Pressed") else print("Released") end
    zam.sleep(500)
end
print("Reading complete")
