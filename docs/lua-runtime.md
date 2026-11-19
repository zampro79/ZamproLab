# Lua Runtime Core

## Runtime and execution

Lua 5.5.1 is enabled by default. Configure a separate diagnostic build with `-DZAM_EXPERIMENTAL_LUA=OFF` to omit the runtime, its task and Lua console commands. `zam_program` manages execution independently from HTTP; `zam_runtime_lua` hosts the interpreter. The administrative console provides `lua run <path>`, `lua stop` and `lua status`; see [Program API](program-api.md) for HTTP.

Only one program runs at a time. States are IDLE, STARTING, RUNNING, STOPPING and CLEANUP; IDLE is published after cleanup. Source is copied under the UserFS lock and executed from a snapshot, with a default 32,768-byte source limit (`CONFIG_ZAM_LUA_SOURCE_LIMIT`). Changes to the file do not change a running snapshot. No program runs automatically at boot.

Classic and generic S3 use an internal allocator with a default limit of 49,152 bytes (48 KiB, `CONFIG_ZAM_LUA_HEAP_LIMIT`). The explicit verified S3-N16R8 PSRAM profile uses 262,144 bytes in PSRAM; see [PSRAM Lua](psram-lua.md). The limit excludes task stack, source snapshot and some native allocations. It is a dynamic ceiling, not a static reservation. Peak requested allocation does not necessarily equal simultaneously live application data. The runtime task stack is 12,288 bytes. STOP is cooperative, checked by an instruction hook every 1,000 instructions and in `zam.sleep()`; arbitrary native calls cannot necessarily be interrupted immediately. Cleanup releases owned resources on normal completion, STOP, error or OOM; failures are reported.

`print` and runtime errors are copied into a 2,048-byte ring independent of UART. New runs invalidate earlier buffered output; cursor-based readers can detect loss. The LAB reads `GET /api/program/output` by HTTP polling. `output.produced` and `output.dropped` are cumulative; `last_memory` reports the last completed run, not live VM usage. See [output design](output-stream-design.md) and [Terminal](terminal-http.md).

## Sources and security

