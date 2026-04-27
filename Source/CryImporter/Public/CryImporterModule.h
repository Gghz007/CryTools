#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Types/SlateEnums.h"

class SDockTab;
class FSpawnTabArgs;
class FReply;

class CRYIMPORTER_API FCryImporterModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    void OpenImportDialog();
    void OpenImportVEGDialog();
    void OpenImportGeometryDialog();
    void OpenImportAnimationDialog();
    void OpenExportDialog();
    FString GetAssetSearchRootPath() const;
    void SetAssetSearchRootPath(const FString& InPath);

private:
    void RegisterMenus();
    void OpenPluginWindow();

    TSharedRef<SDockTab> OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs);

    FReply HandleImportButtonClicked();
    FReply HandleImportVEGButtonClicked();
    FReply HandleImportGeometryButtonClicked();
    FReply HandleImportAnimationButtonClicked();
    FReply HandleExportButtonClicked();
    FReply HandleResetAssetSearchPathClicked();
    FReply HandleResetGameTexturesRootPathClicked();
    FReply HandleResetImportSaveRootPathClicked();
    FReply HandleLandscapeSmoothClicked();
    FReply HandleConvertSelectionToFoliageClicked();
    FReply HandleSnapSelectionToLandscapeClicked();

    void ImportButtonClicked();
    void ImportVEGButtonClicked();
    void ImportGeometryButtonClicked();
    void ImportAnimationButtonClicked();
    bool ImportCryGeometryFile(const FString& FilePath);
    void ImportCryAnimationFile(const FString& FilePath);
    void ExportSelectedGroup();
    void OnAssetSearchPathCommitted(const FText& InText, ETextCommit::Type CommitType);
    void OnGameTexturesRootPathCommitted(const FText& InText, ETextCommit::Type CommitType);
    void OnImportSaveRootPathCommitted(const FText& InText, ETextCommit::Type CommitType);
    void OnSkipCollisionLikeGeometryChanged(ECheckBoxState InState);
    void OnImportPhysicsMaterialMetadataChanged(ECheckBoxState InState);
    void OnCreateMeshAssetCopiesChanged(ECheckBoxState InState);
    void OnCreateMaterialAssetsChanged(ECheckBoxState InState);
    void OnCreateTextureAssetsChanged(ECheckBoxState InState);
    void OnUseLandscapeCenterOriginChanged(ECheckBoxState InState);
    ECheckBoxState GetSkipCollisionLikeGeometryState() const;
    ECheckBoxState GetImportPhysicsMaterialMetadataState() const;
    ECheckBoxState GetCreateMeshAssetCopiesState() const;
    ECheckBoxState GetCreateMaterialAssetsState() const;
    ECheckBoxState GetCreateTextureAssetsState() const;
    ECheckBoxState GetUseLandscapeCenterOriginState() const;

    void ImportGRP(const FString& FilePath);
    void ImportVEG(const FString& FilePath);
    bool ExportActorsToJson(const FString& FilePath);
    bool ApplyLandscapeSmooth(float SmoothStrength);
    bool ConvertSelectedStaticMeshesToFoliage();
    bool SnapSelectedActorsToLandscape();
    void OnLandscapeSmoothPreserveMeanChanged(ECheckBoxState InState);
    void OnLandscapeSmoothBilateralChanged(ECheckBoxState InState);
    void OnLandscapeSmoothRangeSigmaCommitted(const FText& InText, ETextCommit::Type CommitType);
    ECheckBoxState GetLandscapeSmoothPreserveMeanState() const;
    ECheckBoxState GetLandscapeSmoothBilateralState() const;

    FString AssetSearchRootPath;
    FString GameTexturesRootPath;
    FString ImportSaveRootPath;
    FString LastGeometryImportError;
    FString LastLandscapeSmoothError;
    float LandscapeSmoothStrength = 0.3f;
    bool bLandscapeSmoothPreserveMean = true;
    bool bLandscapeSmoothUseBilateral = true;
    float LandscapeSmoothRangeSigma = 256.0f;
    bool bSkipCollisionLikeGeometry = true;
    bool bImportPhysicsMaterialMetadata = true;
    bool bCreateMeshAssetCopies = true;
    bool bCreateMaterialAssets = true;
    bool bCreateTextureAssets = false;
    bool bUseLandscapeCenterOrigin = true;
};
