#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraph;
class FJsonObject;

struct FUEAgentCompileResult
{
	bool bSuccess = false;
	TArray<FString> Errors;
	TArray<FString> CreatedNodeIds;
	FString GraphName;
};

class FUEAgentBpCompiler
{
public:
	static FUEAgentCompileResult CompileFunction(UBlueprint* Blueprint, const FString& Code);
	static bool ValidateSyntax(const FString& Code, FString& OutError);

private:
	FUEAgentBpCompiler(UBlueprint* InBlueprint, UEdGraph* InGraph);

	bool Parse(const FString& Code);
	bool ParseSignature(const FString& SignatureLine);
	void ProcessLines(const TArray<FString>& Lines, int32& LineIndex, const FString& LastExecNodeId, const FString& LastExecPinName);
	
	void ProcessFunctionCall(const FString& Line, FString& InOutNodeId, FString& InOutPinName);
	void ProcessIfStatement(const TArray<FString>& Lines, int32& LineIndex, FString& InOutNodeId, FString& InOutPinName);
	void ProcessForLoop(const TArray<FString>& Lines, int32& LineIndex, FString& InOutNodeId, FString& InOutPinName);
	void ProcessAssignment(const FString& Line, FString& InOutNodeId, FString& InOutPinName);
	void ProcessReturn(const FString& Line, FString& InOutNodeId, FString& InOutPinName);
	void ProcessLocalVariable(const FString& Line, FString& InOutNodeId, FString& InOutPinName);

	FString CreateCallFunctionNode(const FString& FunctionName, const TArray<TPair<FString,FString>>& Args);
	FString CreateBranchNode(const FString& ConditionExpression);
	FString CreateForLoopNode(const FString& StartExpr, const FString& EndExpr);
	
	FString ParseExpression(const FString& Expression);
	TPair<FString, FString> ParseFunctionCallExpression(const FString& Expr);
	
	void AddError(const FString& Error);
	void ConnectExec(const FString& SourceNodeId, const FString& SourcePin, const FString& TargetNodeId, const FString& TargetPin);
	void ConnectData(const FString& SourceNodeId, const FString& SourcePin, const FString& TargetNodeId, const FString& TargetPin);

	UBlueprint* Blueprint;
	UEdGraph* Graph;
	FString FunctionName;
	TArray<FString> CreatedNodeIds;
	TArray<FString> Errors;
	int32 NextNodeX;
	int32 NextNodeY;
	FString EntryNodeId;
	FString ReturnNodeId;
	TMap<FString, FString> LocalVariableNodes;
};
