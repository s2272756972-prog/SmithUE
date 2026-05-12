// Copyright 2026, 123dx-svg. MIT License.

#include "Blueprint/SmithUEBpCompiler.h"

#include "Blueprint/SmithUEBpAtomicAPI.h"
#include "Blueprint/SmithUEBpAtomicAPIHelpers.h"
#include "Components/ActorComponent.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "SmithUEModule.h"
#include "UObject/UObjectIterator.h"

using namespace SmithUEBpAtomicAPIHelpers;

namespace
{
	struct FCompilerParam { FString Name; FString Type; };
	struct FCompilerSignature { FString ReturnType; FString FunctionName; TArray<FCompilerParam> Params; };

	static FString StripInlineComment(const FString& Line)
	{
		int32 CommentIndex = INDEX_NONE;
		bool bInString = false;
		for (int32 Index = 0; Index + 1 < Line.Len(); ++Index)
		{
			if (Line[Index] == '"') { bInString = !bInString; }
			if (!bInString && Line[Index] == '/' && Line[Index + 1] == '/') { CommentIndex = Index; break; }
		}
		return (CommentIndex == INDEX_NONE ? Line : Line.Left(CommentIndex)).TrimStartAndEnd();
	}

	static TArray<FString> SplitLines(const FString& Code)
	{
		TArray<FString> Lines;
		Code.ParseIntoArrayLines(Lines, false);
		for (FString& Line : Lines) { Line = StripInlineComment(Line); }
		return Lines;
	}

	static int32 FindFirstNonEmptyLine(const TArray<FString>& Lines, int32 StartIndex = 0)
	{
		for (int32 Index = StartIndex; Index < Lines.Num(); ++Index) { if (!Lines[Index].TrimStartAndEnd().IsEmpty()) { return Index; } }
		return INDEX_NONE;
	}

	static int32 FindMatchingBrace(const TArray<FString>& Lines, int32 OpenBraceLine)
	{
		int32 Depth = 0;
		for (int32 Index = OpenBraceLine; Index < Lines.Num(); ++Index)
		{
			const FString Trimmed = Lines[Index].TrimStartAndEnd();
			if (Trimmed.Contains(TEXT("{"))) { ++Depth; }
			if (Trimmed.Contains(TEXT("}"))) { --Depth; if (Depth == 0) { return Index; } }
		}
		return INDEX_NONE;
	}

	static int32 FindBlockOpenLine(const TArray<FString>& Lines, int32 HeaderLine)
	{
		if (Lines.IsValidIndex(HeaderLine) && Lines[HeaderLine].Contains(TEXT("{"))) { return HeaderLine; }
		const int32 NextLine = FindFirstNonEmptyLine(Lines, HeaderLine + 1);
		return (NextLine != INDEX_NONE && Lines[NextLine].TrimStartAndEnd() == TEXT("{")) ? NextLine : INDEX_NONE;
	}

	static FString TrimSemicolon(const FString& Text)
	{
		FString Result = Text.TrimStartAndEnd();
		if (Result.EndsWith(TEXT(";"))) { Result.LeftChopInline(1); }
		return Result.TrimStartAndEnd();
	}

	static TArray<FString> SplitArgs(const FString& Text)
	{
		TArray<FString> Args;
		FString Current;
		int32 Depth = 0;
		bool bInString = false;
		for (TCHAR Char : Text)
		{
			if (Char == '"') { bInString = !bInString; Current.AppendChar(Char); continue; }
			if (!bInString && Char == '(') { ++Depth; Current.AppendChar(Char); continue; }
			if (!bInString && Char == ')') { --Depth; Current.AppendChar(Char); continue; }
			if (!bInString && Depth == 0 && Char == ',') { Args.Add(Current.TrimStartAndEnd()); Current.Reset(); continue; }
			Current.AppendChar(Char);
		}
		if (!Current.TrimStartAndEnd().IsEmpty()) { Args.Add(Current.TrimStartAndEnd()); }
		return Args;
	}

	static bool ParseSignatureText(const FString& SignatureLine, FCompilerSignature& OutSignature)
	{
		FString Signature = SignatureLine;
		Signature = Signature.Replace(TEXT("{"), TEXT(" ")).TrimStartAndEnd();
		int32 OpenIndex = INDEX_NONE, CloseIndex = INDEX_NONE;
		if (!Signature.FindChar('(', OpenIndex) || !Signature.FindLastChar(')', CloseIndex) || CloseIndex <= OpenIndex) { return false; }
		TArray<FString> LeftTokens;
		Signature.Left(OpenIndex).TrimStartAndEnd().ParseIntoArrayWS(LeftTokens);
		if (LeftTokens.Num() < 2) { return false; }
		OutSignature.ReturnType = LeftTokens[LeftTokens.Num() - 2];
		OutSignature.FunctionName = LeftTokens.Last();
		OutSignature.Params.Reset();
		for (const FString& ParamText : SplitArgs(Signature.Mid(OpenIndex + 1, CloseIndex - OpenIndex - 1)))
		{
			if (ParamText.IsEmpty()) { continue; }
			TArray<FString> ParamTokens;
			ParamText.ParseIntoArrayWS(ParamTokens);
			if (ParamTokens.Num() < 2) { return false; }
			OutSignature.Params.Add({ ParamTokens[ParamTokens.Num() - 1], ParamTokens[ParamTokens.Num() - 2] });
		}
		return !OutSignature.FunctionName.IsEmpty();
	}

