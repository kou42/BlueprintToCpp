#include "BPGeneratedCodeSaver.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
bool IsSafeFileName(const FString& FileName, const FString& Extension)
{
    if (FileName.IsEmpty() || FileName.EndsWith(Extension) == false ||
        FPaths::GetCleanFilename(FileName) != FileName)
    {
        return false;
    }

    // 相対パスや特殊なファイル名を受け付けず、保存先ディレクトリを固定する。
    for (const TCHAR Character : FileName)
    {
        const bool bAlpha = (Character >= TEXT('A') && Character <= TEXT('Z')) ||
            (Character >= TEXT('a') && Character <= TEXT('z'));
        const bool bDigit = Character >= TEXT('0') && Character <= TEXT('9');
        if (bAlpha == false && bDigit == false && Character != TEXT('_') &&
            Character != TEXT('.'))
        {
            return false;
        }
    }
    return FileName.StartsWith(TEXT(".")) == false &&
        FileName.Contains(TEXT("..")) == false;
}
}

bool FBPGeneratedCodeSaver::SaveNewFilesSplit(const FBPGeneratedActorFiles& Files,
    const FString& HeaderDirectory, const FString& SourceDirectory, FString& OutError)
{
    OutError.Reset();
    if (HeaderDirectory.IsEmpty() || SourceDirectory.IsEmpty() ||
        IsSafeFileName(Files.HeaderFileName, TEXT(".h")) == false ||
        IsSafeFileName(Files.SourceFileName, TEXT(".cpp")) == false ||
        Files.HeaderContent.IsEmpty() || Files.SourceContent.IsEmpty())
    {
        OutError = TEXT("Invalid output directory or generated file contents.");
        return false;
    }

    IFileManager& Manager = IFileManager::Get();
    const FString HeaderDir = FPaths::ConvertRelativePathToFull(HeaderDirectory);
    const FString SourceDir = FPaths::ConvertRelativePathToFull(SourceDirectory);
    if (Manager.DirectoryExists(*HeaderDir) == false || Manager.DirectoryExists(*SourceDir) == false)
    {
        OutError = TEXT("Output directory does not exist. Create it before saving.");
        return false;
    }

    const FString HeaderPath = FPaths::Combine(HeaderDir, Files.HeaderFileName);
    const FString SourcePath = FPaths::Combine(SourceDir, Files.SourceFileName);
    if (Manager.FileExists(*HeaderPath) || Manager.FileExists(*SourcePath))
    {
        OutError = TEXT("One or both target files already exist. No files were overwritten.");
        return false;
    }

    // 一時ファイルを別名で作り、両方の書き込み成功後に正式名へ移動する。
    // 失敗時には今回作成した一時ファイルと移動済みファイルを削除する。
    const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString TempHeader = HeaderPath + TEXT(".") + Token + TEXT(".tmp");
    const FString TempSource = SourcePath + TEXT(".") + Token + TEXT(".tmp");

    if (FFileHelper::SaveStringToFile(Files.HeaderContent, *TempHeader,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) == false)
    {
        OutError = TEXT("Failed to write temporary header file.");
        return false;
    }
    if (FFileHelper::SaveStringToFile(Files.SourceContent, *TempSource,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) == false)
    {
        Manager.Delete(*TempHeader);
        Manager.Delete(*TempSource);
        OutError = TEXT("Failed to write temporary source file.");
        return false;
    }

    // Moveは既存ファイルを置換しない設定で実行する。競合が起きた場合は停止。
    if (Manager.Move(*HeaderPath, *TempHeader, false, false) == false)
    {
        Manager.Delete(*TempHeader);
        Manager.Delete(*TempSource);
        OutError = TEXT("Failed to finalize header file; no generated files committed.");
        return false;
    }
    if (Manager.Move(*SourcePath, *TempSource, false, false) == false)
    {
        const bool bRolledBack = Manager.Delete(*HeaderPath);
        Manager.Delete(*TempSource);
        OutError = bRolledBack
            ? TEXT("Failed to finalize source file; header was rolled back.")
            : TEXT("Failed to finalize source file; header rollback failed. Check output directory.");
        return false;
    }
    return true;
}

bool FBPGeneratedCodeSaver::SaveNewFiles(const FBPGeneratedActorFiles& Files,
    const FString& Directory, FString& OutError)
{
    return SaveNewFilesSplit(Files, Directory, Directory, OutError);
}
