# Serial console

Connect to the identified board's UART0 at the configured speed (115200 baud by default), 8N1. The prompt is `zamprolab>`; press Enter to send commands. On ESP32-S3 use the port physically connected to UART0; native USB Serial/JTAG is not necessarily UART0. Configuring a different transport requires firmware and configuration changes.

One task reads UART0 in NORMAL and SECRET modes. NORMAL provides Backspace, insertion, left/right arrows and a volatile 16-line history using up/down arrows; an ANSI terminal is required. Tab completion is unavailable. Lines longer than 255 characters are discarded until Enter. Logs share UART and may interrupt the prompt. CR, LF and CRLF end a line; an LF immediately following CR is consumed even across a mode transition. To send an empty line immediately after a bare CR, use another CR or CRLF. Never enter secrets as command arguments: normal history retains complete lines.

`console secret-test` is a temporary diagnostic for up to 64 printable ASCII characters without echo or history; Backspace edits and Ctrl+C cancels. Overlong input is discarded. `zam_console_request_secret` delivers a response asynchronously to a callback in the console task; the pointer is valid only during that callback and the caller must protect and clear any copies.

| Command | Purpose |
| --- | --- |
| `help` | List commands |
| `version` | Firmware, ESP-IDF and target versions |
| `info` | Chip, cores, revision, configured CPU frequency, flash and PSRAM state |
| `mem` | Total/internal free heap, largest internal block, external free and minimum heap |
| `uptime` | Seconds since boot |
| `config list`, `config get <key>` | List or read settings |
| `config set <key> <value>` | Validate and save a setting |
| `config reset` | Reset only the two managed keys |
| `reboot` | Restart |
| `wifi` | Wi-Fi status/control; see [Wi-Fi](wifi.md) |
| `console secret-test` | Test secret input |
| `lua run <path>`, `lua stop`, `lua status` | Program controls when Lua is enabled |
| `storage status` | Storage status; see [storage](storage.md) |

NVS namespace `zamprolab` uses `device_name` (default `ZamproLab`, 1–32 printable ASCII characters) and `boot_message` (`on`/`off`, default `on`). Quote values containing spaces. `boot_message` controls the next boot's name message, not system logs. Changes are committed with `nvs_commit`. If NVS fails, the console remains available but configuration is unavailable; nothing is automatically erased. Diagnose full or incompatible NVS before recovery. Reported CPU frequency is configured, not dynamically measured; PSRAM presence is unknown when support is disabled.
