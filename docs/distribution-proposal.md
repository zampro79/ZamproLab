# Distribution and installation proposal

This is a proposal, not a release or an authorization to flash. Public distribution is blocked by unresolved provenance, licensing and notices. The existing `VERSION` remains the project version; historical RC tags do not certify the newer feature set. All artifacts and settings below must be verified for the exact device and package before use.

## Proposed profiles and package

| Profile | Intended configuration | Current evidence and limitation |
| --- | --- | --- |
| `classic-4mb` | ESP32, 4 MiB, Lua 49,152 B, PSRAM off | Hardware tested. |
| `s3-generic-4mb` | ESP32-S3, 4 MiB, Lua 49,152 B, RGB and PSRAM off | Software builds tested; other boards require verification. |
| `s3-rgb-16mb` | Verified S3, 16 MiB, Lua 49,152 B, GPIO48 RGB, PSRAM off | Hardware tested on one board. |
| `s3-rgb-psram-16mb` | Verified S3-N16R8, 16 MiB, Octal 8 MiB PSRAM at 80 MHz, Lua 262,144 B in PSRAM, GPIO48 RGB | Integrated Factory and WebFS exercised on one S3; UART diagnostics were unavailable. The 12S Factory also booted with that board’s installed bootloader in 12U-A; this does not validate every bootloader or the complete installation package. |

A future package per profile could contain `README.txt`, a manifest, an offset table, `bootloader.bin`, `partition-table.bin`, `factory.bin`, `webfs.bin`, and a separately marked `userfs-empty.bin` **only for expressly authorized initial provisioning**. Include complete applicable licenses and notices. The manifest should bind version, commit, dirty status, target, hardware profile, flash settings, IDF and dependency versions, partition layout, image sizes, offsets and SHA-256; check the actual bytes and configuration rather than trusting a name. No real-device backup, credentials or populated UserFS belongs in a package.

`tools/package_build.py` supports the four explicit profiles and packages bootloader, table, factory, WebFS and a freshly generated empty UserFS with a manifest, instructions and license texts. Empty UserFS is only for explicitly authorized initial provisioning. The build process and its provenance limits are described in [versioning](versioning.md). A dirty tree can include unrelated untracked reports; never equate its snapshot with a clean commit. Builds are not demonstrated bit-for-bit reproducible across independent environments. Regenerate and validate each proposed package from its final source and dependencies, including the bootloader and WebFS; assess firmware/WebFS compatibility explicitly. Hashes establish identity, not authenticity or compatibility.

## Verified layout and offsets

The Classic and verified S3 build `flasher_args.json` files identify their target-specific bootloader offsets and flash settings. WebFS and UserFS offsets and sizes below come from the shared partition table, not the app-only flash list. Check generated files and installed table again for each release/device. End offsets are exclusive.

| Image or region | Offset | Reserved size / end | Use |
| --- | ---: | ---: | --- |
| Classic bootloader | `0x1000` | before `0x8000` | Initial installation only, matching Classic build |
| S3 bootloader | `0x0` | before `0x8000` | Initial installation only, matching S3 build |
| Partition table | `0x8000` | `0x1000` / `0x9000` | Initial installation or separately authorized migration |
| NVS | `0x9000` | `0x6000` / `0xF000` | Preserve on update; may contain credentials |
| PHY | `0xF000` | `0x1000` / `0x10000` | Preserve on update |
| Factory | `0x10000` | `0x200000` / `0x210000` | Application image must fit |
| WebFS | `0x210000` | `0xC0000` / `0x2D0000` | Complete filesystem image |
| UserFS | `0x2D0000` | `0x130000` / `0x400000` | Empty image only for authorized initialization |

The examined Classic build uses DIO, 40 MHz and 4 MB; the examined verified S3 build uses DIO, 80 MHz and 16 MB. These are build-specific values, not defaults for all boards. The S3 layout leaves the rest of its 16 MB flash unallocated. WebFS is 786,432 bytes and empty UserFS 1,245,184 bytes; factory and bootloader file lengths vary by build. Filesystem mount failures do not trigger automatic formatting.

## Initial installation versus update

For an initial installation, identify chip, board, actual flash and protection/encryption state, inspect the installed layout and existing data, and arrange verified private backups and explicit authorization before overwriting anything. Select the correct bootloader, partition table, factory and WebFS, plus the matching empty UserFS image when initialization is explicitly authorized. Initial installation can destroy existing data; NVS and PHY are not included in the image list, but may already contain data or be incompatible with a migration. Do not assume an unknown board is empty. Writing the partition table is not itself a data migration.

For an update, first verify the installed table matches the proposed layout, that factory and WebFS are compatible with each other and the board, and that the installed bootloader is compatible with the candidate Factory. If bootloader compatibility cannot be established, stop. Back up and verify data and affected regions. Write only the explicitly selected compatible factory and/or WebFS images; preserve bootloader, table, NVS, PHY and UserFS, including credentials and programs. Never select `userfs-empty.bin` for an update. If layout, flash protections, image identity or compatibility are uncertain, stop and plan a separate migration; preservation cannot be promised merely from using these offsets.

## Flash tools: proposed workflow

The [Espressif Flash Download Tool guide](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32/production_stage/tools/flash_download_tool.html) describes the Windows GUI; consult the [S3 guide](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32s3/production_stage/tools/flash_download_tool.html) for the target. Select the correct chip, port, compatible flash settings and individual images with their verified offsets; select only regions authorized for the scenario. Verify checksums and device contents afterward. The tool can modify boot image flash parameters; the package procedure must specify and verify the appropriate settings and preserve intended image bytes. Do not use whole-flash erase for these workflows. `CombineBin` can fill gaps in a combined image: a contiguous image can overwrite intervening data and is unsuitable for a data-preserving update. A combined image might be evaluated separately for expressly authorized full provisioning after confirming all gaps and affected data. Windows GUI installation and this proposed procedure have not been validated in this phase.

For Linux or automation, [Espressif esptool documentation](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html) describes selecting the chip and port and writing explicit image/offset pairs. Use the version's documented command syntax, matching build flash settings and independent verification. A serial console must release the port; some boards need manual bootloader entry. Neither tool should be used to bypass encryption or protection, modify eFuses, erase the full flash or infer permissions from a backup. No flash operation is performed by this proposal.

## Licensing decisions before distribution

The owner has approved MIT for original project material; third-party terms remain separate. The excluded historical SSD1306 sample used Spleen 5×8, BSD-2-Clause; see [third-party notices](../THIRD_PARTY_NOTICES.md). Classic OLED validation passed with conditions (see SSD1306 (not included in this source candidate)); longer-term testing remains open, and independent retrieval of the pinned upstream copy was unavailable during this revision. Inventory the exact third-party code in each firmware, bootloader, WebFS and source package, verify applicable licenses, modifications and possible obligations, and prepare the complete copyright notices, license texts and any required NOTICE materials. Prior local audits are working documents, not proof that every final binary is compliant. The owner must decide recipients and distribution scope; uncertain legal questions require appropriate review. Private distribution is not automatically exempt from applicable obligations.
