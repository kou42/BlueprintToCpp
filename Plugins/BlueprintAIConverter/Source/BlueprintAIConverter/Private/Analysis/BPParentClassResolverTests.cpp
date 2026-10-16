#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BPEmitterTestActor.h"
#include "BPParentClassResolver.h"
#include "Components/ActorComponent.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Pawn.h"
#include "Kismet2/KismetEditorUtilities.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPParentClassResolverNativeTest,
    "BlueprintAIConverter.CodeGenerator.ParentClass.NativeActorDerived",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPParentClassResolverNativeTest::RunTest(const FString& Parameters)
{
    FBPResolvedParentClass Parent;
    TArray<FString> Diagnostics;
    TestTrue(TEXT("Pawnを解決"), FBPParentClassResolver::ResolveActorParent(
        APawn::StaticClass()->GetPathName(), Parent, Diagnostics));
    TestEqual(TEXT("Pawn C++名"), Parent.CppClassName, FString(TEXT("APawn")));
    TestEqual(TEXT("Pawnヘッダー"), Parent.HeaderPath, FString(TEXT("GameFramework/Pawn.h")));
    TestEqual(TEXT("Actorの生成prefix"), Parent.GeneratedClassPrefix, FString(TEXT("A")));
    TestTrue(TEXT("ActorはBeginPlay対応"), Parent.bSupportsBeginPlay);

    Diagnostics.Reset();
    TestTrue(TEXT("プラグイン内ネイティブActorを解決"), FBPParentClassResolver::ResolveActorParent(
        ABPEmitterTestActor::StaticClass()->GetPathName(), Parent, Diagnostics));
    TestEqual(TEXT("プラグイン親C++名"), Parent.CppClassName, FString(TEXT("ABPEmitterTestActor")));
    TestEqual(TEXT("プラグイン親ヘッダー"), Parent.HeaderPath,
        FString(TEXT("Tests/BPEmitterTestActor.h")));

    Diagnostics.Reset();
    TestTrue(TEXT("ActorComponentを汎用解決"), FBPParentClassResolver::ResolveParent(
        UActorComponent::StaticClass()->GetPathName(), Parent, Diagnostics));
    TestEqual(TEXT("ActorComponent C++名"), Parent.CppClassName, FString(TEXT("UActorComponent")));
    TestEqual(TEXT("ActorComponentの生成prefix"), Parent.GeneratedClassPrefix, FString(TEXT("U")));
    TestFalse(TEXT("ActorComponentはActorではない"), Parent.bIsActor);
    TestTrue(TEXT("ActorComponentはBeginPlay対応"), Parent.bSupportsBeginPlay);

    Diagnostics.Reset();
    TestTrue(TEXT("UObjectを汎用解決"), FBPParentClassResolver::ResolveParent(
        UObject::StaticClass()->GetPathName(), Parent, Diagnostics));
    TestEqual(TEXT("UObject C++名"), Parent.CppClassName, FString(TEXT("UObject")));
    TestFalse(TEXT("通常UObjectはBeginPlay非対応"), Parent.bSupportsBeginPlay);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPParentClassResolverRejectTest,
    "BlueprintAIConverter.CodeGenerator.ParentClass.RejectUnsafe",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPParentClassResolverRejectTest::RunTest(const FString& Parameters)
{
    FBPResolvedParentClass Parent;
    TArray<FString> Diagnostics;
    TestFalse(TEXT("Actor以外を拒否"), FBPParentClassResolver::ResolveActorParent(
        UObject::StaticClass()->GetPathName(), Parent, Diagnostics));

    const FName Name = MakeUniqueObjectName(
        GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_ParentResolver"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        APawn::StaticClass(), GetTransientPackage(), Name, BPTYPE_Normal, NAME_None);
    TestNotNull(TEXT("Blueprint親テストを生成"), Blueprint);
    if (Blueprint == nullptr || Blueprint->GeneratedClass == nullptr)
    {
        return false;
    }
    Diagnostics.Reset();
    TestFalse(TEXT("未変換のBlueprint生成親を拒否"), FBPParentClassResolver::ResolveActorParent(
        Blueprint->GeneratedClass->GetPathName(), Parent, Diagnostics));
    TestTrue(TEXT("拒否理由を返す"), Diagnostics.IsEmpty() == false);
    return true;
}

#endif
