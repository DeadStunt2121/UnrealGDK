// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SchemaGenerator.h"

#include "Algo/Reverse.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "UObject/TextProperty.h"

#include "Interop/SpatialClassInfoManager.h"
#include "SpatialGDKSettings.h"
#include "Utils/CodeWriter.h"
#include "Utils/ComponentIdGenerator.h"
#include "Utils/DataTypeUtilities.h"

DEFINE_LOG_CATEGORY(LogSchemaGenerator);

ESchemaComponentType PropertyGroupToSchemaComponentType(EReplicatedPropertyGroup Group)
{
	if (Group == REP_MultiClient)
	{
		return SCHEMA_Data;
	}
	else if (Group == REP_SingleClient)
	{
		return SCHEMA_OwnerOnly;
	}
	else
	{
		checkNoEntry();
		return SCHEMA_Invalid;
	}
}

ESchemaComponentType RPCTypeToSchemaComponentType(ERPCType RPC)
{
	if (RPC == RPC_Client)
	{
		return SCHEMA_ClientReliableRPC;
	}
	else if (RPC == RPC_Server)
	{
		return SCHEMA_ServerReliableRPC;
	}
	else if (RPC == RPC_NetMulticast)
	{
		return SCHEMA_NetMulticastRPC;
	}
	else if (RPC == RPC_CrossServer)
	{
		return SCHEMA_CrossServerRPC;
	}
	else
	{
		checkNoEntry();
		return SCHEMA_Invalid;
	}

}

void WriteSchemaRepField(FCodeWriter& Writer, const TSharedPtr<FUnrealProperty> RepProp, const int FieldCounter)
{
	Writer.Printf("{0} {1} = {2};",
		*RepProp->DataType,
		*SchemaFieldName(RepProp),
		FieldCounter
	);
}

void WriteSchemaHandoverField(FCodeWriter& Writer, const TSharedPtr<FUnrealProperty> HandoverProp, const int FieldCounter)
{
	Writer.Printf("{0} {1} = {2};",
		*HandoverProp->DataType,
		*SchemaFieldName(HandoverProp),
		FieldCounter
	);
}

void WriteSchemaRPCField(TSharedPtr<FCodeWriter> Writer, const TSharedPtr<FUnrealProperty> RPCProp, const int FieldCounter)
{
	Writer->Printf("{0} {1} = {2};",
		*RPCProp->DataType,
		*SchemaFieldName(RPCProp),
		FieldCounter
	);
}

bool IsReplicatedSubobject(TSharedPtr<FUnrealType> TypeInfo)
{
	for (auto& PropertyGroup : GetFlatRepData(TypeInfo))
	{
		if (PropertyGroup.Value.Num() > 0)
		{
			return true;
		}
	}

	if (GetFlatHandoverData(TypeInfo).Num() > 0)
	{
		return true;
	}

	if (TypeInfo->NumRPCs > 0)
	{
		return true;
	}

	return false;
}