	static FString MakeLiteralRef(const FString& Value) { return FString::Printf(TEXT("literal:%s"), *Value); }
	static FString MakePinRef(const FString& NodeId, const FString& Pin) { return FString::Printf(TEXT("pin:%s|%s"), *NodeId, *Pin); }
	static bool DecodeRef(const FString& Ref, bool& bOutLiteral, FString& OutA, FString& OutB)
	{
		bOutLiteral = Ref.StartsWith(TEXT("literal:"));
		if (bOutLiteral) { OutA = Ref.Mid(8); OutB.Reset(); return true; }
		if (!Ref.StartsWith(TEXT("pin:"))) { return false; }
		FString Payload = Ref.Mid(4);
		return Payload.Split(TEXT("|"), &OutA, &OutB);
	}

	static bool IsIdentifier(const FString& Text)
	{
		if (Text.IsEmpty() || !(FChar::IsAlpha(Text[0]) || Text[0] == '_')) { return false; }
		for (TCHAR Char : Text) { if (!(FChar::IsAlnum(Char) || Char == '_')) { return false; } }
		return true;
	}

	static UEdGraphNode* FindGraphNode(UEdGraph* Graph, const FString& NodeId) { return FindNodeByGuid(Graph, NodeId); }

	static FString FindFirstPinByDirection(UEdGraphNode* Node, EEdGraphPinDirection Direction, bool bExecPin, const FString& PreferredName = FString())
	{
		if (!Node) { return FString(); }
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != Direction || (Pin->PinType.PinCategory == FName(TEXT("exec"))) != bExecPin) { continue; }
			if (!PreferredName.IsEmpty() && Pin->PinName.ToString().Equals(PreferredName, ESearchCase::IgnoreCase)) { return Pin->PinName.ToString(); }
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Direction && (Pin->PinType.PinCategory == FName(TEXT("exec"))) == bExecPin && Pin->PinName != TEXT("self")) { return Pin->PinName.ToString(); }
		}
		return FString();
	}

	static TArray<FString> FindOrderedDataInputPins(UEdGraphNode* Node)
	{
		TArray<FString> Pins;
		if (!Node) { return Pins; }
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input && !(Pin->PinType.PinCategory == FName(TEXT("exec")))
				&& !Pin->bHidden
				&& Pin->PinName != TEXT("self")
				&& Pin->PinName != TEXT("__WorldContext")
				&& Pin->PinName != TEXT("WorldContextObject"))
			{
				Pins.Add(Pin->PinName.ToString());
			}
		}
		return Pins;
	}
}

FSmithUEBpCompiler::FSmithUEBpCompiler(UBlueprint* InBlueprint, UEdGraph* InGraph)
	: Blueprint(InBlueprint), Graph(InGraph), NextNodeX(300), NextNodeY(0)
{
}

