#include "CryImporterModule.h"

#include "AssetToolsModule.h"
#include "AssetImportTask.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InstancedFoliageActor.h"
#include "Components/StaticMeshComponent.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "IDesktopPlatform.h"
#include "Input/Reply.h"
#include "JsonObjectConverter.h"
#include "Math/Transform.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/FileHelper.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Modules/ModuleManager.h"
#include "Landscape.h"
#include "LandscapeEdit.h"
#include "LandscapeProxy.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Subsystems/UnrealEditorSubsystem.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "XmlParser.h"

#define LOCTEXT_NAMESPACE "FCryImporterModule"

namespace CryImporter
{
    static const FName MainTabName(TEXT("CryImporter.MainTab"));
    static constexpr bool bUseLegacyRotationFallback = false;
    // Pipeline helper: apply one global rotation to every spawned placement actor.
    static constexpr float GlobalYawOffsetDeg = 90.0f;
    // Project-specific world offset so imported Cry placements line up with the shifted UE landscape.
    static const FVector GlobalPlacementOffset(-1386.0f, -566.0f, -12848.0f);

    static FString NormalizePathKey(FString InPath)
    {
        InPath.ReplaceInline(TEXT("\\"), TEXT("/"));
        InPath = InPath.ToLower();

        while (InPath.RemoveFromStart(TEXT("/")))
        {
        }

        while (InPath.RemoveFromStart(TEXT("./")))
        {
        }

        if (InPath.EndsWith(TEXT(".cgf")))
        {
            InPath = FPaths::GetPath(InPath) / FPaths::GetBaseFilename(InPath);
        }

        // Unreal assets often normalize dots/dashes/spaces in names.
        InPath.ReplaceInline(TEXT("."), TEXT("_"));
        InPath.ReplaceInline(TEXT("-"), TEXT("_"));
        InPath.ReplaceInline(TEXT(" "), TEXT("_"));

        while (InPath.Contains(TEXT("__")))
        {
            InPath.ReplaceInline(TEXT("__"), TEXT("_"));
        }

        if (InPath.StartsWith(TEXT("game/")))
        {
            InPath.RightChopInline(5, EAllowShrinking::No);
        }

        return InPath;
    }

    static FString NormalizeNameKey(FString InName)
    {
        InName = InName.ToLower();
        InName.ReplaceInline(TEXT("."), TEXT("_"));
        InName.ReplaceInline(TEXT("-"), TEXT("_"));
        InName.ReplaceInline(TEXT(" "), TEXT("_"));

        while (InName.Contains(TEXT("__")))
        {
            InName.ReplaceInline(TEXT("__"), TEXT("_"));
        }

        return InName;
    }

    static FString SanitizeAssetSearchRootPath(FString InPath)
    {
        InPath = InPath.TrimStartAndEnd();
        InPath.ReplaceInline(TEXT("\\"), TEXT("/"));

        if (InPath.IsEmpty())
        {
            return TEXT("/Game/Objects");
        }

        if (!InPath.StartsWith(TEXT("/")))
        {
            InPath = TEXT("/") + InPath;
        }

        if (!InPath.StartsWith(TEXT("/Game"), ESearchCase::IgnoreCase))
        {
            FString Stripped = InPath;
            Stripped.RemoveFromStart(TEXT("/"));
            InPath = TEXT("/Game/") + Stripped;
        }

        while (InPath.EndsWith(TEXT("/")) && InPath.Len() > 1)
        {
            InPath.LeftChopInline(1, EAllowShrinking::No);
        }

        return InPath;
    }

    static FString SanitizeImportSaveRootPath(FString InPath)
    {
        InPath = InPath.TrimStartAndEnd();
        InPath.ReplaceInline(TEXT("\\"), TEXT("/"));

        if (InPath.IsEmpty())
        {
            return TEXT("/Game/CryImported");
        }

        if (!InPath.StartsWith(TEXT("/")))
        {
            InPath = TEXT("/") + InPath;
        }

        if (!InPath.StartsWith(TEXT("/Game"), ESearchCase::IgnoreCase))
        {
            FString Stripped = InPath;
            Stripped.RemoveFromStart(TEXT("/"));
            InPath = TEXT("/Game/") + Stripped;
        }

        while (InPath.EndsWith(TEXT("/")) && InPath.Len() > 1)
        {
            InPath.LeftChopInline(1, EAllowShrinking::No);
        }

        return InPath;
    }

    static FString SanitizeAssetName(FString InName)
    {
        InName = InName.TrimStartAndEnd();
        if (InName.IsEmpty())
        {
            return TEXT("CryAsset");
        }

        for (int32 Index = 0; Index < InName.Len(); ++Index)
        {
            const TCHAR Char = InName[Index];
            if (!FChar::IsAlnum(Char) && Char != TCHAR('_'))
            {
                InName[Index] = TCHAR('_');
            }
        }

        while (InName.Contains(TEXT("__")))
        {
            InName.ReplaceInline(TEXT("__"), TEXT("_"));
        }

        while (InName.StartsWith(TEXT("_")))
        {
            InName.RightChopInline(1, EAllowShrinking::No);
        }

        while (InName.EndsWith(TEXT("_")))
        {
            InName.LeftChopInline(1, EAllowShrinking::No);
        }

        if (InName.IsEmpty())
        {
            InName = TEXT("CryAsset");
        }

        if (!FChar::IsAlpha(InName[0]))
        {
            InName = TEXT("A_") + InName;
        }

        return InName;
    }

    static void AddUniqueString(TArray<FString>& Values, const FString& Value)
    {
        if (!Value.IsEmpty())
        {
            Values.AddUnique(Value);
        }
    }

    static bool IsCollisionLikeMarker(const FString& InValue)
    {
        const FString Lower = InValue.ToLower();
        return Lower.Contains(TEXT("nodraw"))
            || Lower.Contains(TEXT("no_draw"))
            || Lower.Contains(TEXT("mat_nodraw"))
            || Lower.Contains(TEXT("mat_obstruct"))
            || Lower.Contains(TEXT("obstruct"));
    }

    static void AddTagIfMissing(AActor* Actor, const FString& InTagValue)
    {
        if (!Actor || InTagValue.IsEmpty())
        {
            return;
        }

        const FName TagName(*InTagValue);
        Actor->Tags.AddUnique(TagName);
    }

    static bool TryGetLandscapeCenterXY(UWorld* World, FVector2D& OutCenterXY)
    {
        if (!World)
        {
            return false;
        }

        bool bFoundAny = false;
        double BestArea = -1.0;
        FVector BestCenter = FVector::ZeroVector;

        for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
        {
            ALandscapeProxy* Proxy = *It;
            if (!IsValid(Proxy))
            {
                continue;
            }

            const FBox Bounds = Proxy->GetComponentsBoundingBox(true);
            if (!Bounds.IsValid)
            {
                continue;
            }

            const FVector Size = Bounds.GetSize();
            const double Area = static_cast<double>(Size.X) * static_cast<double>(Size.Y);
            if (Area > BestArea)
            {
                BestArea = Area;
                BestCenter = Bounds.GetCenter();
                bFoundAny = true;
            }
        }

        if (!bFoundAny)
        {
            return false;
        }

        OutCenterXY = FVector2D(BestCenter.X, BestCenter.Y);
        return true;
    }

    static bool TryGetLandscapeCenterXYForProxy(ALandscapeProxy* Proxy, FVector2D& OutCenterXY)
    {
        if (!IsValid(Proxy))
        {
            return false;
        }

        const FBox Bounds = Proxy->GetComponentsBoundingBox(true);
        if (!Bounds.IsValid)
        {
            return false;
        }

        const FVector Center = Bounds.GetCenter();
        OutCenterXY = FVector2D(Center.X, Center.Y);
        return true;
    }

    static FString ExtractCryPrefabPathFromDiskPath(const FString& InFilePath, const FString& InGameRoot)
    {
        FString Normalized = InFilePath;
        Normalized.ReplaceInline(TEXT("\\"), TEXT("/"));

        const FString Lower = Normalized.ToLower();
        const int32 ObjectsIdx = Lower.Find(TEXT("/objects/"));
        if (ObjectsIdx != INDEX_NONE)
        {
            return Normalized.Mid(ObjectsIdx + 1);
        }

        if (!InGameRoot.IsEmpty())
        {
            FString GameRoot = InGameRoot;
            GameRoot.ReplaceInline(TEXT("\\"), TEXT("/"));
            while (GameRoot.EndsWith(TEXT("/")))
            {
                GameRoot.LeftChopInline(1, EAllowShrinking::No);
            }

            const FString LowerRoot = GameRoot.ToLower();
            if (Lower.StartsWith(LowerRoot))
            {
                FString Relative = Normalized.Mid(GameRoot.Len());
                while (Relative.StartsWith(TEXT("/")))
                {
                    Relative.RightChopInline(1, EAllowShrinking::No);
                }

                return Relative;
            }
        }

        return FPaths::GetCleanFilename(InFilePath);
    }

    static FString BuildAssetPathKey(const FAssetData& Asset)
    {
        return NormalizePathKey(Asset.PackagePath.ToString() / Asset.AssetName.ToString());
    }

    static TArray<FString> BuildPrefabPathKeyVariants(const FString& PrefabPath)
    {
        TArray<FString> Keys;

        const FString Normalized = NormalizePathKey(PrefabPath);
        AddUniqueString(Keys, Normalized);

        const FString PathPart = FPaths::GetPath(Normalized);
        const FString BaseName = FPaths::GetBaseFilename(Normalized);
        AddUniqueString(Keys, PathPart / NormalizeNameKey(BaseName));

        FString WithoutObjects = Normalized;
        if (WithoutObjects.RemoveFromStart(TEXT("objects/")))
        {
            AddUniqueString(Keys, WithoutObjects);
        }

        return Keys;
    }

    static TArray<FString> BuildShortNameVariants(const FString& PrefabPath)
    {
        TArray<FString> Keys;
        const FString BaseName = FPaths::GetBaseFilename(PrefabPath);

        AddUniqueString(Keys, BaseName.ToLower());
        AddUniqueString(Keys, NormalizeNameKey(BaseName));

        return Keys;
    }

