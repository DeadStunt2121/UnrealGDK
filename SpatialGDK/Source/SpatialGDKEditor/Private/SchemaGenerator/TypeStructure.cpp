// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "TypeStructure.h"

#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "SpatialGDKEditorSchemaGenerator.h"
#include "Utils/RepLayoutUtils.h"

namespace Errors
{
	FString DuplicateComponentError = TEXT("WARNING: Unreal GDK does not currently support multiple static components of the same type.\n"
		"Make sure {0} has only one instance of {1} or don't generate type bindings for {2}");
}

FString GetFullCPPName(UClass* Class)
{
	if (Class->IsChildOf(AActor::StaticClass()))
	{
		return FString::Printf(TEXT("A%s"), *Class->GetName());
	}
	else
	{
		return FString::Printf(TEXT("U%s"), *Class->GetName());
	}
}

FString GetLifetimeConditionAsString(ELifetimeCondition Condition)
{
	const UEnum* EnumPtr = FindObject<UEnum>(ANY_PACKAGE, TEXT("ELifetimeCondition"), true);
	if (!EnumPtr)
	{
		return FString("Invalid");
	}
	return EnumPtr->GetNameByValue((int64)Condition).ToString();
}

FString GetRepNotifyLifetimeConditionAsString(ELifetimeRepNotifyCondition Condition)
{
	switch (Condition)
	{
	case REPNOTIFY_OnChanged: return FString(TEXT("REPNOTIFY_OnChanged"));
	case REPNOTIFY_Always: return FString(TEXT("REPNOTIFY_Always"));
	default:
		checkNoEntry();
	}
	return FString();
}

TArray<EReplicatedPropertyGroup> GetAllReplicatedPropertyGroups()
{
	static TArray<EReplicatedPropertyGroup> Groups = {REP_MultiClient, REP_SingleClient};
	return Groups;
}

FString GetReplicatedPropertyGroupName(EReplicatedPropertyGroup Group)
{
	return Group == REP_SingleClient ? TEXT("OwnerOnly") : TEXT("");
}

TArray<ERPCType> GetRPCTypes()
{
	static TArray<ERPCType> Groups = {RPC_Client, RPC_Server, RPC_CrossServer, RPC_NetMulticast};
	return Groups;
}

ERPCType GetRPCTypeFromFunction(UFunction* Function)
{
	if (Function->FunctionFlags & EFunctionFlags::FUNC_NetClient)
	{
		return ERPCType::RPC_Client;
	}
	if (Function->FunctionFlags & EFunctionFlags::FUNC_NetServer)
	{
		return ERPCType::RPC_Server;
	}
	if (Function->FunctionFlags & EFunctionFlags::FUNC_NetCrossServer)
	{
		return ERPCType::RPC_CrossServer;
	}
	if (Function->FunctionFlags & EFunctionFlags::FUNC_NetMulticast)
	{
		return ERPCType::RPC_NetMulticast;
	}
	else
	{
		checkNoEntry();
		return ERPCType::RPC_Unknown;
	}
}

FString GetRPCTypeName(ERPCType RPCType)
{
	switch (RPCType)
	{
	case ERPCType::RPC_Client:
		return "Client";
	case ERPCType::RPC_Server:
		return "Server";
	case ERPCType::RPC_CrossServer:
		return "CrossServer";
	case ERPCType::RPC_NetMulticast:
		return "NetMulticast";
	default:
		checkf(false, TEXT("RPCType is invalid!"));
		return "";
	}
}

void VisitAllPropertiesMap(TSharedPtr<FUnrealType> TypeNode, TFunction<bool(TSharedPtr<FUnrealProperty>)> Visitor, bool bRecurseIntoSubobjects)
{
	for (auto& PropertyPair : TypeNode->PropertiesMap)
	{
		bool bShouldRecurseFurther = Visitor(PropertyPair.Value);
		if (bShouldRecurseFurther && PropertyPair.Value->Type.IsValid())
		{
			// Either recurse into subobjects if they're structs or bRecurseIntoSubobjects is true.
			if (bRecurseIntoSubobjects || PropertyPair.Value->bStructProperty)
			{
				VisitAllPropertiesMap(PropertyPair.Value->Type, Visitor, bRecurseIntoSubobjects);
			}
		}
	}
}

void VisitAllPropertiesList(TSharedPtr<FUnrealType> TypeNode, TFunction<bool(TSharedPtr<FUnrealProperty>)> Visitor, bool bRecurseIntoSubobjects)
{
	for (auto& PropertyInfo : TypeNode->PropertiesList)
	{
		bool bShouldRecurseFurther = Visitor(PropertyInfo);
		if (bShouldRecurseFurther && PropertyInfo->Type.IsValid())
		{
			// Either recurse into subobjects if they're structs or bRecurseIntoSubobjects is true.
			if (bRecurseIntoSubobjects || PropertyInfo->bStructProperty)
			{
				VisitAllPropertiesList(PropertyInfo->Type, Visitor, bRecurseIntoSubobjects);
			}
		}
	}
}