void GenerateSubobjectSchema(FComponentIdGenerator& IdGenerator, TSharedPtr<FUnrealType> TypeInfo, FString SchemaPath)
{
	FCodeWriter Writer;

	Writer.Printf(R"""(
		// Copyright (c) Improbable Worlds Ltd, All Rights Reserved
		// Note that this file has been generated automatically
		package unreal.generated;)""");

	bool bShouldIncludeCoreTypes = false;

	// Only include core types if the subobject has replicated references to other UObjects
	FUnrealFlatRepData RepData = GetFlatRepData(TypeInfo);
	for (auto& PropertyGroup : RepData)
	{
		for (auto& PropertyPair : PropertyGroup.Value)
		{
			if (PropertyPair.Value->bObjectProperty)
			{
				bShouldIncludeCoreTypes = true;
			}

			if (PropertyPair.Value->bArrayProperty)
			{
				if (PropertyPair.Value->bObjectArrayProperty)
				{
					bShouldIncludeCoreTypes = true;
				}
			}
		}
	}

	if (bShouldIncludeCoreTypes)
	{
		Writer.PrintNewLine();
		Writer.Printf("import \"unreal/gdk/core_types.schema\";");
	}

	for (EReplicatedPropertyGroup Group : GetAllReplicatedPropertyGroups())
	{
		// Since it is possible to replicate subobjects which have no replicated properties.
		// We need to generate a schema component for every subobject. So if we have no replicated
		// properties, we only don't generate a schema component if we are REP_SingleClient
		if (RepData[Group].Num() == 0 && Group == REP_SingleClient)
		{
			continue;
		}

		// If this class is an Actor Component, it MUST have bReplicates at field ID 1.
		/*if (Group == REP_MultiClient && TypeInfo->bIsActorComponent)
		{
			TSharedPtr<FUnrealProperty> ExpectedReplicatesPropData = RepData[Group].FindRef(SpatialConstants::ACTOR_COMPONENT_REPLICATES_ID);
			const UProperty* ReplicatesProp = UActorComponent::StaticClass()->FindPropertyByName("bReplicates");

			if (!(ExpectedReplicatesPropData.IsValid() && ExpectedReplicatesPropData->Property == ReplicatesProp))
			{
				UE_LOG(LogSchemaGenerator, Error, TEXT("Did not find ActorComponent->bReplicates at field %d for class %s. Modifying the base Actor Component class is currently not supported."),
					SpatialConstants::ACTOR_COMPONENT_REPLICATES_ID,
					*TypeInfo->ClassName);
			}
		}*/

		Writer.PrintNewLine();
		Writer.Printf("type {0} {", *SchemaReplicatedDataName(Group, TypeInfo->ClassPath));
		Writer.Indent();
		for (auto& RepProp : RepData[Group])
		{
			WriteSchemaRepField(Writer,
				RepProp.Value,
				RepProp.Value->ReplicationData->Handle);
		}
		Writer.Outdent().Print("}");
	}

	FCmdHandlePropertyMap HandoverData = GetFlatHandoverData(TypeInfo);
	if (HandoverData.Num() > 0)
	{
		Writer.PrintNewLine();

		Writer.Printf("type {0} {", *SchemaHandoverDataName(TypeInfo->ClassPath));
		Writer.Indent();
		int FieldCounter = 0;
		for (auto& Prop : HandoverData)
		{
			FieldCounter++;
			WriteSchemaHandoverField(Writer,
				Prop.Value,
				FieldCounter);
		}
		Writer.Outdent().Print("}");
	}

	// Use the max number of dynamically attached subobjects per class to generate
	// that many schema components for this subobject.
	const uint32 DynamicComponentsPerClass = GetDefault<USpatialGDKSettings>()->MaxDynamicallyAttachedSubobjectsPerClass;

	FSubobjectSchemaData SubobjectSchemaData;

	// Use previously generated component IDs when possible.
	const FSubobjectSchemaData* const ExistingSchemaData = SubobjectClassPathToSchema.Find(TypeInfo->ClassPath);
	if (ExistingSchemaData != nullptr && !ExistingSchemaData->GeneratedSchemaName.IsEmpty()
		&& ExistingSchemaData->GeneratedSchemaName != ClassPathToSchemaName[TypeInfo->ClassPath])
	{
		UE_LOG(LogSchemaGenerator, Error, TEXT("Saved generated schema name does not match in-memory version for class %s - schema %s : %s"),
			*TypeInfo->ClassPath, *ExistingSchemaData->GeneratedSchemaName, *ClassPathToSchemaName[TypeInfo->ClassPath]);
		UE_LOG(LogSchemaGenerator, Error, TEXT("Schema generation may have resulted in component name clash, recommend you perform a full schema generation"));
	}

	for (uint32 i = 1; i <= DynamicComponentsPerClass; i++)
	{
		FDynamicSubobjectSchemaData DynamicSubobjectComponents;

		for (EReplicatedPropertyGroup Group : GetAllReplicatedPropertyGroups())
		{
			// Since it is possible to replicate subobjects which have no replicated properties.
			// We need to generate a schema component for every subobject. So if we have no replicated
			// properties, we only don't generate a schema component if we are REP_SingleClient
			if (RepData[Group].Num() == 0 && Group == REP_SingleClient)
			{
				continue;
			}

			Writer.PrintNewLine();

			Worker_ComponentId ComponentId = 0;
			if (ExistingSchemaData != nullptr)
			{
				ComponentId = ExistingSchemaData->GetDynamicSubobjectComponentId(i - 1, PropertyGroupToSchemaComponentType(Group));
			}

			if (ComponentId == 0)
			{
				ComponentId = IdGenerator.Next();
			}
			FString ComponentName = SchemaReplicatedDataName(Group, TypeInfo->ClassPath) + TEXT("Dynamic") + FString::FromInt(i);

			Writer.Printf("component {0} {", *ComponentName);
			Writer.Indent();
			Writer.Printf("id = {0};", ComponentId);
			Writer.Printf("data {0};", *SchemaReplicatedDataName(Group, TypeInfo->ClassPath));
			Writer.Outdent().Print("}");

			DynamicSubobjectComponents.SchemaComponents[PropertyGroupToSchemaComponentType(Group)] = ComponentId;
		}

		if (HandoverData.Num() > 0)
		{
			Writer.PrintNewLine();

			Worker_ComponentId ComponentId = 0;
			if (ExistingSchemaData != nullptr)
			{
				ComponentId = ExistingSchemaData->GetDynamicSubobjectComponentId(i - 1, SCHEMA_Handover);
			}

			if (ComponentId == 0)
			{
				ComponentId = IdGenerator.Next();
			}
			FString ComponentName = SchemaHandoverDataName(TypeInfo->ClassPath) + TEXT("Dynamic") + FString::FromInt(i);

			Writer.Printf("component {0} {", *ComponentName);
			Writer.Indent();
			Writer.Printf("id = {0};", ComponentId);
			Writer.Printf("data {0};", *SchemaHandoverDataName(TypeInfo->ClassPath));
			Writer.Outdent().Print("}");

			DynamicSubobjectComponents.SchemaComponents[SCHEMA_Handover] = ComponentId;
		}

		SubobjectSchemaData.DynamicSubobjectComponents.Add(MoveTemp(DynamicSubobjectComponents));
	}

	Writer.WriteToFile(FString::Printf(TEXT("%s%s.schema"), *SchemaPath, *ClassPathToSchemaName[TypeInfo->ClassPath]));
	SubobjectSchemaData.GeneratedSchemaName = ClassPathToSchemaName[TypeInfo->ClassPath];
	SubobjectClassPathToSchema.Add(TypeInfo->ClassPath, SubobjectSchemaData);
}

