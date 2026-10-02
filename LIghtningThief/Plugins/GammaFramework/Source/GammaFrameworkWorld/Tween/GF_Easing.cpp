// Ported from FCTween -- MIT License - Copyright (c) 2022 Jared Cook
#include "Tween/GF_Easing.h"

const float BACK_INOUT_OVERSHOOT_MODIFIER = 1.525f;
const float BOUNCE_R = 1.0f / 2.75f;		  // reciprocal
const float BOUNCE_K1 = BOUNCE_R;			  // 36.36%
const float BOUNCE_K2 = 2 * BOUNCE_R;		  // 72.72%
const float BOUNCE_K3 = 1.5f * BOUNCE_R;	  // 54.54%
const float BOUNCE_K4 = 2.5f * BOUNCE_R;	  // 90.90%
const float BOUNCE_K5 = 2.25f * BOUNCE_R;	  // 81.81%
const float BOUNCE_K6 = 2.625f * BOUNCE_R;	  // 95.45%
const float BOUNCE_K0 = 7.5625f;

float FGF_Easing::Ease(float t, EGF_Ease EaseType)
{
	switch (EaseType)
	{
		default:
		case EGF_Ease::Linear:
			return EaseLinear(t);
		case EGF_Ease::Smoothstep:
			return EaseSmoothstep(t);
		case EGF_Ease::Stepped:
			return EaseStepped(t);
		case EGF_Ease::InSine:
			return EaseInSine(t);
		case EGF_Ease::OutSine:
			return EaseOutSine(t);
		case EGF_Ease::InOutSine:
			return EaseInOutSine(t);
		case EGF_Ease::InQuad:
			return EaseInQuad(t);
		case EGF_Ease::OutQuad:
			return EaseOutQuad(t);
		case EGF_Ease::InOutQuad:
			return EaseInOutQuad(t);
		case EGF_Ease::InCubic:
			return EaseInCubic(t);
		case EGF_Ease::OutCubic:
			return EaseOutCubic(t);
		case EGF_Ease::InOutCubic:
			return EaseInOutCubic(t);
		case EGF_Ease::InQuart:
			return EaseInQuart(t);
		case EGF_Ease::OutQuart:
			return EaseOutQuart(t);
		case EGF_Ease::InOutQuart:
			return EaseInOutQuart(t);
		case EGF_Ease::InQuint:
			return EaseInQuint(t);
		case EGF_Ease::OutQuint:
			return EaseOutQuint(t);
		case EGF_Ease::InOutQuint:
			return EaseInOutQuint(t);
		case EGF_Ease::InExpo:
			return EaseInExpo(t);
		case EGF_Ease::OutExpo:
			return EaseOutExpo(t);
		case EGF_Ease::InOutExpo:
			return EaseInOutExpo(t);
		case EGF_Ease::InCirc:
			return EaseInCirc(t);
		case EGF_Ease::OutCirc:
			return EaseOutCirc(t);
		case EGF_Ease::InOutCirc:
			return EaseInOutCirc(t);
		case EGF_Ease::InElastic:
			return EaseInElastic(t);
		case EGF_Ease::OutElastic:
			return EaseOutElastic(t);
		case EGF_Ease::InOutElastic:
			return EaseInOutElastic(t);
		case EGF_Ease::InBounce:
			return EaseInBounce(t);
		case EGF_Ease::OutBounce:
			return EaseOutBounce(t);
		case EGF_Ease::InOutBounce:
			return EaseInOutBounce(t);
		case EGF_Ease::InBack:
			return EaseInBack(t);
		case EGF_Ease::OutBack:
			return EaseOutBack(t);
		case EGF_Ease::InOutBack:
			return EaseInOutBack(t);
	}
}

