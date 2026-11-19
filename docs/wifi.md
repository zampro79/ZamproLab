# Wi-Fi Manager

The default policy for a new installation is `auto`: without STA credentials it starts a WPA2-Personal AP named `ZamproLab-XXXXXX`, using the last three base MAC bytes in uppercase hexadecimal, initially on channel 1, address `192.168.4.1` with ESP-IDF DHCP. The initial AP password is public and educational; change it. Configuration is stored in a dedicated NVS record that is not itself encrypted and does not protect credentials against physical access.

## Console

```text
wifi status
wifi scan
wifi scan results
wifi mode auto
wifi mode ap
wifi mode sta
wifi mode apsta
wifi sta set <ssid>
wifi ap set <ssid>
wifi connect
wifi disconnect
```

Set commands request the password separately in secret mode without echo. Never put passwords on the command line: normal command history may retain them. AP passwords are 8–63 printable ASCII characters; STA passwords are empty for open networks or 8–63 printable ASCII characters. SSIDs are 1–32 non-control bytes; the current console accepts printable ASCII and quotes for spaces. Enterprise networks are unsupported. Queued requests are not proof of completion; inspect `wifi status` and logs. Status does not reveal passwords.

`wifi scan` queues a scan; `wifi scan results` returns up to 20 entries. AP scanning temporarily enables STA; the shared radio may briefly interrupt traffic, and AP+STA follows the STA channel. The driver may reject scans with `ESP_ERR_WIFI_STATE` while connecting.

`auto` selects AP without a configured STA SSID, otherwise AP+STA. In auto, AP remains active even if STA is unavailable. Explicit STA retries with progressive intervals up to 30 seconds; after about 30 seconds without IP, or immediately without credentials, it temporarily falls back to AP+STA. Recovery AP remains active after STA returns without changing persistent mode. An explicit mode removes fallback; reboot restores the saved mode. `wifi disconnect` suspends attempts until `wifi connect` or a new mode. Use UART0 for configuration and [HTTP](http.md) for LAB access; hostname and mDNS are not implemented. Wi-Fi initialization failure is logged without erasing NVS.

> The following validation is historical and remains in Italian.

## Verifiche disponibili

Su ESP32-D0WDQ6 con flash da 4 MB sono stati verificati l'avvio AP e il suo indirizzo, la scansione partendo da AP, il cambio AP/STA/AP+STA, la sospensione dei tentativi e il fallback dopo circa 30 secondi usando un SSID fittizio non disponibile. La scansione durante una connessione in corso ha restituito l'errore previsto dal driver. In una successiva validazione STA su rete autorizzata sono stati verificati acquisizione IP, persistenza dopo riavvio e ripresa dopo disconnessione intenzionale. Autenticazione WPA2/DHCP da un client sull'AP, riconnessione dopo perdita involontaria di una rete reale e mantenimento dell'AP dopo il recupero STA restano da verificare su hardware.

Il valore predefinito `auto` viene usato in assenza di un record; una scelta esplicita viene salvata nel campo `mode` del record Wi-Fi NVS esistente, versione 1: `auto` usa il nuovo valore 4, mentre i valori già registrati 1, 2 e 3 rimangono rispettivamente AP, STA e AP+STA espliciti. Nessun record esistente viene migrato o sovrascritto automaticamente. Per passare una configurazione esistente alla politica automatica usare `wifi mode auto`. Una versione precedente del firmware non riconosce il valore 4: in caso di downgrade può ripiegare sui valori predefiniti, senza che ciò costituisca una migrazione delle credenziali.

## Verifica della politica `auto`

Su ESP32-D0WDQ6 con credenziali STA già presenti, il record preesistente in modalità AP esplicita è rimasto AP dopo l'aggiornamento. Dopo `wifi mode auto`, tre riavvii hanno ripristinato AP+STA e la STA ha acquisito un IP. Le modalità esplicite AP, STA e AP+STA sono state verificate anche dopo riavvio; `wifi disconnect` ha sospeso la STA mantenendo l'AP e `wifi connect` ha ripristinato la connessione. La scheda è stata lasciata in `auto`. La risoluzione senza credenziali è stata verificata con il test isolato `tests/run_wifi_policy_test.sh`, senza modificare le credenziali dell'utente; non è stato eseguito un avvio fisico senza credenziali. HTTP è stato verificato tramite STA, non tramite un client collegato all'AP.

Su tale scheda, in un avvio `auto` con STA configurata, il log subito dopo `esp_wifi_start` riportava 207.196 byte di heap libero/interno; dopo l'acquisizione IP la console ne riportava 195.640, con blocco interno massimo 110.592 e minimo aggregato 190.440 byte. Dopo 20 richieste HTTP su STA sono stati rilevati 195.084 byte liberi/interni, blocco massimo 110.592 e minimo 190.536 byte, in una successiva sequenza di connessione. Sono misure indicative dipendenti dalle attività concorrenti.
