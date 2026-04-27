# CryTools

CryTools is a single Unreal Engine 5 editor plugin for CryEngine scene rebuilding and level utility workflows.

## What Is Included

- GRP placement import
- VEG placement import
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

### Edit Object

- **Snap Selected Meshes To Landscape**: moves selected actors with static mesh components so their pivot sits on landscape at same XY.
- **Convert Selected To Foliage**: converts selected static mesh actors into foliage instances.

### Landscape Edit

- `Landscape Smooth` contains smoothing controls.
- If landscape uses **Edit Layers**, disable them before running smooth.

## Current Placement Transform Setup

### Landscape 
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
- For this project pipeline, after creating a new landscape, reset landscape transform to:
  `Location X=0, Y=0, Z=0`.
- On default Unreal `2017x2017` landscape workflow, placement can still have visible mismatch versus original Cry map in some areas.
- Some meshes/plants may still need manual cleanup adjustment after import.

## Known Limitations

- Placement accuracy is project-specific and sensitive to source data assumptions and world setup.
- GRP/VEG are not guaranteed 100% identical to original CryEngine placement in all cases.

## License

MIT (see `LICENSE`).