//void VisitAllProperties(TSharedPtr<FUnrealRPC> RPCNode, TFunction<bool(TSharedPtr<FUnrealProperty>)> Visitor, bool bRecurseIntoSubobjects)
//{
//	for (auto& PropertyPair : RPCNode->Parameters)
//	{
//		bool bShouldRecurseFurther = Visitor(PropertyPair.Value);
//		if (bShouldRecurseFurther && PropertyPair.Value->Type.IsValid())
//		{
//			// Either recurse into subobjects if they're structs or bRecurseIntoSubobjects is true.
//			if (bRecurseIntoSubobjects || PropertyPair.Value->Property->IsA<UStructProperty>())
//			{
//				VisitAllProperties(PropertyPair.Value->Type, Visitor, bRecurseIntoSubobjects);
//			}
//		}
//	}
//}

// GenerateChecksum is a method which replicates how Unreal generates it's own CompatibleChecksum for RepLayout Cmds.
// The original code can be found in the Unreal Engine's RepLayout. We use this to ensure we have the correct property at run-time.
uint32 GenerateChecksum(UProperty* Property, uint32 ParentChecksum, int32 StaticArrayIndex)
{
	uint32 Checksum = 0;
	Checksum = FCrc::StrCrc32(*Property->GetName().ToLower(), ParentChecksum);            // Evolve checksum on name
	Checksum = FCrc::StrCrc32(*Property->GetCPPType(nullptr, 0).ToLower(), Checksum);     // Evolve by property type
	Checksum = FCrc::MemCrc32(&StaticArrayIndex, sizeof(StaticArrayIndex), Checksum);     // Evolve by StaticArrayIndex (to make all unrolled static array elements unique)
	return Checksum;
}

TSharedPtr<FUnrealProperty> CreateUnrealProperty(TSharedPtr<FUnrealType> TypeNode, UProperty* Property, uint32 ParentChecksum, uint32 StaticArrayIndex)
{
	TSharedPtr<FUnrealProperty> PropertyNode = MakeShared<FUnrealProperty>();
	//PropertyNode->Property = Property;
	PropertyNode->ContainerType = TypeNode;
	PropertyNode->ParentChecksum = ParentChecksum;
	PropertyNode->StaticArrayIndex = StaticArrayIndex;
	PropertyNode->PropertyPath = Property->GetPathName();
	PropertyNode->PropertyName = Property->GetName();
	if (const UArrayProperty* ArrayProp = Cast<UArrayProperty>(Property))
	{
		PropertyNode->bArrayProperty = true;
		PropertyNode->bObjectArrayProperty = ArrayProp->Inner->IsA<UObjectPropertyBase>();
	}
	PropertyNode->bObjectProperty = Property->IsA<UObjectProperty>();
	if (const UStructProperty* StructProp = Cast<UStructProperty>(Property))
	{
		PropertyNode->bStructProperty = true;
		PropertyNode->StructFlags = StructProp->Struct->StructFlags;
	}
	PropertyNode->PropertyPath = Property->GetPathName();
	PropertyNode->ArrayDim = Property->ArrayDim;
	PropertyNode->PropertyFlags = Property->PropertyFlags;
	PropertyNode->DataType = PropertyToSchemaType(Property);

	// Generate a checksum for this PropertyNode to be used to match properties with the RepLayout Cmds later.
	PropertyNode->CompatibleChecksum = GenerateChecksum(Property, ParentChecksum, StaticArrayIndex);
	TypeNode->PropertiesMap.Add(Property, PropertyNode);
	TypeNode->PropertiesList.Add(PropertyNode);
	return PropertyNode;
}

