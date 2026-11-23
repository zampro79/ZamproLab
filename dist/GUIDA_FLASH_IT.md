# ZamproLab — installazione manuale dei binari

Questa guida riguarda i pacchetti sperimentali presenti in `dist`, destinati alla **sola valutazione privata**. Le build sono state verificate tramite manifest e build Gitless; queste immagini specifiche e la procedura Windows non sono state ancora validate su hardware. Non sono una release pubblica.

## 1. Scegli il pacchetto

| Cartella | Scheda prevista | Flash | Impostazioni build | Lua |
| --- | --- | --- | --- | --- |
| `classic-4mb` | ESP32 Classic | 4 MB | DIO, 40 MHz; PSRAM disattivata | 48 KiB |
| `s3-generic-4mb` | ESP32-S3 compatibile, profilo generico | 4 MB | DIO, 80 MHz; PSRAM e RGB disattivati | 48 KiB |
| `s3-rgb-psram-16mb` | ESP32-S3-N16R8 compatibile con la scheda verificata | 16 MB | DIO, 80 MHz; PSRAM octal 8 MiB a 80 MHz; LED GRB su GPIO48 | 256 KiB in PSRAM |

Il nome ESP32-S3 da solo non garantisce compatibilità con il profilo RGB/PSRAM: verifica modulo, flash, PSRAM, LED e GPIO. Non mescolare immagini di cartelle diverse. Ogni cartella include `release-manifest.json`, `SHA256SUMS.txt`, `FLASHING.txt` e le licenze allegate.

## 2. Prima di scrivere

1. Identifica esattamente chip, scheda, capacità della flash e porta USB/seriale. Usa un cavo dati; chiudi eventuali monitor seriali che occupano la porta.
2. Controlla protezioni e cifratura, tabella delle partizioni esistente e compatibilità del bootloader. Se non sono noti o non corrispondono, fermati e determina una procedura adatta.
3. Conserva backup verificati dei dati che servono, in particolare NVS e UserFS, e delle immagini necessarie al ripristino. I backup possono contenere credenziali: conservali in privato.
4. Scegli una **prima installazione** oppure un **aggiornamento con layout compatibile**. La scrittura delle immagini cancella e riscrive le rispettive aree di flash, anche se non si usa il pulsante ERASE.
5. Controlla nomi, dimensioni, offset e SHA-256 nel manifest e confronta i file con `SHA256SUMS.txt`. Su Windows PowerShell, dalla cartella del profilo, puoi mostrare gli hash con:

   ```powershell
   Get-ChildItem *.bin | Get-FileHash -Algorithm SHA256
   ```

   Confronta ogni risultato con `SHA256SUMS.txt` prima di continuare. Su Linux puoi usare `sha256sum -c SHA256SUMS.txt` dalla stessa cartella.

## 3. Indirizzi delle immagini

Gli indirizzi sono esadecimali e si riferiscono ai binari del profilo scelto:

| File | ESP32 Classic | ESP32-S3 | Contenuto |
| --- | --- | --- | --- |
| `bootloader.bin` | `0x1000` | `0x0` | Bootloader specifico del target |
| `partition-table.bin` | `0x8000` | `0x8000` | Tabella delle partizioni |
| `zamprolab-factory.bin` | `0x10000` | `0x10000` | Applicazione Factory |
| `zamprolab-webfs.bin` | `0x210000` | `0x210000` | Interfaccia Web e Sample |
| `userfs-empty.bin` | `0x2D0000` | `0x2D0000` | UserFS vuota |

**`userfs-empty.bin` sovrascrive la UserFS e i suoi file.** Usala soltanto se vuoi inizializzare quella regione e accetti la perdita dei dati presenti. NVS (`0x9000`) e PHY (`0xF000`) non sono incluse tra queste immagini: una prima installazione senza cancellazione completa non equivale ad azzerare le impostazioni preesistenti.

## 4. Prima installazione

Su una scheda nuova o dopo aver deciso di sostituire il contenuto delle regioni coinvolte:

- Seleziona `bootloader.bin`, `partition-table.bin`, `zamprolab-factory.bin` e `zamprolab-webfs.bin` agli offset della tabella.
- Aggiungi `userfs-empty.bin` **solo** se devi inizializzare UserFS e hai deciso di cancellare i dati di quella regione. Una UserFS preesistente incompatibile non diventa automaticamente utilizzabile.
- Accertati che l'intero layout sia compatibile con la dimensione fisica della flash. Se esistono dati da conservare o la vecchia tabella è diversa, pianifica prima backup e migrazione.

## 5. Aggiornamento preservando i dati

