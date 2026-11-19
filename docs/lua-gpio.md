# Lua digital GPIO

With Lua enabled, `zam.gpio.mode(pin, "input"|"output")`, `zam.gpio.write(pin, boolean)` and `zam.gpio.read(pin)` provide digital GPIO. Internal pull-up/pull-down modes, PWM and interrupts are not implemented. Check the board schematic, pin exposure, attached peripherals and 3.3 V electrical levels before connecting anything. A firmware-allowed pin is not necessarily available or safe on a particular board.

ESP32 Classic allows input/output GPIO 18, 19, 21, 22, 23, 25, 26, 27, 32 and 33; GPIO 34, 35, 36 and 39 are input-only. ESP32-S3 allows GPIO 4 and 5 for input/output; external GPIO circuits on S3 have not been physically validated. All other pins are rejected, including GPIO2 on Classic; the onboard LED wiring must not be assumed. GPIO and I²C cannot own the same pin.

The driver sets the output latch LOW before enabling output and does not enable internal pulls. Inputs require suitable external bias. At normal completion, STOP, error, OOM or partial failure, cleanup attempts LOW on outputs and disables direction and pulls. After disabling, the pin is high impedance, not actively held LOW. Cleanup failures are reported and affected pins quarantined until reboot. LOW is not universally safe for active-low circuits; reset and power loss are not controlled by cleanup. Use appropriate 3.3 V circuits and current-limiting resistors; do not directly connect motors or relays.

Example for a verified external active-HIGH LED on Classic GPIO18:

```lua
local led = 18
zam.gpio.mode(led, "output")
for i = 1, 10 do
    zam.gpio.write(led, true)
    zam.sleep(500)
    zam.gpio.write(led, false)
    zam.sleep(500)
end
```

Host mock tests do not prove physical voltages, board pinout or electrical cleanup. Refer to [ESP32 GPIO](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32/api-reference/peripherals/gpio.html) and [ESP32-S3 GPIO](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-reference/peripherals/gpio.html).

> Historical validation below is retained in Italian and describes the configuration and results of that phase.

## Stato della verifica Fase 10A

I test host con driver simulato coprono validazione, pin riservati e solo input, acquisizione e riconfigurazione, STOP, errore, OOM, fallimento parziale, fallimento del cleanup, nuova esecuzione e tentativi di acquisizione da finalizzatori. Le build ESP32 Classic ed ESP32-S3 sono riuscite con Lua ON e OFF. Non è stata eseguita alcuna prova elettrica: la sola indicazione “NodeMCU ESP32-WROOM” non identifica con certezza il collegamento del LED integrato, e non è disponibile un circuito di ingresso polarizzato. Non sono quindi validati su scheda i livelli, il lampeggio o il cleanup elettrico. Il firmware installato non è stato modificato nell'ambito di questa prova.

Le build della campagna, con ESP-IDF 5.5.3, occupano 1.057.360/904.144 B su ESP32 e 1.049.856/901.424 B su ESP32-S3, rispettivamente Lua ON/OFF; le tabelle partizioni coincidono con la fase precedente. Nella build ESP32 le sezioni `.dram0.data`/`.dram0.bss` sono 17.836/40.768 B contro 17.836/40.760 B nella baseline; il registro di quarantena usa 8 B statici e il contesto dell'esecuzione aumenta di 68 B sul target. Heap e watermark stack durante operazioni GPIO non sono stati misurati su hardware.
