// Copyright 2026, 123dx-svg. MIT License.

#include "Commands/SmithUESequencerCommands.h"
#include "ToolRegistry/SmithUEToolRegistry.h"
#include "Utils/SmithUECommonUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneBoolTrack.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieSceneBoolSection.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/Factory.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "MovieSceneBinding.h"
#include "MovieSceneSpawnable.h"

namespace SmithUESequencer
{
    UWorld* GetEditorWorld()
    {
        return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    }

    ULevelSequence* LoadSequence(const FString& SequencePath, FString& OutError)
    {
        UObject* Loaded = StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *SequencePath);
        ULevelSequence* Sequence = Cast<ULevelSequence>(Loaded);
        if (!Sequence)
        {
            OutError = FString::Printf(TEXT("Failed to load LevelSequence at '%s'"), *SequencePath);
        }
        return Sequence;
    }

    AActor* FindActorByLabel(UWorld* World, const FString& Label)
    {
        if (!World) return nullptr;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (It->GetActorLabel() == Label)
            {
                return *It;
            }
        }
        return nullptr;
    }

    FGuid FindBindingGuid(UMovieScene* MovieScene, const FString& BindingName)
    {
        for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
        {
            if (Binding.GetName() == BindingName)
            {
                return Binding.GetObjectGuid();
            }
        }
        return FGuid();
    }
}

// ---------------------------------------------------------------------------
// RegisterTools
// ---------------------------------------------------------------------------

