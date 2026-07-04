# librashader C headers (vendored)

The two headers in `include/` are the MIT-licensed C API headers of
[librashader](https://github.com/SnowflakePowered/librashader), vendored verbatim from
upstream tag `librashader-v0.11.3` (commit `cde6ad63b034dba0621b6a69d3654a79fe34a058`),
API version `LIBRASHADER_CURRENT_VERSION = 5`, shared-library ABI
`LIBRASHADER_CURRENT_ABI = 2`. Only the headers are vendored — the librashader library
itself (MPL-2.0 OR GPL-3.0) is loaded dynamically at runtime via `librashader_ld.h`
(`librashader.dll` / `librashader.so` / `librashader.dylib`), so there is no build-time
dependency. To update: replace `include/librashader.h` and `include/librashader_ld.h`
wholesale with the files from a newer upstream tag (never patch them locally), update the
tag/commit/version numbers in this README, and rebuild.