void GenerateActorSchema(FComponentIdGenerator& IdGenerator, TSharedPtr<FUnrealType> TypeInfo, FString SchemaPath)
{
	const FActorSchemaData* const SchemaData = ActorClassPathToSchema.Find(TypeInfo->ClassPath);

	FCodeWriter Writer;

	Writer.Printf(R"""(
		// Copyright (c) Improbable Worlds Ltd, All Rights Reserved
		// Note that this file has been generated automatically
		package unreal.generated.{0};)""",
		*ClassPathToSchemaName[TypeInfo->ClassPath].ToLower());

	// Will always be included since AActor has replicated pointers to other actors
	Writer.PrintNewLine();
	Writer.Printf("import \"unreal/gdk/core_types.schema\";");

	FActorSchemaData ActorSchemaData;
	ActorSchemaData.GeneratedSchemaName = ClassPathToSchemaName[TypeInfo->ClassPath];

	FUnrealFlatRepData RepData = GetFlatRepData(TypeInfo);

	// Client-server replicated properties.
	for (EReplicatedPropertyGroup Group : GetAllReplicatedPropertyGroups())
	{
		if (RepData[Group].Num() == 0)
		{
			continue;
		}

		// If this class is an Actor, it MUST have bTearOff at field ID 3.	
		/*if (Group == REP_MultiClient && TypeInfo->bIsActorClass)
		{
			TSharedPtr<FUnrealProperty> ExpectedReplicatesPropData = RepData[Group].FindRef(SpatialConstants::ACTOR_TEAROFF_ID);
			const UProperty* ReplicatesProp = AActor::StaticClass()->FindPropertyByName("bTearOff");

			if (!(ExpectedReplicatesPropData.IsValid() && ExpectedReplicatesPropData->Property == ReplicatesProp))
			{
				UE_LOG(LogSchemaGenerator, Error, TEXT("Did not find Actor->bTearOff at field %d for class %s. Modifying the base Actor class is currently not supported."),
					SpatialConstants::ACTOR_TEAROFF_ID,
					*TypeInfo->ClassName);
			}
		}*/

		Worker_ComponentId ComponentId = 0;
		if (SchemaData != nullptr && SchemaData->SchemaComponents[PropertyGroupToSchemaComponentType(Group)] != 0)
		{
			ComponentId = SchemaData->SchemaComponents[PropertyGroupToSchemaComponentType(Group)];
		}
		else
		{
			ComponentId = IdGenerator.Next();
		}

		Writer.PrintNewLine();

		Writer.Printf("component {0} {", *SchemaReplicatedDataName(Group, TypeInfo->ClassPath));
		Writer.Indent();
		Writer.Printf("id = {0};", ComponentId);

		ActorSchemaData.SchemaComponents[PropertyGroupToSchemaComponentType(Group)] = ComponentId;

		int FieldCounter = 0;
		for (auto& RepProp : RepData[Group])
		{
			FieldCounter++;
			WriteSchemaRepField(Writer,
				RepProp.Value,
				RepProp.Value->ReplicationData->Handle);
		}

		Writer.Outdent().Print("}");
	}

	FCmdHandlePropertyMap HandoverData = GetFlatHandoverData(TypeInfo);
	if (HandoverData.Num() > 0)
	{
		Worker_ComponentId ComponentId = 0;
		if (SchemaData != nullptr && SchemaData->SchemaComponents[ESchemaComponentType::SCHEMA_Handover] != 0)
		{
			ComponentId = SchemaData->SchemaComponents[ESchemaComponentType::SCHEMA_Handover];
		}
		else
		{
			ComponentId = IdGenerator.Next();
		}

		Writer.PrintNewLine();

		// Handover (server to server) replicated properties.
		Writer.Printf("component {0} {", *SchemaHandoverDataName(TypeInfo->ClassPath));
		Writer.Indent();
		Writer.Printf("id = {0};", ComponentId);

		ActorSchemaData.SchemaComponents[ESchemaComponentType::SCHEMA_Handover] = ComponentId;

		int FieldCounter = 0;
		for (auto& Prop : HandoverData)
		{
			FieldCounter++;
			WriteSchemaHandoverField(Writer,
				Prop.Value,
				FieldCounter);
		}
		Writer.Outdent().Print("}");
	}

	GenerateSubobjectSchemaForActor(IdGenerator, TypeInfo, SchemaPath, ActorSchemaData, ActorClassPathToSchema.Find(TypeInfo->ClassPath));

	ActorClassPathToSchema.Add(TypeInfo->ClassPath, ActorSchemaData);

	Writer.WriteToFile(FString::Printf(TEXT("%s%s.schema"), *SchemaPath, *ClassPathToSchemaName[TypeInfo->ClassPath]));
}

