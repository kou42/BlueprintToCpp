#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BPGraphIR.h"
#include "BPSemanticFunctionExtractor.h"
#include "BPSemanticGraphTraceExtractor.h"
#include "BPSemanticJsonExporter.h"
#include "BPSemanticVariableExtractor.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/EngineVersion.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPGeneratedVariableAssetTest,
    "BlueprintAIConverter.GeneratedAssets.Variables",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPGeneratedVariableAssetTest::RunTest(const FString& Parameters)
{
    // 再実行可能な固定パスに保存し、Content Browserからテスト用BPを確認できるようにする。
    const FString PackagePath = TEXT("/Game/BlueprintToCppTests/BP_Test_Variables");
    UPackage* Package = CreatePackage(*PackagePath);
    if (TestNotNull(TEXT("保存先パッケージ"), Package) == false)
    {
        return false;
    }

    // 既存アセットをロードしてから再利用し、Editor再起動後も重複生成を防ぐ。
    UBlueprint* Blueprint = LoadObject<UBlueprint>(
        nullptr, TEXT("/Game/BlueprintToCppTests/BP_Test_Variables.BP_Test_Variables"));
    if (Blueprint == nullptr)
    {
        Blueprint = FindObject<UBlueprint>(Package, TEXT("BP_Test_Variables"));
    }
    if (Blueprint == nullptr)
    {
        Blueprint = FKismetEditorUtilities::CreateBlueprint(
            AActor::StaticClass(), Package, TEXT("BP_Test_Variables"),
            BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    }
    if (TestNotNull(TEXT("永続Blueprintを生成"), Blueprint) == false)
    {
        return false;
    }

    struct FVariableCase
    {
        const TCHAR* Name;
        FName Category;
        FName SubCategory;
        const TCHAR* DefaultValue;
    };
    const FVariableCase Cases[] = {
        { TEXT("Enabled"), UEdGraphSchema_K2::PC_Boolean, NAME_None, TEXT("true") },
        { TEXT("Health"), UEdGraphSchema_K2::PC_Int, NAME_None, TEXT("42") },
        { TEXT("LargeCount"), UEdGraphSchema_K2::PC_Int64, NAME_None, TEXT("9000000000") },
        { TEXT("Speed"), UEdGraphSchema_K2::PC_Real, UEdGraphSchema_K2::PC_Float, TEXT("1.25") },
        { TEXT("Precision"), UEdGraphSchema_K2::PC_Real, UEdGraphSchema_K2::PC_Double, TEXT("2.5") },
        { TEXT("Title"), UEdGraphSchema_K2::PC_String, NAME_None, TEXT("Generated Test") },
        { TEXT("Tag"), UEdGraphSchema_K2::PC_Name, NAME_None, TEXT("Runner") }
    };

    for (const FVariableCase& Case : Cases)
    {
        // 既存アセットを再利用した場合、同名変数を重複登録しない。
        const FName VariableName(Case.Name);
        if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VariableName) != INDEX_NONE)
        {
            continue;
        }
        FEdGraphPinType PinType;
        PinType.PinCategory = Case.Category;
        PinType.PinSubCategory = Case.SubCategory;
        if (TestTrue(FString::Printf(TEXT("%s を追加"), Case.Name),
            FBlueprintEditorUtils::AddMemberVariable(
                Blueprint, VariableName, PinType, Case.DefaultValue)) == false)
        {
            return false;
        }
    }

    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (TestTrue(TEXT("Blueprintコンパイルエラーなし"),
        Blueprint->Status != BS_Error) == false)
    {
        return false;
    }
    if (TestNotNull(TEXT("生成クラス"), Blueprint->GeneratedClass.Get()) == false)
    {
        return false;
    }

    FBPGraphIR GraphIR;
    if (TestTrue(TEXT("実BPからGraph IRを生成"),
        FBPGraphIRBuilder::Build(Blueprint, GraphIR)) == false)
    {
        return false;
    }
    TestEqual(TEXT("生成した変数数"), GraphIR.BlueprintVariableCount,
        static_cast<int32>(UE_ARRAY_COUNT(Cases)));

    FBPSemanticIR SemanticIR;
    SemanticIR.AssetPath = GraphIR.AssetPath;
    SemanticIR.ParentClassPath = GraphIR.ParentClassPath;
    FBPSemanticVariableExtractor::Append(Blueprint, SemanticIR);
    FBPSemanticFunctionExtractor::Append(Blueprint, SemanticIR);
    FBPSemanticGraphTraceExtractor::Append(GraphIR, SemanticIR);

    FString JsonPath;
    FString JsonError;
    if (TestTrue(TEXT("既存ExporterでJSONを出力"),
        FBPSemanticJsonExporter::Export(
            SemanticIR, FEngineVersion::Current().ToString(), JsonPath, JsonError)) == false)
    {
        AddError(JsonError);
        return false;
    }

    // BPはContent以下、JSONは既存ExporterのSaved以下に保存する。
    Blueprint->MarkPackageDirty();
    const FString Filename = FPackageName::LongPackageNameToFilename(
        PackagePath, FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_None;
    if (TestTrue(TEXT("BP_Test_Variables.uassetを保存"),
        UPackage::SavePackage(Package, Blueprint, *Filename, SaveArgs)) == false)
    {
        return false;
    }
    AddInfo(FString::Printf(TEXT("GENERATED_BLUEPRINT=%s"), *Filename));
    AddInfo(FString::Printf(TEXT("GENERATED_JSON=%s"), *JsonPath));
    return true;
}

#endif
