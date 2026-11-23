# ZamproLab — manual binary flashing guide

This guide applies to the experimental packages in `dist`, intended for **private evaluation only**. Builds and manifests were checked in a Gitless environment; these exact images and the Windows flashing procedure have not yet been hardware validated. They are not a public release.

## 1. Choose a package

| Folder | Intended board | Flash | Build settings | Lua |
| --- | --- | --- | --- | --- |
| `classic-4mb` | ESP32 Classic | 4 MB | DIO, 40 MHz; PSRAM disabled | 48 KiB |
| `s3-generic-4mb` | Compatible ESP32-S3, generic profile | 4 MB | DIO, 80 MHz; PSRAM and RGB disabled | 48 KiB |
| `s3-rgb-psram-16mb` | ESP32-S3-N16R8 compatible with the verified board | 16 MB | DIO, 80 MHz; 8 MiB octal PSRAM at 80 MHz; GRB LED on GPIO48 | 256 KiB in PSRAM |

An ESP32-S3 label alone does not establish compatibility with the RGB/PSRAM profile: check the module, flash, PSRAM, LED and GPIO. Do not mix images from different folders. Each folder includes `release-manifest.json`, `SHA256SUMS.txt`, `FLASHING.txt` and accompanying licenses.

## 2. Before writing

1. Identify the exact chip, board, flash capacity and USB/serial port. Use a data cable; close any serial monitor using the port.
2. Check flash protections and encryption, the installed partition table and bootloader compatibility. If unknown or incompatible, stop and determine an appropriate procedure.
3. Keep verified backups of data you need, especially NVS and UserFS, and the images needed for recovery. Backups may contain credentials: keep them private.
4. Choose either a **first installation** or an **update with a compatible layout**. Writing images erases and rewrites their flash regions even when the ERASE button is not used.
5. Check file names, sizes, offsets and SHA-256 in the manifest against `SHA256SUMS.txt`. In Windows PowerShell, from the profile folder, display the hashes with:

   ```powershell
   Get-ChildItem *.bin | Get-FileHash -Algorithm SHA256
   ```

   Compare each result with `SHA256SUMS.txt` before proceeding. On Linux, run `sha256sum -c SHA256SUMS.txt` from the same folder.

## 3. Image addresses

Addresses are hexadecimal and apply to the selected profile's binaries:

| File | ESP32 Classic | ESP32-S3 | Contents |
| --- | --- | --- | --- |
| `bootloader.bin` | `0x1000` | `0x0` | Target-specific bootloader |
| `partition-table.bin` | `0x8000` | `0x8000` | Partition table |
| `zamprolab-factory.bin` | `0x10000` | `0x10000` | Factory application |
| `zamprolab-webfs.bin` | `0x210000` | `0x210000` | Web interface and samples |
| `userfs-empty.bin` | `0x2D0000` | `0x2D0000` | Empty UserFS |

**`userfs-empty.bin` overwrites UserFS and its files.** Use it only when you intend to initialize that region and accept losing its existing contents. NVS (`0x9000`) and PHY (`0xF000`) are not included in these images: a first installation without a full erase does not reset existing settings.

## 4. First installation

For a new board, or after deciding to replace the contents of the affected regions:

- Select `bootloader.bin`, `partition-table.bin`, `zamprolab-factory.bin` and `zamprolab-webfs.bin` at the addresses above.
- Add `userfs-empty.bin` **only** if UserFS must be initialized and you have decided to erase data in that region. An incompatible existing UserFS does not become usable automatically.
- Confirm that the complete layout fits the physical flash. If existing data must be kept or the old partition table differs, plan backup and migration first.

## 5. Update while preserving data

Use this procedure only after verifying the board, installed layout and bootloader are compatible with the selected package and keeping verified backups:

| Select | Offset |
| --- | --- |
| `zamprolab-factory.bin` | `0x10000` |
| `zamprolab-webfs.bin` | `0x210000` |

Do not select the bootloader, partition table or empty UserFS; leave NVS and PHY untouched. If the bootloader or layout is incompatible, stop and plan a specific migration. **Do not install this WebFS alone on an incompatible older Factory**: some assets require the package's streaming-capable firmware.

## 6. Windows: Flash Download Tool

Espressif guides: [ESP32](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32/production_stage/tools/flash_download_tool.html) · [ESP32-S3](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32s3/production_stage/tools/flash_download_tool.html).

1. Download Flash Download Tool using the link in Espressif's official guide, extract it and start the Windows executable.
2. Connect the board and identify its `COM` port in Device Manager. For S3, check whether the connection uses native USB or the USB-UART converter; the two connectors can appear as different ports.
3. Select `ChipType` **ESP32** for Classic or **ESP32-S3** for S3, and `WorkMode` **Develop**. Select a `LoadMode` appropriate for the connection: the Classic guide lists `UART`; the S3 guide lists `UART` and `USB`. Confirm.
4. On `SPIDownload`, use the file selector for each authorized image, enter its correct hexadecimal offset and check that row. Uncheck every other row, including paths left from earlier sessions.
5. Select the actual `COM` port and start at `921600` baud; lower it if the connection is unstable. Check the profile's flash mode and frequency. Espressif says that since version 3.9.10 these fields are normally not editable and follow the image's build settings.
6. If available, enable `DoNotChgBin` to retain the original bytes. Without it, the tool may modify image flash parameters: check settings before proceeding. Do not use `ERASE`, which erases the entire flash. Do not use `CombineBin` for an update: it may fill gaps between images and overwrite data in between.
7. If bootloader entry is not automatic, follow the board-specific BOOT/RESET procedure. Recheck chip, port, files, checkboxes and offsets; press `START` and wait for the tool's completion indication.
8. If flashing fails or is interrupted, do not assume flash is unchanged: record the error and assess recovery from backup before retrying.

## 7. Boot and verification

After completion, restart normally if needed and check the serial log, boot without repeated resets, LAB and Settings access, Program Manager and filesystems. For an initial configuration, look for AP `ZamproLab-XXXXXX`, connect using the initial password `ZamproLab`, then open `http://192.168.4.1/`. Existing NVS settings can produce a different name or password. Change the default password using the administrative console; HTTP has no authentication or TLS, so use a trusted network.

Check that UserFS files you intended to preserve are still present. To verify written bytes, use a readback method compatible with the board and its protections and compare the actual image-length portion against its SHA-256; account for any partition padding. `FINISH` is the tool's reported result, not an independent verification of the contents.

## 8. Troubleshooting

- **Port missing or busy:** check the data cable, driver, Device Manager and open serial applications.
- **Bootloader connection fails:** check port and chip, use the board's BOOT/RESET sequence and try a lower baud rate.
- **Boot loop or no LAB:** stop, keep logs and check target, offsets, power, flash settings and bootloader/Factory/WebFS compatibility. Do not erase NVS or UserFS as a trial.
- **Filesystem does not mount:** verify layout and image; do not initialize UserFS if it contains data to preserve.

Profile-specific Linux `esptool.py` commands are included in each `FLASHING.txt`; check the syntax and tool version before use. No flash was performed to prepare this guide.