    static bool TryResolveMeshByCryPath(
        const FString& PrefabPath,
        const TMap<FString, UStaticMesh*>& MeshByPathKey,
        const TMultiMap<FString, UStaticMesh*>& MeshByShortName,
        UStaticMesh*& OutMesh)
    {
        OutMesh = nullptr;

        const TArray<FString> PathKeys = BuildPrefabPathKeyVariants(PrefabPath);
        for (const FString& PathKey : PathKeys)
        {
            if (UStaticMesh* const* FoundByPath = MeshByPathKey.Find(PathKey))
            {
                OutMesh = *FoundByPath;
                return true;
            }
        }

        TArray<UStaticMesh*> CandidatesUnique;
        const TArray<FString> NameKeys = BuildShortNameVariants(PrefabPath);
        for (const FString& NameKey : NameKeys)
        {
            TArray<UStaticMesh*> Candidates;
            MeshByShortName.MultiFind(NameKey, Candidates);
            for (UStaticMesh* Candidate : Candidates)
            {
                CandidatesUnique.AddUnique(Candidate);
            }
        }

        if (CandidatesUnique.Num() == 1)
        {
            OutMesh = CandidatesUnique[0];
            return true;
        }

        if (CandidatesUnique.Num() > 1)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("CryImporter: Ambiguous short-name match for '%s' (%d candidates). Skipping object."),
                *FPaths::GetBaseFilename(PrefabPath),
                CandidatesUnique.Num());
        }

        return false;
    }

    static bool TryParseFloat(const FString& InValue, float& OutValue)
    {
        if (InValue.IsEmpty())
        {
            OutValue = 0.0f;
            return false;
        }

        if (!FDefaultValueHelper::ParseFloat(InValue, OutValue))
        {
            OutValue = 0.0f;
            return false;
        }

        return true;
    }

    static bool ParseVector3Csv(const FString& InValue, FVector& OutVector)
    {
        TArray<FString> Values;
        InValue.ParseIntoArray(Values, TEXT(","), true);

        if (Values.Num() != 3)
        {
            OutVector = FVector::ZeroVector;
            return false;
        }

        OutVector.X = FCString::Atof(*Values[0]);
        OutVector.Y = FCString::Atof(*Values[1]);
        OutVector.Z = FCString::Atof(*Values[2]);
        return true;
    }

    static FVector ConvertCryPositionToUE(const FVector& CryPos)
    {
        return FVector(CryPos.X * 100.0f, -CryPos.Y * 100.0f, CryPos.Z * 100.0f);
    }

    static FVector ApplyGlobalGroupYawToPosition(const FVector& LocalUEPosition)
    {
        return FRotator(0.0f, GlobalYawOffsetDeg, 0.0f).RotateVector(LocalUEPosition);
    }

    static FVector ConvertCryScaleToUE(const FVector& CryScale)
    {
        // Keep same behavior as GRP import scale conversion for parity between formats.
        return CryScale;
    }

    static FMatrix BuildCryRotationMatrixXYZ(const FVector& CryAnglesDeg)
    {
        const FQuat Qx(FVector::XAxisVector, FMath::DegreesToRadians(CryAnglesDeg.X));
        const FQuat Qy(FVector::YAxisVector, FMath::DegreesToRadians(CryAnglesDeg.Y));
        const FQuat Qz(FVector::ZAxisVector, FMath::DegreesToRadians(CryAnglesDeg.Z));

        return FTransform(Qx).ToMatrixNoScale()
            * FTransform(Qy).ToMatrixNoScale()
            * FTransform(Qz).ToMatrixNoScale();
    }

    static FRotator ConvertCryAnglesToUERotator(const FVector& CryAnglesDeg)
    {
        const FMatrix MirrorY = FScaleMatrix(FVector(1.0f, -1.0f, 1.0f));
        const FMatrix CryRotation = BuildCryRotationMatrixXYZ(CryAnglesDeg);
        const FMatrix UERotation = MirrorY * CryRotation * MirrorY;

        return FQuat(UERotation).Rotator().GetNormalized();
    }

    static FRotator ConvertCryAnglesLegacy(const FVector& CryAnglesDeg)
    {
        return FRotator(CryAnglesDeg.X, -CryAnglesDeg.Z, CryAnglesDeg.Y);
    }

    static FRotator ApplyGlobalGroupYawToRotation(const FRotator& InRotation)
    {
        const FQuat GlobalYawQuat(FVector::UpVector, FMath::DegreesToRadians(GlobalYawOffsetDeg));
        return (GlobalYawQuat * InRotation.Quaternion()).Rotator().GetNormalized();
    }

    static FVector BuildFinalPlacementLocation(const FVector& CryPos)
    {
        const FVector LocalPosition = ConvertCryPositionToUE(CryPos);
        return ApplyGlobalGroupYawToPosition(LocalPosition) + GlobalPlacementOffset;
    }

    static FRotator BuildFinalPlacementRotation(const FVector& CryAnglesDeg)
    {
        FRotator Rotation = ConvertCryAnglesToUERotator(CryAnglesDeg);
        if (bUseLegacyRotationFallback)
        {
            Rotation = ConvertCryAnglesLegacy(CryAnglesDeg);
        }

        return ApplyGlobalGroupYawToRotation(Rotation);
    }

    static TSharedPtr<FJsonObject> MakeVectorObject(const FVector& Vector)
    {
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetNumberField(TEXT("x"), Vector.X);
        Json->SetNumberField(TEXT("y"), Vector.Y);
        Json->SetNumberField(TEXT("z"), Vector.Z);
        return Json;
    }

    static TSharedPtr<FJsonObject> MakeRotatorObject(const FRotator& Rotator)
    {
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetNumberField(TEXT("pitch"), Rotator.Pitch);
        Json->SetNumberField(TEXT("yaw"), Rotator.Yaw);
        Json->SetNumberField(TEXT("roll"), Rotator.Roll);
        return Json;
    }

    static bool IsCryGeometryExtension(const FString& ExtensionWithDotLower)
    {
        return ExtensionWithDotLower == TEXT(".cgf")
            || ExtensionWithDotLower == TEXT(".cga")
            || ExtensionWithDotLower == TEXT(".bld");
    }

    static FString FindBlenderExecutable()
    {
        const FString EnvPath = FPlatformMisc::GetEnvironmentVariable(TEXT("BLENDER_EXE"));
        if (!EnvPath.IsEmpty())
        {
            if (FPaths::FileExists(EnvPath))
            {
                return EnvPath;
            }
        }

        const TArray<FString> Candidates = {
            TEXT("E:/GameDevLibrary/Programs/Portable/Blender_v5.0/blender.exe"),
            TEXT("C:/Program Files/Blender Foundation/Blender 5.0/blender.exe"),
            TEXT("C:/Program Files/Blender Foundation/Blender 4.4/blender.exe"),
            TEXT("C:/Program Files/Blender Foundation/Blender 4.3/blender.exe")
        };

        for (const FString& Candidate : Candidates)
        {
            if (FPaths::FileExists(Candidate))
            {
                return Candidate;
            }
        }

        return FString();
    }

    static FString FindCryAddonZip()
    {
        const FString EnvPath = FPlatformMisc::GetEnvironmentVariable(TEXT("CRYTOOLS_CGF_ADDON_ZIP"));
        if (!EnvPath.IsEmpty())
        {
            if (FPaths::FileExists(EnvPath))
            {
                return EnvPath;
            }
        }

        const TArray<FString> Candidates = {
            TEXT("C:/Users/codg2/Downloads/Compressed/CryTools_FC.zip")
        };

        for (const FString& Candidate : Candidates)
        {
            if (FPaths::FileExists(Candidate))
            {
                return Candidate;
            }
        }

        return FString();
    }

    static bool ConvertCryGeometryToFbxViaBlender(
        const FString& InFilePath,
        const FString& OutFbxPath,
        const FString& InGameRootPath,
        bool bSkipCollisionGeometry,
        FString& OutError)
    {
        OutError.Reset();

        const FString BlenderExe = FindBlenderExecutable();
        if (BlenderExe.IsEmpty())
        {
            OutError = TEXT("Blender executable not found. Set BLENDER_EXE env var or install Blender.");
            return false;
        }

        const FString AddonZip = FindCryAddonZip();
        if (AddonZip.IsEmpty())
        {
            OutError = TEXT("CryTools_FC.zip addon archive not found. Set CRYTOOLS_CGF_ADDON_ZIP env var.");
            return false;
        }

        const FString TempDir = FPaths::ProjectSavedDir() / TEXT("CryImporter");
        IFileManager::Get().MakeDirectory(*TempDir, true);
        const FString ScriptPath = TempDir / TEXT("blender_cgf_to_fbx.py");

        const FString Script = TEXT(
R"PY(import bpy
import os
import sys
import addon_utils

def _main():
    argv = sys.argv
    args = []
    if "--" in argv:
        args = argv[argv.index("--")+1:]
    if len(args) < 5:
        raise RuntimeError("Expected args: in_path out_path addon_zip game_root skip_collision")

    in_path = args[0]
    out_path = args[1]
    addon_zip = args[2]
    game_root = args[3]
    skip_collision = (args[4] == "1")

    bpy.ops.wm.read_factory_settings(use_empty=True)

    if addon_zip and os.path.exists(addon_zip):
        try:
            bpy.ops.preferences.addon_install(filepath=addon_zip, overwrite=False)
        except Exception:
            pass

    addon_utils.enable("io_import_cgf", default_set=True, persistent=False)

    kwargs = {
        "filepath": in_path,
        "import_materials": True,
        "import_normals": True,
        "import_uvs": True,
        "import_skeleton": True,
        "import_weights": True
    }
    if game_root:
        kwargs["game_root_override"] = game_root

    result = bpy.ops.import_scene.cgf(**kwargs)
    if "FINISHED" not in result:
        raise RuntimeError(f"import_scene.cgf failed: {result}")

    out_dir = os.path.dirname(out_path)
    if out_dir and not os.path.exists(out_dir):
        os.makedirs(out_dir, exist_ok=True)

    exp = bpy.ops.export_scene.fbx(
        filepath=out_path,
        use_selection=False,
        add_leaf_bones=False,
        bake_space_transform=False,
        apply_unit_scale=True
    )
    if "FINISHED" not in exp:
        raise RuntimeError(f"export_scene.fbx failed: {exp}")

if __name__ == "__main__":
    _main()
)PY");

        if (!FFileHelper::SaveStringToFile(Script, *ScriptPath))
        {
            OutError = TEXT("Failed to write temporary Blender conversion script.");
            return false;
        }

        const FString GameRootArg = InGameRootPath.IsEmpty() ? TEXT("") : InGameRootPath;
        const FString SkipArg = bSkipCollisionGeometry ? TEXT("1") : TEXT("0");

        const FString Args = FString::Printf(
            TEXT("-b --python \"%s\" -- \"%s\" \"%s\" \"%s\" \"%s\" \"%s\""),
            *ScriptPath,
            *InFilePath,
            *OutFbxPath,
            *AddonZip,
            *GameRootArg,
            *SkipArg);

        int32 ReturnCode = 0;
        FString StdOut;
        FString StdErr;
        FPlatformProcess::ExecProcess(*BlenderExe, *Args, &ReturnCode, &StdOut, &StdErr);

        if (ReturnCode != 0 || !FPaths::FileExists(OutFbxPath))
        {
            OutError = FString::Printf(
                TEXT("Blender CGF->FBX conversion failed (code %d). %s %s"),
                ReturnCode, *StdOut, *StdErr);
            return false;
        }

        return true;
    }

    static FString FindPythonExecutable()
    {
        const FString EnvPath = FPlatformMisc::GetEnvironmentVariable(TEXT("PYTHON_EXE"));
        if (!EnvPath.IsEmpty() && FPaths::FileExists(EnvPath))
        {
            return EnvPath;
        }

        const TArray<FString> Candidates = {
            TEXT("C:/Users/codg2/AppData/Local/Programs/Python/Python314/python.exe"),
            TEXT("C:/Users/codg2/AppData/Local/Python/pythoncore-3.14-64/python.exe")
        };

        for (const FString& Candidate : Candidates)
        {
            if (FPaths::FileExists(Candidate))
            {
                return Candidate;
            }
        }

        return FString();
    }

    static bool ConvertCryGeometryToObjViaPython(
        const FString& InFilePath,
        const FString& OutObjPath,
        FString& OutError)
    {
        OutError.Reset();

        const FString PythonExe = FindPythonExecutable();
        if (PythonExe.IsEmpty())
        {
            OutError = TEXT("Python executable not found. Set PYTHON_EXE env var.");
            return false;
        }

        const FString AddonZip = FindCryAddonZip();
        if (AddonZip.IsEmpty())
        {
            OutError = TEXT("CryTools_FC.zip addon archive not found. Set CRYTOOLS_CGF_ADDON_ZIP env var.");
            return false;
        }

        const FString TempDir = FPaths::ProjectSavedDir() / TEXT("CryImporter");
        IFileManager::Get().MakeDirectory(*TempDir, true);
        const FString ScriptPath = TempDir / TEXT("cgf_to_obj.py");

        const FString Script = TEXT(
R"PY(import os
import sys

def _main():
    args = sys.argv[1:]
    if len(args) < 3:
        raise RuntimeError("Expected args: in_path out_path addon_zip")

    in_path = args[0]
    out_path = args[1]
    addon_zip = args[2]

    sys.path.insert(0, addon_zip)
    from io_import_cgf import cry_chunk_reader

    archive = cry_chunk_reader.ChunkReader().read_file(in_path)
    mesh = None
    for m in archive.mesh_chunks:
        if m and m.vertices and m.faces:
            mesh = m
            break
    if mesh is None:
        raise RuntimeError("No mesh chunk with vertices/faces found in CGF file")

    out_dir = os.path.dirname(out_path)
    if out_dir and not os.path.exists(out_dir):
        os.makedirs(out_dir, exist_ok=True)

    with open(out_path, "w", encoding="utf-8") as f:
        f.write("# CGF converted by CryImporter python fallback\n")
        for v in mesh.vertices:
            x, y, z = v.pos
            f.write(f"v {x} {-y} {z}\n")
        for face in mesh.faces:
            a = int(face.v0) + 1
            b = int(face.v1) + 1
            c = int(face.v2) + 1
            f.write(f"f {a} {b} {c}\n")

if __name__ == "__main__":
    _main()
)PY");

        if (!FFileHelper::SaveStringToFile(Script, *ScriptPath))
        {
            OutError = TEXT("Failed to write temporary Python conversion script.");
            return false;
        }

        const FString Args = FString::Printf(
            TEXT("\"%s\" \"%s\" \"%s\" \"%s\""),
            *ScriptPath,
            *InFilePath,
            *OutObjPath,
            *AddonZip);

        int32 ReturnCode = 0;
        FString StdOut;
        FString StdErr;
        FPlatformProcess::ExecProcess(*PythonExe, *Args, &ReturnCode, &StdOut, &StdErr);

        if (ReturnCode != 0 || !FPaths::FileExists(OutObjPath))
        {
            OutError = FString::Printf(
                TEXT("Python CGF->OBJ conversion failed (code %d). %s %s"),
                ReturnCode, *StdOut, *StdErr);
            return false;
        }

        return true;
    }

    static UStaticMesh* DuplicateStaticMeshAsset(UStaticMesh* SourceMesh, const FString& TargetFolder, const FString& PreferredAssetName)
    {
        if (!SourceMesh)
        {
            return nullptr;
        }

        const FString SanitizedFolder = SanitizeImportSaveRootPath(TargetFolder);
        const FString SanitizedName = SanitizeAssetName(PreferredAssetName);
        const FString AssetPath = SanitizedFolder / SanitizedName;

        FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
        FString UniquePackageName;
        FString UniqueAssetName;
        AssetToolsModule.Get().CreateUniqueAssetName(AssetPath, TEXT(""), UniquePackageName, UniqueAssetName);

        UPackage* Package = CreatePackage(*UniquePackageName);
        if (!Package)
        {
            return nullptr;
        }

        UStaticMesh* DuplicatedMesh = DuplicateObject<UStaticMesh>(SourceMesh, Package, *UniqueAssetName);
        if (!DuplicatedMesh)
        {
            return nullptr;
        }

        DuplicatedMesh->SetFlags(RF_Public | RF_Standalone);
        DuplicatedMesh->MarkPackageDirty();
        FAssetRegistryModule::AssetCreated(DuplicatedMesh);
        return DuplicatedMesh;
    }

    static int32 CreateAndAssignMaterialCopies(UStaticMesh* TargetMesh, const FString& TargetFolder)
    {
        if (!TargetMesh)
        {
            return 0;
        }

        const FString SanitizedFolder = SanitizeImportSaveRootPath(TargetFolder);
        const FString MeshName = SanitizeAssetName(TargetMesh->GetName());
        int32 CreatedCount = 0;

        const TArray<FStaticMaterial>& StaticMaterials = TargetMesh->GetStaticMaterials();
        for (int32 SlotIndex = 0; SlotIndex < StaticMaterials.Num(); ++SlotIndex)
        {
            UMaterialInterface* SourceMaterial = StaticMaterials[SlotIndex].MaterialInterface;
            if (!SourceMaterial)
            {
                continue;
            }

            const FString MaterialBaseName = FString::Printf(TEXT("%s_MI_%02d"), *MeshName, SlotIndex);
            const FString AssetPath = SanitizedFolder / SanitizeAssetName(MaterialBaseName);

            FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
            FString UniquePackageName;
            FString UniqueAssetName;
            AssetToolsModule.Get().CreateUniqueAssetName(AssetPath, TEXT(""), UniquePackageName, UniqueAssetName);

            UPackage* Package = CreatePackage(*UniquePackageName);
            if (!Package)
            {
                continue;
            }

            UMaterialInstanceConstant* MaterialCopy = NewObject<UMaterialInstanceConstant>(Package, *UniqueAssetName, RF_Public | RF_Standalone);
            if (!MaterialCopy)
            {
                continue;
            }

            MaterialCopy->SetParentEditorOnly(SourceMaterial);
            MaterialCopy->MarkPackageDirty();
            FAssetRegistryModule::AssetCreated(MaterialCopy);
            TargetMesh->SetMaterial(SlotIndex, MaterialCopy);
            ++CreatedCount;
        }

        if (CreatedCount > 0)
        {
            TargetMesh->MarkPackageDirty();
        }

        return CreatedCount;
    }
}

