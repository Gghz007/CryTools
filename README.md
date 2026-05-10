# CryTools

CryTools is a single Unreal Engine 5 editor plugin for CryEngine scene rebuilding and level utility workflows.

## What Is Included

- GRP placement import
- VEG placement import
- embedded CryEngine EntityLibrary prototype-to-model database for GRP `PrototypeEntity` objects
- object edit helpers
- foliage conversion helpers
- landscape smoothing tools

You only need one plugin installed:

- `Plugins/CryTools`

There is no separate plugin required for importer UI.

## Requirements

- Unreal Engine 5.x (tested with UE 5.5).
- C++ Unreal project.
- Existing Unreal static meshes under configured **Asset Search Root** for `.grp` / `.veg` placement import.
- Imported Cry meshes should keep names close to original `.cgf` / `.cga` / `.bld` filenames so placement import can resolve them.

## Installation

1. Copy plugin to `Plugins/CryTools`.
2. Generate project files if needed.
3. Build `YourProjectEditor` (`Development Editor | Win64`).
4. Open Unreal Editor.
5. Open panel from `Tools -> Open CryTools`.

## Usage

### Import

1. Open `Tools -> Open CryTools`.
2. Expand `Import`.
3. Set **Asset Search Root** (for example `/Game/Objects`).
4. Click **Import GRP (.grp)** or **Import VEG (.veg)**.

### GRP Entity Import

GRP import supports several CryEngine object sources:

- `Brush` objects using the `Prefab` attribute.
- Entity objects with direct model fields such as `objModel`, `fileModel`, `object_Model`, `fileGunModel`, `fileObject`, and related model attributes.
- `PrototypeEntity` objects that only store a `Prototype` GUID and scene instance `Name`.

For `PrototypeEntity`, CryTools uses an embedded database generated from Far Cry `Editor/EntityLibrary` XML files. The database maps:

- `Prototype Id -> Cry model path`
- `Entity Name -> Cry model path`

Example:

- GRP instance: `Name="Cafeteria.food_pizza0"`
- Entity prototype name: `Cafeteria.food_pizza`
- Embedded model path: `Objects\Indoor\props\Cafeteria\plate_pizza1.cgf`

The numeric suffix at the end of a GRP object name is treated as an outliner/instance number. If GUID lookup fails, CryTools removes that suffix and tries the base entity name.

The embedded database is stored in:

- `Source/CryImporter/Private/CryEntityPrototypeModels.inl`

The plugin does not require the original Far Cry Editor XML files at runtime for these built-in mappings. If the original `Editor/EntityLibrary` folder is present, CryTools can also read it as an extra/fallback source.

If a map was built with custom Far Cry Editor entity prototypes, those custom mappings must be added manually. Add the custom entity `Name`, `Prototype Id`, and Cry model path to the embedded table, or provide the original custom `Editor/EntityLibrary/*.xml` files where CryTools can read them as a fallback source.

### Edit Object

- **Snap Selected Meshes To Landscape**: moves selected actors with static mesh components so their pivot sits on landscape at same XY.
- **Convert Selected To Foliage**: converts selected static mesh actors into foliage instances.

### Landscape Edit

- `Landscape Smooth` contains smoothing controls.
- If landscape uses **Edit Layers**, disable them before running smooth.

## Current Placement Transform Setup

### Landscape

- Before import, reset Landscape transform to world zero on all axes.
- Global XY offset: `X=-0`, `Y=-0`
- Global Z offset: `Z=-0`

### GRP

- Global group yaw: `+90` (Z axis)
- Global XY offset: `X=-1386`, `Y=-566`
- Global Z offset: `Z=-12848`
- Local object transforms are composed with parent hierarchy (`Parent` / `Id`) so child local pos/rot/scale stay relative to parent.

### VEG

- Uses global placement pipeline with project offset:
- `X=-1386`, `Y=-566`, `Z=-12848`
- Uses global group yaw conversion path.

## Important Notes

- `.grp` / `.veg` import uses existing Unreal static meshes. It does not rebuild Cry source meshes during placement import.
- GRP entity/prototype import resolves Cry model paths to existing Unreal static meshes under **Asset Search Root**. If a mapped `.cgf` has not been imported to Unreal, that object will be skipped and reported in Output Log.
- For this project pipeline, after creating a new landscape, always reset landscape transform to:
  `Location X=0, Y=0, Z=0`.
- On default Unreal `2017x2017` landscape workflow, placement can still have visible mismatch versus original Cry map in some areas.
- Some meshes/plants may still need manual cleanup adjustment after import.

## Known Limitations

- Placement accuracy is project-specific and sensitive to source data assumptions and world setup.
- GRP/VEG are not guaranteed 100% identical to original CryEngine placement in all cases.
- Some CryEngine entities are gameplay helpers, triggers, AI markers, particles, or logic objects and may not have a static mesh model. These are intentionally skipped by placement import.
- Character, weapon, vehicle, physics, and animation behavior is not recreated. The importer places available static mesh representations only.

## License

MIT (see `LICENSE`).