float FGF_Easing::EaseWithParams(float t, EGF_Ease EaseType, float Param1, float Param2)
{
	if (Param1 == 0 && Param2 == 0)
	{
		return Ease(t, EaseType);
	}

	switch (EaseType)
	{
		default:
		case EGF_Ease::Linear:
			return EaseLinear(t);
		case EGF_Ease::Smoothstep:
			return EaseSmoothstep(t, Param1, Param2);
		case EGF_Ease::Stepped:
			return EaseStepped(t, Param1);
		case EGF_Ease::InSine:
			return EaseInSine(t);
		case EGF_Ease::OutSine:
			return EaseOutSine(t);
		case EGF_Ease::InOutSine:
			return EaseInOutSine(t);
		case EGF_Ease::InQuad:
			return EaseInQuad(t);
		case EGF_Ease::OutQuad:
			return EaseOutQuad(t);
		case EGF_Ease::InOutQuad:
			return EaseInOutQuad(t);
		case EGF_Ease::InCubic:
			return EaseInCubic(t);
		case EGF_Ease::OutCubic:
			return EaseOutCubic(t);
		case EGF_Ease::InOutCubic:
			return EaseInOutCubic(t);
		case EGF_Ease::InQuart:
			return EaseInQuart(t);
		case EGF_Ease::OutQuart:
			return EaseOutQuart(t);
		case EGF_Ease::InOutQuart:
			return EaseInOutQuart(t);
		case EGF_Ease::InQuint:
			return EaseInQuint(t);
		case EGF_Ease::OutQuint:
			return EaseOutQuint(t);
		case EGF_Ease::InOutQuint:
			return EaseInOutQuint(t);
		case EGF_Ease::InExpo:
			return EaseInExpo(t);
		case EGF_Ease::OutExpo:
			return EaseOutExpo(t);
		case EGF_Ease::InOutExpo:
			return EaseInOutExpo(t);
		case EGF_Ease::InCirc:
			return EaseInCirc(t);
		case EGF_Ease::OutCirc:
			return EaseOutCirc(t);
		case EGF_Ease::InOutCirc:
			return EaseInOutCirc(t);
		case EGF_Ease::InElastic:
			return EaseInElastic(t, Param1, Param2);
		case EGF_Ease::OutElastic:
			return EaseOutElastic(t, Param1, Param2);
		case EGF_Ease::InOutElastic:
			return EaseInOutElastic(t, Param1, Param2);
		case EGF_Ease::InBounce:
			return EaseInBounce(t);
		case EGF_Ease::OutBounce:
			return EaseOutBounce(t);
		case EGF_Ease::InOutBounce:
			return EaseInOutBounce(t);
		case EGF_Ease::InBack:
			return EaseInBack(t, Param1);
		case EGF_Ease::OutBack:
			return EaseOutBack(t, Param1);
		case EGF_Ease::InOutBack:
			return EaseInOutBack(t, Param1);
	}
}

float FGF_Easing::EaseLinear(float t)
{
	return t;
}

float FGF_Easing::EaseSmoothstep(float t, float x0, float x1)
{
	float x = FMath::Clamp<float>((t - x0) / (x1 - x0), 0.0f, 1.0f);
	return x * x * (3.0f - 2.0f * x);
}

float FGF_Easing::EaseStepped(float t, int Steps)
{
	if (t <= 0)
	{
		return 0;
	}
	else if (t >= 1)
	{
		return 1;
	}
	else
	{
		return FMath::FloorToFloat(Steps * t) / Steps;
	}
}

float FGF_Easing::EaseInSine(float t)
{
	return 1 - FMath::Cos(t * PI * .5f);
}

float FGF_Easing::EaseOutSine(float t)
{
	return FMath::Sin(t * PI * .5f);
}

float FGF_Easing::EaseInOutSine(float t)
{
	return 0.5f * (1 - FMath::Cos(t * PI));
}

float FGF_Easing::EaseInQuad(float t)
{
	return t * t;
}

float FGF_Easing::EaseOutQuad(float t)
{
	return t * (2 - t);
}