FSmithUECompileResult FSmithUEBpCompiler::CompileFunction(UBlueprint* Blueprint, const FString& Code)
{
	FSmithUECompileResult Result;
	if (!Blueprint) { Result.Errors.Add(TEXT("Blueprint is null")); return Result; }
	FString SyntaxError;
	if (!ValidateSyntax(Code, SyntaxError)) { Result.Errors.Add(SyntaxError); return Result; }
	const TArray<FString> Lines = SplitLines(Code);
	const int32 SignatureLine = FindFirstNonEmptyLine(Lines);
	FCompilerSignature Signature;
	if (SignatureLine == INDEX_NONE || !ParseSignatureText(Lines[SignatureLine], Signature)) { Result.Errors.Add(TEXT("Failed to parse function signature")); return Result; }
	UEdGraph* Graph = FSmithUEBpAtomicAPI::FindGraph(Blueprint, Signature.FunctionName);
	{
		const FScopedTransaction Transaction(NSLOCTEXT("SmithUE", "CompileDslFunction", "SmithUE: Compile Blueprint DSL Function"));
		if (Graph)
		{
			// Recompile: keep existing graph, Entry and Result nodes intact.
			// Only remove intermediate body nodes via the supported Graph->RemoveNode API.
			// This preserves correct Blueprint graph ownership/bookkeeping and avoids
			// stale tick function references that cause FTickFunction::ExecuteTick pure-virtual.
			Graph->Modify();
			UK2Node_FunctionEntry* EntryNode = nullptr;
			UK2Node_FunctionResult* ResultNode = nullptr;
			TArray<UEdGraphNode*> BodiesToRemove;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!EntryNode) { EntryNode = Cast<UK2Node_FunctionEntry>(Node); if (EntryNode) continue; }
				if (!ResultNode) { ResultNode = Cast<UK2Node_FunctionResult>(Node); if (ResultNode) continue; }
				BodiesToRemove.Add(Node);
			}
			// Remove intermediate body nodes through supported destruction
			for (UEdGraphNode* Node : BodiesToRemove)
			{
				Node->BreakAllNodeLinks();
				Graph->RemoveNode(Node);
			}
			// Disconnect Entry/Result from removed intermediates
			if (EntryNode) { EntryNode->BreakAllNodeLinks(); }
			if (ResultNode) { ResultNode->BreakAllNodeLinks(); }
			// Sync Entry user-defined pins with new signature (handles signature changes)
			if (EntryNode)
			{
				TArray<TSharedPtr<FUserPinInfo>> OldPins = EntryNode->UserDefinedPins;
				for (const TSharedPtr<FUserPinInfo>& PinInfo : OldPins) { EntryNode->RemoveUserDefinedPin(PinInfo); }
				for (const FCompilerParam& Param : Signature.Params) { FEdGraphPinType PinType; if (ResolvePinType(Param.Type, PinType)) { EntryNode->CreateUserDefinedPin(FName(*Param.Name), PinType, EGPD_Output); } }
			}
			// Sync Result user-defined pins with new return type
			if (ResultNode)
			{
				TArray<TSharedPtr<FUserPinInfo>> OldPins = ResultNode->UserDefinedPins;
				for (const TSharedPtr<FUserPinInfo>& PinInfo : OldPins) { ResultNode->RemoveUserDefinedPin(PinInfo); }
				if (!Signature.ReturnType.Equals(TEXT("void"), ESearchCase::IgnoreCase)) { FEdGraphPinType PinType; if (ResolvePinType(Signature.ReturnType, PinType)) { ResultNode->CreateUserDefinedPin(TEXT("ReturnValue"), PinType, EGPD_Input); } }
			}
		}
		else
		{
			// New function: use AddFunctionGraph for proper engine initialization.
			// This is only called once per function (first creation), so the structural
			// mark it triggers internally is acceptable.
			Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(*Signature.FunctionName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			FBlueprintEditorUtils::AddFunctionGraph<UFunction>(Blueprint, Graph, true, (UFunction*)nullptr);
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
				{
					for (const FCompilerParam& Param : Signature.Params) { FEdGraphPinType PinType; if (ResolvePinType(Param.Type, PinType)) { Entry->CreateUserDefinedPin(FName(*Param.Name), PinType, EGPD_Output); } }
				}
				if (UK2Node_FunctionResult* Return = Cast<UK2Node_FunctionResult>(Node))
				{
					if (!Signature.ReturnType.Equals(TEXT("void"), ESearchCase::IgnoreCase)) { FEdGraphPinType PinType; if (ResolvePinType(Signature.ReturnType, PinType)) { Return->CreateUserDefinedPin(TEXT("ReturnValue"), PinType, EGPD_Input); } }
				}
			}
		}
	}
	FSmithUEBpCompiler Compiler(Blueprint, Graph);
	Compiler.FunctionName = Signature.FunctionName;
	Result.GraphName = Signature.FunctionName;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (const UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node)) { Compiler.EntryNodeId = Entry->NodeGuid.ToString(); }
		if (const UK2Node_FunctionResult* Return = Cast<UK2Node_FunctionResult>(Node)) { Compiler.ReturnNodeId = Return->NodeGuid.ToString(); }
	}
	// UE 5.2: void functions don't auto-create FunctionResult; create one manually
	if (Compiler.ReturnNodeId.IsEmpty())
	{
		FGraphNodeCreator<UK2Node_FunctionResult> ResultCreator(*Graph);
		UK2Node_FunctionResult* NewResult = ResultCreator.CreateNode();
		NewResult->NodePosX = 800;
		NewResult->NodePosY = 0;
		ResultCreator.Finalize();
		NewResult->AllocateDefaultPins();
		Compiler.ReturnNodeId = NewResult->NodeGuid.ToString();
	}
	if (Compiler.EntryNodeId.IsEmpty()) { Result.Errors.Add(TEXT("Function graph is missing entry node")); return Result; }
	Compiler.Parse(Code);
	// Reposition return node to the rightmost end for readability
	if (UEdGraphNode* ReturnNode = FindGraphNode(Graph, Compiler.ReturnNodeId))
	{
		ReturnNode->NodePosX = Compiler.NextNodeX;
		ReturnNode->NodePosY = 0;
	}
	// Clean up extra unconnected exec input pins on FunctionResult (UE auto-creates them for multi-return support)
	if (UEdGraphNode* ReturnNode = FindGraphNode(Graph, Compiler.ReturnNodeId))
	{
		TArray<UEdGraphPin*> PinsToRemove;
		bool bFoundConnected = false;
		for (UEdGraphPin* Pin : ReturnNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				if (Pin->LinkedTo.Num() > 0) { bFoundConnected = true; }
				else if (bFoundConnected) { PinsToRemove.Add(Pin); }
			}
		}
		for (UEdGraphPin* Pin : PinsToRemove) { ReturnNode->RemovePin(Pin); }
	}
	Result.CreatedNodeIds = Compiler.CreatedNodeIds;
	Result.Errors = Compiler.Errors;
	if (Result.Errors.Num() == 0)
	{
		Graph->NotifyGraphChanged();
		// Pre-compile: unregister tick functions from all live instances of this
		// Blueprint class. During CompileBlueprint, reinstancing destroys old actors
		// and creates new ones. If old actors' FActorTickFunction entries remain in
		// the tick manager after destruction (vtable reset to base FTickFunction),
		// the engine hits a pure-virtual ExecuteTick call 1-3 seconds post-compile.
		// By unregistering ticks before compile, we ensure no stale entries survive.
		// New actors created by reinstancing register their own ticks during construction.
		if (Blueprint->GeneratedClass)
		{
			for (TObjectIterator<AActor> It; It; ++It)
			{
				if (It->GetClass()->IsChildOf(Blueprint->GeneratedClass))
				{
					It->PrimaryActorTick.UnRegisterTickFunction();
					for (UActorComponent* Comp : It->GetComponents())
					{
						if (Comp) { Comp->PrimaryComponentTick.UnRegisterTickFunction(); }
					}
				}
			}
		}
		TArray<FString> CompileErrors;
		// SkipGarbageCollection: CompileBlueprint's internal GC destroys REINST_ actors
		// mid-frame, leaving stale FTickFunction entries in the tick manager (pure virtual crash).
		if (!FSmithUEBpAtomicAPI::CompileBlueprint(Blueprint, CompileErrors, /*bSkipGarbageCollection=*/true)) { Result.Errors.Append(CompileErrors); }
	}
	Result.bSuccess = Result.Errors.Num() == 0;
	return Result;
}

