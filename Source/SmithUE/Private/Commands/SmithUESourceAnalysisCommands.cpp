// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUESourceAnalysisCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"
#include "SmithUEModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

// ---------------------------------------------------------------------------
// Local data structures for parsed relationships
// ---------------------------------------------------------------------------

namespace
{
	struct FClassInfo
	{
		FString ClassName;
		FString ParentClass;
		TArray<FString> Includes;
		TArray<FString> CompositionMembers; // "Type MemberName"
	};

	struct FModuleDepInfo
	{
		FString ModuleName;
		TArray<FString> PublicDeps;
		TArray<FString> PrivateDeps;
	};

	// ---------------------------------------------------------------------------
	// C++ Source Parsing Helpers
	// ---------------------------------------------------------------------------

	void ParseClassDeclarations(const FString& FileContent, const FString& FileName, TArray<FClassInfo>& OutClasses)
	{
		// Match: class [API_EXPORT] ClassName : public/private/protected ParentClass
		// Also handles UCLASS, USTRUCT prefixed classes
		FRegexPattern ClassPattern(TEXT("(?:^|\\n)\\s*(?:UCLASS|USTRUCT|UINTERFACE)\\s*\\([^)]*\\)\\s*\\n\\s*class\\s+(?:[A-Z_]+_API\\s+)?([A-Za-z_][A-Za-z0-9_]*)\\s*(?::\\s*public\\s+([A-Za-z_][A-Za-z0-9_]*))?"));
		FRegexMatcher ClassMatcher(ClassPattern, FileContent);
		while (ClassMatcher.FindNext())
		{
			FClassInfo Info;
			Info.ClassName = ClassMatcher.GetCaptureGroup(1);
			Info.ParentClass = ClassMatcher.GetCaptureGroup(2);
			OutClasses.Add(Info);
		}

		// Also match plain class declarations without UCLASS macro
		FRegexPattern PlainClassPattern(TEXT("(?:^|\\n)\\s*class\\s+(?:[A-Z_]+_API\\s+)?([A-Za-z_][A-Za-z0-9_]*)\\s*(?:final\\s*)?:\\s*public\\s+([A-Za-z_][A-Za-z0-9_]*)"));
		FRegexMatcher PlainClassMatcher(PlainClassPattern, FileContent);
		while (PlainClassMatcher.FindNext())
		{
			FString ClassName = PlainClassMatcher.GetCaptureGroup(1);
			// Avoid duplicates from UCLASS match
			bool bAlreadyFound = false;
			for (const FClassInfo& Existing : OutClasses)
			{
				if (Existing.ClassName == ClassName) { bAlreadyFound = true; break; }
			}
			if (!bAlreadyFound)
			{
				FClassInfo Info;
				Info.ClassName = ClassName;
				Info.ParentClass = PlainClassMatcher.GetCaptureGroup(2);
				OutClasses.Add(Info);
			}
		}
	}

	void ParseIncludes(const FString& FileContent, TArray<FString>& OutIncludes)
	{
		FRegexPattern IncludePattern(TEXT("#include\\s*\"([^\"]+)\""));
		FRegexMatcher IncludeMatcher(IncludePattern, FileContent);
		while (IncludeMatcher.FindNext())
		{
			OutIncludes.Add(IncludeMatcher.GetCaptureGroup(1));
		}
	}

	void ParseComposition(const FString& FileContent, const FString& OwnerClass, TArray<FClassInfo>& OutClasses)
	{
		// Match UPROPERTY pointer members: UPROPERTY(...) UType* Name;
		FRegexPattern PropPattern(TEXT("UPROPERTY\\s*\\([^)]*\\)\\s*\\n?\\s*(?:class\\s+)?([A-Z][A-Za-z0-9_]*)\\s*\\*\\s+([A-Za-z_][A-Za-z0-9_]*)"));
		FRegexMatcher PropMatcher(PropPattern, FileContent);
		while (PropMatcher.FindNext())
		{
			FString TypeName = PropMatcher.GetCaptureGroup(1);
			FString MemberName = PropMatcher.GetCaptureGroup(2);
			// Find or create the owner class entry
			for (FClassInfo& Info : OutClasses)
			{
				if (Info.ClassName == OwnerClass)
				{
					Info.CompositionMembers.Add(FString::Printf(TEXT("%s %s"), *TypeName, *MemberName));
					break;
				}
			}
		}
	}

	// ---------------------------------------------------------------------------
	// Build.cs Parsing
	// ---------------------------------------------------------------------------