float FGF_Easing::EaseInOutQuad(float t)
{
	float t2 = t * 2;
	if (t2 < 1)
	{
		return t * t2;
	}
	else
	{
		float m = t - 1;
		return 1 - m * m * 2;
	}
}

float FGF_Easing::EaseInCubic(float t)
{
	return t * t * t;
}

float FGF_Easing::EaseOutCubic(float t)
{
	float m = t - 1;
	return 1 + m * m * m;
}

float FGF_Easing::EaseInOutCubic(float t)
{
	float t2 = t * 2;
	if (t2 < 1)
	{
		return t * t2 * t2;
	}
	else
	{
		float m = t - 1;
		return 1 + m * m * m * 4;
	}
}

float FGF_Easing::EaseInQuart(float t)
{
	return t * t * t * t;
}

float FGF_Easing::EaseOutQuart(float t)
{
	float m = t - 1;
	return 1 - m * m * m * m;
}

float FGF_Easing::EaseInOutQuart(float t)
{
	float t2 = t * 2;
	if (t2 < 1)
	{
		return t * t2 * t2 * t2;
	}
	else
	{
		float m = t - 1;
		return 1 - m * m * m * m * 8;
	}
}

float FGF_Easing::EaseInQuint(float t)
{
	return t * t * t * t * t;
}

float FGF_Easing::EaseOutQuint(float t)
{
	float m = t - 1;
	return 1 + m * m * m * m * m;
}

float FGF_Easing::EaseInOutQuint(float t)
{
	float t2 = t * 2;
	if (t2 < 1)
	{
		return t * t2 * t2 * t2 * t2;
	}
	else
	{
		float m = t - 1;
		return 1 + m * m * m * m * m * 16;
	}
}

float FGF_Easing::EaseInExpo(float t)
{
	if (t <= 0)
	{
		return 0;
	}
	if (t >= 1)
	{
		return 1;
	}
	return FMath::Pow(2, 10 * (t - 1));
}

float FGF_Easing::EaseOutExpo(float t)
{
	if (t <= 0)
	{
		return 0;
	}
	if (t >= 1)
	{
		return 1;
	}
	return 1 - FMath::Pow(2, -10 * t);
}

float FGF_Easing::EaseInOutExpo(float t)
{
	if (t <= 0)
	{
		return 0;
	}
	if (t >= 1)
	{
		return 1;
	}
	if (t < 0.5f)
	{
		return FMath::Pow(2, 10 * (2 * t - 1) - 1);
	}
	else
	{
		return 1 - FMath::Pow(2, -10 * (2 * t - 1) - 1);
	}
}

float FGF_Easing::EaseInCirc(float t)
{
	return 1 - FMath::Sqrt(1 - t * t);
}

float FGF_Easing::EaseOutCirc(float t)
{
	float m = t - 1;
	return FMath::Sqrt(1 - m * m);
}

float FGF_Easing::EaseInOutCirc(float t)
{
	float t2 = t * 2;
	if (t2 < 1)
	{
		return (1 - FMath::Sqrt(1 - t2 * t2)) * .5f;
	}
	else
	{
		float m = t - 1;
		return (FMath::Sqrt(1 - 4 * m * m) + 1) * .5f;
	}
}

float FGF_Easing::EaseInElastic(float t, float Amplitude, float Period)
{
	if (t == 0)
	{
		return 0;
	}
	else if (t == 1)
	{
		return 1;
	}
	else
	{
		float m = t - 1;
		float s = Period / 4.0f;
		if (Amplitude > 1)
		{
			s = Period * FMath::Asin(1.0f / Amplitude) / (2.0f * PI);
		}

		return -(Amplitude * FMath::Pow(2, 10 * m) * FMath::Sin((m - s) * (2.0f * PI) / Period));
	}
}
// baked-in-parameters version
// float FGF_Tween::EaseInElastic(float t)
// {
// 	float m = t - 1;
// 	return -FMath::Pow(2, 10 * m) * FMath::Sin((m * 40 - 3) * PI / 6);
// }

