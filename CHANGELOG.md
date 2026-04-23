# Changelog

## [1.1.0] - 2026-04-05

### Added
- Plugin panel window with import/export sections.
- Configurable `Asset Search Root` for mesh discovery.
- JSON export of selected actors.
- Scale import from GRP (`Scale` attribute).

### Changed
- Mesh lookup now prioritizes normalized full prefab path.
- Added robust name/path normalization for matching.
- Rotation conversion moved to matrix/quaternion workflow.

### Fixed
- Wrong mesh spawned when names collide in different folders.
- Multiple import misses caused by dot/dash naming differences.