FActorSpecificSubobjectSchemaData GenerateSchemaForStaticallyAttachedSubobject(FCodeWriter& Writer, FComponentIdGenerator& IdGenerator,
	FString PropertyName, TSharedPtr<FUnrealType>& TypeInfo, TSharedPtr<FUnrealType>& ActorTypeInfo, int MapIndex,
	const FActorSpecificSubobjectSchemaData* ExistingSchemaData)
{
	FUnrealFlatRepData RepData = GetFlatRepData(TypeInfo);

	FActorSpecificSubobjectSchemaData SubobjectData;
	SubobjectData.ClassPath = TypeInfo->ClassPath;

	for (EReplicatedPropertyGroup Group : GetAllReplicatedPropertyGroups())
	{
		// Since it is possible to replicate subobjects which have no replicated properties.
		// We need to generate a schema component for every subobject. So if we have no replicated
		// properties, we only don't generate a schema component if we are REP_SingleClient
		if (RepData[Group].Num() == 0 && Group == REP_SingleClient)
		{
			continue;
		}

		Worker_ComponentId ComponentId = 0;
		if (ExistingSchemaData != nullptr && ExistingSchemaData->SchemaComponents[PropertyGroupToSchemaComponentType(Group)] != 0)
		{
			ComponentId = ExistingSchemaData->SchemaComponents[PropertyGroupToSchemaComponentType(Group)];
		}
		else
		{
			ComponentId = IdGenerator.Next();
		}

		Writer.PrintNewLine();

		FString ComponentName = PropertyName + GetReplicatedPropertyGroupName(Group);
		Writer.Printf("component {0} {", *ComponentName);
		Writer.Indent();
		Writer.Printf("id = {0};", ComponentId);
		Writer.Printf("data unreal.generated.{0};", *SchemaReplicatedDataName(Group, TypeInfo->ClassPath));
		Writer.Outdent().Print("}");

		SubobjectData.SchemaComponents[PropertyGroupToSchemaComponentType(Group)] = ComponentId;
	}

	FCmdHandlePropertyMap HandoverData = GetFlatHandoverData(TypeInfo);
	if (HandoverData.Num() > 0)
	{
		Worker_ComponentId ComponentId = 0;
		if (ExistingSchemaData != nullptr && ExistingSchemaData->SchemaComponents[ESchemaComponentType::SCHEMA_Handover] != 0)
		{
			ComponentId = ExistingSchemaData->SchemaComponents[ESchemaComponentType::SCHEMA_Handover];
		}
		else
		{
			ComponentId = IdGenerator.Next();
		}

		Writer.PrintNewLine();

		// Handover (server to server) replicated properties.
		Writer.Printf("component {0} {", *(PropertyName + TEXT("Handover")));
		Writer.Indent();
		Writer.Printf("id = {0};", ComponentId);
		Writer.Printf("data unreal.generated.{0};", *SchemaHandoverDataName(TypeInfo->ClassPath));
		Writer.Outdent().Print("}");

		SubobjectData.SchemaComponents[ESchemaComponentType::SCHEMA_Handover] = ComponentId;
	}

	return SubobjectData;
}