void FCryImporterModule::StartupModule()
{
    AssetSearchRootPath = CryImporter::SanitizeAssetSearchRootPath(AssetSearchRootPath);
    ImportSaveRootPath = CryImporter::SanitizeImportSaveRootPath(ImportSaveRootPath);

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        CryImporter::MainTabName,
        FOnSpawnTab::CreateRaw(this, &FCryImporterModule::OnSpawnPluginTab))
        .SetDisplayName(LOCTEXT("CryImporterTabTitle", "CryTools"))
        .SetMenuType(ETabSpawnerMenuType::Hidden);

    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FCryImporterModule::RegisterMenus)
    );
}

void FCryImporterModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);

    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(CryImporter::MainTabName);
}

void FCryImporterModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
    FToolMenuSection& Section = Menu->AddSection(
        "CryImporter",
        LOCTEXT("CryImporterSection", "CryTools")
    );

    Section.AddMenuEntry(
        "OpenCryImporterPanel",
        LOCTEXT("CryImporterOpenPanelLabel", "Open CryTools"),
        LOCTEXT("CryImporterOpenPanelTooltip", "Open CryTools panel"),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateRaw(this, &FCryImporterModule::OpenPluginWindow))
    );
}

void FCryImporterModule::OpenPluginWindow()
{
    FGlobalTabmanager::Get()->TryInvokeTab(CryImporter::MainTabName);
}

void FCryImporterModule::OpenImportDialog()
{
    ImportButtonClicked();
}

void FCryImporterModule::OpenImportVEGDialog()
{
    ImportVEGButtonClicked();
}

void FCryImporterModule::OpenImportGeometryDialog()
{
    ImportGeometryButtonClicked();
}

void FCryImporterModule::OpenImportAnimationDialog()
{
    ImportAnimationButtonClicked();
}

void FCryImporterModule::OpenExportDialog()
{
    ExportSelectedGroup();
}

FString FCryImporterModule::GetAssetSearchRootPath() const
{
    return AssetSearchRootPath;
}

void FCryImporterModule::SetAssetSearchRootPath(const FString& InPath)
{
    AssetSearchRootPath = CryImporter::SanitizeAssetSearchRootPath(InPath);
}

