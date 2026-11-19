-- Goal: count down with a pause.
-- Lua concepts: for loops, numbers and function calls.
-- Hardware: none.
-- Firmware: Lua ON with zam.sleep; no GPIO required.
-- Ends on its own; STOP interrupts the loop.
for numero = 5, 1, -1 do
    print(numero)
    zam.sleep(1000)
end
print("Go!")
