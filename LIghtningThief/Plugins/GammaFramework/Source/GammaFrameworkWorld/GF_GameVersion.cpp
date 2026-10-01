// Fill out your copyright notice in the Description page of Project Settings.

#include "GF_GameVersion.h"

const UGF_GameVersionSettings* UGF_GameVersionSettings::Get()
{
	// GetDefault never returns null for a UDeveloperSettings, so callers do not
	// need a null check and a missing ini section simply leaves the defaults.
	return GetDefault<UGF_GameVersionSettings>();
}

FString UGF_GameVersionLibrary::GetBuildVersion()
{
	return UGF_GameVersionSettings::Get()->BuildVersion;
}

FText UGF_GameVersionLibrary::GetBuildVersionDisplayText()
{
	// FromString rather than a localised format: a version number is the same in
	// every language, and routing it through the localisation system would only
	// create a string that has to be re-translated every release.
	return FText::FromString(FString::Printf(TEXT("v%s"), *GetBuildVersion()));
}
