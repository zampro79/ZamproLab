# HTTP server and diagnostics

`zam_http` starts one ESP-IDF HTTP server on TCP port 80 after Wi-Fi initialization, without waiting for STA connectivity. It serves available AP and STA interfaces and continues during mode changes. The firmware serves LAB assets, diagnostic APIs, [UserFS APIs](files.md) and [Program APIs](program-api.md). Its lifecycle functions are `zam_http_init`, `zam_http_start`, `zam_http_stop` and `zam_http_is_running`.

HTTP has no authentication or TLS. Any reachable client can read diagnostics, modify UserFS and run saved programs when Lua is enabled. Use a trusted local network and do not expose the server to the Internet. The initial AP password is educational and should be changed. No cloud, captive portal, mDNS or port forwarding is configured.

| Method and path | Response |
| --- | --- |
| `GET /` | LAB/SYSTEM from WebFS |
| `GET /style.css`, `/app.js`, `/terminal.js`, `/highlight.js` | Explicit static WebFS assets |
| `GET /api/health` | `status`, `service`, `http` |
| `GET /api/system` | name, version, target, ESP-IDF, `uptime_ms`, `chip`, `flash_size` |
| `GET /api/memory` | heap measurements in bytes |
| `GET /api/wifi/status` | configured/effective mode, STA, SSIDs, addresses, AP clients and fallback |

On the AP use `http://192.168.4.1/`; on STA use the address from `wifi status`. WebFS unavailable returns 503 and a missing file 404. Unknown paths return 404 and unsupported methods 405. `chip.revision` uses ESP-IDF encoding: 100 means 1.0. `sta_connected` means IP acquired; unavailable addresses are empty strings. `configured_mode` can be `auto`, `ap`, `sta`, `apsta`; `effective_mode` is the actual mode. Passwords are not returned. The output endpoint uses HTTP polling, not WebSocket. Some HTTP timeouts were observed in phase 12D and require further investigation; no cause has been established.

> The following validation is historical and remains in Italian.

## Test e memoria

Nella Fase 3, prima della migrazione a WebFS, su ESP32-D0WDQ6 con ESP-IDF 5.5.3, flash da 4 MB e client sulla rete STA autorizzata, tutti gli endpoint hanno risposto; JSON, 404 e 405 sono stati verificati. Sono riuscite 120 richieste consecutive alternate e altre 20 dopo il passaggio STA → AP+STA. La console è rimasta utilizzabile, il servizio è tornato disponibile dopo riavvio e non sono stati osservati panic. Una richiesta malformata ha ricevuto 400; dopo la chiusura anticipata di un client il servizio ha continuato a rispondere. Il test HTTP attraverso l'IP AP non è stato eseguito: non era disponibile un client collegato all'AP.

In un avvio in AP, subito prima dell'avvio HTTP l'heap libero/interno era 202.172 byte; subito dopo 193.896 byte, con blocco interno massimo 110.592 byte. Nelle richieste STA, `/api/memory` ha riportato 200.252 byte alla prima misura e 200.264 dopo la sequenza; il minimo aggregato era 189.788 byte e il blocco massimo 110.592 byte. In AP+STA, dopo 20 richieste, la console ha riportato 194.824 byte liberi e minimo 188.400 byte. I valori dipendono dalle interfacce attive e dall'attività del Wi-Fi e non sono direttamente confrontabili con ogni misura delle fasi precedenti.

Nella Fase 4 sulla stessa scheda, dopo la migrazione, 120 richieste consecutive su STA hanno verificato pagina identica al file sorgente WebFS e risposte JSON delle quattro API. I percorsi `/user`, `/user/test`, `/web/index.html`, un tentativo di attraversamento e un percorso inesistente hanno risposto 404. Il caso di `index.html` mancante e l'accesso HTTP diretto tramite AP non sono stati provati sulla scheda. Alla prima e all'ultima misura HTTP l'heap libero era rispettivamente 191.096 e 190.984 byte; il blocco interno massimo era 110.592 byte e il minimo finale 182.408 byte.

I percorsi delle risorse statiche sono espliciti; non è possibile richiedere file arbitrari o contenuti UserFS. Le risorse usano rispettivamente `text/css` e `text/javascript` con charset UTF-8. La descrizione della SPA è in [web-ui.md](web-ui.md).

Le API di lettura e scrittura UserFS e i rischi del servizio privo di autenticazione sono descritti in [files.md](files.md).

Le API opzionali di esecuzione, attive solo con Lua e Program Manager disponibili, sono documentate in [program-api.md](program-api.md). Non aggiungono autenticazione o TLS.
