#include "GF_BootPlan.h"

const UGF_BootSettings* UGF_BootSettings::Get()
{
	const UGF_BootSettings* Settings = GetDefault<UGF_BootSettings>();
	return Settings ? Settings : GetMutableDefault<UGF_BootSettings>();
}