TSharedPtr<FUnrealType> CreateUnrealTypeInfo(UStruct* Type, uint32 ParentChecksum, int32 StaticArrayIndex, bool bIsRPC)
{
	// Struct types will set this to nullptr.
	UClass* Class = Cast<UClass>(Type);

	// Create type node.
	TSharedPtr<FUnrealType> TypeNode = MakeShared<FUnrealType>();

	TypeNode->bIsActorClass = Class->IsChildOf<AActor>();
	TypeNode->bIsActorComponent = Class->IsChildOf<UActorComponent>();

	TypeNode->ClassPath = GetPathNameSafe(Class);
	TypeNode->ClassName = GetNameSafe(Class);

	// Iterate through each property in the struct.
	for (TFieldIterator<UProperty> It(Type); It; ++It)
	{
		UProperty* Property = *It;

		// Create property node and add it to the AST.
		TSharedPtr<FUnrealProperty> PropertyNode = CreateUnrealProperty(TypeNode, Property, ParentChecksum, StaticArrayIndex);

		// If this property not a struct or object (which can contain more properties), stop here.
		if (!Property->IsA<UStructProperty>() && !Property->IsA<UObjectProperty>())
		{
			// We check for bIsRPC at this step as we do not want to generate new PropertyNodes for c-style array members in RPCs.
			// RPCs use a different system where the members of the c-style array are added to a dynamic list.
			// This check is made before all handling of c-style arrays in this method.
			if (!bIsRPC)
			{
				for (int i = 1; i < Property->ArrayDim; i++)
				{
					CreateUnrealProperty(TypeNode, Property, ParentChecksum, i);
				}
			}
			continue;
		}

		// If this is a struct property, then get the struct type and recurse into it.
		if (Property->IsA<UStructProperty>())
		{
			UStructProperty* StructProperty = Cast<UStructProperty>(Property);

			// This is the property for the 0th struct array member.
			uint32 ParentPropertyNodeChecksum = PropertyNode->CompatibleChecksum;
			PropertyNode->Type = CreateUnrealTypeInfo(StructProperty->Struct, ParentPropertyNodeChecksum, 0, bIsRPC);
			PropertyNode->Type->ParentProperty = PropertyNode;

			if (!bIsRPC)
			{
				// For static arrays we need to make a new struct array member node.
				for (int i = 1; i < Property->ArrayDim; i++)
				{
					// Create a new PropertyNode.
					TSharedPtr<FUnrealProperty> StaticStructArrayPropertyNode = CreateUnrealProperty(TypeNode, Property, ParentChecksum, i);

					// Generate Type information on the inner struct.
					// Note: The parent checksum of the properties within a struct that is a member of a static struct array, is the checksum for the struct itself after index modification.
					StaticStructArrayPropertyNode->Type = CreateUnrealTypeInfo(StructProperty->Struct, StaticStructArrayPropertyNode->CompatibleChecksum, 0, bIsRPC);
					StaticStructArrayPropertyNode->Type->ParentProperty = StaticStructArrayPropertyNode;
				}
			}
			continue;
		}

		// If this is an object property, then we need to do two things:
		//	 1) Determine whether this property is a strong or weak reference to the object. Some subobjects (such as the CharacterMovementComponent)
		//		are in fact owned by the character, and can be stored in the same entity as the character itself. Some subobjects (such as the Controller
		//		field in AActor) is a weak reference, and should just store a reference to the real object. We inspect the CDO to determine whether
		//		the owner of the property value is equal to itself. As structs don't have CDOs, we assume that all object properties in structs are
		//		weak references.
		//
		//   2) Obtain the concrete object type stored in this property. For example, the property containing the CharacterMovementComponent
		//      might be a property which stores a MovementComponent pointer, so we'd need to somehow figure out the real type being stored there
		//		during runtime. This is determined by getting the CDO of this class to determine what is stored in that property.
		UObjectProperty* ObjectProperty = Cast<UObjectProperty>(Property);
		check(ObjectProperty);

		// If this is a property of a struct, assume it's a weak reference.
		if (!Class)
		{
			continue;
		}

		UObject* ContainerCDO = Class->GetDefaultObject();
		check(ContainerCDO);

		// This is to ensure we handle static array properties only once.
		bool bHandleStaticArrayProperties = true;

		// Obtain the properties actual value from the CDO, so we can figure out its true type.
		UObject* Value = ObjectProperty->GetPropertyValue_InContainer(ContainerCDO);
		if (Value)
		{
			// If this is an editor-only property, skip it. As we've already added to the property list at this stage, just remove it.
			if (Value->IsEditorOnly())
			{
				UE_LOG(LogSpatialGDKSchemaGenerator, Verbose, TEXT("%s - editor only, skipping"), *Property->GetName());
				TypeNode->PropertiesMap.Remove(Property);
				continue;
			}

			// Check whether the outer is the CDO of the class we're generating for
			// or the CDO of any of its parent classes.
			// (this also covers generating schema for a Blueprint derived from the outer's class)
			UObject* Outer = Value->GetOuter();
			if ((Outer != nullptr) &&
				Outer->HasAnyFlags(RF_ClassDefaultObject) &&
				ContainerCDO->IsA(Outer->GetClass()))
			{
				UE_LOG(LogSpatialGDKSchemaGenerator, Verbose, TEXT("Property Class: %s Instance Class: %s"), *ObjectProperty->PropertyClass->GetName(), *Value->GetClass()->GetName());

				// This property is definitely a strong reference, recurse into it.
				PropertyNode->Type = CreateUnrealTypeInfo(Value->GetClass(), ParentChecksum, 0, bIsRPC);
				PropertyNode->Type->ParentProperty = PropertyNode;
				SetObjectPath(PropertyNode->Type, Value->GetPathName());
				//PropertyNode->Type->ObjectPath = Value->GetPathName();
				PropertyNode->Type->bObjectEditorOnly = Value->IsEditorOnly();
				PropertyNode->Type->Name = Value->GetFName();

				if (!bIsRPC)
				{
					// For static arrays we need to make a new object array member node.
					for (int i = 1; i < Property->ArrayDim; i++)
					{
						TSharedPtr<FUnrealProperty> StaticObjectArrayPropertyNode = CreateUnrealProperty(TypeNode, Property, ParentChecksum, i);

						// Note: The parent checksum of static arrays of strong object references will be the parent checksum of this class.
						StaticObjectArrayPropertyNode->Type = CreateUnrealTypeInfo(Value->GetClass(), ParentChecksum, 0, bIsRPC);
						StaticObjectArrayPropertyNode->Type->ParentProperty = StaticObjectArrayPropertyNode;
					}
				}
				bHandleStaticArrayProperties = false;
			}
			else
			{
				// The values outer is not us, store as weak reference.
				UE_LOG(LogSpatialGDKSchemaGenerator, Verbose, TEXT("%s - %s weak reference (outer not this)"), *Property->GetName(), *ObjectProperty->PropertyClass->GetName());
			}
		}
		else
		{
			// If value is just nullptr, then we clearly don't own it.
			UE_LOG(LogSpatialGDKSchemaGenerator, Verbose, TEXT("%s - %s weak reference (null init)"), *Property->GetName(), *ObjectProperty->PropertyClass->GetName());
		}

		// Weak reference static arrays are handled as a single UObjectRef per static array member.
		if (!bIsRPC && bHandleStaticArrayProperties)
		{
			for (int i = 1; i < Property->ArrayDim; i++)
			{
				CreateUnrealProperty(TypeNode, Property, ParentChecksum, i);
			}
		}
	} // END TFieldIterator<UProperty>

	// Blueprint components don't exist on the CDO so we need to iterate over the
	// BlueprintGeneratedClass (and all of its blueprint parents) to find all blueprint components
	UClass* BlueprintClass = Class;
	while (UBlueprintGeneratedClass* BGC = Cast<UBlueprintGeneratedClass>(BlueprintClass))
	{
		if (USimpleConstructionScript* SCS = BGC->SimpleConstructionScript)
		{
			for (USCS_Node* Node : SCS->GetAllNodes())
			{
				if (Node->ComponentTemplate == nullptr)
				{
					continue;
				}

				for (auto& PropertyPair : TypeNode->PropertiesMap)
				{
					UObjectProperty* ObjectProperty = Cast<UObjectProperty>(PropertyPair.Key);
					if (ObjectProperty == nullptr) continue;
					TSharedPtr<FUnrealProperty> PropertyNode = PropertyPair.Value;

					if (ObjectProperty->GetName().Equals(Node->GetVariableName().ToString()))
					{
						PropertyNode->Type = CreateUnrealTypeInfo(ObjectProperty->PropertyClass, ParentChecksum, 0, bIsRPC);
						PropertyNode->Type->ParentProperty = PropertyNode;
						//PropertyNode->Type->ObjectPath = Node->ComponentTemplate->GetPathName();
						SetObjectPath(PropertyNode->Type, Node->ComponentTemplate->GetPathName());
						PropertyNode->Type->bObjectEditorOnly = Node->ComponentTemplate->IsEditorOnly();
						PropertyNode->Type->Name = ObjectProperty->GetFName();

						if (Node->ComponentTemplate->GetPathName().Contains("GEN_VARIABLE"))
						{
							/*UE_LOG(LogTemp, Warning, TEXT("--- FOUND GEN VARIABLE ---"));
							LogUnrealProperty(PropertyNode, 1);*/
						}
					}
				}
			}
		}

		BlueprintClass = BlueprintClass->GetSuperClass();
	}

	// If this is not a class, exit now, as structs cannot have RPCs or replicated properties.
	if (!Class)
	{
		return TypeNode;
	}

	TArray<UFunction*> RelevantClassFunctions = SpatialGDK::GetClassRPCFunctions(Class);

	// Iterate through each RPC in the class.
	TypeNode->NumRPCs += RelevantClassFunctions.Num();

	// Set up replicated properties by reading the rep layout and matching the properties with the ones in the type node.
	// Based on inspection in InitFromObjectClass, the RepLayout will always replicate object properties using NetGUIDs, regardless of
	// ownership. However, the rep layout will recurse into structs and allocate rep handles for their properties, unless the condition
	// "Struct->StructFlags & STRUCT_NetSerializeNative" is true. In this case, the entire struct is replicated as a whole.
	FRepLayout RepLayout;
	RepLayout.InitFromObjectClass(Class);
	for (int CmdIndex = 0; CmdIndex < RepLayout.Cmds.Num(); ++CmdIndex)
	{
		FRepLayoutCmd& Cmd = RepLayout.Cmds[CmdIndex];
		if (Cmd.Type == ERepLayoutCmdType::Return || Cmd.Property == nullptr)
		{
			continue;
		}

		// Jump over invalid replicated property types
		if (Cmd.Property->IsA<UDelegateProperty>() || Cmd.Property->IsA<UMulticastDelegateProperty>() || Cmd.Property->IsA<UInterfaceProperty>())
		{
			continue;
		}

		FRepParentCmd& Parent = RepLayout.Parents[Cmd.ParentIndex];

		// In a FRepLayout, all the root level replicated properties in a class are stored in the Parents array.
		// The Cmds array is an expanded version of the Parents array. This usually maps 1:1 with the Parents array (as most properties
		// don't contain other properties). The main exception are structs which don't have a native serialize function. In this case
		// multiple Cmds map to the structs properties, but they all have the same ParentIndex (which points to the root replicated property
		// which contains them.
		//
		// This might be problematic if we have a property which is inside a struct, nested in another struct which is replicated. For example:
		//
		//	class Foo
		//	{
		//		struct Bar
		//		{
		// 			struct Baz
		// 			{
		// 				int Nested;
		// 			} Baz;
		// 		} Bar;
		//	}
		//
		// The parents array will contain "Bar", and the cmds array will contain "Nested", but we have no reference to "Baz" anywhere in the RepLayout.
		// What we do here is recurse into all of Bar's properties in the AST until we find Baz.

		TSharedPtr<FUnrealProperty> PropertyNode = nullptr;

		// Simple case: Cmd is a root property in the object.
		if (Parent.Property == Cmd.Property)
		{
			// Make sure we have the correct property via the checksums.
			for (auto& PropertyPair : TypeNode->PropertiesMap)
			{
				if (PropertyPair.Value->CompatibleChecksum == Cmd.CompatibleChecksum)
				{
					PropertyNode = PropertyPair.Value;
				}
			}
		}
		else
		{
			// It's possible to have duplicate parent properties (they are distinguished by ArrayIndex), so we make sure to look at them all.
			TArray<TSharedPtr<FUnrealProperty>> RootProperties;
			TypeNode->PropertiesMap.MultiFind(Parent.Property, RootProperties);

			for (TSharedPtr<FUnrealProperty>& RootProperty : RootProperties)
			{
				checkf(RootProperty->Type.IsValid(), TEXT("Properties in the AST which are parent properties in the rep layout must have child properties"));
				VisitAllPropertiesMap(RootProperty->Type, [&PropertyNode, &Cmd](TSharedPtr<FUnrealProperty> Property)
				{
					if (Property->CompatibleChecksum == Cmd.CompatibleChecksum)
					{
						checkf(!PropertyNode.IsValid(), TEXT("We've already found a previous property node with the same property. This indicates that we have a 'diamond of death' style situation."))
						PropertyNode = Property;
					}
					return true;
				}, false);
			}
		}
		checkf(PropertyNode.IsValid(), TEXT("Couldn't find the Cmd property inside the Parent's sub-properties. This shouldn't happen."));

		// We now have the right property node. Fill in the rep data.
		TSharedPtr<FUnrealRepData> RepDataNode = MakeShared<FUnrealRepData>();
		RepDataNode->RepLayoutType = (ERepLayoutCmdType)Cmd.Type;
		RepDataNode->Condition = Parent.Condition;
		RepDataNode->RepNotifyCondition = Parent.RepNotifyCondition;
		RepDataNode->ArrayIndex = PropertyNode->StaticArrayIndex;
		if (Parent.RoleSwapIndex != -1)
		{
			const int32 SwappedCmdIndex = RepLayout.Parents[Parent.RoleSwapIndex].CmdStart;
			RepDataNode->RoleSwapHandle = static_cast<int32>(RepLayout.Cmds[SwappedCmdIndex].RelativeHandle);
		}
		else
		{
			RepDataNode->RoleSwapHandle = -1;
		}
		PropertyNode->ReplicationData = RepDataNode;
		PropertyNode->ReplicationData->Handle = Cmd.RelativeHandle;

		if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
		{
			// Bypass the inner properties and null terminator cmd when processing dynamic arrays.
			CmdIndex = Cmd.EndCmd - 1;
		}
	} // END CMD FOR LOOP

	// Find the handover properties.
	uint16 HandoverDataHandle = 1;
	VisitAllPropertiesMap(TypeNode, [&HandoverDataHandle, &Class](TSharedPtr<FUnrealProperty> PropertyInfo)
	{
		if (PropertyInfo->PropertyFlags & CPF_Handover)
		{
			if (PropertyInfo->bStructProperty)
			{
				if (PropertyInfo->StructFlags & STRUCT_NetDeltaSerializeNative)
				{
					// Warn about delta serialization
					UE_LOG(LogSpatialGDKSchemaGenerator, Warning, TEXT("%s in %s uses delta serialization. " \
						"This is not supported and standard serialization will be used instead."), *PropertyInfo->PropertyName, *Class->GetName());
				}
			}
			PropertyInfo->HandoverData = MakeShared<FUnrealHandoverData>();
			PropertyInfo->HandoverData->Handle = HandoverDataHandle++;
		}
		return true;
	}, false);

	if (ParentChecksum == 0)
	{
		CleanPropertyMaps(TypeNode);
	}

	return TypeNode;
}

