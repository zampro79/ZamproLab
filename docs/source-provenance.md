# Source provenance and visual identity review

The owner selected the MIT license in [LICENSE](../LICENSE) for original ZamproLab code, including original browser code, scripts, Lua samples and original documentation. MIT permits commercial use and requires preservation of the copyright and permission notice. The selection applies to original material for which the owner can grant rights; it does not relicense other people's work or establish the origin of an unidentified fragment. The existing copyright identifier `zampro79` is retained as documented; no additional legal name is inferred.

This is an inventory for review, not an assertion that all listed materials have been cleared for public distribution.

| Material | Classification | Evidence and remaining work |
| --- | --- | --- |
| Original project-authored code, frontend, scripts, samples and documentation | OWNER-IDENTIFIED ORIGINAL — RIGHTS SUBJECT TO REVIEW | Owner's explicit MIT selection. Identify any non-original portions before distribution; presence in Git does not establish authorship. |
| Optional BME280 and SSD1306 Lua drivers and dependent Samples | EXCLUDED FROM THIS CANDIDATE | The source-only candidate omits both drivers and Samples 08–10. This decision does not establish or change their original provenance or licensing. The general Lua, GPIO, I²C and library APIs remain available. |
| Unidentified adapted frontend or other sample fragments | ORIGIN UNCONFIRMED — REVIEW REQUIRED | Investigate any concrete third-party source and retain its terms; presence in Git is not proof of authorship. |
| Lua 5.5.1, LittleFS and ESP-IDF components | THIRD PARTY — LICENSE VERIFIED **for the stated upstream license scope** | External build dependencies are not relicensed by the project. See [third-party notices](../THIRD_PARTY_NOTICES.md), retain applicable texts and check individual component files and binary notices. |
| Spleen 5×8 glyphs | NOT INCLUDED IN THIS SOURCE CANDIDATE | The previously bundled driver and Spleen glyph data are excluded. Their historical notice is retained; this exclusion does not determine the provenance of other material. |
| Flash-linked Xtensa HAL archives | THIRD PARTY — LICENSE UNRESOLVED | Official archive identification is known; redistribution terms for these exact archive members remain unconfirmed. Binary Factory distribution is suspended. These archives are not project source. |
| Web logo and banner assets | OWNER CONFIRMED RIGHTS | `web/assets/README.md` identifies derivatives. The project owner confirmed the necessary rights to distribute the logo, banner and reference images in phase 16C; no separate trademark restriction is imposed. |
| Vector masters and intermediate/legacy artwork | EXCLUDED | The banner documentation describes tracing from reference artwork. The owner confirmed necessary distribution rights for logo, banner and reference images; select only useful material for a source snapshot. |
| Third-party JavaScript or fonts in frontend | ORIGIN UNCONFIRMED — OWNER CONFIRMATION REQUIRED | No external library is established merely by this inventory; review source files and dependencies for copied or adapted material and retain applicable notices if found. |

## Visual identity proposal

Only the build-required `web/assets/` derivatives are included. Vector masters, previews and legacy material are excluded. If the frontend requires logo or banner files, their terms must allow ordinary copying and redistribution of the frontend; do not introduce a restriction that contradicts the source distribution. The owner confirmed necessary distribution rights for the logo, banner and reference images in phase 16C. No trademark claim, graphic license or additional restriction is imposed by this document.

## Source snapshots and packaging

The packager uses Git only when the source root itself has `.git`. Otherwise `SOURCE_SNAPSHOT.json` is required and inventories the source files and SHA-256 digests. It rejects missing, extra or changed files, symlinks, and unlisted caches. Source identity and modified state in the manifest are assertions, not authenticated Git provenance. LittleFS and Lua dependencies are verified separately.

Set `IDF_PATH` to ESP-IDF 5.5.3 and `LUA_DIR` explicitly to a verified Lua 5.5.1 source directory containing `src/lua.h`, outside the snapshot; the packager does not search arbitrary paths. Acquire the pinned LittleFS component under `managed_components/joltwallet__littlefs`. Set `PYTHONDONTWRITEBYTECODE=1` or direct `PYTHONPYCACHEPREFIX` outside the source tree when running Python tools. The packager also disables bytecode for its child tools. If an unlisted cache already exists, inspect it and remove only files you own, then retry; no cleanup is automatic. Build and package destinations must be outside the source tree.

Example (use actual paths and select the correct profile):

```sh
export IDF_PATH=/path/to/esp-idf
export LUA_DIR=/path/to/lua-5.5.1
export PYTHONDONTWRITEBYTECODE=1
python3 tools/package_build.py --target esp32 --lua ON --profile classic-4mb \
  --build-dir /path/to/new-build --output /path/to/new-private-package
```

A manifest can prove consistency but not authenticity. Build dependencies and binary redistribution terms must be assessed separately. The private evaluation output is not a public release.

## Scope of source review

| Category | Evidence available | Remaining evidence |
| --- | --- | --- |
| Firmware, hardware APIs, Program Manager and Autorun | Repository history and current C files identify project changes; ESP-IDF is a separate dependency. | History alone does not establish every contributor's rights or every snippet's origin; review headers and contributions. |
| UserFS, WebFS and Lua module loader | Project implementation builds on LittleFS, ESP-IDF and Lua; dependency terms are in the notices. | Verify imported or adapted implementation fragments and exact dependency inputs. |
| Frontend, editor, Library Manager and Samples | Source files and history exist in the project; the excluded historical SSD1306 Lua module attributed Spleen. | Audit any copied snippets, third-party browser assets and driver portions; absence of a dependency declaration is not proof of original authorship. |
| Build scripts, packager and documentation | Project files are tracked; the packager obtains external build inputs and Git metadata. | Review third-party adaptations, links and dependency notices before publishing a reviewed snapshot. |

No separately bundled frontend framework has been identified by the current inventory. This observation is not a complete provenance clearance. The Spleen glyph conversion remains BSD-2-Clause, not MIT. The output of a build can incorporate other components and has separate distribution obligations.