	FModuleDepInfo ParseBuildCs(const FString& FileContent, const FString& FilePath)
	{
		FModuleDepInfo Info;
		Info.ModuleName = FPaths::GetBaseFilename(FilePath);
		// Remove .Build from module name
		Info.ModuleName.RemoveFromEnd(TEXT(".Build"));

		// Match PublicDependencyModuleNames.AddRange(new string[] { "X", "Y" })
		// or PublicDependencyModuleNames.Add("X")
		FRegexPattern PubRangePattern(TEXT("PublicDependencyModuleNames\\.AddRange\\s*\\([^{]*\\{([^}]+)\\}"));
		FRegexMatcher PubRangeMatcher(PubRangePattern, FileContent);
		while (PubRangeMatcher.FindNext())
		{
			FString Block = PubRangeMatcher.GetCaptureGroup(1);
			FRegexPattern StringPattern(TEXT("\"([^\"]+)\""));
			FRegexMatcher StringMatcher(StringPattern, Block);
			while (StringMatcher.FindNext())
			{
				Info.PublicDeps.AddUnique(StringMatcher.GetCaptureGroup(1));
			}
		}

		FRegexPattern PubSinglePattern(TEXT("PublicDependencyModuleNames\\.Add\\s*\\(\\s*\"([^\"]+)\""));
		FRegexMatcher PubSingleMatcher(PubSinglePattern, FileContent);
		while (PubSingleMatcher.FindNext())
		{
			Info.PublicDeps.AddUnique(PubSingleMatcher.GetCaptureGroup(1));
		}

		// Private dependencies
		FRegexPattern PrivRangePattern(TEXT("PrivateDependencyModuleNames\\.AddRange\\s*\\([^{]*\\{([^}]+)\\}"));
		FRegexMatcher PrivRangeMatcher(PrivRangePattern, FileContent);
		while (PrivRangeMatcher.FindNext())
		{
			FString Block = PrivRangeMatcher.GetCaptureGroup(1);
			FRegexPattern StringPattern(TEXT("\"([^\"]+)\""));
			FRegexMatcher StringMatcher(StringPattern, Block);
			while (StringMatcher.FindNext())
			{
				Info.PrivateDeps.AddUnique(StringMatcher.GetCaptureGroup(1));
			}
		}

		FRegexPattern PrivSinglePattern(TEXT("PrivateDependencyModuleNames\\.Add\\s*\\(\\s*\"([^\"]+)\""));
		FRegexMatcher PrivSingleMatcher(PrivSinglePattern, FileContent);
		while (PrivSingleMatcher.FindNext())
		{
			Info.PrivateDeps.AddUnique(PrivSingleMatcher.GetCaptureGroup(1));
		}

		return Info;
	}

	// ---------------------------------------------------------------------------
	// Nomnoml DSL Generation
	// ---------------------------------------------------------------------------

	FString GenerateModuleNomnoml(const TArray<FClassInfo>& Classes, const TArray<FString>& IncludeEdges)
	{
		FString DSL;
		DSL += TEXT("#direction: down\n");
		DSL += TEXT("#spacing: 60\n");
		DSL += TEXT("#padding: 12\n");
		DSL += TEXT("#fontSize: 12\n");
		DSL += TEXT("#lineWidth: 1.5\n\n");

		// Inheritance edges
		TSet<FString> DrawnEdges;
		for (const FClassInfo& Info : Classes)
		{
			if (!Info.ParentClass.IsEmpty())
			{
				FString Edge = FString::Printf(TEXT("[%s] -> [%s]"), *Info.ParentClass, *Info.ClassName);
				if (!DrawnEdges.Contains(Edge))
				{
					DSL += Edge + TEXT("\n");
					DrawnEdges.Add(Edge);
				}
			}
		}

		// Composition edges
		for (const FClassInfo& Info : Classes)
		{
			for (const FString& Member : Info.CompositionMembers)
			{
				FString TypeName;
				FString MemberName;
				Member.Split(TEXT(" "), &TypeName, &MemberName);
				FString Edge = FString::Printf(TEXT("[%s] o-> [%s]"), *Info.ClassName, *TypeName);
				if (!DrawnEdges.Contains(Edge))
				{
					DSL += Edge + TEXT("\n");
					DrawnEdges.Add(Edge);
				}
			}
		}

		// Include/dependency edges (file-level)
		for (const FString& Edge : IncludeEdges)
		{
			if (!DrawnEdges.Contains(Edge))
			{
				DSL += Edge + TEXT("\n");
				DrawnEdges.Add(Edge);
			}
		}

		return DSL;
	}