void CleanPropertyMaps(TSharedPtr<FUnrealType> TypeInfo)
{
	if (!TypeInfo.IsValid())
	{
		return;
	}

	TypeInfo->PropertiesMap.Empty();

	for (auto& PropertyInfo : TypeInfo->PropertiesList)
	{
		CleanPropertyMaps(PropertyInfo->Type);
	}
}

FUnrealFlatRepData GetFlatRepData(TSharedPtr<FUnrealType> TypeInfo)
{
	FUnrealFlatRepData RepData;
	RepData.Add(REP_MultiClient);
	RepData.Add(REP_SingleClient);

	VisitAllPropertiesList(TypeInfo, [&RepData](TSharedPtr<FUnrealProperty> PropertyInfo)
	{
		if (PropertyInfo->ReplicationData.IsValid())
		{
			EReplicatedPropertyGroup Group = REP_MultiClient;
			switch (PropertyInfo->ReplicationData->Condition)
			{
			case COND_AutonomousOnly:
			case COND_OwnerOnly:
				Group = REP_SingleClient;
				break;
			}
			RepData[Group].Add(PropertyInfo->ReplicationData->Handle, PropertyInfo);
		}
		return true;
	}, false);

	// Sort by replication handle.
	RepData[REP_MultiClient].KeySort([](uint16 A, uint16 B)
	{
		return A < B;
	});
	RepData[REP_SingleClient].KeySort([](uint16 A, uint16 B)
	{
		return A < B;
	});
	return RepData;
}