bool FSmithUEBpCompiler::ValidateSyntax(const FString& Code, FString& OutError)
{
	OutError.Reset();
	const TArray<FString> Lines = SplitLines(Code);
	const int32 SignatureLine = FindFirstNonEmptyLine(Lines);
	if (SignatureLine == INDEX_NONE) { OutError = TEXT("Code is empty"); return false; }
	FCompilerSignature Signature;
	if (!ParseSignatureText(Lines[SignatureLine], Signature)) { OutError = TEXT("Invalid function signature"); return false; }
	const int32 OpenBraceLine = FindBlockOpenLine(Lines, SignatureLine);
	if (OpenBraceLine == INDEX_NONE) { OutError = TEXT("Missing function body opening brace"); return false; }
	const int32 CloseBraceLine = FindMatchingBrace(Lines, OpenBraceLine);
	if (CloseBraceLine == INDEX_NONE) { OutError = TEXT("Mismatched braces in function body"); return false; }
	if (CloseBraceLine <= OpenBraceLine + 1) { OutError = TEXT("Function body is empty"); return false; }
	return true;
}

bool FSmithUEBpCompiler::Parse(const FString& Code)
{
	const TArray<FString> Lines = SplitLines(Code);
	const int32 SignatureLine = FindFirstNonEmptyLine(Lines);
	if (SignatureLine == INDEX_NONE || !ParseSignature(Lines[SignatureLine])) { AddError(TEXT("Unable to parse signature line")); return false; }
	const int32 OpenBraceLine = FindBlockOpenLine(Lines, SignatureLine);
	const int32 CloseBraceLine = FindMatchingBrace(Lines, OpenBraceLine);
	FString CurrentNodeId = EntryNodeId;
	FString CurrentPinName = TEXT("then");
	for (int32 LineIndex = OpenBraceLine + 1; LineIndex < CloseBraceLine; ++LineIndex)
	{
		const FString Trimmed = TrimSemicolon(Lines[LineIndex]);
		if (Trimmed.IsEmpty() || Trimmed == TEXT("{") || Trimmed == TEXT("}")) { continue; }
		if (Trimmed.StartsWith(TEXT("if"))) { ProcessIfStatement(Lines, LineIndex, CurrentNodeId, CurrentPinName); continue; }
		if (Trimmed.StartsWith(TEXT("for"))) { ProcessForLoop(Lines, LineIndex, CurrentNodeId, CurrentPinName); continue; }
		if (Trimmed.StartsWith(TEXT("local "))) { ProcessLocalVariable(Trimmed, CurrentNodeId, CurrentPinName); continue; }
		if (Trimmed.StartsWith(TEXT("return"))) { ProcessReturn(Trimmed, CurrentNodeId, CurrentPinName); continue; }
		if (Trimmed.Contains(TEXT(" = ")) && !Trimmed.Contains(TEXT("("))) { ProcessAssignment(Trimmed, CurrentNodeId, CurrentPinName); continue; }
		ProcessFunctionCall(Trimmed, CurrentNodeId, CurrentPinName);
	}
	if (!CurrentNodeId.IsEmpty() && !CurrentPinName.IsEmpty()) { ConnectExec(CurrentNodeId, CurrentPinName, ReturnNodeId, TEXT("execute")); }
	return Errors.Num() == 0;
}

bool FSmithUEBpCompiler::ParseSignature(const FString& SignatureLine)
{
	FCompilerSignature Signature;
	if (!ParseSignatureText(SignatureLine, Signature)) { return false; }
	FunctionName = Signature.FunctionName;
	return true;
}

void FSmithUEBpCompiler::ProcessLines(const TArray<FString>& Lines, int32& LineIndex, const FString& LastExecNodeId, const FString& LastExecPinName)
{
	FString NodeId = LastExecNodeId;
	FString PinName = LastExecPinName;
	for (; LineIndex < Lines.Num(); ++LineIndex)
	{
		const FString Trimmed = TrimSemicolon(Lines[LineIndex]);
		if (Trimmed.IsEmpty() || Trimmed == TEXT("{")) { continue; }
		if (Trimmed == TEXT("}")) { break; }
		if (Trimmed.StartsWith(TEXT("if"))) { ProcessIfStatement(Lines, LineIndex, NodeId, PinName); continue; }
		if (Trimmed.StartsWith(TEXT("for"))) { ProcessForLoop(Lines, LineIndex, NodeId, PinName); continue; }
		if (Trimmed.StartsWith(TEXT("local "))) { ProcessLocalVariable(Trimmed, NodeId, PinName); continue; }
		if (Trimmed.StartsWith(TEXT("return"))) { ProcessReturn(Trimmed, NodeId, PinName); continue; }
		if (Trimmed.Contains(TEXT(" = ")) && !Trimmed.Contains(TEXT("("))) { ProcessAssignment(Trimmed, NodeId, PinName); continue; }
		ProcessFunctionCall(Trimmed, NodeId, PinName);
	}
}