Questa procedura vale solo dopo aver verificato che scheda, layout installato e bootloader siano compatibili con il pacchetto scelto e dopo aver salvato backup verificati:

| Seleziona | Offset |
| --- | --- |
| `zamprolab-factory.bin` | `0x10000` |
| `zamprolab-webfs.bin` | `0x210000` |

Non selezionare bootloader, partition table o UserFS vuota; non toccare NVS e PHY. Se il bootloader o il layout non sono compatibili, interrompi e prepara una migrazione specifica. **Non installare questa WebFS da sola su una Factory precedente non compatibile**: alcuni asset richiedono il firmware con supporto streaming del pacchetto.

## 6. Windows: Flash Download Tool

Guida Espressif: [ESP32](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32/production_stage/tools/flash_download_tool.html) · [ESP32-S3](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32s3/production_stage/tools/flash_download_tool.html).

1. Scarica il Flash Download Tool dal collegamento nella guida ufficiale Espressif, estrailo e avvia l'eseguibile Windows.
2. Collega la scheda e individua la porta `COM` in Gestione dispositivi. Per S3 verifica se stai usando l'USB nativa o il convertitore USB-UART; i due connettori possono corrispondere a porte diverse.
3. Scegli `ChipType` **ESP32** per Classic o **ESP32-S3** per S3 e `WorkMode` **Develop**. Scegli `LoadMode` adatto al collegamento: la guida Classic riporta `UART`; quella S3 prevede `UART` e `USB`. Conferma.
4. Nella scheda `SPIDownload`, per ogni file autorizzato scegli il percorso con il pulsante di selezione, inserisci l'offset esadecimale corretto e spunta la relativa riga. Deseleziona tutte le altre righe, anche quelle rimaste da sessioni precedenti.
5. Seleziona la `COM` effettiva e, come punto di partenza, `921600` baud; se il collegamento non è stabile, usa una velocità inferiore. Verifica modalità e frequenza flash del profilo. Espressif indica che dalla versione 3.9.10 tali campi normalmente non sono modificabili e seguono la configurazione dell'immagine.
6. Se disponibile, abilita `DoNotChgBin` per mantenere i byte originali. Senza questa opzione il tool può modificare i parametri nell'immagine: verifica le impostazioni prima di continuare. Non usare `ERASE`, che cancella l'intera flash. Non usare `CombineBin` per un aggiornamento: può riempire gli spazi tra immagini e sovrascrivere dati intermedi.
7. Se l'accesso al bootloader non è automatico, segui la procedura BOOT/RESET specifica della scheda. Ricontrolla chip, porta, file, caselle e offset; premi `START` e attendi il completamento segnalato dal programma.
8. In caso di errore o interruzione, non presumere che la flash sia invariata: registra il messaggio e valuta il ripristino dal backup prima di riprovare.

## 7. Avvio e verifica

Dopo il completamento, riavvia normalmente se necessario e verifica log seriale, avvio senza reset ripetuti, accesso a LAB e Settings, Program Manager e filesystem. Se è una configurazione iniziale, cerca l'AP `ZamproLab-XXXXXX`, collegati con la password iniziale `ZamproLab` e apri `http://192.168.4.1/`. Se NVS era già configurata, nome e password possono essere diversi. Cambia la password predefinita tramite la console amministrativa; HTTP non dispone di autenticazione né TLS, quindi usa una rete fidata.

Verifica che i file UserFS da conservare siano ancora presenti. Per controllare i byte scritti, usa un metodo di lettura compatibile con scheda e protezioni e confronta la porzione effettiva di ciascuna immagine con il suo SHA-256; considera eventuale padding della partizione. `FINISH` è l'esito riferito dal tool, non una prova indipendente del contenuto.

## 8. Problemi frequenti

- **Porta assente o occupata:** controlla cavo dati, driver, Gestione dispositivi e programmi seriali aperti.
- **Connessione al bootloader fallita:** verifica porta e chip, usa la sequenza BOOT/RESET della scheda e prova un baud inferiore.
- **Boot loop o nessun LAB:** interrompi, conserva i log, verifica target, offset, alimentazione, impostazioni flash e compatibilità bootloader/Factory/WebFS. Non cancellare NVS o UserFS per tentativi.
- **Filesystem non montato:** verifica layout e immagine corretta; non inizializzare UserFS se contiene dati da preservare.

I comandi Linux `esptool.py` specifici del profilo sono riportati nel rispettivo `FLASHING.txt`; verificane sintassi e versione prima dell'uso. Nessun flash è stato eseguito per preparare questa guida.
