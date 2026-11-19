# Incremental Lua output

The Program Manager keeps the latest 2,048 output bytes in a bounded ring. Writes overwrite old bytes; reads do not consume them. A critical section protects metadata and copies, not network transmission. `zam_program_output_read(cursor, destination, capacity, result)` reports `oldest`, `end`, `next`, `lost` and `copied`; a future cursor returns `ESP_ERR_INVALID_ARG`. A null destination with zero capacity reads metadata only. Each reader owns its cursor; UTF-8 may be split across reads.

Offsets are monotonic 64-bit values until reboot. A new run increments `run_id`, clears the ring and invalidates earlier bytes. When offsets reach `UINT64_MAX`, subsequent bytes cannot be indexed and `exhausted` is set; new runs are refused after `run_id` exhaustion. Neither output nor cursor continuity persists across reboot.

`output.produced` counts cumulative bytes and saturates at `SIZE_MAX`. The compatibility field `output.dropped` counts overwritten, invalidated and unindexable bytes; per-reader loss is `lost`. The HTTP reader is described in [Terminal](terminal-http.md).

> Historical validation below is retained in Italian and describes the configuration and results of that phase.

## Verifica e limiti

I test host coprono buffer vuoto, capacità, sovrascritture, cursori, lettori indipendenti, cambi RUN, UTF-8 diviso, overflow simulato e concorrenza sincronizzata, oltre alle regressioni Program Manager. Su ESP32 Classic un firmware temporaneo di prova ha confrontato la lettura intera con due letture incrementali e l'impronta dei 2.048 byte recenti rispetto al flusso atteso; il comando diagnostico stampava solo metadati e impronta, non l'output Lua. In un ciclo continuo sono stati prodotti 768.921 byte e resi non disponibili 766.873; STOP, deduplicazione e 12 richieste health hanno funzionato. Le 100 copie del buffer pieno hanno impiegato 1.775 µs complessivi; heap interno prima/durante/dopo 151.260/118.444/146.928 B, minimo storico 90.204 B, stack watermark Lua 9.044 B, picco Lua 33.164 B, live zero dopo cleanup. Il simbolo statico del nuovo buffer occupa 2.080 B su ESP32, contro 2.056 B per i precedenti buffer e indici: incremento di 24 B, esclusi eventuali effetti di allineamento del linker. Queste sono misure di una sessione, non una dimostrazione di assenza di perdite.

La prima versione del comando temporaneo di prova aveva due array da 2.048 byte sullo stack della console e ha provocato un reset con `LoadProhibited`; il backtrace terminava nel percorso di scrittura UART della console. Gli array sono stati spostati in memoria statica nel solo strumento temporaneo e la prova è stata ripetuta con successo. Il comando non è presente nel firmware del repository. Una prova separata con il firmware finale ha monitorato la UART durante 6.278.125 byte prodotti: nessun output Lua, reset o watchdog rilevato; STOP è tornato a IDLE. Le misure di copia includono il costo delle chiamate e della sincronizzazione.