TSharedRef<SDockTab> FCryImporterModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SBorder)
            .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
            .Padding(12.0f)
            [
                SNew(SScrollBox)
                + SScrollBox::Slot()
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("PanelTitle", "CryTools"))
                        .Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                    [
                        SNew(SExpandableArea)
                        .InitiallyCollapsed(false)
                        .HeaderContent()
                        [
                            SNew(STextBlock)
                            .Text(LOCTEXT("LandscapeEditSectionTitle", "Landscape Edit"))
                            .Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
                        ]
                        .BodyContent()
                        [
                            SNew(SBorder)
                            .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                            .Padding(FMargin(10.0f, 8.0f))
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                [
                                    SNew(STextBlock)
                                    .Text(LOCTEXT("LandscapeEditIntro", "Landscape tools"))
                                    .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f, 1.0f)))
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                [
                                    SNew(SExpandableArea)
                                    .InitiallyCollapsed(false)
                                    .HeaderContent()
                                    [
                                        SNew(STextBlock)
                                        .Text(LOCTEXT("LandscapeSmoothSectionTitle", "Landscape Smooth"))
                                    ]
                                    .BodyContent()
                                    [
                                        SNew(SBorder)
                                        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                                        .Padding(FMargin(12.0f, 10.0f))
                                        [
                                            SNew(SVerticalBox)
                                            + SVerticalBox::Slot()
                                            .AutoHeight()
                                            .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                            [
                                                SNew(STextBlock)
                                                .Text(LOCTEXT("LandscapeSmoothSubLabel", "Nested tool: height smoothing"))
                                                .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f, 1.0f)))
                                            ]
                                            + SVerticalBox::Slot()
                                            .AutoHeight()
                                            .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                            [
                                                SNew(STextBlock)
                                                .AutoWrapText(true)
                                                .Text(LOCTEXT("LandscapeSmoothHelp", "Select exactly one Landscape actor, then adjust Smooth Strength and click Auto Smooth."))
                                            ]
                                            + SVerticalBox::Slot()
                                            .AutoHeight()
                                            .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                            [
                                                SNew(SCheckBox)
                                                .IsChecked_Raw(this, &FCryImporterModule::GetLandscapeSmoothBilateralState)
                                                .OnCheckStateChanged_Raw(this, &FCryImporterModule::OnLandscapeSmoothBilateralChanged)
                                                [
                                                    SNew(STextBlock)
                                                    .Text(LOCTEXT("LandscapeSmoothBilateral", "Preserve shape (bilateral)"))
                                                ]
                                            ]
                                            + SVerticalBox::Slot()
                                            .AutoHeight()
                                            .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                            [
                                                SNew(SHorizontalBox)
                                                + SHorizontalBox::Slot()
                                                .AutoWidth()
                                                .VAlign(VAlign_Center)
                                                .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                                                [
                                                    SNew(STextBlock)
                                                    .Text(LOCTEXT("LandscapeSmoothRangeSigmaLabel", "Height Preserve"))
                                                ]
                                                + SHorizontalBox::Slot()
                                                .FillWidth(1.0f)
                                                .VAlign(VAlign_Center)
                                                [
                                                    SNew(SEditableTextBox)
                                                    .Text_Lambda([this]() { return FText::AsNumber(LandscapeSmoothRangeSigma); })
                                                    .ToolTipText(LOCTEXT("LandscapeSmoothRangeSigmaTip", "Bilateral range sigma in heightmap units. Higher = more smoothing; lower = better edge/height preservation."))
                                                    .OnTextCommitted_Raw(this, &FCryImporterModule::OnLandscapeSmoothRangeSigmaCommitted)
                                                ]
                                            ]
                                            + SVerticalBox::Slot()
                                            .AutoHeight()
                                            .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                            [
                                                SNew(SCheckBox)
                                                .IsChecked_Raw(this, &FCryImporterModule::GetLandscapeSmoothPreserveMeanState)
                                                .OnCheckStateChanged_Raw(this, &FCryImporterModule::OnLandscapeSmoothPreserveMeanChanged)
                                                [
                                                    SNew(STextBlock)
                                                    .Text(LOCTEXT("LandscapeSmoothPreserveMean", "Preserve average height"))
                                                ]
                                            ]
                                            + SVerticalBox::Slot()
                                            .AutoHeight()
                                            [
                                                SNew(SHorizontalBox)
                                                + SHorizontalBox::Slot()
                                                .AutoWidth()
                                                .VAlign(VAlign_Center)
                                                .Padding(0.0f, 0.0f, 10.0f, 0.0f)
                                                [
                                                    SNew(STextBlock)
                                                    .Text(LOCTEXT("LandscapeSmoothStrengthLabel", "Smooth Strength"))
                                                ]
                                                + SHorizontalBox::Slot()
                                                .FillWidth(1.0f)
                                                .VAlign(VAlign_Center)
                                                [
                                                    SNew(SSpinBox<float>)
                                                    .MinValue(0.0f)
                                                    .MaxValue(1.0f)
                                                    .MinSliderValue(0.0f)
                                                    .MaxSliderValue(1.0f)
                                                    .Delta(0.05f)
                                                    .MinDesiredWidth(170.0f)
                                                    .Value_Lambda([this]() { return LandscapeSmoothStrength; })
                                                    .OnValueChanged_Lambda([this](float InValue) { LandscapeSmoothStrength = InValue; })
                                                ]
                                                + SHorizontalBox::Slot()
                                                .AutoWidth()
                                                .Padding(10.0f, 0.0f, 0.0f, 0.0f)
                                                [
                                                    SNew(SButton)
                                                    .Text(LOCTEXT("LandscapeAutoSmoothButton", "Auto Smooth"))
                                                    .OnClicked_Raw(this, &FCryImporterModule::HandleLandscapeSmoothClicked)
                                                ]
                                            ]
                                        ]
                                    ]
                                ]
                            ]
                        ]
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                    [
                        SNew(SExpandableArea)
                        .InitiallyCollapsed(false)
                        .HeaderContent()
                        [
                            SNew(STextBlock)
                            .Text(LOCTEXT("EditObjectSectionTitle", "Edit Object"))
                            .Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
                        ]
                        .BodyContent()
                        [
                            SNew(SBorder)
                            .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                            .Padding(FMargin(10.0f, 8.0f))
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                [
                                    SNew(STextBlock)
                                    .Text(LOCTEXT("EditObjectIntro", "Selected actor tools"))
                                    .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f, 1.0f)))
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                [
                                    SNew(SButton)
                                    .Text(LOCTEXT("SnapSelectionToLandscapeButton", "Snap Selected Meshes To Landscape"))
                                    .ToolTipText(LOCTEXT("SnapSelectionToLandscapeTooltip", "Moves selected StaticMesh actors so their pivot sits on the Landscape under the same XY position."))
                                    .OnClicked_Raw(this, &FCryImporterModule::HandleSnapSelectionToLandscapeClicked)
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                                [
                                    SNew(SButton)
                                    .Text(LOCTEXT("ConvertSelectionToFoliageButtonText", "Convert Selected To Foliage"))
                                    .ToolTipText(LOCTEXT("ConvertSelectionToFoliageTooltip", "Takes selected StaticMesh actors and converts them into foliage instances (InstancedFoliageActor)."))
                                    .OnClicked_Raw(this, &FCryImporterModule::HandleConvertSelectionToFoliageClicked)
                                ]
                            ]
                        ]
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                    [
                        SNew(SExpandableArea)
                        .InitiallyCollapsed(false)
                        .HeaderContent()
                        [
                            SNew(STextBlock)
                            .Text(LOCTEXT("ImportSectionTitle", "Import"))
                            .Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
                        ]
                        .BodyContent()
                        [
                            SNew(SBorder)
                            .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                            .Padding(FMargin(10.0f, 8.0f))
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                [
                                    SNew(STextBlock)
                                    .Text(LOCTEXT("CryImportIntro", "Placement import"))
                                    .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f, 1.0f)))
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                [
                                    SNew(SHorizontalBox)
                                    + SHorizontalBox::Slot()
                                    .AutoWidth()
                                    .VAlign(VAlign_Center)
                                    .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                                    [
                                        SNew(STextBlock)
                                        .Text(LOCTEXT("AssetSearchRootLabel", "Asset Search Root"))
                                    ]
                                    + SHorizontalBox::Slot()
                                    .FillWidth(1.0f)
                                    [
                                        SNew(SEditableTextBox)
                                        .Text_Lambda([this]() { return FText::FromString(AssetSearchRootPath); })
                                        .HintText(LOCTEXT("AssetSearchRootHint", "/Game/Objects"))
                                        .OnTextCommitted_Raw(this, &FCryImporterModule::OnAssetSearchPathCommitted)
                                    ]
                                    + SHorizontalBox::Slot()
                                    .AutoWidth()
                                    .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                                    [
                                        SNew(SButton)
                                        .Text(LOCTEXT("AssetSearchRootReset", "Reset"))
                                        .OnClicked_Raw(this, &FCryImporterModule::HandleResetAssetSearchPathClicked)
                                    ]
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                [
                                    SNew(SButton)
                                    .Text(LOCTEXT("ImportButtonText", "Import GRP (.grp)"))
                                    .OnClicked_Raw(this, &FCryImporterModule::HandleImportButtonClicked)
                                ]
                                + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                                [
                                    SNew(SButton)
                                    .Text(LOCTEXT("ImportVEGButtonText", "Import VEG (.veg)"))
                                    .OnClicked_Raw(this, &FCryImporterModule::HandleImportVEGButtonClicked)
                                ]

                            ]
                        ]
                    ]
                ]
            ]
        ];
}

FReply FCryImporterModule::HandleImportButtonClicked()
{
    ImportButtonClicked();
    return FReply::Handled();
}

FReply FCryImporterModule::HandleExportButtonClicked()
{
    ExportSelectedGroup();
    return FReply::Handled();
}

FReply FCryImporterModule::HandleImportVEGButtonClicked()
{
    ImportVEGButtonClicked();
    return FReply::Handled();
}

FReply FCryImporterModule::HandleImportGeometryButtonClicked()
{
    ImportGeometryButtonClicked();
    return FReply::Handled();
}

FReply FCryImporterModule::HandleImportAnimationButtonClicked()
{
    ImportAnimationButtonClicked();
    return FReply::Handled();
}

FReply FCryImporterModule::HandleResetAssetSearchPathClicked()
{
    AssetSearchRootPath = TEXT("/Game/Objects");
    return FReply::Handled();
}

FReply FCryImporterModule::HandleResetGameTexturesRootPathClicked()
{
    GameTexturesRootPath = TEXT("");
    return FReply::Handled();
}

FReply FCryImporterModule::HandleResetImportSaveRootPathClicked()
{
    ImportSaveRootPath = TEXT("/Game/CryImported");
    return FReply::Handled();
}

FReply FCryImporterModule::HandleLandscapeSmoothClicked()
{
    if (!ApplyLandscapeSmooth(LandscapeSmoothStrength))
    {
        const FText ReasonText = LastLandscapeSmoothError.IsEmpty()
            ? LOCTEXT("LandscapeSmoothFailedReasonFallback", "Landscape Auto Smooth failed.")
            : FText::FromString(LastLandscapeSmoothError);
        FMessageDialog::Open(
            EAppMsgType::Ok,
            FText::Format(
                LOCTEXT("LandscapeSmoothFailedMessage", "{0}\n\nTip: If your Landscape uses Edit Layers, disable Edit Layers for this tool."),
                ReasonText));
    }
    return FReply::Handled();
}

FReply FCryImporterModule::HandleConvertSelectionToFoliageClicked()
{
    if (!ConvertSelectedStaticMeshesToFoliage())
    {
        FMessageDialog::Open(
            EAppMsgType::Ok,
            LOCTEXT("ConvertSelectionToFoliageFailed", "Convert to foliage failed. Select one or more StaticMesh actors in the level."));
    }
    return FReply::Handled();
}

FReply FCryImporterModule::HandleSnapSelectionToLandscapeClicked()
{
    if (!SnapSelectedActorsToLandscape())
    {
        FMessageDialog::Open(
            EAppMsgType::Ok,
            LOCTEXT("SnapSelectionToLandscapeFailed", "Snap to landscape failed. Select one or more StaticMesh actors and make sure a Landscape is present under them."));
    }
    return FReply::Handled();
}

void FCryImporterModule::OnAssetSearchPathCommitted(const FText& InText, ETextCommit::Type CommitType)
{
    SetAssetSearchRootPath(InText.ToString());
}

void FCryImporterModule::OnGameTexturesRootPathCommitted(const FText& InText, ETextCommit::Type CommitType)
{
    (void)CommitType;
    GameTexturesRootPath = InText.ToString().TrimStartAndEnd();
    GameTexturesRootPath.ReplaceInline(TEXT("\\"), TEXT("/"));
}

void FCryImporterModule::OnImportSaveRootPathCommitted(const FText& InText, ETextCommit::Type CommitType)
{
    (void)CommitType;
    ImportSaveRootPath = CryImporter::SanitizeImportSaveRootPath(InText.ToString());
}

void FCryImporterModule::OnSkipCollisionLikeGeometryChanged(ECheckBoxState InState)
{
    bSkipCollisionLikeGeometry = (InState == ECheckBoxState::Checked);
}

void FCryImporterModule::OnImportPhysicsMaterialMetadataChanged(ECheckBoxState InState)
{
    bImportPhysicsMaterialMetadata = (InState == ECheckBoxState::Checked);
}

void FCryImporterModule::OnCreateMeshAssetCopiesChanged(ECheckBoxState InState)
{
    bCreateMeshAssetCopies = (InState == ECheckBoxState::Checked);
}

void FCryImporterModule::OnCreateMaterialAssetsChanged(ECheckBoxState InState)
{
    bCreateMaterialAssets = (InState == ECheckBoxState::Checked);
}

void FCryImporterModule::OnCreateTextureAssetsChanged(ECheckBoxState InState)
{
    bCreateTextureAssets = (InState == ECheckBoxState::Checked);
}

void FCryImporterModule::OnUseLandscapeCenterOriginChanged(ECheckBoxState InState)
{
    bUseLandscapeCenterOrigin = (InState == ECheckBoxState::Checked);
}

void FCryImporterModule::OnLandscapeSmoothPreserveMeanChanged(ECheckBoxState InState)
{
    bLandscapeSmoothPreserveMean = (InState == ECheckBoxState::Checked);
}

void FCryImporterModule::OnLandscapeSmoothBilateralChanged(ECheckBoxState InState)
{
    bLandscapeSmoothUseBilateral = (InState == ECheckBoxState::Checked);
}

void FCryImporterModule::OnLandscapeSmoothRangeSigmaCommitted(const FText& InText, ETextCommit::Type CommitType)
{
    (void)CommitType;
    float Parsed = 0.0f;
    if (CryImporter::TryParseFloat(InText.ToString(), Parsed))
    {
        LandscapeSmoothRangeSigma = FMath::Clamp(Parsed, 1.0f, 8192.0f);
    }
}

