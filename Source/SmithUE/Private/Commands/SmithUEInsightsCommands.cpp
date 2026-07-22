// Copyright 2026, 123dx-svg. MIT License.
#include "Commands/SmithUEInsightsCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "ToolRegistry/SmithUEToolSchema.h"
#include "Utils/SmithUECommonUtils.h"
#include "SmithUEModule.h"

#include "ProfilingDebugging/TraceAuxiliary.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Frames.h"
#include "TraceServices/Model/Counters.h"

namespace
{
	// Remembers the destination of the trace this plugin last started, so stop/status can report it.
	static FString GLastTracePath;

	FString ResolveTracePath(const FString& InPath)
	{
		FString Path = InPath;
		if (Path.IsEmpty())
		{
			const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
			Path = FPaths::ProfilingDir() / FString::Printf(TEXT("SmithUE_%s.utrace"), *Stamp);
		}
		if (!Path.EndsWith(TEXT(".utrace"), ESearchCase::IgnoreCase))
		{
			Path += TEXT(".utrace");
		}
		return FPaths::ConvertRelativePathToFull(Path);
	}

	// Percentile from an unsorted array (0..1). Returns 0 if empty. Sorts in place.
	double Percentile(TArray<double>& Values, double P)
	{
		if (Values.Num() == 0) { return 0.0; }
		Values.Sort();
		const int32 Idx = FMath::Clamp(static_cast<int32>(FMath::RoundToInt(P * (Values.Num() - 1))), 0, Values.Num() - 1);
		return Values[Idx];
	}

	// Build a frame-time summary object for one frame type. DurationsMs is consumed (sorted).
	TSharedPtr<FJsonObject> BuildFrameSummary(TArray<double>& DurationsMs, double HitchMs)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		const int32 N = DurationsMs.Num();
		Obj->SetNumberField(TEXT("frame_count"), N);
		if (N == 0) { return Obj; }

		double Sum = 0.0, MinV = DurationsMs[0], MaxV = DurationsMs[0];
		int32 Hitches = 0;
		for (double D : DurationsMs)
		{
			Sum += D;
			MinV = FMath::Min(MinV, D);
			MaxV = FMath::Max(MaxV, D);
			if (D > HitchMs) { ++Hitches; }
		}
		const double Avg = Sum / N;
		Obj->SetNumberField(TEXT("avg_ms"), Avg);
		Obj->SetNumberField(TEXT("min_ms"), MinV);
		Obj->SetNumberField(TEXT("max_ms"), MaxV);
		Obj->SetNumberField(TEXT("median_ms"), Percentile(DurationsMs, 0.50));
		Obj->SetNumberField(TEXT("p95_ms"), Percentile(DurationsMs, 0.95));
		Obj->SetNumberField(TEXT("p99_ms"), Percentile(DurationsMs, 0.99));
		Obj->SetNumberField(TEXT("avg_fps"), Avg > 0.0 ? 1000.0 / Avg : 0.0);
		Obj->SetNumberField(TEXT("hitch_count"), Hitches);
		return Obj;
	}

	void CollectFrameDurations(const TraceServices::IFrameProvider& Frames, ETraceFrameType Type, TArray<double>& OutMs)
	{
		const uint64 Count = Frames.GetFrameCount(Type);
		if (Count == 0) { return; }
		OutMs.Reserve(static_cast<int32>(FMath::Min<uint64>(Count, 2000000)));
		Frames.EnumerateFrames(Type, 0, Count, [&OutMs](const TraceServices::FFrame& Frame)
		{
			const double Dur = Frame.EndTime - Frame.StartTime;
			// Guard against the still-open last frame (EndTime is a huge sentinel -> inf ms,
			// which is also invalid JSON). No real frame lasts >60s.
			if (Dur > 0.0 && FMath::IsFinite(Dur) && Dur < 60.0) { OutMs.Add(Dur * 1000.0); }
		});
	}
}

void FSmithUEInsightsCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
	Registry.Register(FSmithUEToolSchema(TEXT("insights_start_trace"), TEXT("Insights"),
		TEXT("Start capturing an Unreal Insights trace to a .utrace file (FTraceAuxiliary). Enable channels to control what is recorded; default 'default,stat,counter' covers frames + CPU + stats + counters (good for perf-triage). If path is omitted the file goes to the project's Saved/Profiling with a timestamped name. Only one trace can be active — call insights_stop_trace first. Analyze the result with analyze_trace."),
		{ FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Output .utrace path (default: Saved/Profiling/SmithUE_<timestamp>.utrace)"), false),
		  FSmithUEToolParam(TEXT("channels"), TEXT("string"), TEXT("Comma-separated trace channels (default 'default,stat,counter'; e.g. add 'gpu','loadtime','memory')"), false, TEXT("default,stat,counter")) }),
		&FSmithUEInsightsCommands::HandleStartTrace);

	Registry.Register(FSmithUEToolSchema(TEXT("insights_stop_trace"), TEXT("Insights"),
		TEXT("Stop the active Unreal Insights trace and flush the .utrace file. Returns the file path + size. Then run analyze_trace on it."),
		{}),
		&FSmithUEInsightsCommands::HandleStopTrace);

	Registry.Register(FSmithUEToolSchema(TEXT("insights_trace_status"), TEXT("Insights"),
		TEXT("Report whether an Insights trace is currently active/paused and the last destination path started by this plugin. Read-only."),
		{}),
		&FSmithUEInsightsCommands::HandleTraceStatus);

	Registry.Register(FSmithUEToolSchema(TEXT("analyze_trace"), TEXT("Insights"),
		TEXT("Analyze a .utrace file offline (TraceServices) into a perf summary: session duration, and for the Game and Rendering threads the frame-time distribution (count, avg/min/max/median/p95/p99 ms, avg FPS, hitch_count over hitch_ms). Optionally summarize stat counters (min/max/avg/last). This is the primary perf-triage readout; top CPU timers are not yet included."),
		{ FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT(".utrace file path to analyze"), true),
		  FSmithUEToolParam(TEXT("hitch_ms"), TEXT("number"), TEXT("Frame time (ms) above which a frame counts as a hitch (default 33.3)"), false, TEXT("33.3")),
		  FSmithUEToolParam(TEXT("counters"), TEXT("bool"), TEXT("Also summarize stat counters (default false)"), false, TEXT("false")),
		  FSmithUEToolParam(TEXT("max_counters"), TEXT("int"), TEXT("Max counters to report when counters=true (default 40)"), false, TEXT("40")) }),
		&FSmithUEInsightsCommands::HandleAnalyzeTrace);
}

