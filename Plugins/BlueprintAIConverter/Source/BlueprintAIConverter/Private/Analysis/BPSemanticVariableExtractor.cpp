#include "BPSemanticVariableExtractor.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraphPin.h"
#include "Internationalization/Text.h"
#include "UObject/UnrealType.h"

namespace
{
EBPSemanticTypeKind ResolveType(const FEdGraphPinType& PinType)
{
    const FString Name = PinType.PinCategory.ToString();
    if (Name == TEXT("bool")) { return EBPSemanticTypeKind::Bool; }
    if (Name == TEXT("int")) { return EBPSemanticTypeKind::Int32; }
    if (Name == TEXT("int64")) { return EBPSemanticTypeKind::Int64; }
    // UE5のrealピンはサブカテゴリでfloat/doubleを区別する。
    if (Name == TEXT("real"))
    {
        if (PinType.PinSubCategory == TEXT("float")) { return EBPSemanticTypeKind::Float; }
        if (PinType.PinSubCategory == TEXT("double")) { return EBPSemanticTypeKind::Double; }
        return EBPSemanticTypeKind::Unresolved;
    }
    if (Name == TEXT("float")) { return EBPSemanticTypeKind::Float; }
    if (Name == TEXT("double")) { return EBPSemanticTypeKind::Double; }
    if (Name == TEXT("string")) { return EBPSemanticTypeKind::String; }
    if (Name == TEXT("name")) { return EBPSemanticTypeKind::Name; }
    if (Name == TEXT("text")) { return EBPSemanticTypeKind::Text; }
    if (Name == TEXT("object") || Name == TEXT("class")) { return EBPSemanticTypeKind::Object; }
    if (Name == TEXT("struct")) { return EBPSemanticTypeKind::Struct; }
    return EBPSemanticTypeKind::Unresolved;
}
    
// リフレクション型とIR型が一致するスカラーだけをCDOから読み取る。
// ExportTextItemなどの汎用文字列化は型の意味を失うためここでは使用しない。
bool ReadScalarDefault(const FProperty* Property, const UObject* CDO,
    EBPSemanticTypeKind Kind, FString& OutValue)
{
    if (Property == nullptr || CDO == nullptr)
    {
        return false;
    }
    const void* Address = Property->ContainerPtrToValuePtr<void>(CDO);
    if (Address == nullptr)
    {
        return false;
    }
    switch (Kind)
    {
    case EBPSemanticTypeKind::Bool:
        if (const FBoolProperty* Typed = CastField<FBoolProperty>(Property))
        {
            OutValue = Typed->GetPropertyValue(Address) ? TEXT("true") : TEXT("false");
            return true;
        }
        break;
    case EBPSemanticTypeKind::Int32:
    case EBPSemanticTypeKind::Int64:
        if (const FNumericProperty* Typed = CastField<FNumericProperty>(Property);
            Typed != nullptr && Typed->IsInteger() && Typed->IsEnum() == false)
        {
            OutValue = LexToString(Typed->GetSignedIntPropertyValue(Address));
            return true;
        }
        break;
    case EBPSemanticTypeKind::Float:
    case EBPSemanticTypeKind::Double:
        if (const FNumericProperty* Typed = CastField<FNumericProperty>(Property);
            Typed != nullptr && Typed->IsFloatingPoint())
        {
            const double Value = Typed->GetFloatingPointPropertyValue(Address);
            OutValue = Kind == EBPSemanticTypeKind::Float
                ? LexToString(static_cast<float>(Value))
                : LexToString(Value);
            return true;
        }
        break;
    case EBPSemanticTypeKind::String:
        if (const FStrProperty* Typed = CastField<FStrProperty>(Property))
        {
            OutValue = Typed->GetPropertyValue(Address);
            return true;
        }
        break;
    case EBPSemanticTypeKind::Name:
        if (const FNameProperty* Typed = CastField<FNameProperty>(Property))
        {
            OutValue = Typed->GetPropertyValue(Address).ToString();
            return true;
        }
        break;
    case EBPSemanticTypeKind::Text:
        if (const FTextProperty* Typed = CastField<FTextProperty>(Property))
        {
            // C2では表示文字列を保持する。名前空間・キーを持つローカライズ済みTextは、
            // 後続段階で識別情報をIRへ追加するまでGenerator側で明示的に扱う。
            OutValue = Typed->GetPropertyValue(Address).ToString();
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

void ReadTextIdentity(const FProperty* Property, const UObject* CDO, FBPSemanticVariable& Variable)
{
    const FTextProperty* TextProperty = CastField<FTextProperty>(Property);
    if (TextProperty == nullptr || CDO == nullptr)
    {
        return;
    }
    const void* Address = TextProperty->ContainerPtrToValuePtr<void>(CDO);
    if (Address == nullptr)
    {
        return;
    }
    const FText Text = TextProperty->GetPropertyValue(Address);
    // ローカライズ収集対象でない一時Textは識別情報を持たないため、
    // 表示文字列をculture-invariant literalとして生成しても翻訳キーを失わない。
    Variable.bTextCultureInvariant = Text.IsCultureInvariant() ||
        FTextInspector::ShouldGatherForLocalization(Text) == false;
    const TOptional<FString> Namespace = FTextInspector::GetNamespace(Text);
    const TOptional<FString> Key = FTextInspector::GetKey(Text);
    const FString* Source = FTextInspector::GetSourceString(Text);
    if (Namespace.IsSet() && Key.IsSet() && Source != nullptr)
    {
        Variable.bTextLocalizationIdentityAvailable = true;
        Variable.TextNamespace = Namespace.GetValue();
        Variable.TextKey = Key.GetValue();
        Variable.TextSource = *Source;
    }
}

EBPVariableInitializationStrategy ResolveInitializationStrategy(const EBPSemanticTypeKind Kind)
{
    switch (Kind)
    {
    case EBPSemanticTypeKind::Bool:
    case EBPSemanticTypeKind::Int32:
    case EBPSemanticTypeKind::Int64:
    case EBPSemanticTypeKind::Float:
    case EBPSemanticTypeKind::Double:
        return EBPVariableInitializationStrategy::DirectLiteral;
    case EBPSemanticTypeKind::String:
        return EBPVariableInitializationStrategy::StringLiteral;
    case EBPSemanticTypeKind::Name:
        return EBPVariableInitializationStrategy::NameLiteral;
    case EBPSemanticTypeKind::Text:
        return EBPVariableInitializationStrategy::TextLiteral;
    default:
        return EBPVariableInitializationStrategy::Unsupported;
    }
}

bool IsValueOnlyProperty(const FProperty* Property)
{
    if (Property == nullptr)
    {
        return false;
    }
    if (CastField<FNumericProperty>(Property) != nullptr ||
        CastField<FBoolProperty>(Property) != nullptr ||
        CastField<FStrProperty>(Property) != nullptr ||
        CastField<FNameProperty>(Property) != nullptr ||
        CastField<FTextProperty>(Property) != nullptr ||
        CastField<FEnumProperty>(Property) != nullptr)
    {
        return true;
    }
    if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        if (StructProperty->Struct == nullptr)
        {
            return false;
        }
        for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
        {
            if (IsValueOnlyProperty(*It) == false)
            {
                return false;
            }
        }
        return true;
    }
    if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
    {
        return IsValueOnlyProperty(ArrayProperty->Inner);
    }
    if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
    {
        return IsValueOnlyProperty(SetProperty->ElementProp);
    }
    if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
    {
        return IsValueOnlyProperty(MapProperty->KeyProp) &&
            IsValueOnlyProperty(MapProperty->ValueProp);
    }
    return false;
}

bool VerifyExportTextRoundTrip(const FProperty* Property, const UObject* CDO,
    const FString& ExportedValue)
{
    if (Property == nullptr || CDO == nullptr || IsValueOnlyProperty(Property) == false)
    {
        return false;
    }
    const void* Original = Property->ContainerPtrToValuePtr<void>(CDO);
    if (Original == nullptr)
    {
        return false;
    }

    void* Imported = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
    if (Imported == nullptr)
    {
        return false;
    }
    Property->InitializeValue(Imported);
    const TCHAR* End = Property->ImportText_Direct(
        *ExportedValue, Imported, const_cast<UObject*>(CDO), PPF_None);
    const bool bConsumed = End != nullptr && FString(End).TrimStartAndEnd().IsEmpty();
    const bool bIdentical = bConsumed && Property->Identical(Original, Imported, PPF_None);
    Property->DestroyValue(Imported);
    FMemory::Free(Imported);
    return bIdentical;
}
}

void FBPSemanticVariableExtractor::Append(const UBlueprint* Blueprint, FBPSemanticIR& IR)
{
    if (Blueprint == nullptr)
    {
        return;
    }

    // GeneratedClassがない未コンパイルBlueprintでは取得不可として記録する。
    UClass* GeneratedClass = Blueprint->GeneratedClass;
    const UObject* CDO = GeneratedClass != nullptr ? GeneratedClass->GetDefaultObject() : nullptr;

    for (const FBPVariableDescription& Description : Blueprint->NewVariables)
    {
        // Lowering済みIRへJSON追跡情報を追加する呼び出しでも重複させない。
        const bool bAlreadyPresent = IR.Variables.ContainsByPredicate(
            [&Description](const FBPSemanticVariable& Existing)
            {
                return Existing.Guid == Description.VarGuid && Existing.Name == Description.VarName;
            });
        if (bAlreadyPresent)
        {
            continue;
        }

        FBPSemanticVariable Variable;
        Variable.Name = Description.VarName;
        Variable.Guid = Description.VarGuid;
        Variable.PinCategory = Description.VarType.PinCategory.ToString();
        Variable.TypePath = GetPathNameSafe(Description.VarType.PinSubCategoryObject.Get());
        Variable.PropertyFlags = FString::Printf(TEXT("%llu"), static_cast<uint64>(Description.PropertyFlags));
        Variable.Type.Kind = ResolveType(Description.VarType);
        // コンテナ型は要素型と区別して保持する。int32配列だけは
        // Blueprintのピン型とFArrayPropertyの双方で一致を確認して解決する。
        // その他のコンテナは従来どおり未解決とし、推測による誤変換を防ぐ。
        if (Description.VarType.ContainerType != EPinContainerType::None)
        {
            Variable.Type.Kind = EBPSemanticTypeKind::Unresolved;
            if (Description.VarType.ContainerType == EPinContainerType::Array)
            {
                Variable.Type.ContainerKind = TEXT("array");
            }
        }
        Variable.Type.TypePath = Variable.TypePath;
        Variable.Type.bIsReference = Description.VarType.bIsReference;
        Variable.Type.bIsConst = Description.VarType.bIsConst;
        Variable.InitializationStrategy = ResolveInitializationStrategy(Variable.Type.Kind);

        const FProperty* Property = GeneratedClass != nullptr
            ? FindFProperty<FProperty>(GeneratedClass, Description.VarName)
            : nullptr;
        if (Description.VarType.ContainerType == EPinContainerType::Array &&
            Description.VarType.PinCategory == TEXT("int") &&
            Description.VarType.bIsReference == false &&
            Description.VarType.bIsConst == false)
        {
            const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
            // BlueprintのPinCategoryのみを信用せず、反射された要素Propertyと
            // 生成クラスのC++型を照合する。列挙型や別の数値型は対象外とする。
            if (ArrayProperty != nullptr &&
                CastField<FIntProperty>(ArrayProperty->Inner) != nullptr)
            {
                FString ExtendedType;
                const FString CppType = Property->GetCPPType(&ExtendedType) + ExtendedType;
                if (CppType == TEXT("TArray<int32>"))
                {
                    Variable.Type.Kind = EBPSemanticTypeKind::Int32;
                    Variable.Type.InnerType = TEXT("int32");
                    Variable.Type.ElementType = MakeShared<FBPSemanticType>();
                    Variable.Type.ElementType->Kind = EBPSemanticTypeKind::Int32;
                }
            }
        }
        if (Property != nullptr)
        {
            FString ExtendedType;
            Variable.ReflectedCppType = Property->GetCPPType(&ExtendedType) + ExtendedType;
            Variable.ReflectedPropertyClass = Property->GetClass()->GetName();
            if (CDO != nullptr)
            {
                const void* Address = Property->ContainerPtrToValuePtr<void>(CDO);
                if (Address != nullptr)
                {
                    Property->ExportTextItem_Direct(
                        Variable.ExportedDefaultValue, Address, nullptr,
                        const_cast<UObject*>(CDO), PPF_None);
                    Variable.bExportedDefaultValueAvailable = true;
                    Variable.bExportTextRoundTripVerified = VerifyExportTextRoundTrip(
                        Property, CDO, Variable.ExportedDefaultValue);
                }
            }
        }

        // 専用リテラルを持たない値型は、FPropertyのExportText/ImportTextを利用する。
        // 同じPropertyで往復して完全一致した値だけをGeneratorへ渡し、Object参照や
        // DelegateなどIsValueOnlyPropertyで拒否した型には適用しない。
        if (Variable.InitializationStrategy == EBPVariableInitializationStrategy::Unsupported &&
            Variable.bExportedDefaultValueAvailable &&
            Variable.bExportTextRoundTripVerified &&
            Variable.TypePath.StartsWith(TEXT("/Game/")) == false)
        {
            Variable.InitializationStrategy = EBPVariableInitializationStrategy::ImportText;
            Variable.bDefaultValueAvailable = true;
            Variable.DefaultValueSource = TEXT("cdo_export_text");
            Variable.DefaultValue = Variable.ExportedDefaultValue;
        }

        // J2-2でCDOから型安全に取得するまで、宣言のDefaultValueをCDO値と誤認しない。
        // コンテナやreal型のサブカテゴリなど未解決情報は推測せず診断する。
        if (Description.VarType.ContainerType != EPinContainerType::None ||
            Variable.Type.Kind == EBPSemanticTypeKind::Unresolved)
        {
            if (Variable.InitializationStrategy != EBPVariableInitializationStrategy::ImportText)
            {
                FBPSemanticDiagnostic Diagnostic;
                // 反射型が得られた変数はDeclarationOnlyでC++宣言を残せるため、
                // Lowering全体を止めずGeneratorの方針選択へ委ねる。
                Diagnostic.Severity = Variable.ReflectedCppType.IsEmpty()
                    ? EBPSemanticDiagnosticSeverity::Error
                    : EBPSemanticDiagnosticSeverity::Warning;
                Diagnostic.Message = FString::Printf(TEXT("Variable %s requires type resolution: %s"),
                    *Variable.Name.ToString(), *Variable.PinCategory);
                IR.Diagnostics.Add(MoveTemp(Diagnostic));
            }
        }
        // 宣言名に対応する生成クラスのPropertyのみ対象とする。
        // 配列・Map・Set・参照型・未解決型はJ2-2の安全なスカラー対象外。
        if (CDO != nullptr &&
            Description.VarType.ContainerType == EPinContainerType::None &&
            Description.VarType.bIsReference == false &&
            Variable.Type.Kind != EBPSemanticTypeKind::Unresolved)
        {
            if (ReadScalarDefault(Property, CDO, Variable.Type.Kind, Variable.DefaultValue))
            {
                Variable.bDefaultValueAvailable = true;
                Variable.DefaultValueSource = TEXT("cdo");
                if (Variable.Type.Kind == EBPSemanticTypeKind::Text)
                {
                    ReadTextIdentity(Property, CDO, Variable);
                }
            }
        }
        IR.Variables.Add(MoveTemp(Variable));
    }
}
