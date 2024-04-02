// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class QuadtreeMesh : ModuleRules
{
	public QuadtreeMesh(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
				System.IO.Path.Combine(GetModuleDirectory("Renderer"), "Private"),
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
		
		bool bWithQuadMeshSelectionSupport = false;
		if (Target.bBuildEditor)
		{
			bWithQuadMeshSelectionSupport = true;

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"MeshDescription"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"SourceControl",
					"UnrealEd",
					"StaticMeshDescription",
					"MeshMergeUtilities"
				}
			);
		}
		// Add a feature define instead of relying on the generic WITH_EDITOR define
		PublicDefinitions.Add("WITH_QUADMESH_SELECTION_SUPPORT=" + (bWithQuadMeshSelectionSupport ? 1 : 0));
	}
	
}