TSharedPtr<FJsonObject> FSmithUEInsightsCommands::HandleStartTrace(const TSharedPtr<FJsonObject>& Params)
{
	if (FTraceAuxiliary::IsConnected())
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("A trace is already active — call insights_stop_trace first"));
	}
	FString InPath, Channels = TEXT("default,stat,counter");
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("path"), InPath);
		Params->TryGetStringField(TEXT("channels"), Channels);
	}
	if (Channels.IsEmpty()) { Channels = TEXT("default,stat,counter"); }
	const FString Path = ResolveTracePath(InPath);

	// Ensure the directory exists.
	const FString Dir = FPaths::GetPath(Path);
	if (!Dir.IsEmpty() && !IFileManager::Get().DirectoryExists(*Dir))
	{
		IFileManager::Get().MakeDirectory(*Dir, true);
	}

	const bool bStarted = FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::File, *Path, *Channels);
	if (!bStarted)
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to start trace to '%s' (channels '%s')"), *Path, *Channels));
	}
	GLastTracePath = Path;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path"), Path);
	Data->SetStringField(TEXT("channels"), Channels);
	Data->SetBoolField(TEXT("active"), true);
	Data->SetStringField(TEXT("note"), TEXT("Tracing started. Exercise the editor/PIE, then call insights_stop_trace, then analyze_trace."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEInsightsCommands::HandleStopTrace(const TSharedPtr<FJsonObject>& Params)
{
	if (!FTraceAuxiliary::IsConnected())
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("No active trace to stop"));
	}
	const bool bStopped = FTraceAuxiliary::Stop();
	if (!bStopped)
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("FTraceAuxiliary::Stop() failed"));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path"), GLastTracePath);
	if (!GLastTracePath.IsEmpty())
	{
		const int64 Size = IFileManager::Get().FileSize(*GLastTracePath);
		Data->SetNumberField(TEXT("size_bytes"), Size >= 0 ? static_cast<double>(Size) : 0.0);
	}
	Data->SetBoolField(TEXT("active"), false);
	Data->SetStringField(TEXT("note"), TEXT("Trace stopped + flushed. Run analyze_trace on 'path'."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEInsightsCommands::HandleTraceStatus(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("active"), FTraceAuxiliary::IsConnected());
	Data->SetBoolField(TEXT("paused"), FTraceAuxiliary::IsPaused());
	Data->SetStringField(TEXT("last_path"), GLastTracePath);
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUEInsightsCommands::HandleAnalyzeTrace(const TSharedPtr<FJsonObject>& Params)
{
	FString Error;
	if (!FSmithUECommonUtils::ValidateRequiredParams(Params, { TEXT("path") }, Error))
	{
		return FSmithUECommonUtils::CreateErrorResponse(Error);
	}
	FString Path;
	Params->TryGetStringField(TEXT("path"), Path);
	Path = FPaths::ConvertRelativePathToFull(Path);
	if (!IFileManager::Get().FileExists(*Path))
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT(".utrace not found: %s"), *Path));
	}

	double HitchMs = 33.3;
	bool bCounters = false;
	double MaxCountersD = 40.0;
	Params->TryGetNumberField(TEXT("hitch_ms"), HitchMs);
	Params->TryGetBoolField(TEXT("counters"), bCounters);
	Params->TryGetNumberField(TEXT("max_counters"), MaxCountersD);
	const int32 MaxCounters = FMath::Clamp(static_cast<int32>(MaxCountersD), 1, 500);

	ITraceServicesModule& TSModule = FModuleManager::LoadModuleChecked<ITraceServicesModule>("TraceServices");
	TSharedPtr<TraceServices::IAnalysisService> AnalysisService = TSModule.GetAnalysisService();
	if (!AnalysisService.IsValid())
	{
		return FSmithUECommonUtils::CreateErrorResponse(TEXT("TraceServices analysis service unavailable"));
	}

	// Analyze() blocks until the trace is fully analyzed and returns a completed session.
	TSharedPtr<const TraceServices::IAnalysisSession> Session = AnalysisService->Analyze(*Path);
	if (!Session.IsValid())
	{
		return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to analyze trace: %s"), *Path));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path"), Path);

	{
		TraceServices::FAnalysisSessionReadScope ReadScope(*Session);
		Data->SetNumberField(TEXT("duration_seconds"), Session->GetDurationSeconds());

		const TraceServices::IFrameProvider& Frames = TraceServices::ReadFrameProvider(*Session);

		TArray<double> GameMs, RenderMs;
		CollectFrameDurations(Frames, TraceFrameType_Game, GameMs);
		CollectFrameDurations(Frames, TraceFrameType_Rendering, RenderMs);
		Data->SetObjectField(TEXT("game_thread"), BuildFrameSummary(GameMs, HitchMs));
		Data->SetObjectField(TEXT("rendering_thread"), BuildFrameSummary(RenderMs, HitchMs));
		Data->SetNumberField(TEXT("hitch_ms"), HitchMs);

		if (bCounters)
		{
			const TraceServices::ICounterProvider& CounterProvider = TraceServices::ReadCounterProvider(*Session);
			const double T0 = 0.0;
			const double T1 = Session->GetDurationSeconds();
			TArray<TSharedPtr<FJsonValue>> CounterArr;
			CounterProvider.EnumerateCounters([&](uint32 /*Id*/, const TraceServices::ICounter& Counter)
			{
				if (CounterArr.Num() >= MaxCounters) { return; }
				double MinV = 0.0, MaxV = 0.0, Sum = 0.0, Last = 0.0;
				int64 Num = 0;
				Counter.EnumerateValues(T0, T1, /*bIncludeExternalBounds*/ true, [&](double /*Time*/, int64 Value)
				{
					const double V = static_cast<double>(Value);
					if (Num == 0) { MinV = MaxV = V; }
					else { MinV = FMath::Min(MinV, V); MaxV = FMath::Max(MaxV, V); }
					Sum += V; Last = V; ++Num;
				});
				if (Num == 0) { return; }
				TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
				C->SetStringField(TEXT("name"), Counter.GetName() ? Counter.GetName() : TEXT(""));
				C->SetNumberField(TEXT("samples"), static_cast<double>(Num));
				C->SetNumberField(TEXT("min"), MinV);
				C->SetNumberField(TEXT("max"), MaxV);
				C->SetNumberField(TEXT("avg"), Sum / Num);
				C->SetNumberField(TEXT("last"), Last);
				CounterArr.Add(MakeShared<FJsonValueObject>(C));
			});
			Data->SetNumberField(TEXT("counter_count"), CounterArr.Num());
			Data->SetArrayField(TEXT("counters"), CounterArr);
		}
	}

	Data->SetStringField(TEXT("note"), TEXT("Frame-time summary from Game/Rendering frames. Top CPU timers (bottleneck breakdown) not yet included."));
	return FSmithUECommonUtils::CreateSuccessResponse(Data);
}
