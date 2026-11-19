# Release readiness

## Source-only preparation — historical review

The project remains private. Source-only preparation is under review; it is **not** permission to publish the current repository or its history. The owner selected MIT for original ZamproLab source, including original frontend, scripts, samples and documentation. Third-party and adapted material retains its own terms; individual adapted source fragments still require review. See [LICENSE](../LICENSE), [third-party notices](../THIRD_PARTY_NOTICES.md) and [source provenance](source-provenance.md).

Redistribution of precompiled Factory binaries remains suspended pending clarification of the terms for the flash-linked Xtensa HAL archives. This is an evidence gap, not a finding of a prohibition. Header or ROM notices do not establish the terms for those archives. Source-only distribution avoids delivering those binaries but does not settle all source or asset rights.

Classic and one S3-N16R8 were tested on hardware, including the explicit PSRAM profile and WebFS streaming. Validation is board- and scenario-specific; S3 external GPIO/I²C hardware and every board variant are not validated. HTTP has no authentication or TLS. Build results are not proof of bit-for-bit reproducibility. Before a future release, resolve the applicable provenance and notices, review the graphics and publication scope, verify the source-only build, packaging and security, and obtain publication authorization.

## Historical source-only consolidation gate

The current source tree is being prepared for a reviewed snapshot without Git history. Do not include Factory images, HAL archives, flash dumps, private backups or populated UserFS. The embedded branding is a required build input whose redistribution rights remain to be established; large masters and legacy artwork need an explicit inclusion decision. Review the full snapshot and history separately: a clean tree alone does not establish provenance. The packager accepts a Gitless snapshot with a manifest; [source provenance](source-provenance.md#source-snapshots-and-packaging) describes the procedure and limits. No publication or release is authorized by this preparation.

## Historical baseline — phase 14D

The following records the conditions and results at that time. References to an unvalidated S3, a pending project license or the old font describe that historical state, not the current project.

### Historical readiness summary

Version source and packaging are described in [versioning.md](versioning.md). This is development work, not a release authorization. Before a private release candidate, review the working tree and source snapshot, independently verify dependency and toolchain provenance, run the required target builds, compare reproducibility results, validate the image and manifest checksums, decide the project license and third-party notices, and resolve the existing SSD1306 font provenance. ESP32-S3 N16R8 is not hardware validated. Build success does not constitute hardware validation or authorization to distribute.

The proposed future identifier is `0.1.0-rc.1`; `VERSION` remains `0.1.0-dev`. No tag or release is created here. Preserve NVS and UserFS; never include device dumps or empty UserFS in a release package. Review actual package contents and recipients before distribution.

## Phase 14D results

Five isolated builds succeeded with ESP-IDF 5.5.3, `SOURCE_DATE_EPOCH=1700000000`, and the same dirty source snapshot `8bbb5edd0bb630488241228b40ba534575896addc80c6986b6a9ca4e1afc4749` based on commit `f6ba60a84b5c35630418ce151f4e706b349c1be9`. ESP32 ON A/B factory size was 1,077,696 bytes; ESP32 OFF 904,160; ESP32-S3 ON 1,071,632; S3 OFF 901,424. All five package copies passed size and SHA-256 checks; all WebFS images were 786,432 bytes with SHA-256 `b86c5d04b3376d122dcd6e8884559f7ab9390507e197f804dfad9a3852f201d5`. The historical package directories were kept outside the repository; build directories can contain an empty UserFS image and are not release packages.

ESP32 ON factory A SHA-256: `9f602d316b87c02530f9b204c1577b682416d7e1bc9ec4c0214b80ea1e63660a`; B: `7e0f8fc232a723146e24520d03cbeda7f79879f99a1c1a987d1281fef09e2e72`. Bootloader and partition table hashes matched, as did WebFS. Factory binary comparison found only bytes 176–207 and the final 33 bytes differ; the first range is the embedded ELF SHA-256, verified against build A's ELF, and the last includes the image digest. Build A's ELF contains its absolute build path; different build directories are a plausible cause of ELF hash differences, not a proven exhaustive explanation. Firmware is therefore **not bit-for-bit reproducible** in this comparison. The binary loadable payload outside these ranges matched; this does not establish identical ELF debug information or general reproducibility.

The source snapshot above describes the exact pre-build working tree; the later addition of this results section and `tests/test_package_build.py` changes the current dirty snapshot. The packages do not claim to represent a clean commit or the subsequent documentation changes. For release, rebuild after all edits are complete. A failed parallel attempt hit temporary filesystem quota; its partial build trees were removed after inspection, while successful packages and logs were retained.

Host tests passed for version parsing, dirty state, commit shape, SHA-256, missing files, rejection of unsupported configurations and existing paths, Lua sample bundling and syntax, BME280 reference vector, SSD1306 simulated frames and error propagation. The packaging script checks four required output files, name uniqueness, version, supported configuration, size and copied hashes before writing the manifest. The failure paths for every possible missing/duplicate artifact have not been independently injected; production use requires further negative-path coverage. No hardware flash was performed; the version endpoint implementation has build coverage, not hardware validation. S3 remains untested on hardware.

After the five-build comparison, dependency fingerprint checks were added to the packager and a further isolated ESP32 Lua ON build succeeded. Its `package-final` manifest records dirty snapshot `2aad93d392b4780d3815efc29451a7807463a44303b9e9c6d33409aee49e81aa`, checksums for Lua and LittleFS, and factory SHA-256 `0f47edb0e6e26fc2536e5e31b457616dbaea0fdbcac5747df9b091d0b595e4f8`; WebFS and bootloader/table hashes remain as above. It also predates this final paragraph. The earlier S3 and OFF builds exercised the same firmware version change but the prior packaging script; repeat those combinations using the final packager before approval.

## Phase 14D.1 — final packager validation

Four fresh firmware builds and packages, and a second independent ESP32 ON build, completed with the updated packager and `SOURCE_DATE_EPOCH=1700000000`. All packages identify commit `f6ba60a84b5c35630418ce151f4e706b349c1be9`, `git_dirty: true`, and pre-build source snapshot `43aef33666d5797c9c71c0f9786fd792e8805642e8f3c662c1183f71fa8f8808`. The later addition of a Git-status negative test and this documentation changes the current snapshot; the manifests correctly describe the earlier builds, not a clean commit or current source tree. Build and packaging inputs must be rebuilt after approval and subsequent edits.

| Configuration | Factory bytes | Factory SHA-256 |
|---|---:|---|
| ESP32 ON A | 1,077,696 | `fbb7d74621bfccabd6f0edeb74f07f8ede8623018acbf3659a04be21aa6fbfff` |
| ESP32 ON B | 1,077,696 | `9593a051c84b63a0440027865602e644cf0818063d2c6574ae8844b090f772f8` |
| ESP32 OFF | 904,160 | `25305391464f3d6595d3bdcdde79a44614ca494fdc3f4e7b94b7b2b783a1fbca` |
| ESP32-S3 ON | 1,071,632 | `435add700cbe9e90b71986801335ddf363ae45cdd8b8db8c40ee4b561cdea59c` |
| ESP32-S3 OFF | 901,424 | `4c7de40e07a5782ad25ca94cc96c5d7708346a98402eede2b4a468013ea3c9cc` |

Every package includes only bootloader, table, factory, WebFS and manifest, with copied size/hash verification. Factory offset `0x10000`, WebFS `0x210000`, table `0x8000`; bootloader `0x1000` Classic and `0x0` S3. All WebFS hashes are `b86c5d04b3376d122dcd6e8884559f7ab9390507e197f804dfad9a3852f201d5`. Historical artifacts were kept outside the repository; build trees are not release packages and can include an empty UserFS image. No device data were captured.

A/B have matching bootloader, table and WebFS, but different factory hashes. The 1,077,696-byte factories differ only at bytes 176–207 (ELF SHA-256 in `esp_app_desc`) and 1,077,663–1,077,695 (image checksum/digest). Each embedded ELF digest matches the corresponding actual ELF. Both ELFs contain their respective absolute build paths; after `objcopy --strip-debug`, both ELF SHA-256 values match `8289b9a06304481fe0e1e5d24542ab3a1b7ac39714ba8e998565065d17eea6f8`. The debug data and paths explain why an ELF digest can vary, but no exhaustive proof attributes every debug difference exclusively to paths. Classification **B: differing but circumscribed**, not bit-for-bit reproducible. Timestamp was fixed for the comparison, not a guarantee for other environments.

Lua, allocator, simulated GPIO/I²C, BME280, SSD1306, Program Manager, HTTP, storage mount and Samples host regressions passed. `test_package_build.py` and `test_package_negative.py` passed, including clean/modified/untracked temporary Git fixtures and controlled negative cases. Permission errors were simulated; arbitrary malicious binary authenticity and every filesystem race were not tested. Missing components, wrong configuration or offsets do not create a valid manifest in the tested fixtures. Full partition-table MD5 and independent image-checksum validation are not implemented; see `versioning.md`. IDF/toolchain and upstream dependencies require independent authenticity verification. Licenses and current font provenance remain open. No flash or hardware validation was performed; ESP32-S3 N16R8 remains unvalidated.

**Status: B, reviewable with limitations.** The implementation can be reviewed for a possible commit, but it is not a release or an assertion of licensing compliance. Before RC, resolve the licensing/font blockers, complete any required artifact-integrity hardening, repeat release builds from the final approved source and verify their actual manifest and recipients.
