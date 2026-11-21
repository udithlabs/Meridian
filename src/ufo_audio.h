// Meridian: stereo-panned audio player for the Midnight flying saucer easter egg
// and full-track stereo jukebox for hidden Settings > About cheat codes.
#pragma once
#include <cstdint>

namespace ufoaudio {

enum class Track { None = 0, HippyShit = 1, Researched = 2 };

// Audio is disabled by default (keeps screen saver, --shot / --bench and unit tests completely silent).
void setEnabled(bool enabled);
// Temporarily suppress playback (e.g. while the timer or alarm chime is ringing).
void setSuppressed(bool suppressed);
// Called each rendered frame while the saucer is flying (xNorm = saucer X / width, u in [0..1]).
void update(uint64_t flightId, float xNorm, float u);
// Smoothly fades out the saucer fly-by audio when the saucer is no longer on screen (does not stop a jukebox track).
void stop();
// Toggles full unedited stereo playback of a hidden About-screen track. Returns the newly active track (Track::None if stopped).
Track toggleTrack(Track track);
// Toggles sequential jukebox playback across all embedded songs (Song 1 -> Song 2 -> stop). Returns true if started, false if stopped.
bool toggleJukebox();
// Returns true while sequential jukebox mode is active.
bool isJukebox();
// Smoothly stops any playing jukebox track (and exits jukebox mode).
void stopTrack();
// Returns whichever hidden jukebox track is currently active (Track::None when idle).
Track currentTrack();
// Immediately stops all playback and joins the background audio thread (call on app exit).
void shutdown();

} // namespace ufoaudio