float FGF_Easing::EaseOutElastic(float t, float Amplitude, float Period)
{
	if (t == 0)
	{
		return 0;
	}
	else if (t == 1)
	{
		return 1;
	}
	else
	{
		float s = Period / 4.0f;
		if (Amplitude > 1)
		{
			s = Period * FMath::Asin(1.0f / Amplitude) / (2.0f * PI);
		}
		return 1.0f + Amplitude * FMath::Pow(2, -10 * t) * FMath::Sin((t - s) * (2.0f * PI) / Period);
	}
}
// baked-in-parameters version
// float FGF_Tween::EaseOutElastic(float t)
// {
// 	return 1 + FMath::Pow(2, 10 * (-t)) * FMath::Sin((-t * 40 - 3) * PI / 6);
// }
float FGF_Easing::EaseInOutElastic(float t, float Amplitude, float Period)
{
	if (t == 0)
	{
		return 0;
	}
	else if (t == 1)
	{
		return 1;
	}
	else
	{
		float m = 2.0f * t - 1;
		float s = Period / 4.0f;
		if (Amplitude > 1)
		{
			s = Period * FMath::Asin(1.0f / Amplitude) / (2.0f * PI);
		}

		if (m < 0)
		{
			return .5f * -(Amplitude * FMath::Pow(2, 10 * m) * FMath::Sin((m - s) * (2.0f * PI) / Period));
		}
		else
		{
			return 1.0f + .5f * (Amplitude * FMath::Pow(2, -10 * t) * FMath::Sin((t - s) * (2.0f * PI) / Period));
		}
	}
}
// baked-in-parameters version
// float FGF_Tween::EaseInOutElastic(float t)
// {
// 	float s = 2 * t - 1;
// 	float k = (80 * s - 9) * PI / 18;
// 	if (s < 0)
// 	{
// 		return -.5f * FMath::Pow(2, 10 * s) * FMath::Sin(k);
// 	}
// 	else
// 	{
// 		return 1 + .5f * FMath::Pow(2, -10 * s) * FMath::Sin(k);
// 	}
// }

float FGF_Easing::EaseInBounce(float t)
{
	return 1 - EaseOutBounce(1 - t);
}

float FGF_Easing::EaseOutBounce(float t)
{
	float t2;

	if (t < BOUNCE_K1)
	{
		return BOUNCE_K0 * t * t;
	}
	else if (t < BOUNCE_K2)
	{
		t2 = t - BOUNCE_K3;
		return BOUNCE_K0 * t2 * t2 + 0.75f;
	}
	else if (t < BOUNCE_K4)
	{
		t2 = t - BOUNCE_K5;
		return BOUNCE_K0 * t2 * t2 + 0.9375f;
	}
	else
	{
		t2 = t - BOUNCE_K6;
		return BOUNCE_K0 * t2 * t2 + 0.984375f;
	}
}

float FGF_Easing::EaseInOutBounce(float t)
{
	float t2 = t * 2;
	if (t2 < 1)
	{
		return .5f - .5f * EaseOutBounce(1 - t2);
	}
	else
	{
		return .5f + .5f * EaseOutBounce(t2 - 1);
	}
}

float FGF_Easing::EaseInBack(float t, float Overshoot)
{
	return t * t * ((Overshoot + 1) * t - Overshoot);
}

float FGF_Easing::EaseOutBack(float t, float Overshoot)
{
	float m = t - 1;
	return 1 + m * m * (m * (Overshoot + 1) + Overshoot);
}

float FGF_Easing::EaseInOutBack(float t, float Overshoot)
{
	float t2 = t * 2;
	float s = Overshoot * BACK_INOUT_OVERSHOOT_MODIFIER;
	if (t < .5f)
	{
		return t * t2 * (t2 * (s + 1) - s);
	}
	else
	{
		float m = t - 1;
		return 1 + 2 * m * m * (2 * m * (s + 1) + s);
	}
}