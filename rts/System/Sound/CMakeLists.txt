
# only one of the two targets needs this
remove_definitions(-DNO_SOUND)

# Dummy Sound implementation
set(noSoundSources
		IAudioChannel.cpp
		ISound.cpp
		Null/SoundChannels.cpp
		Null/NullSound.cpp
	)

add_library(no-sound STATIC EXCLUDE_FROM_ALL ${noSoundSources})
set_target_properties(no-sound PROPERTIES COMPILE_FLAGS "-DNO_SOUND")
target_link_libraries(no-sound Tracy::TracyClient)

# Define default sound implementation
if    (NO_SOUND)
	set(sound-impl no-sound PARENT_SCOPE)
else  (NO_SOUND)
	set(sound-impl sound PARENT_SCOPE)
endif (NO_SOUND)


# Real Sound implementation
if    (NOT NO_SOUND)
	include(CheckIncludeFileCXX)
	check_include_file_cxx("efx.h" RECOIL_HAVE_EFX_H)
	check_include_file_cxx("OpenAL/efx.h" RECOIL_HAVE_OPENAL_EFX_H)
	check_include_file_cxx("AL/efx.h" RECOIL_HAVE_AL_EFX_H)

	if (RECOIL_HAVE_EFX_H OR RECOIL_HAVE_OPENAL_EFX_H OR RECOIL_HAVE_AL_EFX_H)
		set(RECOIL_OPENAL_HAS_EFX 1)
	else ()
		set(RECOIL_OPENAL_HAS_EFX 0)
		message(STATUS "OpenAL EFX headers not found; building sound without EFX support")
	endif ()

	set(soundSources
			ISound.cpp
			IAudioChannel.cpp
			Null/NullSound.cpp
			OpenAL/ALShared.cpp
			OpenAL/EFX.cpp
			OpenAL/AudioChannel.cpp
			OpenAL/OggDecoder.cpp
			OpenAL/Mp3Decoder.cpp
			OpenAL/MusicStream.cpp
			OpenAL/Sound.cpp
			OpenAL/SoundChannels.cpp
			OpenAL/SoundBuffer.cpp
			OpenAL/SoundItem.cpp
			OpenAL/SoundSource.cpp
			OpenAL/VorbisShared.cpp
		)

	if (RECOIL_OPENAL_HAS_EFX)
		list(APPEND soundSources
			OpenAL/EFXfuncs.cpp
			OpenAL/EFXPresets.cpp
		)
	endif ()

	find_package_static(OpenAL 1.18.2 REQUIRED)
	find_package_static(OggVorbis 1.3.4 REQUIRED)

	find_package(SDL2 MODULE REQUIRED)

	include_directories(${CMAKE_SOURCE_DIR}/include/)
	include_directories(${CMAKE_SOURCE_DIR}/include/AL)

	add_library(sound STATIC EXCLUDE_FROM_ALL ${soundSources})
	target_compile_definitions(sound PRIVATE RECOIL_OPENAL_HAS_EFX=${RECOIL_OPENAL_HAS_EFX})
	target_link_libraries(sound SDL2::SDL2)
	target_link_libraries(sound vorbis::vorbisfile vorbis::vorbis Ogg::ogg)
	target_link_libraries(sound OpenAL::OpenAL)
	target_link_libraries(sound Tracy::TracyClient)
	target_link_libraries(sound nowide::nowide)
endif (NOT NO_SOUND)
