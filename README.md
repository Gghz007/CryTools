# CryTools

CryTools is a single Unreal Engine 5 editor plugin for CryEngine scene rebuilding and level utility workflows.

## What Is Included

This plugin includes:

- GRP placement import
- VEG placement import
- object editing helpers
- foliage conversion helpers
- landscape smoothing tools

You only need one plugin installed:

- `Plugins/CryTools`

There is no separate plugin you need to install for the importer UI.

## Features

- Import CryEngine `.grp` placement files into the current editor level.
- Import CryEngine vegetation `.veg` files into the current editor level.
- Resolve meshes from existing Unreal `StaticMesh` assets under **Asset Search Root**.
- Transform conversion pipeline for Cry-to-Unreal placement.
- Global placement offset / rotation adjustments for the current FarCry project pipeline.
- Convert selected placed static meshes into Unreal foliage instances.
- Snap selected static mesh actors to the landscape using their pivot.
- Landscape smoothing tools inside the `CryTools` panel.
- Dockable editor panel with grouped tools.

## Requirements

- Unreal Engine 5.x (tested with UE 5.5).
- C++ Unreal project.
- Existing Unreal static meshes available under the configured **Asset Search Root** for `.grp` / `.veg` placement import.

## Installation

1. Copy the plugin into your project as `Plugins/CryTools`.
2. Generate project files for the `.uproject` if needed.
3. Build `YourProjectEditor` in `Development Editor | Win64`.
4. Open Unreal Editor.
5. Open the panel from `Tools -> Open CryTools`.

## Usage

### Import

1. Open `Tools -> Open CryTools`.
2. Expand `Import`.
3. Set **Asset Search Root** to the folder where your Unreal static meshes live, for example `/Game/Objects`.
4. Click **Import GRP (.grp)** or **Import VEG (.veg)**.
5. The plugin will try to match Cry prefab paths to existing Unreal static meshes and spawn actors in the current level.

### Edit Object

- **Snap Selected Meshes To Landscape**: moves selected static mesh actors so their pivot sits on the landscape under the same XY.
- **Convert Selected To Foliage**: converts selected static mesh actors into Unreal foliage instances.

### Landscape Edit

- `Landscape Smooth` contains the current smoothing controls.
- If your landscape uses **Edit Layers**, disable them before running the smoothing tool.

## Matching Rules For GRP / VEG

1. Match by normalized full prefab path.
2. Try path variants.
3. Fallback by normalized short mesh name.
4. If fallback finds multiple candidates, the object is skipped.

## Important Notes

- `.grp` and `.veg` import use existing Unreal meshes. They do not recreate Cry meshes from source files during placement import.
- Material and texture recreation from Cry placement files is not the main path here; the plugin is focused on placement using existing Unreal assets.
- For this project, landscape smoothing works best when the target landscape is prepared first and edit layers are disabled.
- After creating a new Unreal landscape, reset its transform to `Location X=0`, `Y=0`, `Z=0` before using this pipeline.
- On a default Unreal `2017x2017` landscape, `.grp` / `.veg` placement is not yet a perfect 1:1 match with the original CryEngine landscape placement.
- There can still be visible offset error for some plants and mesh groups.
- In practice, some spawned vegetation or meshes may need to be selected manually and nudged into a more exact position, for example off a road, onto a road, or back into the intended cluster or location.

## Known Limitations

- Placement accuracy is still project-specific and depends on landscape sizing, world offset, and source data assumptions.
- `.grp` and `.veg` do not always line up 100 percent with the original CryEngine result on the default Unreal landscape sizing workflow.
- Some manual cleanup is still expected after import.

## License

MIT (see `LICENSE`).