void GenerateSubobjectSchemaForActor(FComponentIdGenerator& IdGenerator, TSharedPtr<FUnrealType> TypeInfo, FString SchemaPath, FActorSchemaData& ActorSchemaData, const FActorSchemaData* ExistingSchemaData)
{
	FCodeWriter Writer;

	Writer.Printf(R"""(
		// Copyright (c) Improbable Worlds Ltd, All Rights Reserved
		// Note that this file has been generated automatically
		package unreal.generated.{0}.subobjects;)""",
		*ClassPathToSchemaName[TypeInfo->ClassPath].ToLower());

	Writer.PrintNewLine();

	if (TypeInfo->ClassPath == TEXT("/Game/VFX/BP_BulletTracer.BP_BulletTracer_C"))
	{
		UE_LOG(LogTemp, Display, TEXT("FOUND IT"));
	}

	GenerateSubobjectSchemaForActorIncludes(Writer, TypeInfo);

	FSubobjectMap Subobjects = GetAllSubobjects(TypeInfo);

	bool bHasComponents = false;

	for (auto& It : Subobjects)
	{
		TSharedPtr<FUnrealType>& SubobjectTypeInfo = It.Value;

		FActorSpecificSubobjectSchemaData SubobjectData;

		if (SchemaGeneratedClassPaths.Contains(SubobjectTypeInfo->ClassPath))
		{
			bHasComponents = true;

			if (TypeInfo->ClassPath == TEXT("/Game/Blueprints/Weapons/BP_AutomaticRifle.BP_AutomaticRifle_C"))
			{
				bool bar = false;
			}

			const FActorSpecificSubobjectSchemaData* ExistingSubobjectSchemaData = nullptr;
			if (ExistingSchemaData != nullptr)
			{
				for (auto& SubobjectIt : ExistingSchemaData->SubobjectData)
				{
					if (SubobjectIt.Value.Name == SubobjectTypeInfo->Name)
					{
						ExistingSubobjectSchemaData = &SubobjectIt.Value;
						break;
					}
				}
			}
			SubobjectData = GenerateSchemaForStaticallyAttachedSubobject(Writer, IdGenerator, UnrealNameToSchemaComponentName(SubobjectTypeInfo->Name.ToString()), SubobjectTypeInfo, TypeInfo, 0, ExistingSubobjectSchemaData);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Unable to generate"))
			continue;
		}

		SubobjectData.Name = SubobjectTypeInfo->Name;
		uint32 SubobjectOffset = SubobjectData.SchemaComponents[SCHEMA_Data];
		check(SubobjectOffset != 0);
		ActorSchemaData.SubobjectData.Add(SubobjectOffset, SubobjectData);
	}

	if (bHasComponents)
	{
		Writer.WriteToFile(FString::Printf(TEXT("%s%sComponents.schema"), *SchemaPath, *ClassPathToSchemaName[TypeInfo->ClassPath]));
	}
}

void GenerateSubobjectSchemaForActorIncludes(FCodeWriter& Writer, TSharedPtr<FUnrealType>& TypeInfo)
{
	TSet<FString> AlreadyImported;

	for (auto& PropertyInfo : TypeInfo->PropertiesList)
	{
		TSharedPtr<FUnrealType>& PropertyTypeInfo = PropertyInfo->Type;

		if (PropertyInfo->bObjectProperty && PropertyTypeInfo.IsValid())
		{
			if (!PropertyTypeInfo->bObjectEditorOnly)
			{
				FString ClassPath = PropertyTypeInfo->ClassPath;
				if (!AlreadyImported.Contains(ClassPath) && SchemaGeneratedClassPaths.Contains(ClassPath))
				{
					Writer.Printf("import \"unreal/generated/Subobjects/{0}.schema\";", *ClassPathToSchemaName[ClassPath]);
					AlreadyImported.Add(ClassPath);
				}
			}
		}
	}
}