	FString GenerateDependencyNomnoml(const TArray<FModuleDepInfo>& Modules)
	{
		FString DSL;
		DSL += TEXT("#direction: right\n");
		DSL += TEXT("#spacing: 80\n");
		DSL += TEXT("#padding: 12\n");
		DSL += TEXT("#fontSize: 12\n");
		DSL += TEXT("#lineWidth: 1.5\n\n");

		TSet<FString> DrawnEdges;
		for (const FModuleDepInfo& Mod : Modules)
		{
			for (const FString& Dep : Mod.PublicDeps)
			{
				FString Edge = FString::Printf(TEXT("[%s] -> [%s]"), *Mod.ModuleName, *Dep);
				if (!DrawnEdges.Contains(Edge))
				{
					DSL += Edge + TEXT("\n");
					DrawnEdges.Add(Edge);
				}
			}
			for (const FString& Dep : Mod.PrivateDeps)
			{
				FString Edge = FString::Printf(TEXT("[%s] --:> [%s]"), *Mod.ModuleName, *Dep);
				if (!DrawnEdges.Contains(Edge))
				{
					DSL += Edge + TEXT("\n");
					DrawnEdges.Add(Edge);
				}
			}
		}

		return DSL;
	}

	FString GenerateBlueprintNomnoml(const TArray<TPair<FString, FString>>& InheritanceEdges, const TArray<TPair<FString, FString>>& ComponentEdges)
	{
		FString DSL;
		DSL += TEXT("#direction: down\n");
		DSL += TEXT("#spacing: 60\n");
		DSL += TEXT("#padding: 12\n");
		DSL += TEXT("#fontSize: 12\n");
		DSL += TEXT("#lineWidth: 1.5\n\n");

		TSet<FString> DrawnEdges;
		for (const auto& Pair : InheritanceEdges)
		{
			FString Edge = FString::Printf(TEXT("[%s] -> [%s]"), *Pair.Key, *Pair.Value);
			if (!DrawnEdges.Contains(Edge))
			{
				DSL += Edge + TEXT("\n");
				DrawnEdges.Add(Edge);
			}
		}

		for (const auto& Pair : ComponentEdges)
		{
			FString Edge = FString::Printf(TEXT("[%s] o-> [<component> %s]"), *Pair.Key, *Pair.Value);
			if (!DrawnEdges.Contains(Edge))
			{
				DSL += Edge + TEXT("\n");
				DrawnEdges.Add(Edge);
			}
		}

		return DSL;
	}
}

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUESourceAnalysisCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
	Registry.Register(
		FSmithUEToolSchema(TEXT("analyze_module"), TEXT("Analysis"),
			TEXT("Analyze C++ source files in a directory and return a nomnoml relationship diagram (class inheritance, composition, includes). Supports large projects via directory-level scoping."),
			{
				FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Absolute or project-relative path to source directory (e.g. Plugins/SmithUE/Source/SmithUE)"), true),
				FSmithUEToolParam(TEXT("depth"), TEXT("string"), TEXT("Analysis depth: 'module' (file-level overview) or 'class' (full class relationships). Default: class")),
				FSmithUEToolParam(TEXT("max_files"), TEXT("number"), TEXT("Max files to analyze (default: 200, prevents timeout on huge directories)")),
			}),
		&HandleAnalyzeModule);

	Registry.Register(
		FSmithUEToolSchema(TEXT("analyze_dependencies"), TEXT("Analysis"),
			TEXT("Analyze Build.cs module dependencies and return a nomnoml dependency graph."),
			{
				FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Path to directory containing .Build.cs files (or single .Build.cs path). Searches recursively."), true),
			}),
		&HandleAnalyzeDependencies);

	Registry.Register(
		FSmithUEToolSchema(TEXT("analyze_blueprints"), TEXT("Analysis"),
			TEXT("Analyze Blueprint assets in a Content Browser path and return a nomnoml inheritance + component composition diagram."),
			{
				FSmithUEToolParam(TEXT("content_path"), TEXT("string"), TEXT("Content Browser path (e.g. /Game/Blueprints)"), true),
				FSmithUEToolParam(TEXT("recursive"), TEXT("boolean"), TEXT("Search subdirectories (default: true)")),
			}),
		&HandleAnalyzeBlueprints);
}

