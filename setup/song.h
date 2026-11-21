// The song the setup plays while it installs, updates and uninstalls (see song_engine.h for how it is kept and why it cannot just be cut out).
// The player works on its own thread; nothing here ever blocks the window, and if the PC has no sound device it is simply silent.
#pragma once
#include <cstdint>
#include <string>

namespace song {

uint64_t key();                  // the number that unlocks the packed files (payload.h): a hash of the song inside this setup, 0 if there is none
const std::wstring& artist();    // as shown on the hidden panel of the Customise page ("" if the song is missing)
const std::wstring& title();
void start();                    // begins at the start of the song with a gentle fade in; does nothing if it is already playing
void stop();                     // fades out and ends (for an error)
void shutdown();                 // fades out, ends, and waits for the player thread (call before the program exits)
bool muted();
void setMuted(bool m);           // fades the sound out or back in; remembered in HKCU\Software\Meridian\Setup (the uninstaller removes it again)

} // namespace song
