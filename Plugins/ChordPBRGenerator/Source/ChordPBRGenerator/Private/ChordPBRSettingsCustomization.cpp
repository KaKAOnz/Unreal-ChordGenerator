// Copyright 2025 KaKAOnz. All Rights Reserved.

#include "ChordPBRSettingsCustomization.h"

#include "ChordPBRSettings.h"
#include "ComfyUIClient.h"
#include "Async/Async.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Notifications/SNotificationList.h"

TSharedRef<IDetailCustomization> FChordPBRSettingsCustomization::MakeInstance()
{
	return MakeShared<FChordPBRSettingsCustomization>();
}

void FChordPBRSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);

	TWeakObjectPtr<UChordPBRSettings> SettingsWeak;
	if (Objects.Num() > 0)
	{
		SettingsWeak = Cast<UChordPBRSettings>(Objects[0].Get());
	}

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("ComfyUI"));
	TSharedRef<IPropertyHandle> BaseUrlProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UChordPBRSettings, ComfyHttpBaseUrl));

	IDetailPropertyRow& Row = Category.AddProperty(BaseUrlProperty);
	Row.CustomWidget()
	.NameContent()
	[
		BaseUrlProperty->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(350.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			BaseUrlProperty->CreatePropertyValueWidget()
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(6.0f, 0.0f)
		[
			SNew(SButton)
			.Text(NSLOCTEXT("ChordPBRGenerator", "SettingsConnectionTest", "Test Connection"))
			.OnClicked_Lambda([SettingsWeak]()
			{
				if (!SettingsWeak.IsValid())
				{
					FNotificationInfo Info(NSLOCTEXT("ChordPBRGenerator", "SettingsConnectionMissing", "Settings are not available."));
					Info.ExpireDuration = 2.0f;
					FSlateNotificationManager::Get().AddNotification(Info);
					return FReply::Handled();
				}

				TSharedPtr<FComfyUIClient> Client = MakeShared<FComfyUIClient>(*SettingsWeak.Get());
				FNotificationInfo Info(NSLOCTEXT("ChordPBRGenerator", "SettingsConnectionTesting", "Testing ComfyUI..."));
				Info.bFireAndForget = true;
				Info.ExpireDuration = 1.5f;
				FSlateNotificationManager::Get().AddNotification(Info);

				Async(EAsyncExecution::ThreadPool, [Client]()
				{
					FString Error;
					const bool bOk = Client->HealthCheck(Error);

					AsyncTask(ENamedThreads::GameThread, [bOk, Error]()
					{
						const FText ResultText = bOk
							? NSLOCTEXT("ChordPBRGenerator", "SettingsConnectionOk", "Connect succeeded.")
							: FText::FromString(FString::Printf(TEXT("Connect failed: %s"), *Error));

						FNotificationInfo ResultInfo(ResultText);
						ResultInfo.bFireAndForget = true;
						ResultInfo.ExpireDuration = 2.0f;
						FSlateNotificationManager::Get().AddNotification(ResultInfo);
					});
				});

				return FReply::Handled();
			})
		]
	];
}