// ---------------------------------------------------------------------------
// HandleAnalyzeModule
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUESourceAnalysisCommands::HandleAnalyzeModule(const TSharedPtr<FJsonObject>& Params)
{
	FString Path = Params->GetStringField(TEXT("path"));
	if (Path.IsEmpty()) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("path is required")); }

	FString Depth = Params->GetStringField(TEXT("depth"));
	if (Depth.IsEmpty()) { Depth = TEXT("class"); }

	int32 MaxFiles = Params->HasField(TEXT("max_files")) ? (int32)Params->GetNumberField(TEXT("max_files")) : 200;

	// Resolve path
	FString AbsPath = Path;
	if (FPaths::IsRelative(Path))
	{
		AbsPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
	}
	if (!IFileManager::Get().DirectoryExists(*AbsPath))
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Directory not found: %s"), *AbsPath));
	}

	// Collect .h and .cpp files
	TArray<FString> HeaderFiles, CppFiles;
	IFileManager::Get().FindFilesRecursive(HeaderFiles, *AbsPath, TEXT("*.h"), true, false);
	IFileManager::Get().FindFilesRecursive(CppFiles, *AbsPath, TEXT("*.cpp"), true, false);

	TArray<FString> AllFiles;
	AllFiles.Append(HeaderFiles);
	AllFiles.Append(CppFiles);

	if (AllFiles.Num() == 0)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("No .h/.cpp files found in the specified path"));
	}

	// Limit file count
	int32 TotalFiles = AllFiles.Num();
	if (AllFiles.Num() > MaxFiles)
	{
		AllFiles.SetNum(MaxFiles);
	}

	// Parse all files
	TArray<FClassInfo> AllClasses;
	TArray<FString> IncludeEdges;
	TMap<FString, FString> FileToMainClass; // filename -> first class found

	for (const FString& FilePath : AllFiles)
	{
		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *FilePath)) { continue; }

		FString FileName = FPaths::GetBaseFilename(FilePath);

		// Parse class declarations
		int32 ClassesBefore = AllClasses.Num();
		ParseClassDeclarations(Content, FileName, AllClasses);

		// Parse includes for dependency edges (file level)
		if (Depth == TEXT("module"))
		{
			TArray<FString> Includes;
			ParseIncludes(Content, Includes);
			for (const FString& Inc : Includes)
			{
				FString IncBase = FPaths::GetBaseFilename(Inc);
				// Only internal includes (skip engine/third-party)
				bool bIsInternal = false;
				for (const FString& OtherFile : AllFiles)
				{
					if (FPaths::GetBaseFilename(OtherFile) == IncBase)
					{
						bIsInternal = true;
						break;
					}
				}
				if (bIsInternal && IncBase != FileName)
				{
					IncludeEdges.AddUnique(FString::Printf(TEXT("[%s] --> [%s]"), *FileName, *IncBase));
				}
			}
		}

		// Parse composition (only in class depth)
		if (Depth == TEXT("class") && AllClasses.Num() > ClassesBefore)
		{
			FString OwnerClass = AllClasses.Last().ClassName;
			ParseComposition(Content, OwnerClass, AllClasses);
		}
	}

	// Generate nomnoml
	FString Nomnoml = GenerateModuleNomnoml(AllClasses, IncludeEdges);

	// Build response
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("nomnoml"), Nomnoml);
	Data->SetNumberField(TEXT("classes_found"), AllClasses.Num());
	Data->SetNumberField(TEXT("files_analyzed"), AllFiles.Num());
	Data->SetNumberField(TEXT("files_total"), TotalFiles);
	Data->SetStringField(TEXT("depth"), Depth);
	if (TotalFiles > MaxFiles)
	{
		Data->SetStringField(TEXT("warning"), FString::Printf(TEXT("Only analyzed %d of %d files (max_files limit). Increase max_files or narrow the path."), MaxFiles, TotalFiles));
	}
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// HandleAnalyzeDependencies
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUESourceAnalysisCommands::HandleAnalyzeDependencies(const TSharedPtr<FJsonObject>& Params)
{
	FString Path = Params->GetStringField(TEXT("path"));
	if (Path.IsEmpty()) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("path is required")); }

	FString AbsPath = Path;
	if (FPaths::IsRelative(Path))
	{
		AbsPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
	}

	// Find all .Build.cs files
	TArray<FString> BuildFiles;
	if (AbsPath.EndsWith(TEXT(".Build.cs")))
	{
		if (FPaths::FileExists(AbsPath)) { BuildFiles.Add(AbsPath); }
	}
	else
	{
		IFileManager::Get().FindFilesRecursive(BuildFiles, *AbsPath, TEXT("*.Build.cs"), true, false);
	}

	if (BuildFiles.Num() == 0)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("No .Build.cs files found in the specified path"));
	}

	// Parse all Build.cs files
	TArray<FModuleDepInfo> Modules;
	for (const FString& BuildFile : BuildFiles)
	{
		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *BuildFile)) { continue; }
		Modules.Add(ParseBuildCs(Content, BuildFile));
	}

	// Generate nomnoml
	FString Nomnoml = GenerateDependencyNomnoml(Modules);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("nomnoml"), Nomnoml);
	Data->SetNumberField(TEXT("modules_found"), Modules.Num());

	// Also include raw module list for reference
	TArray<TSharedPtr<FJsonValue>> ModuleArray;
	for (const FModuleDepInfo& Mod : Modules)
	{
		TSharedPtr<FJsonObject> ModObj = MakeShared<FJsonObject>();
		ModObj->SetStringField(TEXT("name"), Mod.ModuleName);
		TArray<TSharedPtr<FJsonValue>> PubArr, PrivArr;
		for (const FString& D : Mod.PublicDeps) { PubArr.Add(MakeShared<FJsonValueString>(D)); }
		for (const FString& D : Mod.PrivateDeps) { PrivArr.Add(MakeShared<FJsonValueString>(D)); }
		ModObj->SetArrayField(TEXT("public_deps"), PubArr);
		ModObj->SetArrayField(TEXT("private_deps"), PrivArr);
		ModuleArray.Add(MakeShared<FJsonValueObject>(ModObj));
	}
	Data->SetArrayField(TEXT("modules"), ModuleArray);

	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

