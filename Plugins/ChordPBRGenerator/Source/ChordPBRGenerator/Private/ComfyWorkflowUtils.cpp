// Copyright ChordPBRGenerator

#include "ComfyWorkflowUtils.h"

#include "ChordPBRSettings.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool FindNodeIdByInputName(const TSharedPtr<FJsonObject>& Prompt, const FString& InputName, int32& OutNodeId)
	{
		if (!Prompt.IsValid())
		{
			return false;
		}

		for (const auto& NodeKV : Prompt->Values)
		{
			int32 NodeId = FCString::Atoi(*NodeKV.Key);
			const TSharedPtr<FJsonObject>* NodeObj = nullptr;
			if (NodeKV.Value->TryGetObject(NodeObj))
			{
				const TSharedPtr<FJsonObject>* InputsObj = nullptr;
				if ((*NodeObj)->TryGetObjectField(TEXT("inputs"), InputsObj))
				{
					if ((*InputsObj)->HasField(InputName))
					{
						OutNodeId = NodeId;
						return true;
					}
				}
			}
		}

		return false;
	}

	bool SetInputField(const TSharedPtr<FJsonObject>& Prompt, int32 NodeId, const FString& InputName, const TSharedPtr<FJsonValue>& Value)
	{
		if (!Prompt.IsValid())
		{
			return false;
		}

		const FString NodeKey = LexToString(NodeId);
		const TSharedPtr<FJsonObject>* NodeObj = nullptr;
		if (Prompt->TryGetObjectField(NodeKey, NodeObj))
		{
			const TSharedPtr<FJsonObject>* InputsObj = nullptr;
			if ((*NodeObj)->TryGetObjectField(TEXT("inputs"), InputsObj))
			{
				(*InputsObj)->SetField(InputName, Value);
				return true;
			}
		}

		return false;
	}

	int32 ResolveNodeId(const TSharedPtr<FJsonObject>& Prompt, int32 PreferredId, const FString& InputName)
	{
		int32 NodeId = PreferredId;
		if (NodeId < 0)
		{
			FindNodeIdByInputName(Prompt, InputName, NodeId);
		}
		return NodeId;
	}

	bool FindNodeKeyByInputName(const TSharedPtr<FJsonObject>& Prompt, const FString& InputName, FString& OutNodeKey)
	{
		if (!Prompt.IsValid())
		{
			return false;
		}

		for (const auto& NodeKV : Prompt->Values)
		{
			const TSharedPtr<FJsonObject>* NodeObj = nullptr;
			if (NodeKV.Value->TryGetObject(NodeObj))
			{
				const TSharedPtr<FJsonObject>* InputsObj = nullptr;
				if ((*NodeObj)->TryGetObjectField(TEXT("inputs"), InputsObj))
				{
					if ((*InputsObj)->HasField(InputName))
					{
						OutNodeKey = NodeKV.Key;
						return true;
					}
				}
			}
		}

		return false;
	}

	bool MatchesIdentifier(const FString& NodeKey, const TSharedPtr<FJsonObject>& NodeObj, const FString& Identifier)
	{
		if (Identifier.IsEmpty())
		{
			return false;
		}

		if (NodeKey.Equals(Identifier, ESearchCase::IgnoreCase))
		{
			return true;
		}

		if (NodeObj.IsValid())
		{
			FString Title;
			if (NodeObj->TryGetStringField(TEXT("title"), Title))
			{
				if (Title.Equals(Identifier, ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
		}

		return false;
	}

	bool ResolveNodeKey(const TSharedPtr<FJsonObject>& Prompt, const FString& PreferredIdentifier, const FString& InputName, FString& OutNodeKey)
	{
		if (!Prompt.IsValid())
		{
			return false;
		}

		for (const auto& NodeKV : Prompt->Values)
		{
			const TSharedPtr<FJsonObject>* NodeObj = nullptr;
			if (NodeKV.Value->TryGetObject(NodeObj))
			{
				if (MatchesIdentifier(NodeKV.Key, *NodeObj, PreferredIdentifier))
				{
					OutNodeKey = NodeKV.Key;
					return true;
				}
			}
		}

		if (!InputName.IsEmpty())
		{
			return FindNodeKeyByInputName(Prompt, InputName, OutNodeKey);
		}

		return false;
	}

	bool SetInputFieldByKey(const TSharedPtr<FJsonObject>& Prompt, const FString& NodeKey, const FString& InputName, const TSharedPtr<FJsonValue>& Value)
	{
		if (!Prompt.IsValid() || NodeKey.IsEmpty())
		{
			return false;
		}

		const TSharedPtr<FJsonObject>* NodeObj = nullptr;
		if (Prompt->TryGetObjectField(NodeKey, NodeObj))
		{
			const TSharedPtr<FJsonObject>* InputsObj = nullptr;
			if ((*NodeObj)->TryGetObjectField(TEXT("inputs"), InputsObj))
			{
				(*InputsObj)->SetField(InputName, Value);
				return true;
			}
		}

		return false;
	}

	bool LoadTemplateInternal(const FString& Path, TSharedPtr<FJsonObject>& OutPrompt, FString& OutError)
	{
		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *Path))
		{
			OutError = FString::Printf(TEXT("Failed to read template at %s"), *Path);
			return false;
		}

		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, OutPrompt) || !OutPrompt.IsValid())
		{
			OutError = FString::Printf(TEXT("Invalid JSON template: %s"), *Path);
			return false;
		}

		return true;
	}

	const TArray<FString> DefaultChannelHints = { TEXT("basecolor"), TEXT("normal"), TEXT("roughness"), TEXT("metallic"), TEXT("height") };
}