void FSmithUEBpCompiler::ProcessFunctionCall(const FString& Line, FString& InOutNodeId, FString& InOutPinName)
{
	int32 OpenIndex = INDEX_NONE, CloseIndex = INDEX_NONE;
	if (!Line.FindChar('(', OpenIndex) || !Line.FindLastChar(')', CloseIndex) || CloseIndex <= OpenIndex) { AddError(FString::Printf(TEXT("Invalid function call: %s"), *Line)); return; }
	const FString FunctionNameText = Line.Left(OpenIndex).TrimStartAndEnd();
	TArray<TPair<FString, FString>> Args;
	for (const FString& Arg : SplitArgs(Line.Mid(OpenIndex + 1, CloseIndex - OpenIndex - 1))) { if (!Arg.IsEmpty()) { Args.Add({ FString(), Arg }); } }
	const FString NodeId = CreateCallFunctionNode(FunctionNameText, Args);
	if (NodeId.IsEmpty()) { return; }
	const FString ExecInputPin = FindFirstPinByDirection(FindGraphNode(Graph, NodeId), EGPD_Input, true);
	const FString ExecOutputPin = FindFirstPinByDirection(FindGraphNode(Graph, NodeId), EGPD_Output, true, TEXT("then"));
	if (!ExecInputPin.IsEmpty())
	{
		if (!InOutNodeId.IsEmpty() && !InOutPinName.IsEmpty()) { ConnectExec(InOutNodeId, InOutPinName, NodeId, ExecInputPin); }
		InOutNodeId = NodeId;
		InOutPinName = ExecOutputPin;
	}
}

void FSmithUEBpCompiler::ProcessIfStatement(const TArray<FString>& Lines, int32& LineIndex, FString& InOutNodeId, FString& InOutPinName)
{
	if (Lines[LineIndex].TrimStartAndEnd().Contains(TEXT("else"))) { AddError(TEXT("Standalone else is not supported")); return; }
	const FString Header = Lines[LineIndex].TrimStartAndEnd();
	int32 OpenIndex = INDEX_NONE, CloseIndex = INDEX_NONE;
	if (!Header.FindChar('(', OpenIndex) || !Header.FindLastChar(')', CloseIndex)) { AddError(TEXT("Invalid if statement syntax")); return; }
	const FString BranchNodeId = CreateBranchNode(Header.Mid(OpenIndex + 1, CloseIndex - OpenIndex - 1));
	if (BranchNodeId.IsEmpty()) { return; }
	const int32 ThenOpenLine = FindBlockOpenLine(Lines, LineIndex);
	const int32 ThenCloseLine = FindMatchingBrace(Lines, ThenOpenLine);
	if (ThenOpenLine == INDEX_NONE || ThenCloseLine == INDEX_NONE) { AddError(TEXT("If statement block braces are invalid")); return; }
	ConnectExec(InOutNodeId, InOutPinName, BranchNodeId, TEXT("Execute"));
	const int32 ElseLine = FindFirstNonEmptyLine(Lines, ThenCloseLine + 1);
	const bool bHasElse = ElseLine != INDEX_NONE && Lines[ElseLine].TrimStartAndEnd().StartsWith(TEXT("else"));
	FString ThenEndNode = BranchNodeId, ThenEndPin = TEXT("Then");
	for (int32 Index = ThenOpenLine + 1; Index < ThenCloseLine; ++Index)
		{ const FString T = TrimSemicolon(Lines[Index]); if (T.StartsWith(TEXT("if"))) { AddError(TEXT("Nested if statements are not supported in v1.0")); break; } if (T.IsEmpty() || T == TEXT("{") || T == TEXT("}")) { continue; } if (T.StartsWith(TEXT("for"))) { ProcessForLoop(Lines, Index, ThenEndNode, ThenEndPin); continue; } if (T.StartsWith(TEXT("local "))) { ProcessLocalVariable(T, ThenEndNode, ThenEndPin); continue; } if (T.StartsWith(TEXT("return"))) { ProcessReturn(T, ThenEndNode, ThenEndPin); continue; } if (T.Contains(TEXT(" = ")) && !T.Contains(TEXT("("))) { ProcessAssignment(T, ThenEndNode, ThenEndPin); continue; } ProcessFunctionCall(T, ThenEndNode, ThenEndPin); }
	FString ElseEndNode = BranchNodeId, ElseEndPin = TEXT("Else");
	if (bHasElse)
	{
		const int32 ElseOpenLine = FindBlockOpenLine(Lines, ElseLine);
		const int32 ElseCloseLine = FindMatchingBrace(Lines, ElseOpenLine);
		if (ElseOpenLine == INDEX_NONE || ElseCloseLine == INDEX_NONE) { AddError(TEXT("Else block braces are invalid")); return; }
		for (int32 Index = ElseOpenLine + 1; Index < ElseCloseLine; ++Index)
			{ const FString T = TrimSemicolon(Lines[Index]); if (T.StartsWith(TEXT("if"))) { AddError(TEXT("Nested if statements are not supported in v1.0")); break; } if (T.IsEmpty() || T == TEXT("{") || T == TEXT("}")) { continue; } if (T.StartsWith(TEXT("for"))) { ProcessForLoop(Lines, Index, ElseEndNode, ElseEndPin); continue; } if (T.StartsWith(TEXT("local "))) { ProcessLocalVariable(T, ElseEndNode, ElseEndPin); continue; } if (T.StartsWith(TEXT("return"))) { ProcessReturn(T, ElseEndNode, ElseEndPin); continue; } if (T.Contains(TEXT(" = ")) && !T.Contains(TEXT("("))) { ProcessAssignment(T, ElseEndNode, ElseEndPin); continue; } ProcessFunctionCall(T, ElseEndNode, ElseEndPin); }
		LineIndex = ElseCloseLine;
	}
	else { LineIndex = ThenCloseLine; }
	InOutNodeId.Reset();
	InOutPinName.Reset();
}