ECheckBoxState FCryImporterModule::GetSkipCollisionLikeGeometryState() const
{
    return bSkipCollisionLikeGeometry ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState FCryImporterModule::GetImportPhysicsMaterialMetadataState() const
{
    return bImportPhysicsMaterialMetadata ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState FCryImporterModule::GetCreateMeshAssetCopiesState() const
{
    return bCreateMeshAssetCopies ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState FCryImporterModule::GetCreateMaterialAssetsState() const
{
    return bCreateMaterialAssets ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState FCryImporterModule::GetCreateTextureAssetsState() const
{
    return bCreateTextureAssets ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState FCryImporterModule::GetUseLandscapeCenterOriginState() const
{
    return bUseLandscapeCenterOrigin ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState FCryImporterModule::GetLandscapeSmoothPreserveMeanState() const
{
    return bLandscapeSmoothPreserveMean ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState FCryImporterModule::GetLandscapeSmoothBilateralState() const
{
    return bLandscapeSmoothUseBilateral ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}
void FCryImporterModule::ImportButtonClicked()
{
    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (!DesktopPlatform)
    {
        return;
    }

    TArray<FString> OutFiles;
    const bool bOpened = DesktopPlatform->OpenFileDialog(
        nullptr,
        TEXT("Select GRP file"),
        FPaths::ProjectDir(),
        TEXT(""),
        TEXT("GRP Files (*.grp)|*.grp"),
        EFileDialogFlags::None,
        OutFiles
    );

    if (!bOpened || OutFiles.Num() == 0)
    {
        return;
    }

    ImportGRP(OutFiles[0]);
}

void FCryImporterModule::ImportVEGButtonClicked()
{
    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (!DesktopPlatform)
    {
        return;
    }

    TArray<FString> OutFiles;
    const bool bOpened = DesktopPlatform->OpenFileDialog(
        nullptr,
        TEXT("Select VEG file"),
        FPaths::ProjectDir(),
        TEXT(""),
        TEXT("VEG Files (*.veg)|*.veg"),
        EFileDialogFlags::None,
        OutFiles
    );

    if (!bOpened || OutFiles.Num() == 0)
    {
        return;
    }

    ImportVEG(OutFiles[0]);
}

void FCryImporterModule::ImportGeometryButtonClicked()
{
    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (!DesktopPlatform)
    {
        return;
    }

    TArray<FString> OutFiles;
    const bool bOpened = DesktopPlatform->OpenFileDialog(
        nullptr,
        TEXT("Select Cry Geometry file"),
        FPaths::ProjectDir(),
        TEXT(""),
        TEXT("Cry Geometry Files (*.cgf;*.cga;*.bld)|*.cgf;*.cga;*.bld"),
        EFileDialogFlags::None,
        OutFiles
    );

    if (!bOpened || OutFiles.Num() == 0)
    {
        return;
    }

    LastGeometryImportError.Reset();
    int32 ImportedCount = 0;
    for (const FString& FilePath : OutFiles)
    {
        ImportedCount += ImportCryGeometryFile(FilePath) ? 1 : 0;
    }

    UE_LOG(LogTemp, Display, TEXT("CryImporter: Geometry import completed. Imported %d file(s) out of %d."), ImportedCount, OutFiles.Num());
    if (ImportedCount == 0)
    {
        const FText ReasonText = LastGeometryImportError.IsEmpty()
            ? LOCTEXT("GeometryImportFailedReasonFallback", "See Output Log for detailed import errors.")
            : FText::FromString(LastGeometryImportError);
        FMessageDialog::Open(
            EAppMsgType::Ok,
            FText::Format(
                LOCTEXT("GeometryImportFailed", "No geometry files were imported.\n\nReason: {0}"),
                ReasonText));
    }
}

void FCryImporterModule::ImportAnimationButtonClicked()
{
    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (!DesktopPlatform)
    {
        return;
    }

    TArray<FString> OutFiles;
    const bool bOpened = DesktopPlatform->OpenFileDialog(
        nullptr,
        TEXT("Select Cry Animation file"),
        FPaths::ProjectDir(),
        TEXT(""),
        TEXT("Cry Animation Files (*.caf;*.anm;*.cal)|*.caf;*.anm;*.cal"),
        EFileDialogFlags::None,
        OutFiles
    );

    if (!bOpened || OutFiles.Num() == 0)
    {
        return;
    }

    for (const FString& FilePath : OutFiles)
    {
        ImportCryAnimationFile(FilePath);
    }
}

bool FCryImporterModule::ImportCryGeometryFile(const FString& FilePath)
{
    UWorld* World = GEditor ? GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>()->GetEditorWorld() : nullptr;
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("CryImporter: No editor world available."));
        LastGeometryImportError = TEXT("No editor world available.");
        return false;
    }

    const FString PrefabPath = CryImporter::ExtractCryPrefabPathFromDiskPath(FilePath, GameTexturesRootPath);
    if (PrefabPath.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("CryImporter: Failed to derive prefab path from '%s'."), *FilePath);
        LastGeometryImportError = TEXT("Failed to derive prefab path from source file.");
        return false;
    }

    if (bSkipCollisionLikeGeometry && CryImporter::IsCollisionLikeMarker(PrefabPath))
    {
        UE_LOG(LogTemp, Display, TEXT("CryImporter: Skipped collision-like geometry '%s'."), *PrefabPath);
        LastGeometryImportError = TEXT("Geometry was skipped by collision/no-draw filter.");
        return false;
    }

    const FString SaveRoot = CryImporter::SanitizeImportSaveRootPath(ImportSaveRootPath);
    ImportSaveRootPath = SaveRoot;
    const FString MeshFolder = SaveRoot / TEXT("Meshes");
    const FString MeshAssetName = CryImporter::SanitizeAssetName(FPaths::GetBaseFilename(PrefabPath));

    if (!bCreateMeshAssetCopies)
    {
        UE_LOG(LogTemp, Display, TEXT("CryImporter: 'Create imported mesh assets' disabled, but direct geometry import requires mesh asset creation. Proceeding with import."));
    }

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    auto ImportAsStaticMesh = [&AssetToolsModule, &MeshFolder, &MeshAssetName](const FString& InSourceFile, bool bReplaceExisting) -> UStaticMesh*
    {
        UAssetImportTask* ImportTask = NewObject<UAssetImportTask>();
        ImportTask->Filename = InSourceFile;
        ImportTask->DestinationPath = MeshFolder;
        ImportTask->DestinationName = MeshAssetName;
        ImportTask->bAutomated = true;
        ImportTask->bReplaceExisting = bReplaceExisting;
        ImportTask->bSave = true;

        TArray<UAssetImportTask*> Tasks;
        Tasks.Add(ImportTask);
        AssetToolsModule.Get().ImportAssetTasks(Tasks);

        for (const FString& ObjectPath : ImportTask->ImportedObjectPaths)
        {
            if (UObject* ImportedObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath))
            {
                if (UStaticMesh* ImportedMesh = Cast<UStaticMesh>(ImportedObject))
                {
                    return ImportedMesh;
                }
            }
        }

        return nullptr;
    };

    UStaticMesh* MeshToUse = ImportAsStaticMesh(FilePath, false);

    if (!MeshToUse)
    {
        const FString Extension = FPaths::GetExtension(FilePath, true).ToLower();
        if (CryImporter::IsCryGeometryExtension(Extension))
        {
            const FString TempDir = FPaths::ProjectSavedDir() / TEXT("CryImporter") / TEXT("Converted");
            IFileManager::Get().MakeDirectory(*TempDir, true);
            const FString ConvertedObjPath = TempDir / (FPaths::GetBaseFilename(FilePath) + TEXT("_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".obj"));

            FString ConvertError;
            if (CryImporter::ConvertCryGeometryToObjViaPython(
                FilePath,
                ConvertedObjPath,
                ConvertError))
            {
                UE_LOG(LogTemp, Display, TEXT("CryImporter: Python converted '%s' -> '%s'."), *FilePath, *ConvertedObjPath);
                MeshToUse = ImportAsStaticMesh(ConvertedObjPath, true);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("CryImporter: Python fallback conversion failed for '%s': %s"), *FilePath, *ConvertError);
                LastGeometryImportError = FString::Printf(
                    TEXT("CGF import failed and Python fallback failed. %s"),
                    *ConvertError);
                return false;
            }
        }

        if (!MeshToUse)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("CryImporter: Geometry file '%s' was not imported as UStaticMesh. This usually means no Unreal importer/factory is registered for this format."),
                *FilePath);
            LastGeometryImportError = FString::Printf(
                TEXT("Unreal did not import '%s' as StaticMesh. Most likely there is no factory/importer for .cgf/.cga/.bld."),
                *FPaths::GetCleanFilename(FilePath));
            return false;
        }
    }

    if (bCreateMaterialAssets)
    {
        const FString MaterialsFolder = SaveRoot / TEXT("Materials");
        const int32 CreatedMaterials = CryImporter::CreateAndAssignMaterialCopies(MeshToUse, MaterialsFolder);
        UE_LOG(LogTemp, Display, TEXT("CryImporter: Created %d material asset(s) for '%s'."), CreatedMaterials, *MeshToUse->GetPathName());

        if (bCreateTextureAssets)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("CryImporter: Texture asset creation is marked experimental and is not fully implemented yet for '%s'."),
                *FilePath);
        }
    }

    const FScopedTransaction Transaction(LOCTEXT("CryImportGeometryTransaction", "Import Cry Geometry"));
    World->Modify();

    AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator);
    if (!Actor)
    {
        UE_LOG(LogTemp, Warning, TEXT("CryImporter: Failed to spawn actor for '%s'."), *FilePath);
        LastGeometryImportError = TEXT("StaticMesh was imported but actor spawn failed.");
        return false;
    }

    Actor->SetFlags(RF_Transactional);
    Actor->Modify();
    if (UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent())
    {
        MeshComp->SetStaticMesh(MeshToUse);
    }

    Actor->SetActorLabel(FPaths::GetBaseFilename(PrefabPath));
    CryImporter::AddTagIfMissing(Actor, FString::Printf(TEXT("CrySource:%s"), *FPaths::GetCleanFilename(FilePath)));
    if (bImportPhysicsMaterialMetadata)
    {
        CryImporter::AddTagIfMissing(Actor, TEXT("CryPhysMat:from_source"));
    }

    UE_LOG(LogTemp, Display, TEXT("CryImporter: Imported geometry '%s' using mesh '%s'."), *FilePath, *MeshToUse->GetPathName());
    return true;
}

void FCryImporterModule::ImportCryAnimationFile(const FString& FilePath)
{
    const FString Extension = FPaths::GetExtension(FilePath, true).ToLower();
    if (Extension == TEXT(".cal"))
    {
        FString CalText;
        if (!FFileHelper::LoadFileToString(CalText, *FilePath))
        {
            UE_LOG(LogTemp, Warning, TEXT("CryImporter: Failed to read CAL '%s'."), *FilePath);
            return;
        }

        TArray<FString> Lines;
        CalText.ParseIntoArrayLines(Lines, true);
        int32 AnimationRefs = 0;
        for (FString Line : Lines)
        {
            Line = Line.TrimStartAndEnd();
            if (!Line.IsEmpty() && !Line.StartsWith(TEXT("#")))
            {
                ++AnimationRefs;
            }
        }

        UE_LOG(LogTemp, Display, TEXT("CryImporter: Parsed CAL '%s' with %d animation reference line(s)."), *FilePath, AnimationRefs);
        return;
    }

    TArray<uint8> Binary;
    if (!FFileHelper::LoadFileToArray(Binary, *FilePath))
    {
        UE_LOG(LogTemp, Warning, TEXT("CryImporter: Failed to read animation file '%s'."), *FilePath);
        return;
    }

    if (Binary.Num() < 16)
    {
        UE_LOG(LogTemp, Warning, TEXT("CryImporter: Animation file '%s' is too small or invalid."), *FilePath);
        return;
    }

    UE_LOG(LogTemp, Display, TEXT("CryImporter: Read animation file '%s' (%d bytes). Full UE animation import pipeline is pending implementation."),
        *FilePath, Binary.Num());
}

