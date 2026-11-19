# Integration decisions: PSRAM and UserFS modules

These decisions apply to the internal integration of the PSRAM, UserFS modules and Library Manager work. They do not authorize public distribution or a new release.

## Library deletion

The former unconditional DELETE race was addressed in phase 17B: deleting a regular file requires `If-Match`, checked under the UserFS lock. A stale ETag returns 412; an absent precondition returns 428. A busy UserFS lock or active Autorun reservation can return 409. The ETag is content-derived and cannot distinguish a replacement with identical bytes. Use trusted networks and retain verified backups.

## Bootloader and Factory

Phase 12U-A compared the installed bootloader with the 12S Factory and verified the combination statically and dynamically on the specific ESP32-S3-N16R8 used for testing. UART boot diagnostics were unavailable, and the observation does not establish compatibility for other boards or installed bootloaders. Before preserving any installed bootloader during an update, establish its compatibility with the candidate Factory and board. Stop if that compatibility is uncertain; preserve NVS, PHY and UserFS and verify backups.

## Release and distribution

Internal integration does not authorize distribution. Before a new release candidate, complete the third-party license and redistribution audit, review documentation, verify installation and update procedures, assess the remaining diagnostics and hardware limitations, and prepare and validate the exact new RC artifacts. The existing 0.2.0-rc.1 and historical tags remain unchanged.