bool FComfyWorkflowUtils::LoadPromptTemplate(const FString& Path, TSharedPtr<FJsonObject>& OutPrompt, FString& OutError)
{
	return LoadTemplateInternal(Path, OutPrompt, OutError);
}

bool FComfyWorkflowUtils::PatchTxt2ImgPrompt(const UChordPBRSettings& Settings, const FString& Prompt, int32 Seed, const FString& FilenamePrefix, TSharedPtr<FJsonObject>& OutPrompt, FString& OutError)
{
	if (!LoadTemplateInternal(Settings.Txt2ImgApiPromptPath, OutPrompt, OutError))
	{
		return false;
	}

	const FComfyTxt2ImgBinding& Binding = Settings.Txt2ImgBinding;

	auto SetStringInput = [&](const FString& NodeIdentifier, const FString& InputName, const FString& Value)
	{
		FString NodeKey;
		if (ResolveNodeKey(OutPrompt, NodeIdentifier, InputName, NodeKey))
		{
			return SetInputFieldByKey(OutPrompt, NodeKey, InputName, MakeShared<FJsonValueString>(Value));
		}
		return false;
	};

	auto SetNumberInput = [&](const FString& NodeIdentifier, const FString& InputName, double Value)
	{
		FString NodeKey;
		if (ResolveNodeKey(OutPrompt, NodeIdentifier, InputName, NodeKey))
		{
			return SetInputFieldByKey(OutPrompt, NodeKey, InputName, MakeShared<FJsonValueNumber>(Value));
		}
		return false;
	};

	SetStringInput(Binding.PromptNodeIdentifier, Binding.PromptInputName, Prompt);
	SetNumberInput(Binding.SeedNodeIdentifier, Binding.SeedInputName, Seed);

	const FString SanitizedPrefix = FPaths::GetBaseFilename(FilenamePrefix);
	if (!SanitizedPrefix.IsEmpty())
	{
		FString PrefixNodeKey;
		if (ResolveNodeKey(OutPrompt, FString(), TEXT("filename_prefix"), PrefixNodeKey))
		{
			SetInputFieldByKey(OutPrompt, PrefixNodeKey, TEXT("filename_prefix"), MakeShared<FJsonValueString>(SanitizedPrefix));
		}
	}

	return true;
}

