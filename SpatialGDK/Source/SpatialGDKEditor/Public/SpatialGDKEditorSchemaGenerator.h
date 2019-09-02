// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSpatialGDKSchemaGenerator, Log, All);

SPATIALGDKEDITOR_API bool SpatialGDKGenerateSchema(bool bSaveSchemaDatabase = true, bool bRunSchemaCompiler = true);

SPATIALGDKEDITOR_API bool SpatialGDKGenerateSchemaForClasses(TSet<UClass*> Classes);

SPATIALGDKEDITOR_API bool SaveSchemaDatabase();

SPATIALGDKEDITOR_API bool RunSchemaCompiler();

SPATIALGDKEDITOR_API void ClearGeneratedSchema();

SPATIALGDKEDITOR_API void DeleteGeneratedSchemaFiles();

SPATIALGDKEDITOR_API void CopyWellKnownSchemaFiles();

SPATIALGDKEDITOR_API bool TryLoadExistingSchemaDatabase();

SPATIALGDKEDITOR_API bool GeneratedSchemaFolderExists();

SPATIALGDKEDITOR_API bool IsSupportedClass(UClass* SupportedClass);
