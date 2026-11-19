# WebFS JavaScript assets

The HTTP server exposes fixed routes for `highlight.js`, `terminal.js`, `settings.js` and `app.js`; `file_manager.js` is a source file and has no separate route. `tools/bundle_samples.py` produces the packaged assets: it prepends sample libraries to `highlight.js`, prepends sample programs to `terminal.js`, and appends `web/file_manager.js` to `settings.js`. `tools/build_storage_images.sh` checks and packs those generated files into WebFS.

The HTML loads deferred scripts in this order: `highlight.js`, `terminal.js`, `settings.js`, `app.js`. Highlight defines library import; terminal supplies its byte-reading and directory helpers before import can be invoked; the bundled settings asset supplies File Manager functions before app initialization. Changing this order or serving the unbundled `web/settings.js` on its own will break File Manager initialization.

The existing firmware limits each static asset to 16,384 bytes. The storage-image builder verifies that limit on the generated assets. Source and bundled sizes differ because sample data and File Manager code are included during packaging; check the generated sizes for each change.
