-- Goal: simulate a traffic light in the Terminal.
-- Lua concepts: tables, ipairs and for loops.
-- Hardware: none; no LEDs are controlled.
-- Firmware: Lua ON with zam.sleep; no GPIO required.
-- Ends on its own; STOP interrupts the simulation.
local colori = {"RED", "YELLOW", "GREEN"}
for _, colore in ipairs(colori) do
    print("Traffic light: " .. colore)
    zam.sleep(1000)
end
print("Simulation complete")
