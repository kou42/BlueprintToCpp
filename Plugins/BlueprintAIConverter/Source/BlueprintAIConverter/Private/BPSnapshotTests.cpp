#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BPSnapshotExporter.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSnapshotSchemaTest,
    "BlueprintAIConverter.Snapshot.Schema",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSnapshotSchemaTest::RunTest(const FString& Parameters)
{
    UBlueprint* BP = NewObject<UBlueprint>(GetTransientPackage());
    TestNotNull(TEXT("Transient Blueprint"), BP);
    if (!BP) return false;
    BP->ParentClass = AActor::StaticClass();
    FString Json;
    TestTrue(TEXT("Serialize succeeds"), FBPSnapshotExporter::Serialize(BP, Json));
    TSharedPtr<FJsonObject> Parsed;
    const auto Reader = TJsonReaderFactory<>::Create(Json);
    TestTrue(TEXT("JSON parses"), FJsonSerializer::Deserialize(Reader, Parsed));
    if (!Parsed) return false;
    TestEqual(TEXT("Schema version"), Parsed->GetIntegerField(TEXT("schema_version")), 1);
    TestEqual(TEXT("Parent class"), Parsed->GetStringField(TEXT("parent_class")), AActor::StaticClass()->GetPathName());
    TestTrue(TEXT("Graphs array exists"), Parsed->HasTypedField<EJson::Array>(TEXT("graphs")));
    TestTrue(TEXT("Variables array exists"), Parsed->HasTypedField<EJson::Array>(TEXT("variables")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBPSnapshotNullTest,
    "BlueprintAIConverter.Snapshot.NullInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPSnapshotNullTest::RunTest(const FString& Parameters)
{
    FString Json = TEXT("stale"), Path, Error;
    TestFalse(TEXT("Null serialization rejected"), FBPSnapshotExporter::Serialize(nullptr, Json));
    TestTrue(TEXT("Output reset"), Json.IsEmpty());
    TestFalse(TEXT("Null export rejected"), FBPSnapshotExporter::Export(nullptr, Path, Error));
    TestFalse(TEXT("Error reported"), Error.IsEmpty());
    return true;
}
#endif