// ---------------------------------------------------------------------------
// HandleAnalyzeBlueprints
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUESourceAnalysisCommands::HandleAnalyzeBlueprints(const TSharedPtr<FJsonObject>& Params)
{
	FString ContentPath = Params->GetStringField(TEXT("content_path"));
	if (ContentPath.IsEmpty()) { return FSmithUECommonUtils::CreateErrorResponse(TEXT("content_path is required")); }

	bool bRecursive = !Params->HasField(TEXT("recursive")) || Params->GetBoolField(TEXT("recursive"));

	// Query AssetRegistry for Blueprint assets
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*ContentPath));
	Filter.bRecursivePaths = bRecursive;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	if (AssetDataList.Num() == 0)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("No Blueprint assets found in: %s"), *ContentPath));
	}

	// Analyze inheritance and components
	TArray<TPair<FString, FString>> InheritanceEdges;
	TArray<TPair<FString, FString>> ComponentEdges;
	int32 Analyzed = 0;

	for (const FAssetData& AssetData : AssetDataList)
	{
		UBlueprint* BP = Cast<UBlueprint>(AssetData.GetAsset());
		if (!BP) { continue; }
		Analyzed++;

		FString BpName = BP->GetName();

		// Inheritance
		if (BP->ParentClass)
		{
			FString ParentName = BP->ParentClass->GetName();
			InheritanceEdges.Add(TPair<FString, FString>(ParentName, BpName));
		}

		// Component composition from SCS
		if (BP->SimpleConstructionScript)
		{
			const TArray<USCS_Node*>& Nodes = BP->SimpleConstructionScript->GetAllNodes();
			for (const USCS_Node* Node : Nodes)
			{
				if (Node && Node->ComponentClass)
				{
					FString CompClassName = Node->ComponentClass->GetName();
					FString CompVarName = Node->GetVariableName().ToString();
					FString Label = FString::Printf(TEXT("%s\\n(%s)"), *CompVarName, *CompClassName);
					ComponentEdges.Add(TPair<FString, FString>(BpName, Label));
				}
			}
		}
	}

	// Generate nomnoml
	FString Nomnoml = GenerateBlueprintNomnoml(InheritanceEdges, ComponentEdges);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("nomnoml"), Nomnoml);
	Data->SetNumberField(TEXT("blueprints_found"), AssetDataList.Num());
	Data->SetNumberField(TEXT("blueprints_analyzed"), Analyzed);
	Data->SetNumberField(TEXT("inheritance_edges"), InheritanceEdges.Num());
	Data->SetNumberField(TEXT("component_edges"), ComponentEdges.Num());
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}