void FSmithUEBpCompiler::ProcessForLoop(const TArray<FString>& Lines, int32& LineIndex, FString& InOutNodeId, FString& InOutPinName)
{
	int32 OpenIndex = INDEX_NONE, CloseIndex = INDEX_NONE;
	const FString Header = Lines[LineIndex].TrimStartAndEnd();
	if (!Header.FindChar('(', OpenIndex) || !Header.FindLastChar(')', CloseIndex)) { AddError(TEXT("Invalid for loop syntax")); return; }
	const TArray<FString> Parts = SplitArgs(Header.Mid(OpenIndex + 1, CloseIndex - OpenIndex - 1));
	if (Parts.Num() != 2) { AddError(TEXT("For loop expects (start, end)")); return; }
	const FString LoopNodeId = CreateForLoopNode(Parts[0], Parts[1]);
	if (LoopNodeId.IsEmpty()) { return; }
	const int32 BodyOpenLine = FindBlockOpenLine(Lines, LineIndex);
	const int32 BodyCloseLine = FindMatchingBrace(Lines, BodyOpenLine);
	if (BodyOpenLine == INDEX_NONE || BodyCloseLine == INDEX_NONE) { AddError(TEXT("For loop block braces are invalid")); return; }
	ConnectExec(InOutNodeId, InOutPinName, LoopNodeId, TEXT("execute"));
	FString BodyNode = LoopNodeId, BodyPin = TEXT("LoopBody");
	for (int32 Index = BodyOpenLine + 1; Index < BodyCloseLine; ++Index)
		{ const FString T = TrimSemicolon(Lines[Index]); if (T.IsEmpty() || T == TEXT("{") || T == TEXT("}")) { continue; } if (T.StartsWith(TEXT("if"))) { AddError(TEXT("Nested if statements inside loops are not supported in v1.0")); break; } if (T.StartsWith(TEXT("local "))) { ProcessLocalVariable(T, BodyNode, BodyPin); continue; } if (T.StartsWith(TEXT("return"))) { ProcessReturn(T, BodyNode, BodyPin); continue; } if (T.Contains(TEXT(" = ")) && !T.Contains(TEXT("("))) { ProcessAssignment(T, BodyNode, BodyPin); continue; } ProcessFunctionCall(T, BodyNode, BodyPin); }
	InOutNodeId = LoopNodeId;
	InOutPinName = TEXT("Completed");
	LineIndex = BodyCloseLine;
}

void FSmithUEBpCompiler::ProcessAssignment(const FString& Line, FString& InOutNodeId, FString& InOutPinName)
{
	FString VarName, ValueExpr;
	if (!Line.Split(TEXT("="), &VarName, &ValueExpr)) { AddError(FString::Printf(TEXT("Invalid assignment: %s"), *Line)); return; }
	VarName = VarName.TrimStartAndEnd();
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("variable_name"), VarName);
	const FString SetNodeId = FSmithUEBpAtomicAPI::CreateNode(Blueprint, Graph, TEXT("K2Node_VariableSet"), FVector2D(NextNodeX, NextNodeY), Params);
	if (SetNodeId.IsEmpty()) { AddError(FString::Printf(TEXT("Failed to create variable set node for %s"), *VarName)); return; }
	CreatedNodeIds.Add(SetNodeId);
	ConnectExec(InOutNodeId, InOutPinName, SetNodeId, TEXT("execute"));
	const FString ValueRef = ParseExpression(TrimSemicolon(ValueExpr));
	bool bLiteral = false; FString A, B; DecodeRef(ValueRef, bLiteral, A, B);
	const FString ValuePin = FindFirstPinByDirection(FindGraphNode(Graph, SetNodeId), EGPD_Input, false, VarName);
	if (ValuePin.IsEmpty()) { AddError(FString::Printf(TEXT("Variable set pin not found for %s"), *VarName)); return; }
	if (bLiteral) { if (!FSmithUEBpAtomicAPI::SetPinDefault(Blueprint, Graph, SetNodeId, ValuePin, A)) { AddError(FString::Printf(TEXT("Failed to set variable value for %s"), *VarName)); } }
	else if (!A.IsEmpty() && !B.IsEmpty()) { ConnectData(A, B, SetNodeId, ValuePin); }
	InOutNodeId = SetNodeId; InOutPinName = TEXT("then"); NextNodeX += 300;
}

void FSmithUEBpCompiler::ProcessReturn(const FString& Line, FString& InOutNodeId, FString& InOutPinName)
{
	const FString Expr = TrimSemicolon(Line.Mid(6));
	if (!InOutNodeId.IsEmpty() && !InOutPinName.IsEmpty()) { ConnectExec(InOutNodeId, InOutPinName, ReturnNodeId, TEXT("execute")); }
	if (!Expr.IsEmpty())
	{
		const FString ValueRef = ParseExpression(Expr);
		bool bLiteral = false; FString A, B; if (!DecodeRef(ValueRef, bLiteral, A, B)) { AddError(TEXT("Invalid return expression")); }
		else if (bLiteral) { if (!FSmithUEBpAtomicAPI::SetPinDefault(Blueprint, Graph, ReturnNodeId, TEXT("ReturnValue"), A)) { AddError(TEXT("Failed to set return value")); } }
		else { ConnectData(A, B, ReturnNodeId, TEXT("ReturnValue")); }
	}
	InOutNodeId.Reset();
	InOutPinName.Reset();
}