Run `python3 tools/prepare_lua.py` to download and verify the official [Lua 5.5.1](https://www.lua.org/ftp/lua-5.5.1.tar.gz) sources. The archive SHA-256 is `1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce`. Lua uses the [MIT license](https://www.lua.org/license.html); preserve attribution and license text on redistribution. CMake checks the source manifest; see [build reproducibility](build-reproducibility.md).

The base, table, string, math and utf8 libraries are opened; `dofile`, `loadfile`, `load`, `pcall`, `xpcall` and `collectgarbage` are disabled. `io`, `os`, `package`, `debug` and dynamic C module loading are not exposed. These restrictions do not establish a security boundary.

HTTP is neither authenticated nor encrypted; use a trusted local network and do not expose the device to the Internet. Lua is not a complete sandbox. GPIO and I²C are restricted as described in [GPIO](lua-gpio.md) and [I²C](lua-i2c.md). The 12A–12D resource studies retain their historical measurements: their Classic 48 KiB configuration remains distinct from the later S3 PSRAM profile; neither forced GC nor the OLED prototype was adopted.

> Historical validation below is retained in Italian and describes the configuration and results of that phase.

## Fase 7C — Validazione della preparazione

Il branch di integrazione è stato derivato da `main` a `325ea91` e include selettivamente il codice definitivo e i test permanenti del commit di ricerca `3d34834`. Sono esclusi prototipi MicroPython, arbitro UART, task e code TX, checkpoint snapshot, strumenti che alterano le temporizzazioni e relativi flag di validazione. I branch di ricerca rimangono disponibili. I 61 file C/header dell'archivio Lua ufficiale sono stati confrontati con la copia usata, senza differenze; una prova di configurazione con un sorgente alterato è stata rifiutata dal controllo SHA-256.

Le quattro build ESP-IDF 5.5.3 sono riuscite senza warning rilevati: ESP32 senza Lua 904.032 B, con Lua 1.041.568 B; ESP32-S3 senza Lua 901.296 B, con Lua 1.036.608 B. Il margine factory da 2.097.152 B è rispettivamente 1.193.120, 1.055.584, 1.195.856 e 1.060.544 B. Nella configurazione OFF della prova i simboli del Program Manager e del runtime Lua erano assenti. ESP32-S3 N16R8 non era disponibile fisicamente: nessuna prova hardware o PSRAM S3 è dichiarata. La configurazione S3 compilata usa flash 4 MB e PSRAM disabilitata, non caratterizza la N16R8 reale.

Su ESP32 Classic sono riusciti 100 cicli misti, con 35 errori attesi, 25 STOP e nessun reset o watchdog durante la campagna; 30 richieste HTTP sono riuscite senza errori. I dati essenziali sono in `benchmarks/lua-7c-regression.csv`. Altre 20 prove di output continuo hanno completato STOP e recupero: 19.346.897 byte prodotti, 19.344.849 scartati, 2.048 conservati, ultimo STOP 57.357 µs e `live=0`. L'output degli script non è apparso sulla UART. Heap interno prima/durante/dopo nella sessione di output: 157.452/131.344/167.296 B; il primo valore è influenzato dai servizi e non è confrontabile direttamente con il successivo. Minimo storico 98.384 B, blocco massimo contiguo 110.592 B, stack watermark dell'ultima prova 9.228 B. Nella fase 7B.3, in una sessione separata, l'heap finale era 167.292 B: non si interpreta una singola differenza come leak o miglioramento.

Sono stati verificati in hardware prompt, NORMAL, backspace, history normale, CR/LF/CRLF e SECRET senza echo durante output, oltre a diagnostica, Wi-Fi AP+STA, HTTP e UserFS. Le modalità AP e STA separate non sono state riconfigurate in questa fase. Lo snapshot ha consentito il completamento dopo eliminazione e rinomina del file di prova; un upload incompleto ha bloccato il caricamento e il file è rimasto integro e rieseguibile. La prova hardware verifica gli stati, mentre il test host verifica anche il contenuto dello snapshot.

Firefox headless è stato usato contro la scheda reale: LAB e SYSTEM, temi Auto/Light/Dark, pulsanti RUN/STOP disabilitati, explorer, apertura, modifica, salvataggio e protezione delle modifiche non salvate sono stati verificati; schermate LAB e SYSTEM sono state esaminate. L'azione download è stata avviata ma la verifica automatica del risultato non è terminata: il file scaricato non è stato verificato. I test non provano l'intera esperienza browser o tutte le combinazioni di rete.

La factory stabile è stata ripristinata dal backup e riletta indipendentemente: SHA-256 `d2eeeaee3d4b74067cc049ee86a4c1621bfdfac1f23bdb8dfde77cccd15db29d` per `0x10000–0x10ffff`. WebFS coincide col backup, il file UserFS preesistente accessibile coincide per contenuto e la directory di prova è stata rimossa. Le impostazioni NVS controllate, i mount, la console, Wi-Fi AP+STA, HTTP e File Manager rispondono. La verifica UserFS è logica e non un confronto byte per byte della partizione modificata dai test; non è stato calcolato un hash dell'intera NVS.

**Proposta di integrazione: valutazione B, pronta con limitazioni esplicite da approvare.** Il diff introduce i componenti opt-in, il manifest di integrità Lua, i test permanenti e la documentazione; non modifica Web UI, layout o servizi HTTP e non introduce avvio Web. Prima del merge vanno accettati esplicitamente i limiti indicati: assenza di test hardware S3/PSRAM, download browser non verificato e mancata ripetizione separata delle modalità AP e STA. L'approvazione del merge su `main` resta un atto distinto.

La build Lua espone RUN, STOP, STATUS e output tramite [Program Execution API](program-api.md); la build diagnostica OFF lascia gli endpoint non registrati.

## Messa in servizio Fase 9E

La build ordinaria abilita Lua con `ZAM_EXPERIMENTAL_LUA=ON` per default; una build diagnostica separata può impostare `-DZAM_EXPERIMENTAL_LUA=OFF`. Se una directory CMake è già stata configurata con OFF, il valore in cache va cambiato esplicitamente. Non sono stati modificati runtime, deduplicazione o partizioni.

Su ESP32 Classic D0WDQ6 rev. 1.0, con ESP-IDF 5.5.3 e Lua 5.5.1, la campagna ha completato 100 cicli misti, 50 STOP, 40 errori Lua, 10 OOM e 562 letture STATUS/output durante la serie. Dopo ciascuna esecuzione è stato controllato IDLE e memoria Lua live zero. RUN e STOP concorrenti, deduplicazione, errori e recupero sono riusciti. Un RUN inviato subito dopo STOP è stato verificato, ma non si afferma di avere isolato la breve finestra STOPPING. Una serie iniziale troppo rapida ha ricevuto il 503 di saturazione previsto per il registro idempotente da 64 intenzioni/120 s; la campagna finale è stata cadenzata senza modificare il registro.

In Firefox sulla scheda sono riusciti il salvataggio del programma `/projects/zamprolab_hello.lua`, RUN, output progressivo, STOP e un successivo RUN; ETag ha bloccato un salvataggio concorrente. L'output continuo ha prodotto almeno 11.826.450 byte, con perdita rilevata dal lettore rimasto indietro e STOP completato. La cattura UART continua della campagna non ha mostrato output Lua, reset o watchdog. Le prove iniziali di strumentazione avevano rilevato un avvio seriale come reset e un'ulteriore apertura della porta aveva causato un riavvio prima della serie; dopo i rispettivi ripristini la campagna finale ha usato una sola cattura seriale continua.

Heap interno libero prima/dopo la serie: 155.020/154.836 B, blocco contiguo finale 110.592 B. Durante output continuo: 118.352–127.780 B su 12 campioni, minimo storico 80.920 B; picco Lua 33.164 B, live dopo cleanup zero, watermark stack Lua 8.824 B. Nella serie cadenzata le latenze mediana/P95/massimo in ms sono: RUN 185,46/674,54/4.821,16 (154 richieste), STOP 61,55/381,73/539,88 (53), STATUS 66,51/490,26/1.045,94 (317), output 70,67/488,62/682,52 (250), health 70,07/576,34/576,34 (10), File Manager 70,34/456,63/456,63 (10). Nessun timeout nella serie finale; campioni e misure non dimostrano assenza di perdite in ogni condizione.

L'utente ha verificato l'accesso AP con un secondo dispositivo; STA e modalità AP+STA sono state controllate sulla scheda. UserFS preesistente è stata confrontata logicamente con il backup. NVS è stata confrontata per voci valide: risultano invariati i dati fuori da `phy/cal_data`, la cui calibrazione automatica è stata esplicitamente autorizzata; la NVS grezza non è byte-identica. Hardware S3 N16R8, IME, accessibilità assistiva e download fino al file salvato nel browser restano da verificare.

Le API GPIO digitali limitate, le allowlist per target e il cleanup per esecuzione sono descritti in [lua-gpio.md](lua-gpio.md).

La libreria opzionale Lua include `zam.i2c` per un bus master per esecuzione;
contratto, pin, timeout e cleanup sono descritti in [Lua I2C](lua-i2c.md).

## RGB indicator on the verified S3 board

The optional `CONFIG_ZAM_RGB_VERIFIED_S3` profile reserves GPIO48 for the verified board's single GRB WS2812-compatible LED. Generic S3 and Classic builds leave it disabled. READY is green, RUNNING blue, and errors red until the next run; each active system color uses brightness 4/255. During a run, `zam.rgb.set(r, g, b)` temporarily controls the LED with integer values 0–255; completion or STOP restores green and errors restore red. `zam.rgb` is absent when the indicator is unavailable, including on Classic. The portable Welcome Sample checks for the optional API. The indicator and Welcome animation were verified on the identified ESP32-S3-N16R8; this does not establish compatibility with other S3 boards.

For the verified S3 board, append `config/esp32s3-rgb.defaults` after `sdkconfig.defaults` in `SDKCONFIG_DEFAULTS`; this profile selects 16 MB flash and does not enable PSRAM. The packaging tool supports this explicit 16 MB profile; its output remains a private evaluation package until release and licensing checks are complete.