void FSmithUESequencerCommands::RegisterTools(FSmithUEToolRegistry& Registry)
{
    Registry.Register(
        FSmithUEToolSchema(
            TEXT("seq_create"),
            TEXT("Sequencer"),
            TEXT("Create a LevelSequence asset"),
            {
                FSmithUEToolParam(TEXT("name"), TEXT("string"), TEXT("Sequence asset name"), true),
                FSmithUEToolParam(TEXT("path"), TEXT("string"), TEXT("Content folder path (e.g. /Game/Cinematics)"), true)
            }),
        &HandleSeqCreate);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("seq_read"),
            TEXT("Sequencer"),
            TEXT("Read sequence info (bindings, tracks, range)"),
            {
                FSmithUEToolParam(TEXT("sequence_path"), TEXT("string"), TEXT("LevelSequence asset path"), true)
            }),
        &HandleSeqRead);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("seq_add_binding"),
            TEXT("Sequencer"),
            TEXT("Bind a world actor to a sequence"),
            {
                FSmithUEToolParam(TEXT("sequence_path"), TEXT("string"), TEXT("LevelSequence asset path"), true),
                FSmithUEToolParam(TEXT("actor_label"), TEXT("string"), TEXT("Actor label in the world"), true)
            }),
        &HandleSeqAddBinding);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("seq_add_track"),
            TEXT("Sequencer"),
            TEXT("Add a track to a binding (Transform, Float, Bool)"),
            {
                FSmithUEToolParam(TEXT("sequence_path"), TEXT("string"), TEXT("LevelSequence asset path"), true),
                FSmithUEToolParam(TEXT("binding_name"), TEXT("string"), TEXT("Binding name in the sequence"), true),
                FSmithUEToolParam(TEXT("track_type"), TEXT("string"), TEXT("Track type: Transform, Float, Bool"), true)
            }),
        &HandleSeqAddTrack);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("seq_add_keyframe"),
            TEXT("Sequencer"),
            TEXT("Add a keyframe to a track at a given time"),
            {
                FSmithUEToolParam(TEXT("sequence_path"), TEXT("string"), TEXT("LevelSequence asset path"), true),
                FSmithUEToolParam(TEXT("binding_name"), TEXT("string"), TEXT("Binding name in the sequence"), true),
                FSmithUEToolParam(TEXT("track_type"), TEXT("string"), TEXT("Track type: Transform, Float, Bool"), true),
                FSmithUEToolParam(TEXT("time"), TEXT("float"), TEXT("Time in seconds for the keyframe"), true),
                FSmithUEToolParam(TEXT("value"), TEXT("object"), TEXT("Keyframe value. Transform: {lx,ly,lz,rx,ry,rz,sx,sy,sz}. Float: {value}. Bool: {value}"), true)
            }),
        &HandleSeqAddKeyframe);

    Registry.Register(
        FSmithUEToolSchema(
            TEXT("seq_set_range"),
            TEXT("Sequencer"),
            TEXT("Set the playback range in frames"),
            {
                FSmithUEToolParam(TEXT("sequence_path"), TEXT("string"), TEXT("LevelSequence asset path"), true),
                FSmithUEToolParam(TEXT("start_frame"), TEXT("int"), TEXT("Start frame number"), true),
                FSmithUEToolParam(TEXT("end_frame"), TEXT("int"), TEXT("End frame number"), true)
            }),
        &HandleSeqSetRange);
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FSmithUESequencerCommands::HandleSeqCreate(const TSharedPtr<FJsonObject>& Params)
{
    FString Name, Path;
    Params->TryGetStringField(TEXT("name"), Name);
    Params->TryGetStringField(TEXT("path"), Path);

    if (Name.IsEmpty() || Path.IsEmpty())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("name and path are required"));
    }

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FindObject<UClass>(ANY_PACKAGE, TEXT("LevelSequenceFactoryNew")));
    if (!Factory)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("LevelSequenceFactoryNew not found"));
    }

    UObject* NewAsset = AssetTools.CreateAsset(Name, Path, ULevelSequence::StaticClass(), Factory);
    if (!NewAsset)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to create LevelSequence asset"));
    }

    UEditorAssetLibrary::SaveLoadedAsset(NewAsset);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Name);
    Data->SetStringField(TEXT("path"), NewAsset->GetPathName());
    UE_LOG(LogSmithUE, Log, TEXT("seq_create: created %s"), *NewAsset->GetPathName());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUESequencerCommands::HandleSeqRead(const TSharedPtr<FJsonObject>& Params)
{
    FString SequencePath;
    Params->TryGetStringField(TEXT("sequence_path"), SequencePath);

    FString Error;
    ULevelSequence* Sequence = SmithUESequencer::LoadSequence(SequencePath, Error);
    if (!Sequence) return FSmithUECommonUtils::CreateErrorResponse(Error);

    UMovieScene* MovieScene = Sequence->GetMovieScene();
    if (!MovieScene) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No MovieScene found in sequence"));

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("sequence_path"), Sequence->GetPathName());

    // Playback range
    FFrameNumber StartFrame = MovieScene->GetPlaybackRange().GetLowerBoundValue();
    FFrameNumber EndFrame = MovieScene->GetPlaybackRange().GetUpperBoundValue();
    FFrameRate TickResolution = MovieScene->GetTickResolution();
    Data->SetNumberField(TEXT("start_frame"), StartFrame.Value);
    Data->SetNumberField(TEXT("end_frame"), EndFrame.Value);
    Data->SetNumberField(TEXT("tick_resolution_num"), TickResolution.Numerator);
    Data->SetNumberField(TEXT("tick_resolution_den"), TickResolution.Denominator);

    // Bindings
    TArray<TSharedPtr<FJsonValue>> BindingsArray;
    for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
    {
        TSharedPtr<FJsonObject> BindingObj = MakeShared<FJsonObject>();
        BindingObj->SetStringField(TEXT("name"), Binding.GetName());
        BindingObj->SetStringField(TEXT("guid"), Binding.GetObjectGuid().ToString());

        TArray<TSharedPtr<FJsonValue>> TracksArray;
        for (UMovieSceneTrack* Track : Binding.GetTracks())
        {
            if (!Track) continue;
            TSharedPtr<FJsonObject> TrackObj = MakeShared<FJsonObject>();
            TrackObj->SetStringField(TEXT("type"), Track->GetClass()->GetName());
            TrackObj->SetNumberField(TEXT("section_count"), Track->GetAllSections().Num());
            TracksArray.Add(MakeShared<FJsonValueObject>(TrackObj));
        }
        BindingObj->SetArrayField(TEXT("tracks"), TracksArray);
        BindingsArray.Add(MakeShared<FJsonValueObject>(BindingObj));
    }
    Data->SetArrayField(TEXT("bindings"), BindingsArray);
    Data->SetNumberField(TEXT("binding_count"), BindingsArray.Num());

    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUESequencerCommands::HandleSeqAddBinding(const TSharedPtr<FJsonObject>& Params)
{
    FString SequencePath, ActorLabel;
    Params->TryGetStringField(TEXT("sequence_path"), SequencePath);
    Params->TryGetStringField(TEXT("actor_label"), ActorLabel);

    FString Error;
    ULevelSequence* Sequence = SmithUESequencer::LoadSequence(SequencePath, Error);
    if (!Sequence) return FSmithUECommonUtils::CreateErrorResponse(Error);

    UWorld* World = SmithUESequencer::GetEditorWorld();
    if (!World) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No editor world"));

    AActor* Actor = SmithUESequencer::FindActorByLabel(World, ActorLabel);
    if (!Actor) return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel));

    UMovieScene* MovieScene = Sequence->GetMovieScene();
    if (!MovieScene) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No MovieScene"));

    // Check if already bound
    for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
    {
        if (Binding.GetName() == ActorLabel)
        {
            TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
            Data->SetStringField(TEXT("binding_name"), ActorLabel);
            Data->SetStringField(TEXT("guid"), Binding.GetObjectGuid().ToString());
            Data->SetBoolField(TEXT("already_existed"), true);
            return FSmithUECommonUtils::CreateSuccessResponse(Data);
        }
    }

    // Create a possessable binding
    FGuid BindingGuid = MovieScene->AddPossessable(ActorLabel, Actor->GetClass());
    Sequence->BindPossessableObject(BindingGuid, *Actor, World);
    Sequence->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("binding_name"), ActorLabel);
    Data->SetStringField(TEXT("guid"), BindingGuid.ToString());
    Data->SetBoolField(TEXT("already_existed"), false);
    UE_LOG(LogSmithUE, Log, TEXT("seq_add_binding: bound '%s' → %s"), *ActorLabel, *BindingGuid.ToString());
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUESequencerCommands::HandleSeqAddTrack(const TSharedPtr<FJsonObject>& Params)
{
    FString SequencePath, BindingName, TrackType;
    Params->TryGetStringField(TEXT("sequence_path"), SequencePath);
    Params->TryGetStringField(TEXT("binding_name"), BindingName);
    Params->TryGetStringField(TEXT("track_type"), TrackType);

    FString Error;
    ULevelSequence* Sequence = SmithUESequencer::LoadSequence(SequencePath, Error);
    if (!Sequence) return FSmithUECommonUtils::CreateErrorResponse(Error);

    UMovieScene* MovieScene = Sequence->GetMovieScene();
    if (!MovieScene) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No MovieScene"));

    FGuid BindingGuid = SmithUESequencer::FindBindingGuid(MovieScene, BindingName);
    if (!BindingGuid.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Binding '%s' not found"), *BindingName));
    }

    UClass* TrackClass = nullptr;
    if (TrackType.Equals(TEXT("Transform"), ESearchCase::IgnoreCase))
    {
        TrackClass = UMovieScene3DTransformTrack::StaticClass();
    }
    else if (TrackType.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
    {
        TrackClass = UMovieSceneFloatTrack::StaticClass();
    }
    else if (TrackType.Equals(TEXT("Bool"), ESearchCase::IgnoreCase))
    {
        TrackClass = UMovieSceneBoolTrack::StaticClass();
    }
    else
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unsupported track_type '%s'. Use: Transform, Float, Bool"), *TrackType));
    }

    UMovieSceneTrack* Track = MovieScene->AddTrack(TrackClass, BindingGuid);
    if (!Track)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Failed to add track (may already exist)"));
    }

    // Add a default section
    UMovieSceneSection* Section = Track->CreateNewSection();
    if (Section)
    {
        Section->SetRange(MovieScene->GetPlaybackRange());
        Track->AddSection(*Section);
    }

    Sequence->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("binding_name"), BindingName);
    Data->SetStringField(TEXT("track_type"), TrackType);
    Data->SetStringField(TEXT("track_class"), Track->GetClass()->GetName());
    UE_LOG(LogSmithUE, Log, TEXT("seq_add_track: added %s track to '%s'"), *TrackType, *BindingName);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FSmithUESequencerCommands::HandleSeqAddKeyframe(const TSharedPtr<FJsonObject>& Params)
{
    FString SequencePath, BindingName, TrackType;
    double Time = 0.0;
    Params->TryGetStringField(TEXT("sequence_path"), SequencePath);
    Params->TryGetStringField(TEXT("binding_name"), BindingName);
    Params->TryGetStringField(TEXT("track_type"), TrackType);
    Params->TryGetNumberField(TEXT("time"), Time);

    const TSharedPtr<FJsonObject>* ValueObjPtr = nullptr;
    if (!Params->TryGetObjectField(TEXT("value"), ValueObjPtr) || !ValueObjPtr || !ValueObjPtr->IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("value must be a JSON object"));
    }
    const TSharedPtr<FJsonObject>& ValueObj = *ValueObjPtr;

    FString Error;
    ULevelSequence* Sequence = SmithUESequencer::LoadSequence(SequencePath, Error);
    if (!Sequence) return FSmithUECommonUtils::CreateErrorResponse(Error);

    UMovieScene* MovieScene = Sequence->GetMovieScene();
    if (!MovieScene) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No MovieScene"));

    FGuid BindingGuid = SmithUESequencer::FindBindingGuid(MovieScene, BindingName);
    if (!BindingGuid.IsValid())
    {
        return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Binding '%s' not found"), *BindingName));
    }

    FFrameRate TickResolution = MovieScene->GetTickResolution();
    FFrameNumber FrameNumber = (Time * TickResolution).FloorToFrame();

    // Find the matching track
    const FMovieSceneBinding* FoundBinding = MovieScene->FindBinding(BindingGuid);
    if (!FoundBinding)
    {
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("Binding not found in MovieScene"));
    }

    if (TrackType.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
    {
        for (UMovieSceneTrack* Track : FoundBinding->GetTracks())
        {
            UMovieSceneFloatTrack* FloatTrack = Cast<UMovieSceneFloatTrack>(Track);
            if (!FloatTrack) continue;

            for (UMovieSceneSection* Section : FloatTrack->GetAllSections())
            {
                UMovieSceneFloatSection* FloatSection = Cast<UMovieSceneFloatSection>(Section);
                if (!FloatSection) continue;

                TArrayView<FMovieSceneFloatChannel*> Channels = FloatSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
                if (Channels.Num() > 0)
                {
                    FMovieSceneFloatChannel* Channel = Channels[0];
                    double Val = 0.0;
                    ValueObj->TryGetNumberField(TEXT("value"), Val);
                    Channel->AddCubicKey(FrameNumber, (float)Val);
                    Sequence->MarkPackageDirty();

                    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
                    Data->SetStringField(TEXT("track_type"), TEXT("Float"));
                    Data->SetNumberField(TEXT("frame"), FrameNumber.Value);
                    Data->SetNumberField(TEXT("value"), Val);
                    return FSmithUECommonUtils::CreateSuccessResponse(Data);
                }
            }
            break;
        }
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No Float track/section/channel found for binding"));
    }
    else if (TrackType.Equals(TEXT("Transform"), ESearchCase::IgnoreCase))
    {
        for (UMovieSceneTrack* Track : FoundBinding->GetTracks())
        {
            UMovieScene3DTransformTrack* TransformTrack = Cast<UMovieScene3DTransformTrack>(Track);
            if (!TransformTrack) continue;

            for (UMovieSceneSection* Section : TransformTrack->GetAllSections())
            {
                UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(Section);
                if (!TransformSection) continue;

                TArrayView<FMovieSceneFloatChannel*> Channels = TransformSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
                // Order: LX, LY, LZ, RX, RY, RZ, SX, SY, SZ
                if (Channels.Num() >= 9)
                {
                    double LX = 0, LY = 0, LZ = 0, RX = 0, RY = 0, RZ = 0, SX = 1, SY = 1, SZ = 1;
                    ValueObj->TryGetNumberField(TEXT("lx"), LX);
                    ValueObj->TryGetNumberField(TEXT("ly"), LY);
                    ValueObj->TryGetNumberField(TEXT("lz"), LZ);
                    ValueObj->TryGetNumberField(TEXT("rx"), RX);
                    ValueObj->TryGetNumberField(TEXT("ry"), RY);
                    ValueObj->TryGetNumberField(TEXT("rz"), RZ);
                    ValueObj->TryGetNumberField(TEXT("sx"), SX);
                    ValueObj->TryGetNumberField(TEXT("sy"), SY);
                    ValueObj->TryGetNumberField(TEXT("sz"), SZ);

                    Channels[0]->AddCubicKey(FrameNumber, (float)LX);
                    Channels[1]->AddCubicKey(FrameNumber, (float)LY);
                    Channels[2]->AddCubicKey(FrameNumber, (float)LZ);
                    Channels[3]->AddCubicKey(FrameNumber, (float)RX);
                    Channels[4]->AddCubicKey(FrameNumber, (float)RY);
                    Channels[5]->AddCubicKey(FrameNumber, (float)RZ);
                    Channels[6]->AddCubicKey(FrameNumber, (float)SX);
                    Channels[7]->AddCubicKey(FrameNumber, (float)SY);
                    Channels[8]->AddCubicKey(FrameNumber, (float)SZ);

                    Sequence->MarkPackageDirty();

                    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
                    Data->SetStringField(TEXT("track_type"), TEXT("Transform"));
                    Data->SetNumberField(TEXT("frame"), FrameNumber.Value);
                    return FSmithUECommonUtils::CreateSuccessResponse(Data);
                }
            }
            break;
        }
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No Transform track/section found for binding"));
    }
    else if (TrackType.Equals(TEXT("Bool"), ESearchCase::IgnoreCase))
    {
        for (UMovieSceneTrack* Track : FoundBinding->GetTracks())
        {
            UMovieSceneBoolTrack* BoolTrack = Cast<UMovieSceneBoolTrack>(Track);
            if (!BoolTrack) continue;

            for (UMovieSceneSection* Section : BoolTrack->GetAllSections())
            {
                UMovieSceneBoolSection* BoolSection = Cast<UMovieSceneBoolSection>(Section);
                if (!BoolSection) continue;

                TArrayView<FMovieSceneBoolChannel*> Channels = BoolSection->GetChannelProxy().GetChannels<FMovieSceneBoolChannel>();
                if (Channels.Num() > 0)
                {
                    FMovieSceneBoolChannel* Channel = Channels[0];
                    bool Val = false;
                    ValueObj->TryGetBoolField(TEXT("value"), Val);
                    Channel->GetData().AddKey(FrameNumber, Val);
                    Sequence->MarkPackageDirty();

                    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
                    Data->SetStringField(TEXT("track_type"), TEXT("Bool"));
                    Data->SetNumberField(TEXT("frame"), FrameNumber.Value);
                    Data->SetBoolField(TEXT("value"), Val);
                    return FSmithUECommonUtils::CreateSuccessResponse(Data);
                }
            }
            break;
        }
        return FSmithUECommonUtils::CreateErrorResponse(TEXT("No Bool track/section/channel found for binding"));
    }

    return FSmithUECommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unsupported track_type: %s"), *TrackType));
}