void FSmithUEBpCompiler::ProcessLocalVariable(const FString& Line, FString& InOutNodeId, FString& InOutPinName)
{
	FString Content = TrimSemicolon(Line.Mid(5)).TrimStartAndEnd();
	FString VarName, Expr;
	if (!Content.Split(TEXT("="), &VarName, &Expr)) { AddError(FString::Printf(TEXT("Invalid local declaration: %s"), *Line)); return; }
	LocalVariableNodes.Add(VarName.TrimStartAndEnd(), ParseExpression(TrimSemicolon(Expr)));
}

FString FSmithUEBpCompiler::CreateCallFunctionNode(const FString& FunctionNameText, const TArray<TPair<FString, FString>>& Args)
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("function_name"), FunctionNameText);
	const FString NodeId = FSmithUEBpAtomicAPI::CreateNode(Blueprint, Graph, TEXT("K2Node_CallFunction"), FVector2D(NextNodeX, NextNodeY), Params);
	if (NodeId.IsEmpty()) { AddError(FString::Printf(TEXT("Failed to create function node: %s"), *FunctionNameText)); return FString(); }
	CreatedNodeIds.Add(NodeId);
	UEdGraphNode* Node = FindGraphNode(Graph, NodeId);
	const TArray<FString> InputPins = FindOrderedDataInputPins(Node);
	for (int32 Index = 0; Index < Args.Num() && Index < InputPins.Num(); ++Index)
	{
		const FString ValueRef = ParseExpression(Args[Index].Value);
		bool bLiteral = false; FString A, B; if (!DecodeRef(ValueRef, bLiteral, A, B)) { AddError(TEXT("Failed to parse function argument")); continue; }
		if (bLiteral) { if (!FSmithUEBpAtomicAPI::SetPinDefault(Blueprint, Graph, NodeId, InputPins[Index], A)) { AddError(FString::Printf(TEXT("Failed to set argument pin %s"), *InputPins[Index])); } }
		else { ConnectData(A, B, NodeId, InputPins[Index]); }
	}
	NextNodeX += 300;
	return NodeId;
}

FString FSmithUEBpCompiler::CreateBranchNode(const FString& ConditionExpression)
{
	const FString NodeId = FSmithUEBpAtomicAPI::CreateNode(Blueprint, Graph, TEXT("K2Node_IfThenElse"), FVector2D(NextNodeX, NextNodeY), MakeShared<FJsonObject>());
	if (NodeId.IsEmpty()) { AddError(TEXT("Failed to create branch node")); return FString(); }
	CreatedNodeIds.Add(NodeId);
	const TCHAR* Ops[] = { TEXT(">="), TEXT("<="), TEXT("=="), TEXT("!="), TEXT(">"), TEXT("<") };
	for (const TCHAR* Op : Ops)
		{ FString Left, Right; if (!ConditionExpression.Split(Op, &Left, &Right)) { continue; } const FString FunctionPath = FCString::Strcmp(Op, TEXT(">")) == 0 ? TEXT("KismetMathLibrary::Greater_IntInt") : FCString::Strcmp(Op, TEXT("<")) == 0 ? TEXT("KismetMathLibrary::Less_IntInt") : FCString::Strcmp(Op, TEXT("==")) == 0 ? TEXT("KismetMathLibrary::EqualEqual_IntInt") : FCString::Strcmp(Op, TEXT(">=")) == 0 ? TEXT("KismetMathLibrary::GreaterEqual_IntInt") : FCString::Strcmp(Op, TEXT("<=")) == 0 ? TEXT("KismetMathLibrary::LessEqual_IntInt") : TEXT("KismetMathLibrary::NotEqual_IntInt"); const FString CompareNodeId = CreateCallFunctionNode(FunctionPath, { { FString(), Left.TrimStartAndEnd() }, { FString(), Right.TrimStartAndEnd() } }); const FString OutputPin = FindFirstPinByDirection(FindGraphNode(Graph, CompareNodeId), EGPD_Output, false, TEXT("ReturnValue")); if (!CompareNodeId.IsEmpty() && !OutputPin.IsEmpty()) { ConnectData(CompareNodeId, OutputPin, NodeId, TEXT("Condition")); } NextNodeY += 200; return NodeId; }
	const FString ValueRef = ParseExpression(ConditionExpression.TrimStartAndEnd());
	bool bLiteral = false; FString A, B; if (!DecodeRef(ValueRef, bLiteral, A, B)) { AddError(TEXT("Invalid branch condition")); return NodeId; }
	if (bLiteral) { if (!FSmithUEBpAtomicAPI::SetPinDefault(Blueprint, Graph, NodeId, TEXT("Condition"), A)) { AddError(TEXT("Failed to set branch condition")); } }
	else { ConnectData(A, B, NodeId, TEXT("Condition")); }
	NextNodeX += 300;
	return NodeId;
}