FCmdHandlePropertyMap GetFlatHandoverData(TSharedPtr<FUnrealType> TypeInfo)
{
	FCmdHandlePropertyMap HandoverData;
	VisitAllPropertiesList(TypeInfo, [&HandoverData](TSharedPtr<FUnrealProperty> PropertyInfo)
	{
		if (PropertyInfo->HandoverData.IsValid())
		{
			HandoverData.Add(PropertyInfo->HandoverData->Handle, PropertyInfo);
		}
		return true;
	}, false);

	// Sort by property handle.
	HandoverData.KeySort([](uint16 A, uint16 B)
	{
		return A < B;
	});
	return HandoverData;
}

//FUnrealRPCsByType GetAllRPCsByType(TSharedPtr<FUnrealType> TypeInfo)
//{
//	FUnrealRPCsByType RPCsByType;
//	RPCsByType.Add(RPC_Client);
//	RPCsByType.Add(RPC_Server);
//	RPCsByType.Add(RPC_CrossServer);
//	RPCsByType.Add(RPC_NetMulticast);
//	VisitAllObjects(TypeInfo, [&RPCsByType](TSharedPtr<FUnrealType> Type)
//	{
//		for (auto& RPC : Type->RPCs)
//		{
//			RPCsByType.FindOrAdd(RPC.Value->Type).Add(RPC.Value);
//		}
//		return true;
//	}, false);
//	return RPCsByType;
//}

