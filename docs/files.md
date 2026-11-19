# File Manager backend

`zam_files` mediates UserFS access; `zam_http` implements HTTP. WebFS is not exposed through these APIs. A non-blocking mutex serializes UserFS operations, so concurrent requests, including requests for different files, can return 409. Callers must hold the same lock throughout their operation.

Public paths are rooted at logical `/`, decoded once and validated. Components use ASCII letters, digits, `.`, `-`, `_`, up to 63 characters per component and 192 per path. Empty paths, `.` and `..`, repeated separators, `%`, backslashes, controls, destructive root operations, `/web`, `/user` and reserved `.zam*` names are rejected. Reserved `.zam*` entries are omitted; directory listings return at most 128 entries and fail if a visible entry cannot be represented or read.

| Method | Path | Operation |
| --- | --- | --- |
| GET | `/api/files?path=/` | JSON `path` and `entries`, including `name`, `type` and file size |
| GET | `/api/files/content?path=...` | Stream binary content |
| PUT | `/api/files/content?path=...` | Create or replace using raw body |
| POST | `/api/files/mkdir` | JSON `{"path":"/directory"}` |
| POST | `/api/files/rename` | JSON `{"from":"/vecchio","to":"/nuovo"}`; does not overwrite |
| DELETE | `/api/files?path=...` | Delete a file or empty directory |

Successful requests return 200 or 201; errors include 400, 404, 409, 412, 413, 428, 500 and 507. JSON bodies are limited to 512 bytes and files to 512 KiB; Content-Length is required. Uploads use 1 KiB blocks and downloads 2 KiB blocks. Upload requires at least content size plus 8 KiB free, but ENOSPC can still occur. Uploads use an exclusive temporary file, sync, close and rename; failed or interrupted writes do not publish partial content. Power loss may leave an inaccessible `.zamupload-*` temporary file that is not removed automatically.

`GET /api/files/content` returns a SHA-256 ETag. `PUT` with `If-Match` rejects a changed or missing file with 412; `If-None-Match: *` creates only if absent. A successful PUT returns a new ETag. Regular file DELETE requires `If-Match`: a matching ETag permits deletion, a stale ETag or missing conditional target returns 412, and a missing precondition for an existing file returns 428. A missing target without `If-Match` returns 404; empty directories do not require an ETag. Verification and deletion hold the same UserFS lock; a busy lock or Autorun reservation can return 409. ETags identify content, so a replacement with identical bytes is indistinguishable. Unconditional legacy PUT can overwrite another client; protection relies on all filesystem users respecting the lock.

Directory listings return HTTP 200 only after enumeration and directory closure succeed; read or close errors return an HTTP error instead of a successful partial list. A transport failure after transmission begins can leave the response incomplete.

When socket capacity is exhausted, the server may evict the least recently used connection; this does not remove serialization by the HTTP server or UserFS lock.

HTTP has no authentication or TLS. Reachable clients can read, change and delete UserFS files. Use trusted networks; do not expose the server to the Internet. DNS rebinding and cross-origin requests have no specific protection; permissive CORS is not enabled.

> The following validation is historical and remains in Italian.

## Verifica hardware Fase 6A

Su ESP32-D0WDQ6 rev. 1.0 sono stati verificati creazione, lettura binaria, file vuoto, sostituzione, rinomina, eliminazione e rifiuto della directory non vuota; un upload interrotto ha lasciato intatto il contenuto precedente. Sono riusciti 20 cicli upload/download, oltre 100 richieste e cinque riavvii con persistenza del file di prova. La build definitiva è stata verificata con altre 100 letture dell'elenco, verifica dell'uptime e lettura accodata durante un upload interrotto. La root UserFS risulta vuota dopo la pulizia dei soli file di prova; l'occupazione UserFS è tornata a 8.192 byte. WebFS, tabella partizioni e impostazioni NVS controllate sono rimasti invariati.

Durante lo sviluppo, una prima build con stack HTTP predefinito ha causato un reset watchdog durante l'upload. La build corretta usa stack HTTP da 8 KiB e buffer upload da 1 KiB; il problema non si è ripresentato nei test descritti. Un test ha anche osservato un timeout intermittente prima di essere ripetuto con successo. Su build definitiva l'heap API era 184.772 byte prima e 185.396 dopo 100 elenchi, minimo 165.476 e massimo blocco interno 110.592; dopo riavvio la console ha riportato 185.460 byte liberi. L'heap durante l'upload non è stato misurato. La condizione fisica di spazio insufficiente, due upload simultanei e accesso concorrente su file diversi non sono stati verificati; la semantica serializzata è descritta sopra. Non è stato eseguito un test hardware ESP32-S3.