FString FSmithUEBpCompiler::CreateForLoopNode(const FString& StartExpr, const FString& EndExpr)
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("macro_path"), TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:ForLoop"));
	const FString NodeId = FSmithUEBpAtomicAPI::CreateNode(Blueprint, Graph, TEXT("K2Node_MacroInstance"), FVector2D(NextNodeX, NextNodeY), Params);
	if (NodeId.IsEmpty()) { AddError(TEXT("Failed to create for loop node")); return FString(); }
	CreatedNodeIds.Add(NodeId);
	const TPair<FString, FString> LoopPins[] = { TPair<FString, FString>(TEXT("FirstIndex"), StartExpr), TPair<FString, FString>(TEXT("LastIndex"), EndExpr) };
	for (const TPair<FString, FString>& Pair : LoopPins)
	{
		const FString Ref = ParseExpression(Pair.Value);
		bool bLiteral = false; FString A, B; if (!DecodeRef(Ref, bLiteral, A, B)) { AddError(TEXT("Invalid for loop expression")); continue; }
		if (bLiteral) { if (!FSmithUEBpAtomicAPI::SetPinDefault(Blueprint, Graph, NodeId, Pair.Key, A)) { AddError(FString::Printf(TEXT("Failed to set %s"), *Pair.Key)); } }
		else { ConnectData(A, B, NodeId, Pair.Key); }
	}
	NextNodeX += 300;
	return NodeId;
}

FString FSmithUEBpCompiler::ParseExpression(const FString& Expression)
{
	const FString Trimmed = TrimSemicolon(Expression);
	if (Trimmed.StartsWith(TEXT("\"")) && Trimmed.EndsWith(TEXT("\""))) { return MakeLiteralRef(Trimmed.Mid(1, Trimmed.Len() - 2)); }
	if (Trimmed.IsNumeric() || Trimmed.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Trimmed.Equals(TEXT("false"), ESearchCase::IgnoreCase)) { return MakeLiteralRef(Trimmed); }
	if (LocalVariableNodes.Contains(Trimmed)) { return LocalVariableNodes[Trimmed]; }
	if (IsIdentifier(Trimmed))
	{
		if (FindPin(FindGraphNode(Graph, EntryNodeId), Trimmed)) { return MakePinRef(EntryNodeId, Trimmed); }
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("variable_name"), Trimmed);
		const FString GetterNodeId = FSmithUEBpAtomicAPI::CreateNode(Blueprint, Graph, TEXT("K2Node_VariableGet"), FVector2D(NextNodeX, NextNodeY), Params);
		if (!GetterNodeId.IsEmpty()) { CreatedNodeIds.Add(GetterNodeId); NextNodeX += 300; return MakePinRef(GetterNodeId, Trimmed); }
	}
	if (Trimmed.Contains(TEXT("("))) { const TPair<FString, FString> Ref = ParseFunctionCallExpression(Trimmed); if (!Ref.Key.IsEmpty() && !Ref.Value.IsEmpty()) { return MakePinRef(Ref.Key, Ref.Value); } }
	AddError(FString::Printf(TEXT("Unsupported expression: %s"), *Trimmed));
	return FString();
}

TPair<FString, FString> FSmithUEBpCompiler::ParseFunctionCallExpression(const FString& Expr)
{
	int32 OpenIndex = INDEX_NONE, CloseIndex = INDEX_NONE;
	if (!Expr.FindChar('(', OpenIndex) || !Expr.FindLastChar(')', CloseIndex)) { return {}; }
	TArray<TPair<FString, FString>> Args;
	for (const FString& Arg : SplitArgs(Expr.Mid(OpenIndex + 1, CloseIndex - OpenIndex - 1))) { Args.Add({ FString(), Arg }); }
	const FString NodeId = CreateCallFunctionNode(Expr.Left(OpenIndex).TrimStartAndEnd(), Args);
	return { NodeId, FindFirstPinByDirection(FindGraphNode(Graph, NodeId), EGPD_Output, false, TEXT("ReturnValue")) };
}

void FSmithUEBpCompiler::AddError(const FString& Error)
{
	if (!Error.IsEmpty()) { Errors.Add(Error); UE_LOG(LogSmithUE, Warning, TEXT("SmithUEBpCompiler: %s"), *Error); }
}

void FSmithUEBpCompiler::ConnectExec(const FString& SourceNodeId, const FString& SourcePin, const FString& TargetNodeId, const FString& TargetPin)
{
	if (SourceNodeId.IsEmpty() || TargetNodeId.IsEmpty()) { return; }
	if (!FSmithUEBpAtomicAPI::ConnectPins(Blueprint, Graph, SourceNodeId, SourcePin, TargetNodeId, TargetPin)) { AddError(FString::Printf(TEXT("Failed exec connection %s.%s -> %s.%s"), *SourceNodeId, *SourcePin, *TargetNodeId, *TargetPin)); }
}

void FSmithUEBpCompiler::ConnectData(const FString& SourceNodeId, const FString& SourcePin, const FString& TargetNodeId, const FString& TargetPin)
{
	if (SourceNodeId.IsEmpty() || TargetNodeId.IsEmpty() || SourcePin.IsEmpty() || TargetPin.IsEmpty()) { return; }
	if (!FSmithUEBpAtomicAPI::ConnectPins(Blueprint, Graph, SourceNodeId, SourcePin, TargetNodeId, TargetPin)) { AddError(FString::Printf(TEXT("Failed data connection %s.%s -> %s.%s"), *SourceNodeId, *SourcePin, *TargetNodeId, *TargetPin)); }
}
