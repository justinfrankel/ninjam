# libninjam — NINJAM Client Static Library

A minimal, buildable extraction of the [NINJAM](https://www.cockos.com/ninjam/) client engine (`NJClient`) as a static library. All GUI code, audio backends, and server code have been removed. Only the network protocol, audio encode/decode pipeline, and session management remain.

The public API surface is `ninjam/njclient.h`. Everything else in this repository is an implementation detail.

For downstream integration, you only need two deliverables:

- `njclient.h`
- the matching `njclient.lib`

Every build now also packages those client-facing deliverables into `output/` so consumers can copy from a single location instead of pulling files from `bin/` and `ninjam/` separately.

The `WDL/` tree remains a build-time implementation dependency for this repository, but consumers no longer need to copy any WDL headers into their own project just to include `njclient.h`.

---

## Repository Layout

```
ninjam/
  njclient.h        ← Public API (the only header you consume)
  njclient.cpp      ← Full engine implementation
  netmsg.h/.cpp     ← Network message framing
  mpb.h/.cpp        ← NINJAM protocol message parsers/builders
WDL/
  vorbisencdec.h    ← OGG Vorbis encoder/decoder wrapper
  jnetlib/          ← Minimal TCP networking (asyncdns, connection, util)
  *.h / sha.cpp     ← WDL support headers and crypto
njclient.vcxproj    ← MSBuild project (Visual Studio / MSBuild)
deps.props          ← OGG / Vorbis include path configuration
```

---

## Dependencies

| Dependency | Purpose | Install |
|---|---|---|
| **libvorbis** + **libvorbisenc** | OGG Vorbis audio codec | `vcpkg install libvorbis:x64-windows-static` |
| **libogg** | OGG bitstream framing | `vcpkg install libogg:x64-windows-static` |

**vcpkg** (recommended): install [vcpkg](https://github.com/microsoft/vcpkg) and run:

```bat
vcpkg install libogg:x64-windows-static libvorbis:x64-windows-static
```

**Manual**: download pre-built Windows binaries from https://xiph.org/downloads/ and configure the paths in `deps.props`.

---

## Building

### MSBuild (Windows)

1. **Configure dependency paths** in `deps.props`:

   ```xml
   <OggIncludeDir>C:\path\to\vcpkg\installed\x64-windows-static\include</OggIncludeDir>
   <VorbisIncludeDir>C:\path\to\vcpkg\installed\x64-windows-static\include</VorbisIncludeDir>
   ```

   With vcpkg both headers live under the same `include` directory, so both properties point to the same path.

2. **Build from the command line:**

   ```bat
   "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" ^
       njclient.vcxproj /p:Configuration=Release /p:Platform=x64
   ```

   Build both runtime variants explicitly:

   ```bat
   rem Release MT
   "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" ^
       njclient.vcxproj /p:Configuration=Release /p:Platform=x64 /p:NinjamRuntimeMode=Static

   rem Release MD
   "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" ^
       njclient.vcxproj /p:Configuration=Release /p:Platform=x64 /p:NinjamRuntimeMode=Dynamic
   ```

   Or for a Debug build:

   ```bat
   "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" ^
       njclient.vcxproj /p:Configuration=Debug /p:Platform=x64
   ```

   And for both Debug runtime variants:

   ```bat
   rem Debug MT
   "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" ^
       njclient.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:NinjamRuntimeMode=Static

   rem Debug MD
   "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" ^
       njclient.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:NinjamRuntimeMode=Dynamic
   ```

3. **Build output**:
   - `bin\x64\Release\MT\njclient.lib`
   - `bin\x64\Release\MD\njclient.lib`
   - `bin\x64\Debug\MT\njclient.lib`
   - `bin\x64\Debug\MD\njclient.lib`

4. **Packaged integration output**:
    - `output\njclient.h`
    - `output\x64\Release\MT\njclient.lib`
    - `output\x64\Release\MD\njclient.lib`
    - `output\x64\Debug\MT\njclient.lib`
    - `output\x64\Debug\MD\njclient.lib`
    - `output\x64\Debug\MT\njclient.pdb`
    - `output\x64\Debug\MD\njclient.pdb`

    Release builds do not produce PDBs. Debug builds package the matching `njclient.pdb` beside the library.

### Makefile (Linux / macOS)

```makefile
CXX     = g++
CC      = gcc
AR      = ar
CFLAGS  = -O2 -I. $(shell pkg-config --cflags ogg vorbis vorbisenc)
CXXFLAGS = $(CFLAGS) -std=c++11

SRCS = ninjam/njclient.cpp ninjam/netmsg.cpp ninjam/mpb.cpp \
       WDL/sha.cpp WDL/rng.cpp \
       WDL/jnetlib/connection.cpp WDL/jnetlib/asyncdns.cpp WDL/jnetlib/util.cpp

SRCS_C = WDL/win32_utf8.c

OBJS = $(SRCS:.cpp=.o) $(SRCS_C:.c=.o)

libnjclient.a: $(OBJS)
	$(AR) rcs $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) libnjclient.a
```

---

## Integrating into Your Project

### 1. Copy the packaged files

From `output/`, copy:

- `njclient.h`
- the matching `x64\<Configuration>\<MT|MD>\njclient.lib`
- the matching `njclient.pdb` for Debug builds if you want debugger symbols

Choose `MT` when your application links the static CRT and `MD` when it links the dynamic CRT.

### 2. Include the header

```cpp
#include "njclient.h"
```

No other NINJAM or WDL headers are required by consumers.

### 3. Connect to a server

```cpp
NJClient client;

// Called once when user accepts the license agreement:
client.LicenseAgreementCallback = [](void *, const char *) { return 1; };

// Receive chat messages:
client.ChatMessage_Callback = [](void *, NJClient *, const char **parms, int n) {
    // parms[0] = "MSG", parms[1] = username, parms[2] = text
};

client.Connect("hostname.example.com:2049", "username", "password");
```

### 4. Drive the Run loop (separate thread or timer, ≤50 ms cadence)

```cpp
// In your network/UI thread:
while (!client.Run()); // Run() returns 0 when it wants to be called again immediately
```

### 5. Drive audio from your audio thread

```cpp
// Called from your audio callback (e.g. JACK, PortAudio, CoreAudio):
// inbuf[ch] and outbuf[ch] are non-interleaved float arrays of `len` samples.
client.AudioProc(inbuf, innch, outbuf, outnch, len, sampleRate);
```

`AudioProc` handles all encoding, decoding, mixing, and metering internally.
It is safe to call from a dedicated audio thread concurrently with `Run()`.

### 6. Set up a local channel for transmit

```cpp
// Register channel 0 as a mono input from hardware input channel 0:
client.SetLocalChannelInfo(
    /*ch*/       0,
    /*name*/     "my channel",
    /*setsrcch*/ true,  /*srcch*/ 0,       // input channel index
    /*setbr*/    true,  /*br*/   64,        // kbps
    /*setbcast*/ true,  /*bcast*/true       // broadcasting on
);
```

### 7. Query remote user state (for a custom UI)

```cpp
int nusers = client.GetNumUsers();
for (int u = 0; u < nusers; u++) {
    float vol, pan;
    bool mute;
    const char *name = client.GetUserState(u, &vol, &pan, &mute);

    for (int ci = 0; (ci = client.EnumUserChannels(u, ci)) >= 0; ) {
        bool sub;
        const char *chname = client.GetUserChannelState(u, ci, &sub);
    }
}
```

---

## Notes

### Thread Safety

- **`AudioProc()`** must be called from your **audio thread only**. No mutex is required by the caller.
- **`Run()`** must be called from a **single dedicated thread** (not the audio thread).
- All other `Get*`/`Set*` methods on remote channel state should be called with `client.m_remotechannel_rd_mutex` held if called outside the `Run()` thread. `WDL_Mutex` and `WDL_MutexLock` are provided directly by `njclient.h`, so no extra mutex header is required.

### Working directory

`NJClient` writes temporary `.OGGv` segment files during a session. Call `SetWorkDir()` before `Connect()`:

```cpp
client.SetWorkDir("/tmp/ninjam-work");
```

Set `config_savelocalaudio = -1` to delete segment files as soon as they are decoded (saves disk space).

### Transmit support

Transmit (encoding and uploading local audio) is compiled in by default. To build a receive-only client, define `NJCLIENT_NO_XMIT_SUPPORT` in your compiler flags.

### Default port

NINJAM servers listen on TCP port **2049** by default. Pass it as part of the hostname string: `"hostname:2049"`.

### Sample rate

`NJClient` is sample-rate agnostic. Pass your actual hardware sample rate to `AudioProc()`. Common values: 44100, 48000.

### License

This code is licensed under the **GNU General Public License v2 or later** (see `LICENSE`). The WDL headers and JNetLib are provided under a permissive Zlib/WDL license. If you link this library into a proprietary project, the GPL terms apply to the linked result.
