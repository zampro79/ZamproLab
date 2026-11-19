# Lua modules in UserFS

Each RUN creates a fresh Lua state. A program may import a pure Lua module from UserFS:

```lua
local utils = require("lib.utils")
print(utils.hello("ZamproLab"))
```

The name `lib.utils` maps to `/lib/utils.lua` relative to UserFS. Each dot-separated component must start with an ASCII letter or underscore, followed by ASCII letters, digits or underscores. Absolute paths, slashes, empty components and other characters are rejected. The existing UserFS path validation also applies. The file must be a regular text-mode Lua file, at most `CONFIG_ZAM_LUA_SOURCE_LIMIT` bytes (32 KiB by default).

In LAB's file panel, choose **Import Library**, select a local `.lua` file and confirm its destination. The default is `/lib/<filename>.lua`; missing destination folders are created in UserFS. The confirmation and result show the corresponding `require("lib.utils")` name. A destination already present is replaced only after explicit confirmation and an ETag check; an open file with unsaved changes cannot be replaced. The browser reads back the uploaded bytes before reporting success. A lost connection during upload can leave the outcome unconfirmed: inspect the destination before trying again.

## Installed libraries

In the File Manager, choose **Libraries** to browse Lua modules under `/lib`. An absent `/lib` is distinct from an existing empty directory; importing a library can create the required destination folders. Returning to UserFS returns to `/`. Subfolders can be opened to find nested modules; each candidate shows its `require("lib.name")` expression. The list contains installed files, not the modules loaded in a running Lua state. The view excludes Lua bytecode signatures; syntax, dependencies and available memory still determine whether a candidate can load.

Select a library to open it in the existing editor, or use its Delete control and confirm the library name. Unsaved editor changes require confirmation. Deleting a module can break programs that require it; no dependent files are removed. File deletion requires If-Match; the server checks it under the UserFS lock. A stale ETag returns 412, a missing precondition returns 428, and a busy lock can return 409. ETags identify content and cannot distinguish an identical-content replacement. Use **Import Library** to add a module; it is not executed automatically.

Modules execute once per RUN and are cached by name for that Lua state. On first load, `require` also returns the UserFS path as its second result; cached loads return only the module. A module that returns nil is cached as `true`; a module that returns false is reloaded on the next `require`. Existing built-in libraries such as `string` remain available through `require`. Circular dependencies fail explicitly. A later RUN starts with a fresh cache and sees saved file changes.

The loader reads under the existing file-manager lock in small chunks and compiles using Lua's allocator. A busy UserFS, missing or oversized module, invalid name, syntax or runtime error fails the RUN. Lua's existing memory limit and STOP behavior continue to apply. The loader does not expose `package`, general file APIs, native libraries or external search paths. Libraries that depend on an operating system, native extensions or an unrestricted Lua environment may not work.

## Compatibility and memory

The Classic and generic ESP32-S3 profiles normally limit the Lua VM to 49,152 bytes; the actual configured limit is reported by the runtime. Module compilation and module data share this limit with the program. The source snapshot and other system resources are outside the Lua VM quota. Modules can fail with a memory error even when their source is below the file-size limit.

The sandbox disables `pcall` and `xpcall` as well as dynamic `load`, `loadfile`, `dofile` and `collectgarbage`. The `io`, `os`, `debug` and `package` libraries and dynamic native modules are not exposed. Pure Lua libraries that depend on these facilities may need adaptation and are not automatically compatible. For example, the upstream `inspect.lua` 3.1.0 tested during development cannot load unchanged because it calls `pcall`. These restrictions do not establish a security boundary.

## Installing compatible libraries

Peripheral-specific drivers are not included. Install compatible Lua libraries into UserFS and respect each library's own license. For example, place `esempio.lua` in `UserFS/lib/` and load it with `local esempio = require("lib.esempio")`. General I²C and GPIO APIs remain available.