TArray<TSharedPtr<FUnrealProperty>> GetPropertyChain(TSharedPtr<FUnrealProperty> LeafProperty)
{
	TArray<TSharedPtr<FUnrealProperty>> OutputChain;
	TSharedPtr<FUnrealProperty> CurrentProperty = LeafProperty;
	while (CurrentProperty.IsValid())
	{
		OutputChain.Add(CurrentProperty);
		if (CurrentProperty->ContainerType.IsValid())
		{
			TSharedPtr<FUnrealType> EnclosingType = CurrentProperty->ContainerType.Pin();
			CurrentProperty = EnclosingType->ParentProperty.Pin();
		}
		else
		{
			CurrentProperty.Reset();
		}
	}

	// As we started at the leaf property and worked our way up, we need to reverse the list at the end.
	Algo::Reverse(OutputChain);
	return OutputChain;
}

FSubobjectMap GetAllSubobjects(TSharedPtr<FUnrealType> TypeInfo)
{
	FSubobjectMap Subobjects;

	TSet<FString> SeenComponents;
	uint32 CurrentOffset = 1;

	for (auto& PropertyInfo : TypeInfo->PropertiesList)
	{
		TSharedPtr<FUnrealType>& PropertyTypeInfo = PropertyInfo->Type;

		if (PropertyInfo->bObjectProperty && PropertyTypeInfo.IsValid())
		{
			if (!PropertyTypeInfo->bObjectEditorOnly)
			{
				if (!SeenComponents.Contains(PropertyTypeInfo->ObjectPath))
				{
					SeenComponents.Add(PropertyTypeInfo->ObjectPath);
					Subobjects.Add(CurrentOffset, PropertyTypeInfo);
				}

				CurrentOffset++;
			}
		}
	}

	return Subobjects;
}

FString PropertyToSchemaType(UProperty* Property)
{
	FString DataType;

	if (Property->IsA(UStructProperty::StaticClass()))
	{
		UStructProperty* StructProp = Cast<UStructProperty>(Property);
		UScriptStruct* Struct = StructProp->Struct;
		DataType = TEXT("bytes");
	}
	else if (Property->IsA(UBoolProperty::StaticClass()))
	{
		DataType = TEXT("bool");
	}
	else if (Property->IsA(UFloatProperty::StaticClass()))
	{
		DataType = TEXT("float");
	}
	else if (Property->IsA(UDoubleProperty::StaticClass()))
	{
		DataType = TEXT("double");
	}
	else if (Property->IsA(UInt8Property::StaticClass()))
	{
		DataType = TEXT("int32");
	}
	else if (Property->IsA(UInt16Property::StaticClass()))
	{
		DataType = TEXT("int32");
	}
	else if (Property->IsA(UIntProperty::StaticClass()))
	{
		DataType = TEXT("int32");
	}
	else if (Property->IsA(UInt64Property::StaticClass()))
	{
		DataType = TEXT("int64");
	}
	else if (Property->IsA(UByteProperty::StaticClass()))
	{
		DataType = TEXT("uint32"); // uint8 not supported in schema.
	}
	else if (Property->IsA(UUInt16Property::StaticClass()))
	{
		DataType = TEXT("uint32");
	}
	else if (Property->IsA(UUInt32Property::StaticClass()))
	{
		DataType = TEXT("uint32");
	}
	else if (Property->IsA(UUInt64Property::StaticClass()))
	{
		DataType = TEXT("uint64");
	}
	else if (Property->IsA(UNameProperty::StaticClass()) || Property->IsA(UStrProperty::StaticClass()) || Property->IsA(UTextProperty::StaticClass()))
	{
		DataType = TEXT("string");
	}
	else if (Property->IsA(UObjectPropertyBase::StaticClass()))
	{
		DataType = TEXT("UnrealObjectRef");
	}
	else if (Property->IsA(UArrayProperty::StaticClass()))
	{
		DataType = PropertyToSchemaType(Cast<UArrayProperty>(Property)->Inner);
		DataType = FString::Printf(TEXT("list<%s>"), *DataType);
	}
	else if (Property->IsA(UEnumProperty::StaticClass()))
	{
		DataType = GetEnumDataType(Cast<UEnumProperty>(Property));
	}
	else
	{
		DataType = TEXT("bytes");
	}

	return DataType;
}