bool FComfyWorkflowUtils::PatchChordPrompt(const UChordPBRSettings& Settings, const FComfyImageReference& UploadedImage, TSharedPtr<FJsonObject>& OutPrompt, FString& OutError)
{
	if (Settings.ChordImg2PbrApiPromptPath.IsEmpty())
	{
		OutError = TEXT("Chord PBR template path is not configured.");
		return false;
	}

	if (!LoadTemplateInternal(Settings.ChordImg2PbrApiPromptPath, OutPrompt, OutError))
	{
		return false;
	}

	const FComfyChordBinding& Binding = Settings.ChordBinding;
	int32 LoadNodeId = ResolveNodeId(OutPrompt, Binding.LoadImageNodeId, Binding.LoadImageInputName);
	if (LoadNodeId < 0)
	{
		OutError = TEXT("Unable to find LoadImage node in CHORD template.");
		return false;
	}

	const FString ResolvedName = UploadedImage.Subfolder.IsEmpty()
		? UploadedImage.Filename
		: FString::Printf(TEXT("%s/%s"), *UploadedImage.Subfolder, *UploadedImage.Filename);

	const FString NodeKey = LexToString(LoadNodeId);
	const TSharedPtr<FJsonObject>* NodeObj = nullptr;
	const TSharedPtr<FJsonObject>* InputsObj = nullptr;
	TSharedPtr<FJsonValue> ExistingValue;

	bool bUsedObject = false;
	if (OutPrompt->TryGetObjectField(NodeKey, NodeObj) && (*NodeObj)->TryGetObjectField(TEXT("inputs"), InputsObj))
	{
		ExistingValue = (*InputsObj)->TryGetField(Binding.LoadImageInputName);
		if (ExistingValue.IsValid() && ExistingValue->Type == EJson::Object)
		{
			TSharedPtr<FJsonObject> ImageObj = ExistingValue->AsObject();
			if (ImageObj.IsValid())
			{
				if (ImageObj->HasField(TEXT("filename")))
				{
					ImageObj->SetStringField(TEXT("filename"), UploadedImage.Filename);
				}
				else
				{
					ImageObj->SetStringField(TEXT("image"), UploadedImage.Filename);
				}

				if (!UploadedImage.Subfolder.IsEmpty())
				{
					ImageObj->SetStringField(TEXT("subfolder"), UploadedImage.Subfolder);
				}

				if (!UploadedImage.Type.IsEmpty())
				{
					ImageObj->SetStringField(TEXT("type"), UploadedImage.Type);
				}

				bUsedObject = true;
			}
		}
	}

	if (!bUsedObject)
	{
		SetInputField(OutPrompt, LoadNodeId, Binding.LoadImageInputName, MakeShared<FJsonValueString>(ResolvedName));
	}

	return true;
}

bool FComfyWorkflowUtils::ExtractImagesFromHistory(const UChordPBRSettings& Settings, const TSharedPtr<FJsonObject>& History, TArray<FComfyImageReference>& OutImages, FString& OutError)
{
	if (!History.IsValid())
	{
		OutError = TEXT("No history data.");
		return false;
	}

	for (const auto& PromptKV : History->Values)
	{
		const TSharedPtr<FJsonObject>* PromptObj = nullptr;
		if (PromptKV.Value->TryGetObject(PromptObj))
		{
			const TSharedPtr<FJsonObject>* OutputsObj = nullptr;
			if ((*PromptObj)->TryGetObjectField(TEXT("outputs"), OutputsObj))
			{
				for (const auto& OutputKV : (*OutputsObj)->Values)
				{
					const TSharedPtr<FJsonObject>* OutputObj = nullptr;
					if (OutputKV.Value->TryGetObject(OutputObj))
					{
						const TArray<TSharedPtr<FJsonValue>>* ImagesArray = nullptr;
						if ((*OutputObj)->TryGetArrayField(TEXT("images"), ImagesArray))
						{
							for (const TSharedPtr<FJsonValue>& Val : *ImagesArray)
							{
								const TSharedPtr<FJsonObject>* ImgObj = nullptr;
								if (Val->TryGetObject(ImgObj))
								{
									FComfyImageReference Ref;
									(*ImgObj)->TryGetStringField(TEXT("filename"), Ref.Filename);
									(*ImgObj)->TryGetStringField(TEXT("subfolder"), Ref.Subfolder);
									(*ImgObj)->TryGetStringField(TEXT("type"), Ref.Type);
									if (!Ref.Filename.IsEmpty())
									{
										OutImages.Add(Ref);
									}
								}
							}
						}
					}
				}
			}
		}
	}

	if (OutImages.Num() == 0)
	{
		OutError = TEXT("No images found in history outputs.");
		return false;
	}

	return true;
}

static FString ToLower(const FString& In)
{
	FString Lower = In;
	Lower.ToLowerInline();
	return Lower;
}