bool FCryImporterModule::ApplyLandscapeSmooth(float SmoothStrength)
{
    if (!GEditor)
    {
        LastLandscapeSmoothError = TEXT("GEditor is not available.");
        return false;
    }

    LastLandscapeSmoothError.Reset();
    ULandscapeInfo* LandscapeInfo = nullptr;
    ALandscapeProxy* TargetLandscape = nullptr;
    bool bHasAnySelection = false;
    int32 SelectedLandscapeCount = 0;

    if (USelection* SelectedActors = GEditor->GetSelectedActors())
    {
        for (FSelectionIterator It(*SelectedActors); It; ++It)
        {
            bHasAnySelection = true;
            if (ALandscapeProxy* LandscapeProxy = Cast<ALandscapeProxy>(*It))
            {
                if (!IsValid(LandscapeProxy))
                {
                    continue;
                }

                if (LandscapeProxy->GetLandscapeActor() == nullptr || LandscapeProxy->LandscapeComponents.Num() == 0)
                {
                    continue;
                }

                ULandscapeInfo* CandidateInfo = LandscapeProxy->GetLandscapeInfo();
                if (IsValid(CandidateInfo))
                {
                    ++SelectedLandscapeCount;
                    TargetLandscape = LandscapeProxy;
                    LandscapeInfo = CandidateInfo;
                }
            }
        }
    }

    if (SelectedLandscapeCount != 1)
    {
        LastLandscapeSmoothError = TEXT("Select exactly one Landscape actor.");
        return false;
    }

    if (!bHasAnySelection || !IsValid(TargetLandscape) || !IsValid(LandscapeInfo))
    {
        LastLandscapeSmoothError = TEXT("Landscape selection is invalid.");
        return false;
    }

    // Edit Layers: our simple heightmap accessor path does not apply cleanly.
    if (TargetLandscape->HasLayersContent() || TargetLandscape->CanHaveLayersContent())
    {
        LastLandscapeSmoothError = TEXT("Landscape Edit Layers are enabled. Disable Edit Layers (Layers) on the Landscape to use this tool.");
        return false;
    }

    int32 MinX = 0;
    int32 MinY = 0;
    int32 MaxX = 0;
    int32 MaxY = 0;
    if (!LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
    {
        LastLandscapeSmoothError = TEXT("Failed to get Landscape extent.");
        return false;
    }

    const int32 Width = MaxX - MinX + 1;
    const int32 Height = MaxY - MinY + 1;
    if (Width <= 1 || Height <= 1)
    {
        LastLandscapeSmoothError = TEXT("Landscape extent is too small.");
        return false;
    }

    const int64 NumSamples64 = static_cast<int64>(Width) * static_cast<int64>(Height);
    if (NumSamples64 <= 0 || NumSamples64 > TNumericLimits<int32>::Max())
    {
        LastLandscapeSmoothError = TEXT("Landscape extent is too large.");
        return false;
    }
    const int32 NumSamples = static_cast<int32>(NumSamples64);

    const float Strength = FMath::Clamp(SmoothStrength, 0.0f, 1.0f);
    if (Strength <= KINDA_SMALL_NUMBER)
    {
        return true;
    }

    const int32 KernelRadius = 4;
    const float RangeSigma = FMath::Max(1.0f, LandscapeSmoothRangeSigma);
    const float TwoRangeSigmaSqInv = 1.0f / (2.0f * RangeSigma * RangeSigma);
    const float SpatialSigma = FMath::Max(1.0f, static_cast<float>(KernelRadius) * 0.75f);
    const float TwoSpatialSigmaSqInv = 1.0f / (2.0f * SpatialSigma * SpatialSigma);

    FScopedTransaction Transaction(LOCTEXT("CryImporterLandscapeAutoSmooth", "CryImporter Landscape Auto Smooth"));
    LandscapeInfo->Modify();

    FHeightmapAccessor<false> HeightAccessor(LandscapeInfo);

    TArray<uint16> SourceData;
    SourceData.SetNumUninitialized(NumSamples);
    HeightAccessor.GetDataFast(MinX, MinY, MaxX, MaxY, SourceData.GetData());

    TArray<uint16> SmoothedData;
    SmoothedData.SetNumUninitialized(SourceData.Num());
    if (SmoothedData.Num() != NumSamples)
    {
        LastLandscapeSmoothError = TEXT("Failed to allocate height buffers.");
        return false;
    }

    int64 SumBefore = 0;
    for (uint16 V : SourceData)
    {
        SumBefore += static_cast<int64>(V);
    }

    for (int32 Y = MinY; Y <= MaxY; ++Y)
    {
        const int32 SampleYMin = FMath::Max(MinY, Y - KernelRadius);
        const int32 SampleYMax = FMath::Min(MaxY, Y + KernelRadius);

        for (int32 X = MinX; X <= MaxX; ++X)
        {
            const int32 SampleXMin = FMath::Max(MinX, X - KernelRadius);
            const int32 SampleXMax = FMath::Min(MaxX, X + KernelRadius);

            int64 Sum = 0;
            int32 Count = 0;
            double WeightedSum = 0.0;
            double WeightTotal = 0.0;

            const int32 CenterIndex = (Y - MinY) * Width + (X - MinX);
            const double CenterH = static_cast<double>(SourceData[CenterIndex]);

            for (int32 SampleY = SampleYMin; SampleY <= SampleYMax; ++SampleY)
            {
                const int32 RowOffset = (SampleY - MinY) * Width;
                for (int32 SampleX = SampleXMin; SampleX <= SampleXMax; ++SampleX)
                {
                    const uint16 SampleH16 = SourceData[RowOffset + (SampleX - MinX)];
                    Sum += static_cast<int64>(SampleH16);
                    ++Count;

                    if (bLandscapeSmoothUseBilateral)
                    {
                        const double Dh = static_cast<double>(SampleH16) - CenterH;
                        const double RangeW = FMath::Exp(-Dh * Dh * TwoRangeSigmaSqInv);
                        const int32 Dx = SampleX - X;
                        const int32 Dy = SampleY - Y;
                        const double SpatialW = FMath::Exp(-(static_cast<double>(Dx * Dx + Dy * Dy)) * TwoSpatialSigmaSqInv);
                        const double W = RangeW * SpatialW;
                        WeightTotal += W;
                        WeightedSum += W * static_cast<double>(SampleH16);
                    }
                }
            }

            const int32 Index = CenterIndex;
            const float Current = static_cast<float>(SourceData[Index]);
            float Average = static_cast<float>(Sum) / static_cast<float>(Count);

            if (bLandscapeSmoothUseBilateral && WeightTotal > 1e-6)
            {
                Average = static_cast<float>(WeightedSum / WeightTotal);
            }

            const int32 Blended = FMath::RoundToInt(FMath::Lerp(Current, Average, Strength));
            SmoothedData[Index] = static_cast<uint16>(FMath::Clamp(Blended, 0, static_cast<int32>(LandscapeDataAccess::MaxValue)));
        }
    }

    if (bLandscapeSmoothPreserveMean)
    {
        int64 SumAfter = 0;
        for (uint16 V : SmoothedData)
        {
            SumAfter += static_cast<int64>(V);
        }

        const double MeanBefore = static_cast<double>(SumBefore) / static_cast<double>(NumSamples);
        const double MeanAfter = static_cast<double>(SumAfter) / static_cast<double>(NumSamples);
        const int32 Offset = FMath::RoundToInt(static_cast<float>(MeanBefore - MeanAfter));

        if (Offset != 0)
        {
            for (int32 Idx = 0; Idx < SmoothedData.Num(); ++Idx)
            {
                const int32 Adjusted = static_cast<int32>(SmoothedData[Idx]) + Offset;
                SmoothedData[Idx] = static_cast<uint16>(FMath::Clamp(Adjusted, 0, static_cast<int32>(LandscapeDataAccess::MaxValue)));
            }
        }
    }

    HeightAccessor.SetData(MinX, MinY, MaxX, MaxY, SmoothedData.GetData());
    HeightAccessor.Flush();

    return true;
}

bool FCryImporterModule::ConvertSelectedStaticMeshesToFoliage()
{
    if (!GEditor)
    {
        return false;
    }

    UWorld* World = GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>()->GetEditorWorld();
    if (!World)
    {
        return false;
    }

    USelection* Selection = GEditor->GetSelectedActors();
    if (!Selection || Selection->Num() == 0)
    {
        return false;
    }

    TMap<UStaticMesh*, TArray<FTransform>> TransformsByMesh;
    TArray<AActor*> ActorsToDelete;

    for (FSelectionIterator It(*Selection); It; ++It)
    {
        AActor* Actor = Cast<AActor>(*It);
        if (!Actor)
        {
            continue;
        }

        // Keep it conservative: only convert plain StaticMeshActors so we don't destroy complex actors unexpectedly.
        AStaticMeshActor* StaticMeshActor = Cast<AStaticMeshActor>(Actor);
        if (!StaticMeshActor)
        {
            continue;
        }

        UStaticMeshComponent* MeshComp = StaticMeshActor->GetStaticMeshComponent();
        if (!MeshComp)
        {
            continue;
        }

        UStaticMesh* Mesh = MeshComp->GetStaticMesh();
        if (!Mesh)
        {
            continue;
        }

        TransformsByMesh.FindOrAdd(Mesh).Add(MeshComp->GetComponentTransform());
        ActorsToDelete.Add(StaticMeshActor);
    }

    if (TransformsByMesh.Num() == 0)
    {
        return false;
    }

    AInstancedFoliageActor* FoliageActor = AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, true);
    if (!FoliageActor)
    {
        return false;
    }

    const FScopedTransaction Transaction(LOCTEXT("CryImporterConvertToFoliageTx", "CryImporter Convert To Foliage"));
    World->Modify();
    FoliageActor->Modify();

    int32 TotalInstances = 0;

    for (const TPair<UStaticMesh*, TArray<FTransform>>& Pair : TransformsByMesh)
    {
        UStaticMesh* Mesh = Pair.Key;
        const TArray<FTransform>& Xforms = Pair.Value;
        if (!Mesh || Xforms.Num() == 0)
        {
            continue;
        }

        UFoliageType* FoliageType = nullptr;
        FFoliageInfo* FoliageInfo = FoliageActor->AddMesh(Mesh, &FoliageType);
        if (!FoliageType)
        {
            FoliageType = FoliageActor->GetLocalFoliageTypeForSource(Mesh);
        }

        if (!FoliageType)
        {
            UE_LOG(LogTemp, Warning, TEXT("CryImporter: Failed to create foliage type for mesh '%s'."), *Mesh->GetPathName());
            continue;
        }

        if (!FoliageInfo)
        {
            FoliageInfo = FoliageActor->FindOrAddMesh(FoliageType);
        }

        if (!FoliageInfo)
        {
            UE_LOG(LogTemp, Warning, TEXT("CryImporter: Failed to get foliage info for mesh '%s'."), *Mesh->GetPathName());
            continue;
        }

        TArray<FFoliageInstance> Instances;
        Instances.Reserve(Xforms.Num());

        TArray<const FFoliageInstance*> InstancePtrs;
        InstancePtrs.Reserve(Xforms.Num());

        for (const FTransform& Xf : Xforms)
        {
            FFoliageInstance& NewInst = Instances.AddDefaulted_GetRef();
            NewInst.SetInstanceWorldTransform(Xf);
            InstancePtrs.Add(&NewInst);
        }

        FoliageInfo->AddInstances(FoliageType, InstancePtrs);
        TotalInstances += InstancePtrs.Num();
    }

    for (AActor* Actor : ActorsToDelete)
    {
        if (IsValid(Actor))
        {
            World->EditorDestroyActor(Actor, true);
        }
    }

    GEditor->SelectNone(false, true);

    UE_LOG(LogTemp, Display, TEXT("CryImporter: Converted %d instance(s) into foliage."), TotalInstances);
    return TotalInstances > 0;
}

