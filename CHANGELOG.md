## Next

### Changed

* Lock repository during backup

## 0.5.1

2023-07-30

### Fixed

* Display user prompt properly on some musl systems

## 0.5.0

2023-01-29

### Added

* Integrity check command
* Musl support

## 0.4.0

2020-08-30

### Changed

* Major codebase overhaul. This includes a memory management rewrite, with parts factored out into
  its own library. See [third-party/CRegion](third-party/CRegion/README.md)
* Reworked documentation

### Fixed

* Valgrind warnings
* ASAN and UBSAN warnings
* Compilation with newer, stricter compilers
* Unaligned memory access (only relevant on some platforms)

## 0.3.0

2017-07-11

This release contains **breaking changes**. Migration guide:

* Restore all files from the repository to the host system
* Copy the repositories config file into a new repository (empty directory)
* Do a fresh backup

### Changed

* Moved from SHA-1 to [BLAKE2](https://www.blake2.net)
* Moved from Murmur2 to [SipHash](https://github.com/veorq/SipHash)

### Removed

* Dependency on OpenSSL
* Dependency on pkg-config

## 0.2.0

2017-06-18

### Added

* Manpage
* Config file: allow comment lines starting with `#`

### Changed

* Switched to MIT license
* Stricter checks for config files. Ambiguous search patterns now cause an error when they match the
  same file
* Improved detection for corrupted metadata
* Improved change summary

## 0.1.1

2017-04-18

### Fixed

* Deduplication: prevent specific files from being stored multiple times

## 0.1.0

2016-10-27

### Added

* Backup
* Restore
* `gc` command
* Config file: warn about search patterns which don't match any files
