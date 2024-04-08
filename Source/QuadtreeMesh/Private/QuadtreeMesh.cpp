// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuadtreeMesh.h"
#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "FQuadtreeMeshModule"

void FQuadtreeMeshModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	const FString ShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("QuadtreeMesh"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping("/Plugin/QuadtreeMesh", ShaderDir);
}

void FQuadtreeMeshModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FQuadtreeMeshModule, QuadtreeMesh)