#include "BPSemanticJsonExporter.h"

#include "BPSemanticJsonSerializer.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

bool FBPSemanticJsonExporter::Export(
    const FBPSemanticIR& IR, const FString& EngineVersion, FString& OutPath, FString& OutError)
{
    OutPath.Reset();
    OutError.Reset();
    if (IR.AssetPath.IsEmpty())
    {
        OutError = TEXT("Semantic IR asset path is empty");
        return false;
    }

    FString Json;
    if (FBPSemanticJsonSerializer::Serialize(IR, EngineVersion, Json) == false)
    {
        OutError = TEXT("Semantic JSON serialization failed");
        return false;
    }

    // アセットのフルパスをファイル名に変換し、保存先は専用のSavedフォルダに限定する。
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BlueprintSemanticJson"));
    if (IFileManager::Get().MakeDirectory(*Directory, true) == false)
    {
        OutError = TEXT("Could not create Semantic JSON directory");
        return false;
    }

    const FString Filename = FPaths::MakeValidFileName(IR.AssetPath) + TEXT(".semantic.json");
    const FString Candidate = FPaths::Combine(Directory, Filename);
    const FString FullDirectory = FPaths::ConvertRelativePathToFull(Directory);
    const FString FullCandidate = FPaths::ConvertRelativePathToFull(Candidate);
    if (FPaths::IsUnderDirectory(FullCandidate, FullDirectory) == false)
    {
        OutError = TEXT("Semantic JSON path is outside the output directory");
        return false;
    }

    if (FFileHelper::SaveStringToFile(
        Json, *FullCandidate, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) == false)
    {
        OutError = TEXT("Could not save Semantic JSON");
        return false;
    }
    OutPath = FullCandidate;
    return true;
}