bool FCryImporterModule::SnapSelectedActorsToLandscape()
{
    if (!GEditor)
    {
        return false;
    }

    UWorld* World = GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>()->GetEditorWorld();
    if (!World)
    {
        return false;
    }

    USelection* Selection = GEditor->GetSelectedActors();
    if (!Selection || Selection->Num() == 0)
    {
        return false;
    }

    const double TraceHalf = 10000000.0;
    int32 SnappedCount = 0;

    const FScopedTransaction Transaction(LOCTEXT("CryImporterSnapToLandscapeTx", "CryImporter Snap To Landscape"));
    World->Modify();

    for (FSelectionIterator It(*Selection); It; ++It)
    {
        AActor* Actor = Cast<AActor>(*It);
        if (!Actor)
        {
            continue;
        }

        AStaticMeshActor* StaticMeshActor = Cast<AStaticMeshActor>(Actor);
        if (!StaticMeshActor)
        {
            continue;
        }

        UStaticMeshComponent* MeshComp = StaticMeshActor->GetStaticMeshComponent();
        if (!MeshComp || !MeshComp->GetStaticMesh())
        {
            continue;
        }

        const FVector Loc = StaticMeshActor->GetActorLocation();
        const FVector Start(Loc.X, Loc.Y, Loc.Z + TraceHalf);
        const FVector End(Loc.X, Loc.Y, Loc.Z - TraceHalf);

        FHitResult Hit;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(CryImporterSnapToLandscape), false);
        Params.AddIgnoredActor(StaticMeshActor);

        bool bHitLandscape = false;

        if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
        {
            if (Hit.GetActor() && Hit.GetActor()->IsA<ALandscapeProxy>())
            {
                bHitLandscape = true;
            }
        }

        if (!bHitLandscape)
        {
            // Fallback channel in case the project collision settings don't route landscape to WorldStatic.
            if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
            {
                if (Hit.GetActor() && Hit.GetActor()->IsA<ALandscapeProxy>())
                {
                    bHitLandscape = true;
                }
            }
        }

        if (!bHitLandscape)
        {
            continue;
        }

        StaticMeshActor->Modify();
        StaticMeshActor->SetActorLocation(FVector(Loc.X, Loc.Y, Hit.Location.Z));
        ++SnappedCount;
    }

    UE_LOG(LogTemp, Display, TEXT("CryImporter: Snapped %d actor(s) to landscape."), SnappedCount);
    return SnappedCount > 0;
}

void FCryImporterModule::ExportSelectedGroup()
{
    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (!DesktopPlatform)
    {
        return;
    }

    FString DefaultPath = FPaths::ProjectSavedDir();
    FString DefaultFile = TEXT("UE_GroupExport.json");

    TArray<FString> OutFiles;
    const bool bSaved = DesktopPlatform->SaveFileDialog(
        nullptr,
        TEXT("Export Selected Group"),
        DefaultPath,
        DefaultFile,
        TEXT("JSON Files (*.json)|*.json"),
        EFileDialogFlags::None,
        OutFiles
    );

    if (!bSaved || OutFiles.Num() == 0)
    {
        return;
    }

    if (!ExportActorsToJson(OutFiles[0]))
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("ExportFailedMessage", "Export failed. Check Output Log for details."));
    }
}

bool FCryImporterModule::ExportActorsToJson(const FString& FilePath)
{
    if (!GEditor)
    {
        UE_LOG(LogTemp, Warning, TEXT("CryImporter: GEditor is not available for export."));
        return false;
    }

    USelection* Selection = GEditor->GetSelectedActors();
    if (!Selection || Selection->Num() == 0)
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoSelectionMessage", "Select one or more actors to export."));
        return false;
    }

    TArray<TSharedPtr<FJsonValue>> ActorArray;

    for (FSelectionIterator It(*Selection); It; ++It)
    {
        AActor* Actor = Cast<AActor>(*It);
        if (!Actor)
        {
            continue;
        }

        TSharedPtr<FJsonObject> ActorJson = MakeShared<FJsonObject>();
        ActorJson->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
        ActorJson->SetStringField(TEXT("actor_name"), Actor->GetName());
        ActorJson->SetStringField(TEXT("actor_class"), Actor->GetClass()->GetPathName());
        ActorJson->SetObjectField(TEXT("location"), CryImporter::MakeVectorObject(Actor->GetActorLocation()));
        ActorJson->SetObjectField(TEXT("rotation"), CryImporter::MakeRotatorObject(Actor->GetActorRotation()));
        ActorJson->SetObjectField(TEXT("scale"), CryImporter::MakeVectorObject(Actor->GetActorScale3D()));

        if (const AStaticMeshActor* StaticMeshActor = Cast<AStaticMeshActor>(Actor))
        {
            if (const UStaticMeshComponent* MeshComp = StaticMeshActor->GetStaticMeshComponent())
            {
                if (const UStaticMesh* Mesh = MeshComp->GetStaticMesh())
                {
                    ActorJson->SetStringField(TEXT("static_mesh"), Mesh->GetPathName());
                }
            }
        }

        ActorArray.Add(MakeShared<FJsonValueObject>(ActorJson));
    }

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("format"), TEXT("CryImporterExport"));
    Root->SetStringField(TEXT("version"), TEXT("1.0"));
    Root->SetStringField(TEXT("exported_at"), FDateTime::Now().ToIso8601());
    Root->SetNumberField(TEXT("actor_count"), ActorArray.Num());
    Root->SetArrayField(TEXT("actors"), ActorArray);

    FString Output;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
    {
        UE_LOG(LogTemp, Warning, TEXT("CryImporter: Failed to serialize export JSON."));
        return false;
    }

    if (!FFileHelper::SaveStringToFile(Output, *FilePath))
    {
        UE_LOG(LogTemp, Warning, TEXT("CryImporter: Failed to write export file '%s'."), *FilePath);
        return false;
    }

    UE_LOG(LogTemp, Display, TEXT("CryImporter: Exported %d actor(s) to '%s'."), ActorArray.Num(), *FilePath);
    return true;
}

void FCryImporterModule::ImportGRP(const FString& FilePath)
{
    FString XmlContent;
    if (!FFileHelper::LoadFileToString(XmlContent, *FilePath))
    {
        UE_LOG(LogTemp, Warning, TEXT("CryImporter: Failed to read GRP '%s'."), *FilePath);
        return;
    }

    FXmlFile XmlFile(XmlContent, EConstructMethod::ConstructFromBuffer);
    if (!XmlFile.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("CryImporter: Invalid XML in GRP '%s'."), *FilePath);
        return;
    }

    FXmlNode* RootNode = XmlFile.GetRootNode();
    if (!RootNode)
    {
        return;
    }

    UWorld* World = GEditor ? GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>()->GetEditorWorld() : nullptr;
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("CryImporter: No editor world available."));
        return;
    }

    const FScopedTransaction Transaction(LOCTEXT("CryImportTransaction", "Import CryEngine GRP"));
    World->Modify();

    const FVector LandscapeCenterOffset = FVector::ZeroVector;

    TMap<FString, UStaticMesh*> MeshByPathKey;
    TMultiMap<FString, UStaticMesh*> MeshByShortName;
    TMap<FString, FVector> LastKnownLocationByLayer;
    int32 DebugLoggedTransforms = 0;

    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

    FARFilter Filter;
    const FString SearchRoot = CryImporter::SanitizeAssetSearchRootPath(AssetSearchRootPath);
    AssetSearchRootPath = SearchRoot;
    Filter.PackagePaths.Add(FName(*SearchRoot));
    Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
    Filter.bRecursivePaths = true;

    TArray<FAssetData> AssetList;
    AssetRegistryModule.Get().GetAssets(Filter, AssetList);

    for (const FAssetData& Asset : AssetList)
    {
        UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.GetAsset());
        if (!Mesh)
        {
            continue;
        }

        MeshByPathKey.Add(CryImporter::BuildAssetPathKey(Asset), Mesh);

        TArray<FString> NameVariants;
        CryImporter::AddUniqueString(NameVariants, Asset.AssetName.ToString().ToLower());
        CryImporter::AddUniqueString(NameVariants, CryImporter::NormalizeNameKey(Asset.AssetName.ToString()));

        for (const FString& NameKey : NameVariants)
        {
            MeshByShortName.Add(NameKey, Mesh);
        }
    }

    TFunction<void(FXmlNode*)> ParseNode;
    ParseNode = [&](FXmlNode* Node)
    {
        if (!Node)
        {
            return;
        }

        if (Node->GetTag() == TEXT("Object"))
        {
            const FString PrefabPath = Node->GetAttribute(TEXT("Prefab"));
            const FString PosString = Node->GetAttribute(TEXT("Pos"));
            const FString AnglesString = Node->GetAttribute(TEXT("Angles"));
            const FString ScaleString = Node->GetAttribute(TEXT("Scale"));
            const FString MaterialString = Node->GetAttribute(TEXT("Material"));
            const FString LayerName = Node->GetAttribute(TEXT("Layer"));
            const FString ParentId = Node->GetAttribute(TEXT("Parent"));

            if (!PrefabPath.IsEmpty())
            {
                if (bSkipCollisionLikeGeometry
                    && (CryImporter::IsCollisionLikeMarker(PrefabPath) || CryImporter::IsCollisionLikeMarker(MaterialString)))
                {
                    return;
                }

                UStaticMesh* SelectedMesh = nullptr;

                const TArray<FString> PathKeys = CryImporter::BuildPrefabPathKeyVariants(PrefabPath);
                for (const FString& PathKey : PathKeys)
                {
                    if (UStaticMesh** FoundByPath = MeshByPathKey.Find(PathKey))
                    {
                        SelectedMesh = *FoundByPath;
                        break;
                    }
                }

                if (!SelectedMesh)
                {
                    TArray<UStaticMesh*> CandidatesUnique;
                    const TArray<FString> NameKeys = CryImporter::BuildShortNameVariants(PrefabPath);

                    for (const FString& NameKey : NameKeys)
                    {
                        TArray<UStaticMesh*> Candidates;
                        MeshByShortName.MultiFind(NameKey, Candidates);

                        for (UStaticMesh* Candidate : Candidates)
                        {
                            CandidatesUnique.AddUnique(Candidate);
                        }
                    }

                    if (CandidatesUnique.Num() == 1)
                    {
                        SelectedMesh = CandidatesUnique[0];
                    }
                    else if (CandidatesUnique.Num() > 1)
                    {
                        UE_LOG(LogTemp, Warning,
                            TEXT("CryImporter: Ambiguous short-name match for '%s' (%d candidates). Skipping object."),
                            *FPaths::GetBaseFilename(PrefabPath),
                            CandidatesUnique.Num());
                    }
                }

                if (SelectedMesh)
                {
                    FVector Location = FVector::ZeroVector;
                    FVector CryPos = FVector::ZeroVector;
                    const bool bHasExplicitPos = CryImporter::ParseVector3Csv(PosString, CryPos);
                    if (bHasExplicitPos)
                    {
                        Location = CryImporter::BuildFinalPlacementLocation(CryPos) + LandscapeCenterOffset;

                        if (DebugLoggedTransforms < 5)
                        {
                            const FVector LocalPosition = CryImporter::ConvertCryPositionToUE(CryPos);
                            const FVector RotatedPosition = CryImporter::ApplyGlobalGroupYawToPosition(LocalPosition);
                            UE_LOG(LogTemp, Display,
                                TEXT("CryImporter: GRP transform '%s' cryPos=(%.3f, %.3f, %.3f) localUE=(%.3f, %.3f, %.3f) rotated=(%.3f, %.3f, %.3f) offset=(%.3f, %.3f, %.3f) final=(%.3f, %.3f, %.3f)"),
                                *FPaths::GetBaseFilename(PrefabPath),
                                CryPos.X, CryPos.Y, CryPos.Z,
                                LocalPosition.X, LocalPosition.Y, LocalPosition.Z,
                                RotatedPosition.X, RotatedPosition.Y, RotatedPosition.Z,
                                CryImporter::GlobalPlacementOffset.X, CryImporter::GlobalPlacementOffset.Y, CryImporter::GlobalPlacementOffset.Z,
                                Location.X, Location.Y, Location.Z);
                            ++DebugLoggedTransforms;
                        }
                    }
                    else if (!ParentId.IsEmpty() && !LayerName.IsEmpty())
                    {
                        if (const FVector* LayerFallbackLocation = LastKnownLocationByLayer.Find(LayerName))
                        {
                            Location = *LayerFallbackLocation;
                            UE_LOG(LogTemp, Warning,
                                TEXT("CryImporter: Object '%s' has Parent='%s' but no Pos. Using last known location from Layer '%s'."),
                                *FPaths::GetBaseFilename(PrefabPath),
                                *ParentId,
                                *LayerName);
                        }
                        else
                        {
                            UE_LOG(LogTemp, Warning,
                                TEXT("CryImporter: Object '%s' has Parent='%s' and no Pos, with no layer fallback for '%s'. Skipping object."),
                                *FPaths::GetBaseFilename(PrefabPath),
                                *ParentId,
                                *LayerName);
                            return;
                        }
                    }

                    FRotator Rotation = FRotator::ZeroRotator;
                    FVector CryAngles = FVector::ZeroVector;
                    if (CryImporter::ParseVector3Csv(AnglesString, CryAngles))
                    {
                        Rotation = CryImporter::BuildFinalPlacementRotation(CryAngles);
                    }

                    FVector Scale3D(1.0f, 1.0f, 1.0f);
                    FVector CryScale = FVector::ZeroVector;
                    if (CryImporter::ParseVector3Csv(ScaleString, CryScale))
                    {
                        Scale3D = CryImporter::ConvertCryScaleToUE(CryScale);
                    }

                    AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Location, Rotation);
                    if (Actor)
                    {
                        Actor->SetFlags(RF_Transactional);
                        Actor->Modify();

                        if (UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent())
                        {
                            MeshComp->SetStaticMesh(SelectedMesh);
                        }

                        Actor->SetActorScale3D(Scale3D);
                        Actor->SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
                        Actor->SetActorLabel(FPaths::GetBaseFilename(PrefabPath));

                        if (bImportPhysicsMaterialMetadata && !MaterialString.IsEmpty())
                        {
                            CryImporter::AddTagIfMissing(Actor, FString::Printf(TEXT("CryPhysMat:%s"), *MaterialString));
                        }

                        if (!LayerName.IsEmpty())
                        {
                            LastKnownLocationByLayer.Add(LayerName, Location);
                        }
                    }
                }
            }
        }

        for (FXmlNode* Child : Node->GetChildrenNodes())
        {
            ParseNode(Child);
        }
    };

    ParseNode(RootNode);
}