TSharedPtr<FJsonObject> FSmithUESequencerCommands::HandleSeqSetRange(const TSharedPtr<FJsonObject>& Params)
{
    FString SequencePath;
    double StartFrame = 0, EndFrame = 0;
    Params->TryGetStringField(TEXT("sequence_path"), SequencePath);
    Params->TryGetNumberField(TEXT("start_frame"), StartFrame);
    Params->TryGetNumberField(TEXT("end_frame"), EndFrame);

    FString Error;
    ULevelSequence* Sequence = SmithUESequencer::LoadSequence(SequencePath, Error);
    if (!Sequence) return FSmithUECommonUtils::CreateErrorResponse(Error);

    UMovieScene* MovieScene = Sequence->GetMovieScene();
    if (!MovieScene) return FSmithUECommonUtils::CreateErrorResponse(TEXT("No MovieScene"));

    FFrameNumber Start((int32)StartFrame);
    FFrameNumber End((int32)EndFrame);
    MovieScene->SetPlaybackRange(TRange<FFrameNumber>(Start, End));
    Sequence->MarkPackageDirty();
    UEditorAssetLibrary::SaveLoadedAsset(Sequence);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("start_frame"), Start.Value);
    Data->SetNumberField(TEXT("end_frame"), End.Value);
    UE_LOG(LogSmithUE, Log, TEXT("seq_set_range: set range [%d, %d]"), Start.Value, End.Value);
    return FSmithUECommonUtils::CreateSuccessResponse(Data);
}