FString GetEnumDataType(const UEnumProperty* EnumProperty)
{
	FString DataType;

	if (EnumProperty->ElementSize < 4)
	{
		// schema types don't include support for 8 or 16 bit data types
		DataType = TEXT("uint32");
	}
	else
	{
		DataType = EnumProperty->GetUnderlyingProperty()->GetCPPType();
	}

	return DataType;
}

void LogUnrealType(const TSharedPtr<FUnrealType> Type, int RecurseDepth, FString Indent)
{
	UE_LOG(LogTemp, Display, TEXT("%s| ---- Type ----"), *Indent);
	if (!Type.IsValid())
	{
		UE_LOG(LogTemp, Display, TEXT("%s| Invalid"), *Indent);
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("%s| Name: %s"), *Indent, *Type->Name.ToString());
	UE_LOG(LogTemp, Display, TEXT("%s| ObjectPath: %s"), *Indent, *Type->ObjectPath);
	UE_LOG(LogTemp, Display, TEXT("%s| ClassPath: %s"), *Indent, *Type->ClassPath);
	UE_LOG(LogTemp, Display, TEXT("%s| ClassName: %s"), *Indent, *Type->ClassName);
	UE_LOG(LogTemp, Display, TEXT("%s| NumRPCs: %d"), *Indent, Type->NumRPCs);
	UE_LOG(LogTemp, Display, TEXT("%s| bIsActorClass: %d"), *Indent, Type->bIsActorClass);
	UE_LOG(LogTemp, Display, TEXT("%s| bIsActorComponent: %d"), *Indent, Type->bIsActorComponent);
	UE_LOG(LogTemp, Display, TEXT("%s| ParentProperty:"), *Indent);
	if (RecurseDepth > 0)
	{
		LogUnrealProperty(Type->ParentProperty.Pin(), RecurseDepth - 1, Indent + "  ");
	}
	UE_LOG(LogTemp, Display, TEXT("%s| Properties: %d"), *Indent, Type->PropertiesList.Num());
	if (RecurseDepth > 0)
	{
		for (auto& Property : Type->PropertiesList)
		{
			LogUnrealProperty(Property, RecurseDepth - 1, Indent + "  ");
		}
		
	}
}

void LogUnrealProperty(const TSharedPtr<FUnrealProperty> Property, int RecurseDepth, FString Indent)
 {
	UE_LOG(LogTemp, Display, TEXT(""));
	UE_LOG(LogTemp, Display, TEXT("%s|==================="), *Indent);
	UE_LOG(LogTemp, Display, TEXT("%s| ---- Property ----"), *Indent);
	if (!Property.IsValid())
	{
		UE_LOG(LogTemp, Display, TEXT("%s| Invalid"), *Indent);
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("%s| PropertyPath: %s"), *Indent, *Property->PropertyPath);
	UE_LOG(LogTemp, Display, TEXT("%s| PropertyName: %s"), *Indent, *Property->PropertyName);
	UE_LOG(LogTemp, Display, TEXT("%s| bObjectProperty: %d"), *Indent, Property->bObjectProperty);
	UE_LOG(LogTemp, Display, TEXT("%s| bStructProperty: %d"), *Indent, Property->bStructProperty);
	UE_LOG(LogTemp, Display, TEXT("%s| bArrayProperty: %d"), *Indent, Property->bArrayProperty);
	UE_LOG(LogTemp, Display, TEXT("%s| bObjectArrayProperty: %d"), *Indent, Property->bObjectArrayProperty);
	UE_LOG(LogTemp, Display, TEXT("%s| ArrayDim: %d"), *Indent, Property->ArrayDim);
	UE_LOG(LogTemp, Display, TEXT("%s| PropertyFlags: %d"), *Indent, (int64)Property->PropertyFlags);
	UE_LOG(LogTemp, Display, TEXT("%s| StructFlags: %d"), *Indent, (int64)Property->StructFlags);
	UE_LOG(LogTemp, Display, TEXT("%s| DataType: %s"), *Indent, *Property->DataType);
	UE_LOG(LogTemp, Display, TEXT("%s| StaticArrayIndex: %d"), *Indent, Property->StaticArrayIndex);
	UE_LOG(LogTemp, Display, TEXT("%s| CompatibleChecksum: %u"), *Indent, Property->CompatibleChecksum);
	UE_LOG(LogTemp, Display, TEXT("%s| ParentChecksum: %u"), *Indent, Property->ParentChecksum);
	if (RecurseDepth > 0)
	{
		UE_LOG(LogTemp, Display, TEXT("%s| Type:"), *Indent);
		LogUnrealType(Property->Type, RecurseDepth - 1, Indent + "  ");
		UE_LOG(LogTemp, Display, TEXT("%s| ContainerType:"), *Indent);
		LogUnrealType(Property->ContainerType.Pin(), RecurseDepth - 1, Indent + "  ");
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("%s| Type / ContainerType: - Hit Recurse Depth"), *Indent);
	}
}