void FCryImporterModule::ImportVEG(const FString& FilePath)
{
    FString XmlContent;
    if (!FFileHelper::LoadFileToString(XmlContent, *FilePath))
    {
        UE_LOG(LogTemp, Warning, TEXT("CryImporter: Failed to read VEG '%s'."), *FilePath);
        return;
    }

    FXmlFile XmlFile(XmlContent, EConstructMethod::ConstructFromBuffer);
    if (!XmlFile.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("CryImporter: Invalid XML in VEG '%s'."), *FilePath);
        return;
    }

    FXmlNode* RootNode = XmlFile.GetRootNode();
    if (!RootNode || RootNode->GetTag() != TEXT("Vegetation"))
    {
        UE_LOG(LogTemp, Warning, TEXT("CryImporter: VEG '%s' has unexpected root node."), *FilePath);
        return;
    }

    UWorld* World = GEditor ? GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>()->GetEditorWorld() : nullptr;
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("CryImporter: No editor world available."));
        return;
    }

    const FScopedTransaction Transaction(LOCTEXT("CryImportVegTransaction", "Import CryEngine VEG"));
    World->Modify();

    const FVector LandscapeCenterOffset = FVector::ZeroVector;

    TMap<FString, UStaticMesh*> MeshByPathKey;
    TMultiMap<FString, UStaticMesh*> MeshByShortName;

    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

    FARFilter Filter;
    const FString SearchRoot = CryImporter::SanitizeAssetSearchRootPath(AssetSearchRootPath);
    AssetSearchRootPath = SearchRoot;
    Filter.PackagePaths.Add(FName(*SearchRoot));
    Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
    Filter.bRecursivePaths = true;

    TArray<FAssetData> AssetList;
    AssetRegistryModule.Get().GetAssets(Filter, AssetList);

    for (const FAssetData& Asset : AssetList)
    {
        UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.GetAsset());
        if (!Mesh)
        {
            continue;
        }

        MeshByPathKey.Add(CryImporter::BuildAssetPathKey(Asset), Mesh);

        TArray<FString> NameVariants;
        CryImporter::AddUniqueString(NameVariants, Asset.AssetName.ToString().ToLower());
        CryImporter::AddUniqueString(NameVariants, CryImporter::NormalizeNameKey(Asset.AssetName.ToString()));
        for (const FString& NameKey : NameVariants)
        {
            MeshByShortName.Add(NameKey, Mesh);
        }
    }

    int32 SpawnedCount = 0;
    int32 SkippedObjects = 0;
    int32 DebugLoggedTransforms = 0;
    for (FXmlNode* VegetationObjectNode : RootNode->GetChildrenNodes())
    {
        if (!VegetationObjectNode || VegetationObjectNode->GetTag() != TEXT("VegetationObject"))
        {
            continue;
        }

        const FString PrefabPath = VegetationObjectNode->GetAttribute(TEXT("FileName"));
        const FString MaterialString = VegetationObjectNode->GetAttribute(TEXT("Material"));
        const FString CategoryString = VegetationObjectNode->GetAttribute(TEXT("Category"));
        if (PrefabPath.IsEmpty())
        {
            ++SkippedObjects;
            continue;
        }

        if (bSkipCollisionLikeGeometry
            && (CryImporter::IsCollisionLikeMarker(PrefabPath)
                || CryImporter::IsCollisionLikeMarker(MaterialString)
                || CryImporter::IsCollisionLikeMarker(CategoryString)))
        {
            ++SkippedObjects;
            continue;
        }

        UStaticMesh* SelectedMesh = nullptr;
        if (!CryImporter::TryResolveMeshByCryPath(PrefabPath, MeshByPathKey, MeshByShortName, SelectedMesh) || !SelectedMesh)
        {
            ++SkippedObjects;
            continue;
        }

        float ObjectSize = 1.0f;
        if (!CryImporter::TryParseFloat(VegetationObjectNode->GetAttribute(TEXT("Size")), ObjectSize) || ObjectSize <= 0.0f)
        {
            ObjectSize = 1.0f;
        }

        FXmlNode* InstancesNode = nullptr;
        for (FXmlNode* ChildNode : VegetationObjectNode->GetChildrenNodes())
        {
            if (ChildNode && ChildNode->GetTag() == TEXT("Instances"))
            {
                InstancesNode = ChildNode;
                break;
            }
        }

        if (!InstancesNode)
        {
            ++SkippedObjects;
            continue;
        }

        for (FXmlNode* InstanceNode : InstancesNode->GetChildrenNodes())
        {
            if (!InstanceNode || InstanceNode->GetTag() != TEXT("Instance"))
            {
                continue;
            }

            FVector CryPos = FVector::ZeroVector;
            if (!CryImporter::ParseVector3Csv(InstanceNode->GetAttribute(TEXT("Pos")), CryPos))
            {
                continue;
            }

            const FVector LocalPosition = CryImporter::ConvertCryPositionToUE(CryPos);
            const FVector RotatedPosition = CryImporter::ApplyGlobalGroupYawToPosition(LocalPosition);
            FVector Location = CryImporter::BuildFinalPlacementLocation(CryPos) + LandscapeCenterOffset;

            if (DebugLoggedTransforms < 5)
            {
                UE_LOG(LogTemp, Display,
                    TEXT("CryImporter: VEG transform '%s' cryPos=(%.3f, %.3f, %.3f) localUE=(%.3f, %.3f, %.3f) rotated=(%.3f, %.3f, %.3f) offset=(%.3f, %.3f, %.3f) final=(%.3f, %.3f, %.3f)"),
                    *FPaths::GetBaseFilename(PrefabPath),
                    CryPos.X, CryPos.Y, CryPos.Z,
                    LocalPosition.X, LocalPosition.Y, LocalPosition.Z,
                    RotatedPosition.X, RotatedPosition.Y, RotatedPosition.Z,
                    CryImporter::GlobalPlacementOffset.X, CryImporter::GlobalPlacementOffset.Y, CryImporter::GlobalPlacementOffset.Z,
                    Location.X, Location.Y, Location.Z);
                ++DebugLoggedTransforms;
            }

            FRotator Rotation = FRotator::ZeroRotator;
            const FString InstanceAngles = InstanceNode->GetAttribute(TEXT("Angles"));
            const FString ObjectAngles = VegetationObjectNode->GetAttribute(TEXT("Angles"));
            const FString AnglesString = !InstanceAngles.IsEmpty() ? InstanceAngles : ObjectAngles;
            FVector CryAngles = FVector::ZeroVector;
            if (CryImporter::ParseVector3Csv(AnglesString, CryAngles))
            {
                Rotation = CryImporter::BuildFinalPlacementRotation(CryAngles);
            }

            // In .veg files, per-instance Scale is typically the final baked scale.
            // VegetationObject Size is more of a brush/preset value; use it only as fallback.
            float InstanceScale = 0.0f;
            if (!CryImporter::TryParseFloat(InstanceNode->GetAttribute(TEXT("Scale")), InstanceScale) || InstanceScale <= 0.0f)
            {
                InstanceScale = ObjectSize > 0.0f ? ObjectSize : 1.0f;
            }

            const float FinalUniformScale = InstanceScale;
            const FVector Scale3D = CryImporter::ConvertCryScaleToUE(
                FVector(FinalUniformScale, FinalUniformScale, FinalUniformScale));

            AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Location, Rotation);
            if (!Actor)
            {
                continue;
            }

            Actor->SetFlags(RF_Transactional);
            Actor->Modify();

            if (UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent())
            {
                MeshComp->SetStaticMesh(SelectedMesh);
            }

            Actor->SetActorScale3D(Scale3D);
            Actor->SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
            Actor->SetActorLabel(FPaths::GetBaseFilename(PrefabPath));
            if (bImportPhysicsMaterialMetadata && !MaterialString.IsEmpty())
            {
                CryImporter::AddTagIfMissing(Actor, FString::Printf(TEXT("CryPhysMat:%s"), *MaterialString));
            }
            ++SpawnedCount;
        }
    }

    UE_LOG(LogTemp, Display, TEXT("CryImporter: VEG import complete. Spawned %d instance(s), skipped %d vegetation object(s)."), SpawnedCount, SkippedObjects);
}

#undef LOCTEXT_NAMESPACE
IMPLEMENT_MODULE(FCryImporterModule, CryImporter)























