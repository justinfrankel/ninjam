#include "precomp.h"

#include "attrs.h"

_int mainwnd_x("Main window X pos", 32);
_int mainwnd_y("Main window Y pos", 32);
_int mainwnd_w("Main window W", 800);
_int mainwnd_h("Main window H", 420);

_bool timestamp_chat("Timestamp chat lines", TRUE);
_bool chat_was_showing("Chat was showing", TRUE);
_string jesus_install_dir("Jesusonic install dir", "c:\\program files\\jesusonic");

_bool master_was_showing("Master showing", TRUE);
_bool metro_was_showing("Metronome showing", TRUE);
_bool audio_was_showing("Audio controls showing", TRUE);
_bool sweep_was_showing("Sweep showing", TRUE);

_bool metronome_was_muted("Metronome mute", TRUE);

_string channels_had("Channels had");

_string prev_servers("Previous servers");

_string asio_device("ASIO config");

_string main_session_dir("Session dir");
//
_string session_format("Session format(2)", "%Y%m%d_%H%M.ninjam");

_bool write_ogg("Write OGG", FALSE);
_int ogg_bitrate("OGG bitrate", 128);

_bool write_wav("Write WAV", FALSE);

_bool write_log("Write LOG", FALSE);