bool FComfyWorkflowUtils::ExtractPBRFromHistory(const UChordPBRSettings& Settings, const TSharedPtr<FJsonObject>& History, TMap<FString, FComfyImageReference>& OutChannels, FString& OutError)
{
	if (!History.IsValid())
	{
		OutError = TEXT("No history data.");
		return false;
	}

	const FComfyChordBinding& Binding = Settings.ChordBinding;

	auto CollectOutputs = [](const TSharedPtr<FJsonObject>& HistoryObj) -> TMap<FString, TArray<FComfyImageReference>>
	{
		TMap<FString, TArray<FComfyImageReference>> OutputMap;
		for (const auto& PromptKV : HistoryObj->Values)
		{
			const TSharedPtr<FJsonObject>* PromptObj = nullptr;
			if (PromptKV.Value->TryGetObject(PromptObj))
			{
				const TSharedPtr<FJsonObject>* OutputsObj = nullptr;
				if ((*PromptObj)->TryGetObjectField(TEXT("outputs"), OutputsObj))
				{
					for (const auto& OutputKV : (*OutputsObj)->Values)
					{
						const TSharedPtr<FJsonObject>* OutputObj = nullptr;
						if (OutputKV.Value->TryGetObject(OutputObj))
						{
							const TArray<TSharedPtr<FJsonValue>>* ImagesArray = nullptr;
							if ((*OutputObj)->TryGetArrayField(TEXT("images"), ImagesArray))
							{
								TArray<FComfyImageReference> Refs;
								for (const TSharedPtr<FJsonValue>& Val : *ImagesArray)
								{
									const TSharedPtr<FJsonObject>* ImgObj = nullptr;
									if (Val->TryGetObject(ImgObj))
									{
										FComfyImageReference Ref;
										(*ImgObj)->TryGetStringField(TEXT("filename"), Ref.Filename);
										(*ImgObj)->TryGetStringField(TEXT("subfolder"), Ref.Subfolder);
										(*ImgObj)->TryGetStringField(TEXT("type"), Ref.Type);
										if (!Ref.Filename.IsEmpty())
										{
											Refs.Add(Ref);
										}
									}
								}

								if (Refs.Num() > 0)
								{
									OutputMap.Add(OutputKV.Key, Refs);
								}
							}
						}
					}
				}
			}
		}

		return OutputMap;
	};

	TMap<FString, TArray<FComfyImageReference>> Outputs = CollectOutputs(History);

	auto ResolveByBinding = [&](const FComfyPBRChannelBinding& ChannelBinding, const FString& DefaultHint, FComfyImageReference& OutRef) -> bool
	{
		if (ChannelBinding.NodeId >= 0)
		{
			const FString NodeKey = LexToString(ChannelBinding.NodeId);
			if (Outputs.Contains(NodeKey))
			{
				OutRef = Outputs[NodeKey][0];
				return true;
			}
		}

		const FString Hint = !ChannelBinding.FilenameHintContains.IsEmpty() ? ChannelBinding.FilenameHintContains : DefaultHint;
		const FString HintLower = ToLower(Hint);
		for (const auto& Pair : Outputs)
		{
			for (const FComfyImageReference& Ref : Pair.Value)
			{
				if (ToLower(Ref.Filename).Contains(HintLower))
				{
					OutRef = Ref;
					return true;
				}
			}
		}

		return false;
	};

	TArray<FString> ChannelNames = { TEXT("BaseColor"), TEXT("Normal"), TEXT("Roughness"), TEXT("Metallic"), TEXT("Height") };
	const FComfyPBRChannelBinding Channels[] = { Binding.BaseColor, Binding.Normal, Binding.Roughness, Binding.Metallic, Binding.Height };

	for (int32 Index = 0; Index < ChannelNames.Num(); ++Index)
	{
		FComfyImageReference Ref;
		if (ResolveByBinding(Channels[Index], DefaultChannelHints.IsValidIndex(Index) ? DefaultChannelHints[Index] : ChannelNames[Index], Ref))
		{
			OutChannels.Add(ChannelNames[Index], Ref);
		}
		else
		{
			OutError = FString::Printf(TEXT("Missing PBR output for %s"), *ChannelNames[Index]);
			return false;
		}
	}

	return true;
}
