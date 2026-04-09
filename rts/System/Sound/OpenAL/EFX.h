/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef _EFX_H_
#define _EFX_H_

#include <string>

#include <al.h>
#include <alc.h>

#ifndef RECOIL_OPENAL_HAS_EFX
#if defined(__has_include)
#if __has_include(<OpenAL/efx.h>)
#define RECOIL_OPENAL_HAS_EFX 1
#elif __has_include(<AL/efx.h>)
#define RECOIL_OPENAL_HAS_EFX 1
#elif __has_include(<efx.h>)
#define RECOIL_OPENAL_HAS_EFX 1
#else
#define RECOIL_OPENAL_HAS_EFX 0
#endif
#else
#define RECOIL_OPENAL_HAS_EFX 1
#endif
#endif

#if RECOIL_OPENAL_HAS_EFX
#if defined(__has_include)
#if __has_include(<OpenAL/efx.h>)
#include <OpenAL/efx.h>
#elif __has_include(<AL/efx.h>)
#include <AL/efx.h>
#else
#include <efx.h>
#endif
#else
#include <efx.h>
#endif
#else
#ifndef AL_EFFECTSLOT_NULL
#define AL_EFFECTSLOT_NULL 0x0000
#endif
#ifndef AL_FILTER_NULL
#define AL_FILTER_NULL 0x0000
#endif
#ifndef AL_AUXILIARY_SEND_FILTER
#define AL_AUXILIARY_SEND_FILTER 0x20006
#endif
#ifndef AL_DIRECT_FILTER
#define AL_DIRECT_FILTER 0x20005
#endif
#ifndef AL_AIR_ABSORPTION_FACTOR
#define AL_AIR_ABSORPTION_FACTOR 0x20007
#endif
#ifndef AL_MIN_AIR_ABSORPTION_FACTOR
#define AL_MIN_AIR_ABSORPTION_FACTOR 0.0f
#endif
#ifndef AL_MAX_AIR_ABSORPTION_FACTOR
#define AL_MAX_AIR_ABSORPTION_FACTOR 10.0f
#endif
#ifndef AL_LOWPASS_GAIN
#define AL_LOWPASS_GAIN 0x0001
#endif
#ifndef AL_LOWPASS_GAINHF
#define AL_LOWPASS_GAINHF 0x0002
#endif
#endif

#include "EFXPresets.h"
#include "System/UnorderedMap.hpp"

/// Default sound effects system implementation
class CEFX
{
public:
#if RECOIL_OPENAL_HAS_EFX
	void Init(ALCdevice *device);
	void Kill();
	void ResetState()
	{
		updates = 0;
		maxSlots = 0;

		enabled = false;
		supported = false;

		sfxProperties = {};

		sfxSlot = 0;
		sfxReverb = 0;
		sfxFilter = 0;
		maxSlotsPerSource = 0;

		airAbsorptionFactor = 0.0f;
		heightRolloffModifier = 1.0f;

		effectsSupported.clear();
		filtersSupported.clear();
	}

	void SetPreset(const std::string &name, bool verbose = true, bool commit = true);
	void CommitEffects(const EAXSfxProps *sfxProps = nullptr);

	void Enable();
	void Disable();

	void SetHeightRolloffModifer(float mod);

	bool Enabled() const { return enabled; }
	bool Supported() const { return supported; }

public:
	/// @see ConfigHandler::ConfigNotifyCallback
	void ConfigNotify(const std::string &key, const std::string &value);

	void SetAirAbsorptionFactor(ALfloat value);
	ALfloat GetAirAbsorptionFactor() const { return airAbsorptionFactor; }
#else
	void Init(ALCdevice * /*device*/) {}
	void Kill() {}
	void ResetState()
	{
		updates = 0;
		maxSlots = 0;

		enabled = false;
		supported = false;

		sfxProperties = {};

		sfxSlot = 0;
		sfxReverb = 0;
		sfxFilter = 0;
		maxSlotsPerSource = 0;

		airAbsorptionFactor = 0.0f;
		heightRolloffModifier = 1.0f;
	}

	void SetPreset(const std::string & /*name*/, bool /*verbose*/ = true, bool /*commit*/ = true) {}
	void CommitEffects(const EAXSfxProps *sfxProps = nullptr)
	{
		if (sfxProps != nullptr)
			sfxProperties = *sfxProps;
	}

	void Enable() {}
	void Disable() {}

	void SetHeightRolloffModifer(float mod) { heightRolloffModifier = mod; }

	bool Enabled() const { return false; }
	bool Supported() const { return false; }

	void ConfigNotify(const std::string & /*key*/, const std::string & /*value*/) {}

	void SetAirAbsorptionFactor(ALfloat value) { airAbsorptionFactor = value; }
	ALfloat GetAirAbsorptionFactor() const { return airAbsorptionFactor; }
#endif

public:
	int updates = 0;
	int maxSlots = 0;

	bool enabled = false;
	bool supported = false;

	EAXSfxProps sfxProperties;

	ALuint sfxSlot = 0;
	ALuint sfxReverb = 0;
	ALuint sfxFilter = 0;
	ALuint maxSlotsPerSource = 0;

private:
	ALfloat airAbsorptionFactor = 0.0f;

	// reduces the rolloff when camera is high above the ground (so we still hear something in tab mode or far zoom)
	float heightRolloffModifier = 1.0f;

private:
	// information about the supported features
	spring::unsynced_map<ALuint, bool> effectsSupported;
	spring::unsynced_map<ALuint, bool> filtersSupported;
};

// initialized in Sound.cpp
extern CEFX efx;

#endif // _EFX_H_
