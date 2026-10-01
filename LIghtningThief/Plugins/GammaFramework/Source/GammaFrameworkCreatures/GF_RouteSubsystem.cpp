#include "GF_RouteSubsystem.h"
#include "GF_OSTManager.h"
#include "GF_QuestSubsystem.h"
#include "Engine/GameInstance.h"

void UGF_RouteSubsystem::SetCurrentRoute(UGF_RouteData* NewRoute)
{
    if (NewRoute == CurrentRoute) return;

    CurrentRoute = NewRoute;
    RecordRouteVisited(NewRoute);
    OnRouteChanged.Broadcast(CurrentRoute);

    if (!bMusicSuppressed)
    {
        PlayRouteMusicInternal(CurrentRoute);
    }
}

void UGF_RouteSubsystem::RecordRouteVisited(const UGF_RouteData* Route)
{
    if (!Route || Route->RouteName.IsNone())
    {
        return;
    }

    // Stored as a QuestSubsystem story flag so it rides the existing save with no new
    // plumbing. Fetched lazily rather than cached in Initialize -- a subsystem grabbed during
    // Initialize can silently come back null depending on creation order.
    UGameInstance* GameInstance = GetGameInstance();
    if (!GameInstance)
    {
        return;
    }

    if (UGF_QuestSubsystem* Quest = GameInstance->GetSubsystem<UGF_QuestSubsystem>())
    {
        Quest->AddFlag(MakeMapSeenFlag(Route->RouteName));
    }
}

FName UGF_RouteSubsystem::MakeMapSeenFlag(FName RouteName)
{
    return FName(*(FString(TEXT("MapSeen_")) + RouteName.ToString()));
}

bool UGF_RouteSubsystem::HasVisitedRoute(FName RouteName) const
{
    if (RouteName.IsNone())
    {
        return false;
    }

    const UGameInstance* GameInstance = GetGameInstance();
    if (!GameInstance)
    {
        return false;
    }

    const UGF_QuestSubsystem* Quest = GameInstance->GetSubsystem<UGF_QuestSubsystem>();
    return Quest && Quest->HasFlag(MakeMapSeenFlag(RouteName));
}

FName UGF_RouteSubsystem::GetCurrentRouteName() const
{
    return CurrentRoute ? CurrentRoute->RouteName : NAME_None;
}

FText UGF_RouteSubsystem::GetCurrentRouteDisplayName(const FString& PlayerName) const
{
    if (!CurrentRoute) return FText::GetEmpty();
    return CurrentRoute->GetFormattedDisplayName(PlayerName);
}

bool UGF_RouteSubsystem::HasWildEncounters() const
{
    return CurrentRoute && CurrentRoute->GrassEncounters.Num() > 0;
}

void UGF_RouteSubsystem::RestoreRouteMusic(float FadeInDuration)
{
    PlayRouteMusicInternal(CurrentRoute, FadeInDuration);
}

void UGF_RouteSubsystem::PlayRouteMusicInternal(UGF_RouteData* Route, float FadeInOverride)
{
    if (!Route || !Route->MusicLoop) return;

    UGF_OSTManager* OST = GetOSTManager();
    if (!OST) return;

    float FadeIn     = (FadeInOverride >= 0.f) ? FadeInOverride : Route->MusicFadeInDuration;
    bool  bHasIntro  = (Route->MusicIntro != nullptr);

    OST->StartOST(
        Route->MusicIntro,   // IntroSound  (can be null)
        Route->MusicLoop,    // LoopSound
        bHasIntro,           // bHasIntro
        FadeIn > 0.f,        // bFadeIn
        FadeIn,              // FadeDuration
        1.0f                 // FadeVolume
    );
}

UGF_OSTManager* UGF_RouteSubsystem::GetOSTManager() const
{
    UGameInstance* GI = GetGameInstance();
    return GI ? GI->GetSubsystem<UGF_OSTManager>() : nullptr;
}