void SetObjectPath(TSharedPtr<FUnrealType> UnrealType, FString ObjectPath)
{
	const FString GenVariablePostFix = TEXT("_GEN_VARIABLE");
	UnrealType->ObjectPath = ObjectPath;
	UnrealType->ObjectPath.ReplaceInline(*GenVariablePostFix, TEXT(""));
}

void VisitAllObjects(TSharedPtr<FUnrealType> TypeNode, TFunction<bool(TSharedPtr<FUnrealType>)> Visitor, bool bRecurseIntoSubobjects)
{
	bool bShouldRecurseFurther = Visitor(TypeNode);
	for (auto& Property : TypeNode->PropertiesList)
	{
		if (bShouldRecurseFurther && Property->Type.IsValid())
		{
			// Either recurse into subobjects if they're structs or bRecurseIntoSubobjects is true.
			if (bRecurseIntoSubobjects || Property->bStructProperty)
			{
				VisitAllObjects(Property->Type, Visitor, bRecurseIntoSubobjects);
			}
		}
	}
}

uint32 GetTypeHash(const FUnrealType& A)
{
	uint32 Hash = 0;
	Hash = HashCombine(Hash, GetTypeHash(A.ObjectPath));
	Hash = HashCombine(Hash, GetTypeHash(A.bObjectEditorOnly));
	Hash = HashCombine(Hash, GetTypeHash(A.Name));
	for (const TSharedPtr<FUnrealProperty> Property : A.PropertiesList)
	{
		if (Property.IsValid())
		{
			Hash = HashCombine(Hash, GetTypeHash(Property.Get()));
		}
	}
	Hash = HashCombine(Hash, GetTypeHash(A.NumRPCs));
	if (A.ParentProperty.IsValid())
	{
		TSharedPtr<FUnrealProperty> ParentPropertyPtr = A.ParentProperty.Pin();
		Hash = HashCombine(Hash, GetTypeHash(ParentPropertyPtr.Get()));
	}
	Hash = HashCombine(Hash, GetTypeHash(A.bIsActorComponent));
	Hash = HashCombine(Hash, GetTypeHash(A.bIsActorComponent));
	Hash = HashCombine(Hash, GetTypeHash(A.ClassPath));
	Hash = HashCombine(Hash, GetTypeHash(A.ClassName));
	return Hash;
}

uint32 GetTypeHash(const FUnrealProperty& A)
{
	uint32 Hash = 0;
	if (A.Type.IsValid())
	{
		Hash = HashCombine(Hash, GetTypeHash(A.Type.Get()));
	}
	if (A.ReplicationData.IsValid())
	{
		Hash = HashCombine(Hash, GetTypeHash(A.ReplicationData.Get()));
	}
	if (A.HandoverData.IsValid())
	{
		Hash = HashCombine(Hash, GetTypeHash(A.HandoverData.Get()));
	}
	if (A.ContainerType.IsValid())
	{
		Hash = HashCombine(Hash, GetTypeHash(A.ContainerType.Pin().Get()));
	}

	Hash = HashCombine(Hash, GetTypeHash(A.StaticArrayIndex));
	Hash = HashCombine(Hash, GetTypeHash(A.CompatibleChecksum));
	Hash = HashCombine(Hash, GetTypeHash(A.ParentChecksum));

	Hash = HashCombine(Hash, GetTypeHash(A.bObjectProperty));
	Hash = HashCombine(Hash, GetTypeHash(A.bStructProperty));
	Hash = HashCombine(Hash, GetTypeHash(A.bArrayProperty));
	Hash = HashCombine(Hash, GetTypeHash(A.bObjectArrayProperty));

	Hash = HashCombine(Hash, GetTypeHash(A.PropertyPath));
	Hash = HashCombine(Hash, GetTypeHash(A.PropertyName));

	Hash = HashCombine(Hash, GetTypeHash(A.ArrayDim));
	Hash = HashCombine(Hash, GetTypeHash(A.PropertyFlags));
	Hash = HashCombine(Hash, GetTypeHash(A.StructFlags));

	Hash = HashCombine(Hash, GetTypeHash(A.DataType));

	return Hash;
}
