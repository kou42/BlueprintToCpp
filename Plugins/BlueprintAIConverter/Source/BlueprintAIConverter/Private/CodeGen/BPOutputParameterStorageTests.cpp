#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BPOutputParameterStorage.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPOutputParameterStorageTest,
    "BlueprintAIConverter.SemanticIR.Emitter.OutputParameterStorage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPOutputParameterStorageTest::RunTest(const FString& Parameters)
{
    FBPSemanticType Type;
    Type.Kind = EBPSemanticTypeKind::Struct;
    Type.TypePath = TEXT("/Script/Engine.HitResult");
    const FGuid NodeId = FGuid::NewGuid();
    FString Name;
    FString Declaration;
    TestTrue(TEXT("HitResult storage generated"),
        FBPOutputParameterStorage::Emit(Type, NodeId, Name, Declaration));
    TestTrue(TEXT("Declaration uses FHitResult"),
        Declaration.StartsWith(TEXT("FHitResult BP_Hit_")));
    TestTrue(TEXT("Declaration initializes storage"),
        Declaration.EndsWith(TEXT("{};")));
    TestTrue(TEXT("Identifier derived from node GUID"),
        Name.Contains(NodeId.ToString(EGuidFormats::Digits)));
    Type.TypePath = TEXT("/Script/CoreUObject.Vector");
    TestFalse(TEXT("Other struct rejected"),
        FBPOutputParameterStorage::Emit(Type, NodeId, Name, Declaration));
    TestTrue(TEXT("Failure clears name"), Name.IsEmpty());
    TestTrue(TEXT("Failure clears declaration"), Declaration.IsEmpty());
    return true;
}
#endif
