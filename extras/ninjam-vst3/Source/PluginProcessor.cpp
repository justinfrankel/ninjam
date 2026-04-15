#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstring>

// ---- Video pipeline debug log ----
static void vlog(const char* msg)
{
    juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("ninjam_video_debug.txt");
    const juce::String line = juce::Time::getCurrentTime().toString(true, true, true, true)
        + "  " + juce::String::fromUTF8(msg) + "\n";
    f.appendText(line, false, false);
}
static void vlogStr(const juce::String& msg) { vlog(msg.toRawUTF8()); }
// ----------------------------------

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif


namespace
{
    constexpr unsigned int makeNjFourcc(const char a, const char b, const char c, const char d)
    {
        return ((unsigned int)(unsigned char)a) |
               ((unsigned int)(unsigned char)b << 8) |
               ((unsigned int)(unsigned char)c << 16) |
               ((unsigned int)(unsigned char)d << 24);
    }
    constexpr const char* opusSyncAppFamily = "ninjam-vst3";
    constexpr int opusSyncHandshakeVersion = 1;
    constexpr const char* opusSyncChatPrefix = "__NINJAM_VST3_OPUSSYNC__ ";
    // Custom FOURCC for opusSyncSupport broadcast via NINJAM interval channel
    // Any server routes it transparently; other clients ignore unknown FOURCCs
    constexpr unsigned int kOpusSyncFourcc    = makeNjFourcc('N','J','S','3');
    // Custom FOURCC for interval sync signals (intervalSyncTag, transportProbe, latencyReport)
    constexpr unsigned int kSyncSignalFourcc  = makeNjFourcc('N','J','S','4');
    constexpr const char* sideSignalChatPrefix = "__NINJAM_VST3_SIDESIGNAL__ ";
    constexpr int remoteLatencyUpdateCadenceIntervals = 1;

    juce::String normaliseOpusPeerId(juce::String userId)
    {
        userId = userId.trim();
        const int atPos = userId.indexOfChar('@');
        if (atPos > 0)
            userId = userId.substring(0, atPos);
        return userId.toLowerCase();
    }

    juce::String normaliseChatTargetNick(juce::String userId)
    {
        userId = userId.trim();
        const int atPos = userId.indexOfChar('@');
        if (atPos > 0)
            userId = userId.substring(0, atPos);
        const int colonPos = userId.lastIndexOfChar(':');
        if (colonPos >= 0 && colonPos < userId.length() - 1)
            userId = userId.substring(colonPos + 1);
        return userId.trim();
    }

    juce::String canonicalDelayUserKey(juce::String userId)
    {
        userId = normaliseOpusPeerId(userId);
        if (userId.startsWith("anonymous:"))
            userId = userId.substring(10);
        userId = userId.trim().toLowerCase();
        return userId;
    }

    juce::String getWrapperTypeName(juce::AudioProcessor::WrapperType wrapperType)
    {
        using WrapperType = juce::AudioProcessor::WrapperType;
        switch (wrapperType)
        {
            case WrapperType::wrapperType_Standalone: return "standalone";
            case WrapperType::wrapperType_VST: return "vst";
            case WrapperType::wrapperType_VST3: return "vst3";
            case WrapperType::wrapperType_AudioUnit: return "au";
            case WrapperType::wrapperType_AudioUnitv3: return "auv3";
            case WrapperType::wrapperType_AAX: return "aax";
            case WrapperType::wrapperType_LV2: return "lv2";
            default: break;
        }
        return "unknown";
    }

    inline float softClipSample(float x)
    {
        const float k = 2.0f;
        const float d = std::tanh(k);
        const float c = d / k;
        const float target = 0.891251f;

        float y = std::tanh(k * c * x);
        if (d != 0.0f)
            y = (y / d) * target;
        return y;
    }

    inline juce::String buildDefaultLocalChannelName(int channelIndex)
    {
        return "Ch" + juce::String(channelIndex + 1);
    }

    inline bool isDefaultLocalChannelName(const juce::String& name)
    {
        auto trimmed = name.trim();
        if (!trimmed.startsWithIgnoreCase("ch"))
            return false;

        auto numberPart = trimmed.substring(2).trim();
        if (numberPart.isEmpty() || !numberPart.containsOnly("0123456789"))
            return false;

        return numberPart.getIntValue() > 0;
    }

}

NinjamVst3AudioProcessor::NinjamVst3AudioProcessor()
     : AudioProcessor (BusesProperties()
                     .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                     .withInput  ("Input 2", juce::AudioChannelSet::stereo(), false)
                     .withInput  ("Input 3", juce::AudioChannelSet::stereo(), false)
                     .withInput  ("Input 4", juce::AudioChannelSet::stereo(), false)
                     .withInput  ("Input 5", juce::AudioChannelSet::stereo(), false)
                     .withInput  ("Input 6", juce::AudioChannelSet::stereo(), false)
                     .withInput  ("Input 7", juce::AudioChannelSet::stereo(), false)
                     .withInput  ("Input 8", juce::AudioChannelSet::stereo(), false)
                     .withOutput ("Output Main", juce::AudioChannelSet::stereo(), true)
                     .withOutput ("Output 2", juce::AudioChannelSet::stereo(), false)
                     .withOutput ("Output 3", juce::AudioChannelSet::stereo(), false)
                     .withOutput ("Output 4", juce::AudioChannelSet::stereo(), false)
                     .withOutput ("Output 5", juce::AudioChannelSet::stereo(), false)
                     .withOutput ("Output 6", juce::AudioChannelSet::stereo(), false)
                     .withOutput ("Output 7", juce::AudioChannelSet::stereo(), false)
                     .withOutput ("Output 8", juce::AudioChannelSet::stereo(), false)
                     .withOutput ("Output 9", juce::AudioChannelSet::stereo(), false)
                     .withOutput ("Output 10", juce::AudioChannelSet::stereo(), false)
                     .withOutput ("Output 11", juce::AudioChannelSet::stereo(), false)
                     .withOutput ("Output 12", juce::AudioChannelSet::stereo(), false)
                     .withOutput ("Output 13", juce::AudioChannelSet::stereo(), false)
                     .withOutput ("Output 14", juce::AudioChannelSet::stereo(), false)
                     .withOutput ("Output 15", juce::AudioChannelSet::stereo(), false)
                     .withOutput ("Output 16", juce::AudioChannelSet::stereo(), false)
                       )
{
    for (int i = 0; i < maxLocalChannels; ++i)
    {
        localChannelGains[(size_t)i].store(1.0f);
        localChannelPeaks[(size_t)i].store(0.0f);
        localChannelPeaksL[(size_t)i].store(0.0f);
        localChannelPeaksR[(size_t)i].store(0.0f);
        localChannelInputs[(size_t)i].store(-1);
        localChannelReverbSends[(size_t)i].store(0.0f);
        localChannelDelaySends[(size_t)i].store(0.0f);
        localChannelNames[(size_t)i] = buildDefaultLocalChannelName(i);
    }
    
    startTimer(20); // Run NINJAM client loop every 20ms

    // Set callbacks
    ninjamClient.LicenseAgreementCallback = LicenseAgreementCallback;
    ninjamClient.LicenseAgreement_User = this;

    ninjamClient.ChatMessage_Callback = ChatMessage_Callback;
    ninjamClient.ChatMessage_User = this;
    ninjamClient.IntervalMediaItem_Callback = IntervalMediaItem_Callback;
    ninjamClient.IntervalMediaItem_User = this;
    ninjamClient.IntervalChunkCallback = IntervalChunkCallback_cb;
    ninjamClient.IntervalChunkCallbackUser = this;
    ninjamClient.NewIntervalCallback = NewIntervalCallback_cb;
    ninjamClient.NewIntervalCallbackUser = this;
    opusSyncInstanceId = juce::Uuid().toString();
    
    // Default Metronome
    ninjamClient.config_metronome = 1.0f; // -12dB or similar? 1.0 is 0dB
    
    // Ensure disconnected state
    ninjamClient.Disconnect();

    // Initialize JNetLib (WSAStartup on Windows)
    JNL::open_socketlib();

    videoHelperRootDir = resolveVideoHelperRootDir();
    if (videoHelperRootDir.isDirectory())
        intervalJsonFile = videoHelperRootDir.getChildFile("intervals.json");
}

void NinjamVst3AudioProcessor::connectToServer(juce::String host, juce::String user, juce::String pass)
{
    host = host.trim();
    user = user.trim();
    pass = pass.trim();

    if (host.isEmpty())
        host = "127.0.0.1";

    if (user.isEmpty())
    {
        user = "anonymous:jammer";
        pass = "anon";
    }

    {
        const juce::ScopedLock lock(opusSyncPeerLock);
        opusSyncPeers.clear();
    }
    {
        const juce::ScopedLock lock(intervalSyncAnnouncementLock);
        lastAnnouncedRemoteIntervalByUser.clear();
        localIntervalStartMsByInterval.clear();
        pendingRemoteIntervalStartsByUser.clear();
        remoteTransportRttMsByUser.clear();
        pendingTransportProbeSentMsById.clear();
        remoteLatencyLastAppliedIntervalByUser.clear();
        remoteLatencyAverageByUser.clear();
        remoteLatencyFirmDelayMsByUser.clear();
    }
    opusSyncAvailable.store(false);
    opusSyncHasLegacyClients.store(false);
    lastOpusSupportBroadcastMs = 0.0;
    lastTransportProbeBroadcastMs = 0.0;

    applyCodecPreference();

    ninjamClient.Connect(host.toRawUTF8(), user.toRawUTF8(), pass.toRawUTF8());
    currentServer = host;
    currentUser = user;

    // Do NOT reset isTransmitting here — the user may have toggled it before
    // connecting. The NJC_STATUS_OK handler calls syncLocalIntervalChannelConfig()
    // which re-applies the current isTransmitting state to NJClient.
}

void NinjamVst3AudioProcessor::disconnectFromServer()
{
    ninjamClient.Disconnect();
    currentServer = {};
    currentUser = {};
    {
        const juce::ScopedLock lock(opusSyncPeerLock);
        opusSyncPeers.clear();
    }
    {
        const juce::ScopedLock lock(intervalSyncAnnouncementLock);
        lastAnnouncedRemoteIntervalByUser.clear();
        localIntervalStartMsByInterval.clear();
        pendingRemoteIntervalStartsByUser.clear();
        remoteTransportRttMsByUser.clear();
        pendingTransportProbeSentMsById.clear();
        remoteLatencyLastAppliedIntervalByUser.clear();
        remoteLatencyAverageByUser.clear();
        remoteLatencyFirmDelayMsByUser.clear();
    }
    opusSyncAvailable.store(false);
    opusSyncHasLegacyClients.store(false);
    applyCodecPreference();
}

void NinjamVst3AudioProcessor::sendChatMessage(juce::String msg)
{
    msg = msg.trim();
    if (msg.isEmpty())
        return;

    {
        juce::ScopedLock lock(chatLock);
        juce::String localLine = "Me: " + msg;
        chatHistory.add(localLine);
        chatSenders.add("me");
        if (chatHistory.size() > 100)
        {
            chatHistory.removeRange(0, chatHistory.size() - 100);
            chatSenders.removeRange(0, juce::jmax(0, chatSenders.size() - 100));
        }
    }

    if (ninjamClient.GetStatus() == NJClient::NJC_STATUS_OK)
        ninjamClient.ChatMessage_Send("MSG", msg.toRawUTF8());
    juce::Logger::writeToLog("NINJAM Chat (local): " + msg);
}

void NinjamVst3AudioProcessor::setMetronomeVolume(float vol)
{
    ninjamClient.config_metronome = vol;
}

float NinjamVst3AudioProcessor::getMetronomeVolume() const
{
    return ninjamClient.config_metronome;
}

bool NinjamVst3AudioProcessor::isOpusSyncAvailable() const
{
    return opusSyncAvailable.load();
}

juce::String NinjamVst3AudioProcessor::getIntervalSyncStatusText() const
{
    const juce::ScopedLock lock(intervalSyncStatusLock);
    return intervalSyncStatusText;
}

void NinjamVst3AudioProcessor::setIntervalSyncStatusText(const juce::String& text)
{
    const juce::ScopedLock lock(intervalSyncStatusLock);
    intervalSyncStatusText = text;
}

void NinjamVst3AudioProcessor::broadcastIntervalSyncTag(const juce::String& target)
{
    if (ninjamClient.GetStatus() != NJClient::NJC_STATUS_OK)
        return;

    const int displayInterval = getDisplayIntervalIndex();
    const int bpi = juce::jmax(1, getBPI());
    const float intervalProgress = juce::jlimit(0.0f, 1.0f, getIntervalProgress());
    const int beatIndex = juce::jlimit(0, bpi - 1, (int)std::floor(intervalProgress * (float)bpi));
    const juce::String userId = normaliseOpusPeerId(currentUser);
    const juce::String tag = buildIntervalSyncTag(displayInterval, bpi);

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("type", "intervalSyncTag");
    obj->setProperty("userId", userId.isNotEmpty() ? userId : currentUser);
    obj->setProperty("tag", tag);
    obj->setProperty("intervalIndex", displayInterval);
    obj->setProperty("intervalAbsolute", intervalIndex.load());
    obj->setProperty("bpi", bpi);
    obj->setProperty("beatIndex", beatIndex);
    obj->setProperty("intervalProgress", intervalProgress);
    obj->setProperty("eventId", "intervalTag:" + (userId.isNotEmpty() ? userId : currentUser) + ":" + juce::String(++sideSignalEventCounter));
    const juce::String payload = juce::JSON::toString(juce::var(obj.get()));
    const juce::String safeTarget = target.isNotEmpty() ? target : "*";
    sendIntervalSignal("intervalSyncTag", payload);
    return;
}

void NinjamVst3AudioProcessor::broadcastTransportProbe(const juce::String& target)
{
    if (ninjamClient.GetStatus() != NJClient::NJC_STATUS_OK)
        return;

    const juce::String userId = normaliseOpusPeerId(currentUser);
    const juce::String probeId = "probe:" + (userId.isNotEmpty() ? userId : currentUser) + ":" + juce::String(++transportProbeCounter);
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    {
        const juce::ScopedLock lock(intervalSyncAnnouncementLock);
        pendingTransportProbeSentMsById[probeId] = nowMs;
        while ((int)pendingTransportProbeSentMsById.size() > 256)
            pendingTransportProbeSentMsById.erase(pendingTransportProbeSentMsById.begin());
    }

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("type", "intervalTransportProbe");
    obj->setProperty("userId", userId.isNotEmpty() ? userId : currentUser);
    obj->setProperty("probeId", probeId);
    obj->setProperty("eventId", "transportProbe:" + probeId);
    const juce::String payload = juce::JSON::toString(juce::var(obj.get()));
    const juce::String safeTarget = target.isNotEmpty() ? target : "*";
    sendIntervalSignal("intervalTransportProbe", payload);
}

void NinjamVst3AudioProcessor::broadcastOpusSyncSupport(const juce::String& target)
{
    if (ninjamClient.GetStatus() != NJClient::NJC_STATUS_OK)
        return;

    const juce::String userId = normaliseOpusPeerId(currentUser);
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("type", "opusSyncSupport");
    obj->setProperty("userId", userId.isNotEmpty() ? userId : currentUser);
    obj->setProperty("clientId", opusSyncInstanceId);
    obj->setProperty("appFamily", opusSyncAppFamily);
    obj->setProperty("handshakeVersion", opusSyncHandshakeVersion);
    obj->setProperty("runtimeFormat", getWrapperTypeName(wrapperType));
    obj->setProperty("pluginName", juce::String(JucePlugin_Name));
    obj->setProperty("pluginVersion", juce::String(JucePlugin_VersionString));
    obj->setProperty("supportsOpus", true);
    obj->setProperty("enabled", numLocalChannels.load() > 1);
    obj->setProperty("numChannels", numLocalChannels.load());
    obj->setProperty("eventId", "opusSupport:" + (userId.isNotEmpty() ? userId : currentUser) + ":" + juce::String(++sideSignalEventCounter));
    const juce::String payload = juce::JSON::toString(juce::var(obj.get()));
    vlogStr("broadcastOpusSyncSupport -> target=" + (target.isNotEmpty() ? target : "*") + " enabled=" + juce::String(numLocalChannels.load() > 1 ? "true" : "false"));
    // Use NINJAM interval channel with custom FOURCC — works on any standard server,
    // routed like audio data, other clients silently ignore unknown FOURCCs.
    // Target is ignored here (interval data goes to all subscribers of our channel 0).
    juce::ignoreUnused(target);
    ninjamClient.SendRawIntervalItem(0, kOpusSyncFourcc, payload.toRawUTF8(), (int)payload.getNumBytesAsUTF8());
}

void NinjamVst3AudioProcessor::refreshOpusSyncAvailabilityFromUsers()
{
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    bool available = false;
    int freshPeerCount = 0;
    {
        const juce::ScopedLock lock(opusSyncPeerLock);
        for (auto it = opusSyncPeers.begin(); it != opusSyncPeers.end();)
        {
            const auto& peer = it->second;
            const bool isFresh = (nowMs - peer.lastSeenMs) <= 6500.0;
            if (peer.supportsOpus && isFresh)
                ++it;
            else
                it = opusSyncPeers.erase(it);
        }
        available = !opusSyncPeers.empty();
        freshPeerCount = (int)opusSyncPeers.size();
    }

    // Rebuild the quick username→multiChan snapshot (separate lock, no njclient calls)
    {
        const juce::ScopedLock lock2(opusSyncPeerLock);
        const juce::ScopedLock mcLock(peerMultiChanLock);
        peerMultiChanByName.clear();
        for (auto& [key, peer] : opusSyncPeers)
        {
            if (peer.supportsOpus && !peer.userId.isEmpty())
            {
                const juce::String snapKey = canonicalDelayUserKey(peer.userId);
                peerMultiChanByName[snapKey] = { peer.multiChanEnabled, peer.numChannels };
                vlogStr("[MCSnap] stored snapKey='" + snapKey + "' (userId='" + peer.userId + "') multiChan=" + juce::String(peer.multiChanEnabled ? 1 : 0) + " nCh=" + juce::String(peer.numChannels));
            }
        }
        vlogStr("[MCSnap] rebuild done mapSize=" + juce::String((int)peerMultiChanByName.size()));
    }

    const int remoteUserCount = juce::jmax(0, ninjamClient.GetNumUsers());
    const bool hasLegacyClients = remoteUserCount > freshPeerCount;

    const bool previous = opusSyncAvailable.exchange(available);
    const bool previousLegacy = opusSyncHasLegacyClients.exchange(hasLegacyClients);
    if (!previous && available)
    {
        juce::ScopedLock lock(chatLock);
        chatHistory.add("Multi-Channel Audio Detected.");
        chatSenders.add("");
        if (chatHistory.size() > 100)
        {
            chatHistory.removeRange(0, chatHistory.size() - 100);
            chatSenders.removeRange(0, juce::jmax(0, chatSenders.size() - 100));
        }
    }
    if (previous != available || previousLegacy != hasLegacyClients)
    {
        applyCodecPreference();
        syncLocalIntervalChannelConfig();
    }
}

void NinjamVst3AudioProcessor::setTransmitLocal(bool shouldTransmit)
{
    isTransmitting = shouldTransmit;
    syncLocalIntervalChannelConfig();
}

void NinjamVst3AudioProcessor::syncLocalIntervalChannelConfig()
{
    const bool shouldTransmit = isTransmitting;
    const int bitrate = shouldTransmit ? localBitrate : 24;
    const int flags = voiceChatMode ? 2 : 0;
    const int numCh = juce::jlimit(1, maxLocalChannels, numLocalChannels.load());
    const bool multiChanAuto = numCh > 1 && opusSyncAvailable.load() && shouldTransmit;

    if (multiChanAuto)
    {
        // NINJAM ch 0: Vorbis mixdown (for all clients including legacy)
        // NINJAM ch 1..N: Opus per-channel (for our VST3 clients only)
        juce::String ch0Name = getLocalChannelName(0);
        if (ch0Name.isEmpty()) ch0Name = "Mix";
        ninjamClient.SetLocalChannelInfo(0, ch0Name.toRawUTF8(),
            true, numCh,          // srcch = mix buffer at inputs[numCh]
            true, bitrate, true, true, false, 0, true, flags);
        for (int i = 0; i < numCh; ++i)
        {
            juce::String chName = getLocalChannelName(i);
            if (chName.isEmpty()) chName = "Ch " + juce::String(i + 1);
            ninjamClient.SetLocalChannelInfo(i + 1, chName.toRawUTF8(),
                true, i,          // srcch = original buffer slot i
                true, bitrate, true, true, false, 0, true, flags);
        }
        for (int i = numCh + 1; i <= maxLocalChannels; ++i)
            ninjamClient.DeleteLocalChannel(i);
    }
    else
    {
        // Vorbis only: single channel
        juce::String ch0Name = getLocalChannelName(0);
        if (ch0Name.isEmpty()) ch0Name = "Input";
        const int sourceChannel = shouldTransmit ? 0 : 1023;
        ninjamClient.SetLocalChannelInfo(0, ch0Name.toRawUTF8(),
            true, sourceChannel, true, bitrate, true, true, false, 0, true, flags);
        for (int i = 1; i <= maxLocalChannels; ++i)
            ninjamClient.DeleteLocalChannel(i);
    }

    if (ninjamClient.GetStatus() == NJClient::NJC_STATUS_OK)
        ninjamClient.NotifyServerOfChannelChange();
}

void NinjamVst3AudioProcessor::setLocalBitrate(int bitrate)
{
    localBitrate = bitrate;
    syncLocalIntervalChannelConfig();
}

int NinjamVst3AudioProcessor::getLocalBitrate() const
{
    return localBitrate;
}

void NinjamVst3AudioProcessor::setVoiceChatMode(bool enabled)
{
    voiceChatMode = enabled;
    syncLocalIntervalChannelConfig();
}

bool NinjamVst3AudioProcessor::isVoiceChatMode() const
{
    return voiceChatMode;
}

void NinjamVst3AudioProcessor::applyCodecPreference()
{
    const int numCh = juce::jlimit(1, maxLocalChannels, numLocalChannels.load());
    const bool multiChanAuto = numCh > 1 && opusSyncAvailable.load();
    const int decodeCaps = NJClient::NJCLIENT_CAP_DECODE_VORBIS | NJClient::NJCLIENT_CAP_DECODE_OPUS;

    if (multiChanAuto)
    {
        // ch 0: Vorbis only (mixdown for all clients)
        // ch 1..N: Opus only (per-channel for our VST3 clients)
        unsigned int vorbisMask = 0x1u;
        unsigned int opusMask = 0u;
        for (int i = 0; i < numCh; ++i)
            opusMask |= (1u << (i + 1));
        ninjamClient.SetCodecCapabilities(
            NJClient::NJCLIENT_CAP_ENCODE_VORBIS | NJClient::NJCLIENT_CAP_ENCODE_OPUS, decodeCaps);
        ninjamClient.SetCodecConfig(vorbisMask, opusMask);
    }
    else
    {
        // Single channel or no VST3 peers: Vorbis only
        ninjamClient.SetCodecCapabilities(NJClient::NJCLIENT_CAP_ENCODE_VORBIS, decodeCaps);
        ninjamClient.SetCodecConfig(0x1u, 0u);
    }
}

juce::String NinjamVst3AudioProcessor::buildIntervalSyncTag(int interval, int length) const
{
    const juce::String userPart = currentUser.isNotEmpty() ? currentUser : "unknown";
    return userPart + ":" + juce::String(interval) + ":" + juce::String(length);
}

juce::File NinjamVst3AudioProcessor::resolveVideoHelperRootDir() const
{
    juce::Array<juce::File> candidates;
    const juce::File exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    juce::File probe = exe.getParentDirectory();
    for (int i = 0; i < 8; ++i)
    {
        candidates.add(probe.getChildFile("advanced-vdo-client"));
        probe = probe.getParentDirectory();
    }
    candidates.add(juce::File("E:\\Web stuff\\NINJAM VST3\\advanced-vdo-client"));

    for (const auto& dir : candidates)
    {
        if (dir.isDirectory() && dir.getChildFile("index.html").existsAsFile() && dir.getChildFile("server.js").existsAsFile())
            return dir;
    }

    return {};
}

bool NinjamVst3AudioProcessor::isAdvancedVideoClientAvailable() const
{
#ifdef _WIN32
    HINTERNET hSession = WinHttpOpen(L"NINJAM_VST3/1.0",
                                     WINHTTP_ACCESS_TYPE_NO_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS,
                                     0);
    if (!hSession)
        return false;

    HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", 8100, 0);
    if (!hConnect)
    {
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect,
                                            L"HEAD",
                                            L"/",
                                            nullptr,
                                            WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            0);
    if (!hRequest)
    {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    const bool ok = WinHttpSendRequest(hRequest,
                                       WINHTTP_NO_ADDITIONAL_HEADERS,
                                       0,
                                       WINHTTP_NO_REQUEST_DATA,
                                       0,
                                       0,
                                       0) &&
                    WinHttpReceiveResponse(hRequest, nullptr);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
#else
    // Use JUCE's cross-platform TCP socket to probe 127.0.0.1:8100
    juce::StreamingSocket sock;
    if (!sock.connect("127.0.0.1", 8100, 500))
        return false;
    const juce::String req = "HEAD / HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n";
    sock.write(req.toRawUTF8(), (int)req.getNumBytesAsUTF8());
    char buf[16] = {};
    sock.read(buf, sizeof(buf) - 1, false);
    return juce::String(buf).startsWith("HTTP/");
#endif
}

bool NinjamVst3AudioProcessor::ensureAdvancedVideoClientStarted()
{
    if (isAdvancedVideoClientAvailable())
    {
        videoHelperRunning.store(true);
        return true;
    }

    if (advancedVideoProcess && advancedVideoProcess->isRunning())
    {
        for (int i = 0; i < 30; ++i)
        {
            juce::Thread::sleep(100);
            if (isAdvancedVideoClientAvailable())
            {
                videoHelperRunning.store(true);
                return true;
            }
        }
        return false;
    }

    const juce::File rootDir = resolveVideoHelperRootDir();
    const juce::File script = rootDir.getChildFile("server.js");
    if (!script.existsAsFile())
        return false;

    juce::StringArray nodeCandidates;
    const juce::File exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
#ifdef _WIN32
    const juce::String nodeFilename = "node.exe";
#else
    const juce::String nodeFilename = "node";
#endif
    const juce::File rootNode       = rootDir.getChildFile(nodeFilename);
    const juce::File rootParentNode = rootDir.getParentDirectory().getChildFile(nodeFilename);
    const juce::File exeNode        = exeDir.getChildFile(nodeFilename);
    if (rootNode.existsAsFile())
        nodeCandidates.add("\"" + rootNode.getFullPathName() + "\"");
    if (rootParentNode.existsAsFile())
        nodeCandidates.add("\"" + rootParentNode.getFullPathName() + "\"");
    if (exeNode.existsAsFile())
        nodeCandidates.add("\"" + exeNode.getFullPathName() + "\"");
    nodeCandidates.add("node");

    advancedVideoProcess = std::make_unique<juce::ChildProcess>();
    bool started = false;
    for (const auto& nodeCmd : nodeCandidates)
    {
        const juce::String cmd = nodeCmd + " \"" + script.getFullPathName() + "\"";
        if (advancedVideoProcess->start(cmd))
        {
            started = true;
            break;
        }
    }
    if (!started)
    {
        {
            juce::ScopedLock lock(chatLock);
            chatHistory.add("Video helper failed to start (node not found). Place node beside NINJAM executable or inside advanced-vdo-client.");
            chatSenders.add("");
            if (chatHistory.size() > 100)
            {
                chatHistory.removeRange(0, chatHistory.size() - 100);
                chatSenders.removeRange(0, juce::jmax(0, chatSenders.size() - 100));
            }
        }
        advancedVideoProcess.reset();
        return false;
    }

    for (int i = 0; i < 40; ++i)
    {
        juce::Thread::sleep(100);
        if (isAdvancedVideoClientAvailable())
        {
            videoHelperRunning.store(true);
            return true;
        }
    }
    return false;
}

void NinjamVst3AudioProcessor::stopAdvancedVideoClient()
{
    videoHelperRunning.store(false);
    if (advancedVideoProcess && advancedVideoProcess->isRunning())
        advancedVideoProcess->kill();
    advancedVideoProcess.reset();
}



void NinjamVst3AudioProcessor::launchVideoSession()
{
    if (ninjamClient.GetStatus() != NJClient::NJC_STATUS_OK)
        return;

    juce::String roomSource = currentServer.trim();
    const int schemePos = roomSource.indexOf("://");
    if (schemePos >= 0)
        roomSource = roomSource.substring(schemePos + 3);
    const int slashPos = roomSource.indexOfChar('/');
    if (slashPos >= 0)
        roomSource = roomSource.substring(0, slashPos);
    const int atPos = roomSource.lastIndexOfChar('@');
    if (atPos >= 0 && atPos + 1 < roomSource.length())
        roomSource = roomSource.substring(atPos + 1);

    juce::String hostPart = roomSource.trim();
    juce::String portPart;
    const int lastColonPos = hostPart.lastIndexOfChar(':');
    if (lastColonPos > 0 && lastColonPos + 1 < hostPart.length())
    {
        const juce::String candidatePort = hostPart.substring(lastColonPos + 1).trim();
        bool allDigits = candidatePort.isNotEmpty();
        for (int i = 0; i < candidatePort.length() && allDigits; ++i)
            allDigits = juce::CharacterFunctions::isDigit(candidatePort[i]);
        if (allDigits)
        {
            hostPart = hostPart.substring(0, lastColonPos);
            portPart = candidatePort;
        }
    }

    const int firstDotPos = hostPart.indexOfChar('.');
    if (firstDotPos > 0)
        hostPart = hostPart.substring(0, firstDotPos);

    juce::String roomRaw = hostPart;
    if (portPart.isNotEmpty())
        roomRaw << "_" << portPart;

    juce::String room;
    bool lastWasUnderscore = false;
    for (int i = 0; i < roomRaw.length(); ++i)
    {
        const juce_wchar ch = roomRaw[i];
        if (juce::CharacterFunctions::isLetterOrDigit(ch))
        {
            room << juce::String::charToString((juce_wchar) juce::CharacterFunctions::toLowerCase(ch));
            lastWasUnderscore = false;
        }
        else if (!lastWasUnderscore)
        {
            room << "_";
            lastWasUnderscore = true;
        }
    }
    room = room.trimCharactersAtStart("_").trimCharactersAtEnd("_");
    if (room.isEmpty())
        room = "ninjam_room";
    const juce::String label = currentUser.isNotEmpty() ? currentUser : "NINJAM";
    int viewDelayMs = 0;
    {
        const juce::ScopedLock lock(intervalSyncAnnouncementLock);
        for (const auto& entry : remoteLatencyFirmDelayMsByUser)
            viewDelayMs = juce::jmax(viewDelayMs, juce::jmax(0, entry.second));
    }
    const int chunkMs = juce::jlimit(60, 800, viewDelayMs > 0 ? (int)std::llround((double)viewDelayMs * 0.25) : 120);

    if (ensureAdvancedVideoClientStarted())
    {
        juce::URL helperUrl("http://127.0.0.1:8100/sync-buffer-room");
        helperUrl = helperUrl.withParameter("room", room)
                             .withParameter("label", label)
                             .withParameter("intervalSource", "ws://127.0.0.1:8100/ws")
                             .withParameter("chunked", juce::String(chunkMs));
        if (viewDelayMs > 0)
            helperUrl = helperUrl.withParameter("buffer", juce::String(viewDelayMs));
        {
            juce::ScopedLock lock(chatLock);
            chatHistory.add("Tip: If your cam isn't showing, refresh the video page and select your camera before entering the room.");
            chatSenders.add("");
            if (chatHistory.size() > 100)
            {
                chatHistory.removeRange(0, chatHistory.size() - 100);
                chatSenders.removeRange(0, juce::jmax(0, chatSenders.size() - 100));
            }
        }
        helperUrl.launchInDefaultBrowser();
        return;
    }

    juce::URL url("https://vdo.ninja/");
    url = url.withParameter("room", room)
             .withParameter("label", label)
             .withParameter("chunked", juce::String(chunkMs))
             .withParameter("chunkbufferadaptive", "0")
             .withParameter("chunkbufferceil", "180000")
             .withParameter("noaudio", "1")
             .withParameter("buffer2", "0");
    if (viewDelayMs > 0)
        url = url.withParameter("buffer", juce::String(viewDelayMs));
    {
        juce::ScopedLock lock(chatLock);
        chatHistory.add("Advanced sync helper unavailable on this machine; opening direct VDO view without live auto-buffer updates.");
        chatSenders.add("");
        chatHistory.add("Tip: If your cam isn't showing, refresh the video page and select your camera before entering the room.");
        chatSenders.add("");
        if (chatHistory.size() > 100)
        {
            chatHistory.removeRange(0, chatHistory.size() - 100);
            chatSenders.removeRange(0, juce::jmax(0, chatSenders.size() - 100));
        }
    }
    url.launchInDefaultBrowser();
}

void NinjamVst3AudioProcessor::writeIntervalHelperJson(int pos, int length)
{
    if (!videoHelperRunning.load())
        return;
    if (intervalJsonFile.getFullPathName().isEmpty())
        return;

    if (!intervalJsonFile.getParentDirectory().isDirectory())
        intervalJsonFile.getParentDirectory().createDirectory();

    const int safeLength = juce::jmax(1, length);
    const int displayInterval = getDisplayIntervalIndex();
    const int bpi = juce::jmax(1, getBPI());
    const double bpm = juce::jmax(1.0, (double)getBPM());
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const double globalUnit = (double)displayInterval * (double)safeLength + (double)juce::jlimit(0, safeLength, pos);
    const double beatLength = (double)safeLength / (double)bpi;
    const double globalBeat = beatLength > 0.0 ? std::floor(globalUnit / beatLength) : 0.0;
    const juce::String syncTag = buildIntervalSyncTag(displayInterval, safeLength);

    juce::Array<juce::var> entries;
    {
        juce::DynamicObject::Ptr infoObj = new juce::DynamicObject();
        infoObj->setProperty("type", "intervalInfo");
        infoObj->setProperty("interval", displayInterval);
        infoObj->setProperty("pos", pos);
        infoObj->setProperty("length", safeLength);
        infoObj->setProperty("bpm", bpm);
        infoObj->setProperty("bpi", bpi);
        infoObj->setProperty("globalUnit", globalUnit);
        infoObj->setProperty("globalBeat", globalBeat);
        infoObj->setProperty("videoClockMs", nowMs);
        infoObj->setProperty("syncTag", syncTag);
        infoObj->setProperty("bufferMode", "remote");
        entries.add(juce::var(infoObj.get()));
    }

    const int numUsers = ninjamClient.GetNumUsers();
    for (int userIdx = 0; userIdx < numUsers; ++userIdx)
    {
        const char* userNameChars = ninjamClient.GetUserState(userIdx, nullptr, nullptr, nullptr);
        if (!userNameChars || !userNameChars[0])
            continue;

        const juce::String userName = juce::String::fromUTF8(userNameChars);
        const juce::String senderKey = normaliseOpusPeerId(userName);
        const juce::String canonicalUserKey = canonicalDelayUserKey(userName);
        time_t lastUpdate = 0;
        double maxLen = 0.0;
        const double userPos = ninjamClient.GetUserSessionPos(userIdx, &lastUpdate, &maxLen);

        int bufferMs = -1;
        {
            const juce::ScopedLock lock(intervalSyncAnnouncementLock);
            auto firmIt = remoteLatencyFirmDelayMsByUser.find(senderKey);
            if (firmIt != remoteLatencyFirmDelayMsByUser.end())
                bufferMs = juce::jmax(0, firmIt->second);
            if (bufferMs < 0 && canonicalUserKey.isNotEmpty())
            {
                auto canonicalFirmIt = remoteLatencyFirmDelayMsByUser.find(canonicalUserKey);
                if (canonicalFirmIt != remoteLatencyFirmDelayMsByUser.end())
                    bufferMs = juce::jmax(0, canonicalFirmIt->second);
            }
            if (bufferMs < 0)
            {
                auto avgIt = remoteLatencyAverageByUser.find(senderKey);
                if (avgIt != remoteLatencyAverageByUser.end())
                {
                    const auto& state = avgIt->second;
                    double fallback = state.firmAverageMs;
                    if (!(fallback > 0.0))
                        fallback = state.averageMs;
                    if (!(fallback > 0.0))
                        fallback = state.lastMeasurementMs;
                    if (fallback > 0.0)
                        bufferMs = juce::jmax(0, (int)std::llround(fallback));
                }
            }
            if (bufferMs < 0 && canonicalUserKey.isNotEmpty())
            {
                auto canonicalAvgIt = remoteLatencyAverageByUser.find(canonicalUserKey);
                if (canonicalAvgIt != remoteLatencyAverageByUser.end())
                {
                    const auto& state = canonicalAvgIt->second;
                    double fallback = state.firmAverageMs;
                    if (!(fallback > 0.0))
                        fallback = state.averageMs;
                    if (!(fallback > 0.0))
                        fallback = state.lastMeasurementMs;
                    if (fallback > 0.0)
                        bufferMs = juce::jmax(0, (int)std::llround(fallback));
                }
            }
            // Diagnostic log per-user buffer decision
        }

        juce::DynamicObject::Ptr userObj = new juce::DynamicObject();
        userObj->setProperty("type", "videoTimecode");
        userObj->setProperty("userId", userName);
        userObj->setProperty("userKey", canonicalUserKey);
        userObj->setProperty("interval", displayInterval);
        userObj->setProperty("timecode", userPos);
        userObj->setProperty("globalUnit", (double)displayInterval * (double)safeLength + userPos);
        userObj->setProperty("globalBeat", globalBeat);
        userObj->setProperty("videoClockMs", nowMs);
        userObj->setProperty("syncTag", syncTag);
        userObj->setProperty("bufferMode", "remote");
        if (bufferMs >= 0)
        {
            userObj->setProperty("bufferTotalMs", (double)bufferMs);
            userObj->setProperty("senderBufferMs", 0.0);
            userObj->setProperty("receiverBufferMs", (double)bufferMs);
            userObj->setProperty("measuredAudioDelayMs", (double)bufferMs);
        }
        entries.add(juce::var(userObj.get()));
    }

    const juce::String payload = juce::JSON::toString(juce::var(entries), false);
    intervalJsonFile.replaceWithText(payload);
    // Also broadcast the interval payload over the sync interval channel so
    // other instances of our client can receive and write the same JSON
    // locally (avoids chat leakage on non-aware clients).
    sendIntervalSignal("intervals", payload);
}

bool NinjamVst3AudioProcessor::isTransmittingLocal() const
{
    return isTransmitting;
}

juce::StringArray NinjamVst3AudioProcessor::getChatMessages()
{
    juce::ScopedLock lock(chatLock);
    return chatHistory;
}

void NinjamVst3AudioProcessor::setAutoTranslateEnabled(bool shouldEnable)
{
    {
        juce::ScopedLock lock(chatLock);
        autoTranslate = shouldEnable;
    }
}

bool NinjamVst3AudioProcessor::isAutoTranslateEnabled() const
{
    return autoTranslate;
}

void NinjamVst3AudioProcessor::setTranslateTargetLang(const juce::String& langCode)
{
    juce::ScopedLock lock(chatLock);
    translateTargetLang = langCode;
}

juce::String NinjamVst3AudioProcessor::getTranslateTargetLang() const
{
    return translateTargetLang;
}

std::vector<NinjamVst3AudioProcessor::UserInfo> NinjamVst3AudioProcessor::getConnectedUsers()
{
    std::vector<UserInfo> users;
    int numUsers = ninjamClient.GetNumUsers();
    bool spread = spreadOutputsEnabled.load();

    const int maxOutputPairs = 16;
    std::set<int> reservedPairs;
    if (spread)
    {
        for (auto& kv : userOutputAssignment)
        {
            int pair = kv.second;
            if (pair >= 0 && pair < maxOutputPairs)
                reservedPairs.insert(pair);
        }
    }

    std::set<int> usedPairsThisCall;

    for (int i=0; i<numUsers; ++i)
    {
        const char* name = ninjamClient.GetUserState(i, nullptr, nullptr, nullptr);
        if (name)
        {
            UserInfo u;
            u.index = i;
            juce::String fullName = juce::String::fromUTF8(name);
            int atPos = fullName.indexOfChar('@');
            if (atPos > 0)
                u.name = fullName.substring(0, atPos);
            else
                u.name = fullName;
            
            bool sub = false;
            float chVol = 1.0f, chPan = 0.0f;
            bool chMute = false, chSolo = false;
            int outCh = 0, flags = 0;
            const char* chName = ninjamClient.GetUserChannelState(i, 0, &sub, &chVol, &chPan, &chMute, &chSolo, &outCh, &flags);
            if (chName)
            {
                float baseVol = chVol;
                bool hasStored = false;
                auto byNameIt = userVolumeByName.find(u.name);
                if (byNameIt != userVolumeByName.end())
                {
                    baseVol = byNameIt->second;
                    hasStored = true;
                }

                auto volIt = userBaseVolume.find(i);
                if (volIt != userBaseVolume.end())
                {
                    baseVol = volIt->second;
                    hasStored = true;
                }

                if (!hasStored)
                    baseVol = 1.0f;

                u.volume = baseVol;

                auto panIt = userPanOverrides.find(i);
                if (panIt != userPanOverrides.end())
                    u.pan = panIt->second;
                else
                    u.pan = chPan;

                u.isMuted = chMute;
                u.outputChannel = outCh;

                if (!hasStored || std::abs(baseVol - chVol) > 1.0e-4f)
                    setUserVolume(i, baseVol);
            }
            else
            {
                float baseVol = 1.0f;
                bool hasStored = false;
                auto byNameIt = userVolumeByName.find(u.name);
                if (byNameIt != userVolumeByName.end())
                {
                    baseVol = byNameIt->second;
                    hasStored = true;
                }

                auto volIt = userBaseVolume.find(i);
                if (volIt != userBaseVolume.end())
                {
                    baseVol = volIt->second;
                    hasStored = true;
                }

                u.volume = baseVol;

                u.pan = 0.0f;
                u.isMuted = false;
                u.outputChannel = ninjamClient.GetUserChannelOutput(i, 0);

                if (!hasStored)
                    setUserVolume(i, baseVol);
            }

            if (spread)
            {
                juce::String shortName = u.name;
                auto itAssign = userOutputAssignment.find(shortName);
                int desiredPair = -1;

                if (itAssign != userOutputAssignment.end())
                {
                    desiredPair = itAssign->second;
                }
                else
                {
                    if ((int)reservedPairs.size() < maxOutputPairs)
                    {
                        for (int cand = 0; cand < maxOutputPairs; ++cand)
                        {
                            if (!reservedPairs.count(cand))
                            {
                                desiredPair = cand;
                                reservedPairs.insert(cand);
                                break;
                            }
                        }
                    }
                    else
                    {
                        std::set<int> connectedNow = usedPairsThisCall;
                        int fallback = -1;
                        for (int cand = 0; cand < maxOutputPairs; ++cand)
                        {
                            if (!connectedNow.count(cand))
                            {
                                fallback = cand;
                                break;
                            }
                        }
                        if (fallback < 0)
                            fallback = 0;
                        desiredPair = fallback;
                    }

                    userOutputAssignment[shortName] = desiredPair;
                }

                if (desiredPair >= 0)
                {
                    int desiredChannel = desiredPair * 2;
                    if (u.outputChannel != desiredChannel)
                        setUserOutput(i, desiredChannel);
                    u.outputChannel = desiredChannel;
                    usedPairsThisCall.insert(desiredPair);
                }
            }

            userBaseVolume[i] = u.volume;
            userVolumeByName[u.name] = u.volume;

            // Look up multichannel state from the snapshot updated by refreshOpusSyncAvailabilityFromUsers().
            // This map is keyed by normalised username and never holds njclient locks.
            {
                const juce::String normName = canonicalDelayUserKey(u.name);
                const juce::ScopedLock mcLock(peerMultiChanLock);
                vlogStr("[MCLookup] normName='" + normName + "' mapSize=" + juce::String((int)peerMultiChanByName.size()));
                for (auto& [mk, mv] : peerMultiChanByName)
                    vlogStr("  key='" + mk + "' isMultiChan=" + juce::String(mv.isMultiChan ? 1 : 0));
                auto it = peerMultiChanByName.find(normName);
                if (it != peerMultiChanByName.end())
                {
                    u.isMultiChanPeer = it->second.isMultiChan;
                    if (u.isMultiChanPeer)
                        u.numChannels = juce::jmax(2, it->second.numChannels);
                }
            }

            // Populate channel names from NINJAM state (safe: no locks held here)
            if (u.isMultiChanPeer)
            {
                u.channelNames.clear();
                for (int ch = 0; ch < u.numChannels; ++ch)
                {
                    const char* chName = ninjamClient.GetUserChannelState(i, ch + 1); // ch0=mix, ch1..N=individual
                    if (chName != nullptr && *chName != '\0')
                        u.channelNames.add(juce::String::fromUTF8(chName));
                    else
                        u.channelNames.add("Ch " + juce::String(ch + 1));
                }
            }
            else
            {
                // Count basic NINJAM channel names for non-VST3 peers (display only, no expand button)
                u.channelNames.clear();
                for (int ch = 0; ch < 32; ++ch)
                {
                    const char* chName = ninjamClient.GetUserChannelState(i, ch);
                    if (chName != nullptr)
                    {
                        ++u.numChannels;
                        u.channelNames.add(juce::String::fromUTF8(chName));
                    }
                }
                if (u.numChannels < 1) { u.numChannels = 1; u.channelNames.add(""); }
            }
            
            users.push_back(u);
        }
    }
    // Log final result for each user
    for (const auto& u : users)
        vlogStr("[GCU] user='" + u.name + "' isMultiChanPeer=" + juce::String(u.isMultiChanPeer ? 1 : 0) + " nCh=" + juce::String(u.numChannels));
    return users;
}

void NinjamVst3AudioProcessor::rememberUserVolume(int userIndex, float volume, const juce::String& name)
{
    userBaseVolume[userIndex] = volume;
    juce::String shortName = name;
    int atPos = shortName.indexOfChar('@');
    if (atPos > 0)
        shortName = shortName.substring(0, atPos);
    userVolumeByName[shortName] = volume;
}

void NinjamVst3AudioProcessor::setUserOutput(int userIndex, int outputChannelIndex)
{
    // Update all channels for this user to the new output
    // Iterate through all potential channels (MAX_USER_CHANNELS is 32)
    for (int i = 0; i < 32; ++i)
    {
        // SetUserChannelState arguments: useridx, channelidx, setsub, sub, setvol, vol, setpan, pan, setmute, mute, setsolo, solo, setoutch, outchannel
        ninjamClient.SetUserChannelState(userIndex, i, false, false, false, 0, false, 0, false, false, false, false, true, outputChannelIndex);
    }

    const char* name = ninjamClient.GetUserState(userIndex, nullptr, nullptr, nullptr);
    if (name)
    {
        juce::String fullName = juce::String::fromUTF8(name);
        int atPos = fullName.indexOfChar('@');
        juce::String shortName;
        if (atPos > 0)
            shortName = fullName.substring(0, atPos);
        else
            shortName = fullName;
        int pairIndex = (outputChannelIndex & 1023) / 2;
        userOutputAssignment[shortName] = pairIndex;
    }
}

void NinjamVst3AudioProcessor::setUserLevel(int userIndex, float volume, float pan, bool isMuted, bool isSolo)
{
    userBaseVolume[userIndex] = volume;
    int numUsers = ninjamClient.GetNumUsers();
    if (userIndex >= 0 && userIndex < numUsers)
    {
        const char* name = ninjamClient.GetUserState(userIndex, nullptr, nullptr, nullptr);
        if (name)
        {
            juce::String fullName = juce::String::fromUTF8(name);
            int atPos = fullName.indexOfChar('@');
            juce::String shortName;
            if (atPos > 0)
                shortName = fullName.substring(0, atPos);
            else
                shortName = fullName;
            userVolumeByName[shortName] = volume;
        }
    }
    userPanOverrides[userIndex] = pan;
    for (int i = 0; i < 32; ++i)
    {
        ninjamClient.SetUserChannelState(userIndex, i, false, false, true, volume, true, pan, true, isMuted, true, isSolo);
    }
}

void NinjamVst3AudioProcessor::setUserVolume(int userIndex, float volume)
{
    for (int i = 0; i < 32; ++i)
    {
        ninjamClient.SetUserChannelState(userIndex, i, false, false, true, volume, false, 0, false, false, false, false, false, 0);
    }
}

float NinjamVst3AudioProcessor::getUserPeak(int userIndex, int channelIndex)
{
    if (isSyncToHostEnabled() && (!hostWasPlaying.load() || syncWaitForInterval.load()))
        return 0.0f;

    float maxPeak = 0.0f;
    for (int i = 0; i < 32; ++i)
    {
        float p = ninjamClient.GetUserChannelPeak(userIndex, i, channelIndex);
        if (p > maxPeak) maxPeak = p;
    }
    return maxPeak;
}

float NinjamVst3AudioProcessor::getUserChannelPeak(int userIndex, int njChanIdx, int lrSide)
{
    return ninjamClient.GetUserChannelPeak(userIndex, njChanIdx, lrSide);
}

void NinjamVst3AudioProcessor::setUserNjChannelVolume(int userIndex, int njChanIdx, float volume)
{
    ninjamClient.SetUserChannelState(userIndex, njChanIdx, false, false, true, volume, false, 0, false, false, false, false);
}

void NinjamVst3AudioProcessor::setMasterOutputGain(float gain)
{
    masterOutputGain.store(gain);
}

float NinjamVst3AudioProcessor::getMasterOutputGain() const
{
    return masterOutputGain.load();
}

float NinjamVst3AudioProcessor::getMasterPeak() const
{
    return masterPeak.load();
}

float NinjamVst3AudioProcessor::getMasterPeakLeft() const
{
    return masterPeakL.load();
}

float NinjamVst3AudioProcessor::getMasterPeakRight() const
{
    return masterPeakR.load();
}

void NinjamVst3AudioProcessor::setSoftLimiterEnabled(bool shouldEnable)
{
    softLimiterEnabled.store(shouldEnable);
}

bool NinjamVst3AudioProcessor::isSoftLimiterEnabled() const
{
    return softLimiterEnabled.load();
}

void NinjamVst3AudioProcessor::setUserClipEnabled(int userIndex, bool enabled)
{
    userClipEnabled[userIndex] = enabled;
}

bool NinjamVst3AudioProcessor::isUserClipEnabled(int userIndex) const
{
    return true;
}

void NinjamVst3AudioProcessor::setMasterLimiterEnabled(bool shouldEnable)
{
    dspLimiterEnabled.store(shouldEnable);
}

bool NinjamVst3AudioProcessor::isMasterLimiterEnabled() const
{
    return dspLimiterEnabled.load();
}

void NinjamVst3AudioProcessor::setLimiterThreshold(float db)
{
    limiterThresholdDb.store(db);
    masterLimiter.setThreshold(db);
}

void NinjamVst3AudioProcessor::setLimiterRelease(float ms)
{
    limiterReleaseMs.store(ms);
    masterLimiter.setRelease(ms);
}

void NinjamVst3AudioProcessor::setLocalInputGain(float gain)
{
    localInputGain.store(gain);
    setLocalChannelGain(0, gain);
}

float NinjamVst3AudioProcessor::getLocalInputGain() const
{
    return localChannelGains[0].load();
}

void NinjamVst3AudioProcessor::setNumLocalChannels(int num)
{
    const int previous = numLocalChannels.load();
    int clamped = juce::jlimit(1, maxLocalChannels, num);

    {
        juce::ScopedLock lock(localChannelNamesLock);
        for (int i = 0; i < maxLocalChannels; ++i)
        {
            auto& name = localChannelNames[(size_t)i];
            if (name.isEmpty() || isDefaultLocalChannelName(name))
                name = buildDefaultLocalChannelName(i);
        }
    }

    numLocalChannels.store(clamped);
    syncLocalIntervalChannelConfig();
    applyCodecPreference();

    // Post a local status message when transitioning into or out of multichannel
    if (previous != clamped)
    {
        juce::String msg;
        if (clamped > 1 && previous <= 1)
            msg = "MultiChannel mode enabled (" + juce::String(clamped) + " channels). Waiting for peer detection.";
        else if (clamped > 1)
            msg = "Local channels: " + juce::String(clamped) + ".";
        else
            msg = "MultiChannel mode disabled (single channel).";
        juce::ScopedLock lock(chatLock);
        chatHistory.add(msg);
        chatSenders.add("");
        if (chatHistory.size() > 100)
        {
            chatHistory.removeRange(0, chatHistory.size() - 100);
            chatSenders.removeRange(0, juce::jmax(0, chatSenders.size() - 100));
        }
    }

    // Immediately tell peers about the change so they update their expand buttons
    if (ninjamClient.GetStatus() == NJClient::NJC_STATUS_OK)
        broadcastOpusSyncSupport();
}

int NinjamVst3AudioProcessor::getNumLocalChannels() const
{
    return numLocalChannels.load();
}

void NinjamVst3AudioProcessor::setLocalChannelName(int channel, const juce::String& name)
{
    if (channel < 0 || channel >= maxLocalChannels) return;
    { juce::ScopedLock lock(localChannelNamesLock); localChannelNames[(size_t)channel] = name; }
    syncLocalIntervalChannelConfig();
}

juce::String NinjamVst3AudioProcessor::getLocalChannelName(int channel) const
{
    if (channel < 0 || channel >= maxLocalChannels) return {};
    juce::ScopedLock lock(localChannelNamesLock);
    return localChannelNames[(size_t)channel];
}

void NinjamVst3AudioProcessor::setLocalChannelGain(int channel, float gain)
{
    if (channel < 0 || channel >= maxLocalChannels)
        return;
    localChannelGains[(size_t)channel].store(gain);
}

float NinjamVst3AudioProcessor::getLocalChannelGain(int channel) const
{
    if (channel < 0 || channel >= maxLocalChannels)
        return 1.0f;
    return localChannelGains[(size_t)channel].load();
}

void NinjamVst3AudioProcessor::setLocalChannelInput(int channel, int inputIndex)
{
    if (channel < 0 || channel >= maxLocalChannels)
        return;
    localChannelInputs[(size_t)channel].store(inputIndex);
}

int NinjamVst3AudioProcessor::getLocalChannelInput(int channel) const
{
    if (channel < 0 || channel >= maxLocalChannels)
        return 0;
    return localChannelInputs[(size_t)channel].load();
}

float NinjamVst3AudioProcessor::getLocalChannelPeak(int channel) const
{
    if (channel < 0 || channel >= maxLocalChannels)
        return 0.0f;
    return localChannelPeaks[(size_t)channel].load();
}

float NinjamVst3AudioProcessor::getLocalChannelPeakLeft(int channel) const
{
    if (channel < 0 || channel >= maxLocalChannels)
        return 0.0f;
    return localChannelPeaksL[(size_t)channel].load();
}

float NinjamVst3AudioProcessor::getLocalChannelPeakRight(int channel) const
{
    if (channel < 0 || channel >= maxLocalChannels)
        return 0.0f;
    return localChannelPeaksR[(size_t)channel].load();
}

void NinjamVst3AudioProcessor::setLocalMonitorEnabled(bool enabled)
{
    localMonitorEnabled.store(enabled);
}

bool NinjamVst3AudioProcessor::isLocalMonitorEnabled() const
{
    return localMonitorEnabled.load();
}

void NinjamVst3AudioProcessor::setFxReverbEnabled(bool enabled)
{
    fxReverbEnabled.store(enabled);
}

bool NinjamVst3AudioProcessor::isFxReverbEnabled() const
{
    return fxReverbEnabled.load();
}

void NinjamVst3AudioProcessor::setFxDelayEnabled(bool enabled)
{
    fxDelayEnabled.store(enabled);
}

bool NinjamVst3AudioProcessor::isFxDelayEnabled() const
{
    return fxDelayEnabled.load();
}

void NinjamVst3AudioProcessor::setFxReverbRoomSize(float roomSize)
{
    fxReverbRoomSize.store(juce::jlimit(0.0f, 1.0f, roomSize));
}

float NinjamVst3AudioProcessor::getFxReverbRoomSize() const
{
    return fxReverbRoomSize.load();
}

void NinjamVst3AudioProcessor::setFxReverbDamping(float damping)
{
    fxReverbDamping.store(juce::jlimit(0.0f, 1.0f, damping));
}

float NinjamVst3AudioProcessor::getFxReverbDamping() const
{
    return fxReverbDamping.load();
}

void NinjamVst3AudioProcessor::setFxReverbWetDryMix(float wetDryMix)
{
    fxReverbWetDryMix.store(juce::jlimit(0.0f, 1.0f, wetDryMix));
}

float NinjamVst3AudioProcessor::getFxReverbWetDryMix() const
{
    return fxReverbWetDryMix.load();
}

void NinjamVst3AudioProcessor::setFxReverbEarlyReflections(float earlyReflections)
{
    fxReverbEarlyReflections.store(juce::jlimit(0.0f, 1.0f, earlyReflections));
}

float NinjamVst3AudioProcessor::getFxReverbEarlyReflections() const
{
    return fxReverbEarlyReflections.load();
}

void NinjamVst3AudioProcessor::setFxReverbTail(float tail)
{
    fxReverbTail.store(juce::jlimit(0.0f, 1.0f, tail));
}

float NinjamVst3AudioProcessor::getFxReverbTail() const
{
    return fxReverbTail.load();
}

void NinjamVst3AudioProcessor::setFxDelayTimeMs(float timeMs)
{
    fxDelayTimeMs.store(juce::jlimit(20.0f, 2000.0f, timeMs));
}

float NinjamVst3AudioProcessor::getFxDelayTimeMs() const
{
    return fxDelayTimeMs.load();
}

void NinjamVst3AudioProcessor::setFxDelaySyncToHost(bool enabled)
{
    fxDelaySyncToHost.store(enabled);
}

bool NinjamVst3AudioProcessor::isFxDelaySyncToHost() const
{
    return fxDelaySyncToHost.load();
}

void NinjamVst3AudioProcessor::setFxDelayDivision(int division)
{
    if (division != 1 && division != 8 && division != 16)
        division = 8;
    fxDelayDivision.store(division);
}

int NinjamVst3AudioProcessor::getFxDelayDivision() const
{
    return fxDelayDivision.load();
}

void NinjamVst3AudioProcessor::setFxDelayPingPong(bool enabled)
{
    fxDelayPingPong.store(enabled);
}

bool NinjamVst3AudioProcessor::isFxDelayPingPong() const
{
    return fxDelayPingPong.load();
}

void NinjamVst3AudioProcessor::setFxDelayWetDryMix(float wetDryMix)
{
    fxDelayWetDryMix.store(juce::jlimit(0.0f, 1.0f, wetDryMix));
}

float NinjamVst3AudioProcessor::getFxDelayWetDryMix() const
{
    return fxDelayWetDryMix.load();
}

void NinjamVst3AudioProcessor::setFxDelayFeedback(float feedback)
{
    fxDelayFeedback.store(juce::jlimit(0.0f, 0.95f, feedback));
}

float NinjamVst3AudioProcessor::getFxDelayFeedback() const
{
    return fxDelayFeedback.load();
}

void NinjamVst3AudioProcessor::setLocalChannelReverbSend(int channel, float send)
{
    if (channel < 0 || channel >= maxLocalChannels)
        return;
    localChannelReverbSends[(size_t)channel].store(juce::jlimit(0.0f, 1.0f, send));
}

float NinjamVst3AudioProcessor::getLocalChannelReverbSend(int channel) const
{
    if (channel < 0 || channel >= maxLocalChannels)
        return 0.0f;
    return localChannelReverbSends[(size_t)channel].load();
}

void NinjamVst3AudioProcessor::setLocalChannelDelaySend(int channel, float send)
{
    if (channel < 0 || channel >= maxLocalChannels)
        return;
    localChannelDelaySends[(size_t)channel].store(juce::jlimit(0.0f, 1.0f, send));
}

float NinjamVst3AudioProcessor::getLocalChannelDelaySend(int channel) const
{
    if (channel < 0 || channel >= maxLocalChannels)
        return 0.0f;
    return localChannelDelaySends[(size_t)channel].load();
}

int NinjamVst3AudioProcessor::getBPI()
{
    return ninjamClient.GetBPI();
}

float NinjamVst3AudioProcessor::getIntervalProgress()
{
    if (isSyncToHostEnabled() && (!hostWasPlaying.load() || syncWaitForInterval.load()))
        return 0.0f;

    int pos = 0;
    int length = 0;
    ninjamClient.GetPosition(&pos, &length);
    if (length > 0)
    {
        if (isSyncToHostEnabled() && hostWasPlaying.load())
        {
            int basePos = syncDisplayPositionOffset.load();
            int relativePos = pos - basePos;
            if (relativePos < 0)
                relativePos += length;
            return (float)relativePos / (float)length;
        }
        return (float)pos / (float)length;
    }
    return 0.0f;
}

float NinjamVst3AudioProcessor::getBPM()
{
    return ninjamClient.GetActualBPM();
}

int NinjamVst3AudioProcessor::getIntervalIndex() const
{
    return getDisplayIntervalIndex();
}

float NinjamVst3AudioProcessor::getLocalPeak() const
{
    return localPeak.load();
}

float NinjamVst3AudioProcessor::getLocalPeakLeft() const
{
    return localPeakL.load();
}

float NinjamVst3AudioProcessor::getLocalPeakRight() const
{
    return localPeakR.load();
}

void NinjamVst3AudioProcessor::sendSideSignal(const juce::String& target, const juce::String& type, const juce::String& payload)
{
    const char* tgt = target.isNotEmpty() ? target.toRawUTF8() : "*";
    ninjamClient.ChatMessage_Send("SIDE_SIGNAL", tgt, type.toRawUTF8(), payload.toRawUTF8());
}

void NinjamVst3AudioProcessor::sendIntervalSignal(const juce::String& type, const juce::String& payload)
{
    if (ninjamClient.GetStatus() != NJClient::NJC_STATUS_OK) return;
    // Wrap in {"sig":type, "data":payload} so the receiver knows the type
    juce::DynamicObject::Ptr wrapper = new juce::DynamicObject();
    wrapper->setProperty("sig", type);
    wrapper->setProperty("data", payload);
    const juce::String msg = juce::JSON::toString(juce::var(wrapper.get()));
    ninjamClient.SendRawIntervalItem(0, kSyncSignalFourcc, msg.toRawUTF8(), (int)msg.getNumBytesAsUTF8());
}

void NinjamVst3AudioProcessor::setSpreadOutputsEnabled(bool shouldEnable)
{
    bool wasEnabled = spreadOutputsEnabled.load();
    spreadOutputsEnabled.store(shouldEnable);

    if (wasEnabled && !shouldEnable)
    {
        userOutputAssignment.clear();

        int numUsers = ninjamClient.GetNumUsers();
        for (int userIdx = 0; userIdx < numUsers; ++userIdx)
        {
            for (int ch = 0; ch < 32; ++ch)
            {
                ninjamClient.SetUserChannelState(userIdx, ch,
                                                 false, false,
                                                 false, 0.0f,
                                                 false, 0.0f,
                                                 false, false,
                                                 false, false,
                                                 true, 0);
            }
        }
    }
}

bool NinjamVst3AudioProcessor::isSpreadOutputsEnabled() const
{
    return spreadOutputsEnabled.load();
}

int NinjamVst3AudioProcessor::getCodecMode() const
{
    const bool multiChanAuto = numLocalChannels.load() > 1 && opusSyncAvailable.load();
    if (!multiChanAuto)
        return 0;
    // multiChanAuto: always mixed mode (Vorbis ch0 + Opus ch1..N)
    return 1;
}

unsigned int NinjamVst3AudioProcessor::getVorbisMask() const
{
    return ninjamClient.GetCodecVorbisMask();
}

unsigned int NinjamVst3AudioProcessor::getOpusMask() const
{
    return ninjamClient.GetCodecOpusMask();
}

juce::String NinjamVst3AudioProcessor::translateText(const juce::String& text)
{
    if (!autoTranslate)
        return text;

#if defined(_WIN32)
    const wchar_t* host = L"api.mymemory.translated.net";

    HINTERNET hSession = WinHttpOpen(L"NINJAMVST3/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS,
                                     0);
    if (!hSession)
        return text;

    HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect)
    {
        WinHttpCloseHandle(hSession);
        return text;
    }

    juce::String target = translateTargetLang.isNotEmpty() ? translateTargetLang : "en";

    const char* srcUtf8 = text.toRawUTF8();
    juce::String targetCode = target.toLowerCase();
    const char* tgtUtf8 = targetCode.toRawUTF8();

    auto urlEncode = [](const char* s) -> std::string
    {
        std::string out;
        const unsigned char* p = (const unsigned char*)s;
        while (*p)
        {
            unsigned char c = *p++;
            if ((c >= 'A' && c <= 'Z') ||
                (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') ||
                c == '-' || c == '_' || c == '.' || c == '~')
            {
                out.push_back((char)c);
            }
            else if (c == ' ')
            {
                out.push_back('+');
            }
            else
            {
                char buf[4];
                std::snprintf(buf, sizeof(buf), "%%%02X", c);
                out.append(buf);
            }
        }
        return out;
    };

    std::string qParam = urlEncode(srcUtf8);
        std::string langpair = "auto|";
        langpair += tgtUtf8;
        std::string langpairParam = urlEncode(langpair.c_str());

    std::string pathStr = "/get?q=";
    pathStr += qParam;
    pathStr += "&langpair=";
    pathStr += langpairParam;

    std::wstring wpath(pathStr.begin(), pathStr.end());

    HINTERNET hRequest = WinHttpOpenRequest(hConnect,
                                            L"GET",
                                            wpath.c_str(),
                                            NULL,
                                            WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
    if (!hRequest)
    {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return text;
    }

    BOOL ok = WinHttpSendRequest(hRequest,
                                 WINHTTP_NO_ADDITIONAL_HEADERS,
                                 0,
                                 WINHTTP_NO_REQUEST_DATA,
                                 0,
                                 0,
                                 0);
    if (!ok)
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return text;
    }

    ok = WinHttpReceiveResponse(hRequest, NULL);
    if (!ok)
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return text;
    }

    std::string response;
    DWORD dwSize = 0;
    do
    {
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize) || dwSize == 0)
            break;

        std::string chunk;
        chunk.resize(dwSize);
        DWORD dwDownloaded = 0;
        if (!WinHttpReadData(hRequest, &chunk[0], dwSize, &dwDownloaded) || dwDownloaded == 0)
            break;

        response.append(chunk.data(), dwDownloaded);
    }
    while (dwSize > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (response.empty())
        return text;

    std::string translated;
    std::size_t keyPos = response.find("\"translatedText\"");
    if (keyPos != std::string::npos)
    {
        std::size_t colonPos = response.find(':', keyPos);
        if (colonPos != std::string::npos)
        {
            std::size_t firstQuote = response.find('\"', colonPos);
            if (firstQuote != std::string::npos)
            {
                std::size_t endQuote = firstQuote + 1;
                while (endQuote < response.size())
                {
                    char c = response[endQuote];
                    if (c == '\\')
                    {
                        if (endQuote + 1 < response.size())
                        {
                            char next = response[endQuote + 1];
                            if (next == '\\' || next == '\"')
                            {
                                translated.push_back(next);
                                endQuote += 2;
                                continue;
                            }
                        }
                    }
                    if (c == '\"')
                        break;

                    translated.push_back(c);
                    ++endQuote;
                }
            }
        }
    }

    if (translated.empty())
        return text;

    return juce::String::fromUTF8(translated.c_str(), (int)translated.size());
#else
    return text;
#endif
}

std::vector<NinjamVst3AudioProcessor::PublicServerInfo> NinjamVst3AudioProcessor::getPublicServers() const
{
    std::vector<PublicServerInfo> copy;
    const juce::ScopedLock lock(serverListLock);
    copy = publicServers;
    return copy;
}

void NinjamVst3AudioProcessor::refreshPublicServers()
{
    std::vector<PublicServerInfo> result;

#if defined(_WIN32)
    const wchar_t* host = L"ninbot.com";
    const wchar_t* path = L"/app/servers.php";

    HINTERNET hSession = WinHttpOpen(L"NINJAMVST3/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS,
                                     0);
    if (!hSession)
        return;

    HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTP_PORT, 0);
    if (!hConnect)
    {
        WinHttpCloseHandle(hSession);
        return;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect,
                                            L"GET",
                                            path,
                                            NULL,
                                            WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            0);
    if (!hRequest)
    {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return;
    }

    BOOL ok = WinHttpSendRequest(hRequest,
                                 WINHTTP_NO_ADDITIONAL_HEADERS,
                                 0,
                                 WINHTTP_NO_REQUEST_DATA,
                                 0,
                                 0,
                                 0);
    if (!ok)
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return;
    }

    ok = WinHttpReceiveResponse(hRequest, NULL);
    if (!ok)
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return;
    }

    std::string response;
    DWORD dwSize = 0;
    do
    {
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize) || dwSize == 0)
            break;

        std::string chunk;
        chunk.resize(dwSize);
        DWORD dwDownloaded = 0;
        if (!WinHttpReadData(hRequest, &chunk[0], dwSize, &dwDownloaded) || dwDownloaded == 0)
            break;

        response.append(chunk.data(), dwDownloaded);
    }
    while (dwSize > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (response.empty())
        return;

    juce::String jsonText = juce::String::fromUTF8(response.c_str(), (int)response.size());

    juce::var root;
    juce::Result parseError = juce::JSON::parse(jsonText, root);
    if (parseError.failed() || !root.isObject())
        return;

    auto* rootObj = root.getDynamicObject();
    if (!rootObj)
        return;

    juce::var serversVar = rootObj->getProperty("servers");
    if (!serversVar.isArray())
        return;

    auto* serversArray = serversVar.getArray();
    if (!serversArray)
        return;

    for (auto& serverVar : *serversArray)
    {
        if (!serverVar.isObject())
            continue;
        auto* obj = serverVar.getDynamicObject();
        if (!obj)
            continue;

        PublicServerInfo info;
        juce::String nameText = obj->getProperty("name").toString();
        info.name = nameText;

        int colon = nameText.lastIndexOfChar(':');
        if (colon > 0)
        {
            info.host = nameText.substring(0, colon);
            info.port = nameText.substring(colon + 1).getIntValue();
        }
        else
        {
            info.host = nameText;
            info.port = 2049;
        }

        info.bpi = obj->getProperty("bpi").toString().getIntValue();
        info.bpm = (float)obj->getProperty("bpm").toString().getDoubleValue();

        juce::var usersVar = obj->getProperty("users");
        if (usersVar.isArray() && usersVar.getArray() != nullptr)
            info.userCount = usersVar.getArray()->size();
        else
            info.userCount = obj->getProperty("user_count").toString().getIntValue();

        info.userMax = obj->getProperty("user_max").toString().getIntValue();
        result.push_back(info);
    }
#endif

    const juce::ScopedLock lock(serverListLock);
    publicServers.swap(result);
}

NinjamVst3AudioProcessor::~NinjamVst3AudioProcessor()
{
    stopTimer();
    stopAdvancedVideoClient();
    ninjamClient.Disconnect();
    JNL::close_socketlib();
}

const juce::String NinjamVst3AudioProcessor::getName() const
{
    return "NINJAM VST3";
}

bool NinjamVst3AudioProcessor::acceptsMidi() const
{
    return true;
}

bool NinjamVst3AudioProcessor::producesMidi() const
{
    return true;
}

bool NinjamVst3AudioProcessor::isMidiEffect() const
{
    return false;
}

double NinjamVst3AudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int NinjamVst3AudioProcessor::getNumPrograms()
{
    return 1;
}

int NinjamVst3AudioProcessor::getCurrentProgram()
{
    return 0;
}

void NinjamVst3AudioProcessor::setCurrentProgram (int index)
{
}

const juce::String NinjamVst3AudioProcessor::getProgramName (int index)
{
    return {};
}

void NinjamVst3AudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

void NinjamVst3AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    intervalSyncSampleCounter.store(0, std::memory_order_relaxed);
    processingSampleRate = sampleRate > 1.0 ? sampleRate : 44100.0;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels = (juce::uint32) getTotalNumOutputChannels();
    masterLimiter.prepare(spec);
    masterLimiter.setThreshold(limiterThresholdDb.load());
    masterLimiter.setRelease(limiterReleaseMs.load());
    masterLimiter.reset();

    fxReverb.reset();
    juce::Reverb::Parameters params;
    params.roomSize = fxReverbRoomSize.load();
    params.damping = 0.45f;
    params.width = 1.0f;
    params.wetLevel = 0.35f;
    params.dryLevel = 0.0f;
    params.freezeMode = 0.0f;
    fxReverb.setParameters(params);

    const int maxDelaySamples = juce::jmax(1, (int)std::ceil(processingSampleRate * 2.5));
    fxDelayBuffer.setSize(2, maxDelaySamples, false, true, true);
    fxDelayBuffer.clear();
    fxDelayWritePosition = 0;

    fxReverbInputBuffer.setSize(1, juce::jmax(1, samplesPerBlock), false, true, true);
    fxDelayInputBuffer.setSize(1, juce::jmax(1, samplesPerBlock), false, true, true);
    fxReturnBuffer.setSize(2, juce::jmax(1, samplesPerBlock), false, true, true);
}

void NinjamVst3AudioProcessor::releaseResources()
{
}

bool NinjamVst3AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    auto mainIn = layouts.getMainInputChannelSet();
    if (!mainIn.isDisabled()
        && mainIn != juce::AudioChannelSet::stereo()
        && mainIn != juce::AudioChannelSet::mono())
        return false;

    for (int i = 1; i < layouts.inputBuses.size(); ++i)
    {
        if (!layouts.inputBuses[i].isDisabled() && layouts.inputBuses[i] != juce::AudioChannelSet::stereo())
            return false;
    }

    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        if (!layouts.outputBuses[i].isDisabled() && layouts.outputBuses[i] != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

int NinjamVst3AudioProcessor::LicenseAgreementCallback(void* userData, const char* licensetext)
{
    // Auto-accept license for now (or log it)
    // Ideally, show a dialog to the user
    // Since this is called from Run(), which we call from timerCallback (UI thread),
    // we can show a message box.
    // However, for automation/testing, we might want to auto-accept.
    
    // Simple auto-accept for this proof of concept:
    juce::Logger::writeToLog("License Agreement Requested: " + juce::String(licensetext));
    return 1; 
}

void NinjamVst3AudioProcessor::processSyncSignal(const juce::String& sender, const juce::String& type, const juce::String& payload)
{
    if (type == "intervalLatencyReport")
        return;
    if (type == "intervalTransportProbe")
    {
        juce::String payloadUserId;
        juce::String probeId;
        const juce::var parsed = juce::JSON::parse(payload);
        if (auto* obj = parsed.getDynamicObject())
        {
            if (obj->hasProperty("userId"))
                payloadUserId = obj->getProperty("userId").toString();
            if (obj->hasProperty("probeId"))
                probeId = obj->getProperty("probeId").toString();
        }
        const juce::String senderKey = normaliseOpusPeerId(payloadUserId.isNotEmpty() ? payloadUserId : sender);
        const juce::String localUserKey = normaliseOpusPeerId(currentUser);
        if (probeId.isEmpty() || sender.isEmpty() || senderKey.isEmpty() || senderKey == localUserKey)
            return;

        juce::DynamicObject::Ptr ackObj = new juce::DynamicObject();
        ackObj->setProperty("type", "intervalTransportProbeAck");
        ackObj->setProperty("userId", localUserKey.isNotEmpty() ? localUserKey : currentUser);
        ackObj->setProperty("probeId", probeId);
        ackObj->setProperty("eventId", "transportProbeAck:" + probeId);
        const juce::String ackPayload = juce::JSON::toString(juce::var(ackObj.get()));
        sendIntervalSignal("intervalTransportProbeAck", ackPayload);
        return;
    }
    if (type == "intervalTransportProbeAck")
    {
        juce::String payloadUserId;
        juce::String probeId;
        const juce::var parsed = juce::JSON::parse(payload);
        if (auto* obj = parsed.getDynamicObject())
        {
            if (obj->hasProperty("userId"))
                payloadUserId = obj->getProperty("userId").toString();
            if (obj->hasProperty("probeId"))
                probeId = obj->getProperty("probeId").toString();
        }
        if (probeId.isEmpty())
            return;
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        const juce::String senderKey = normaliseOpusPeerId(payloadUserId.isNotEmpty() ? payloadUserId : sender);
        if (senderKey.isEmpty())
            return;
        const juce::String canonicalSenderKey = canonicalDelayUserKey(senderKey);
        const juce::ScopedLock lock(intervalSyncAnnouncementLock);
        auto sentIt = pendingTransportProbeSentMsById.find(probeId);
        if (sentIt == pendingTransportProbeSentMsById.end())
            return;
        const double rttMs = nowMs - sentIt->second;
        pendingTransportProbeSentMsById.erase(sentIt);
        if (rttMs <= 0.0 || rttMs > 3000.0)
            return;
        const auto updateRtt = [&](const juce::String& key)
        {
            if (key.isEmpty())
                return;
            auto it = remoteTransportRttMsByUser.find(key);
            if (it == remoteTransportRttMsByUser.end())
                remoteTransportRttMsByUser[key] = rttMs;
            else
                it->second = (it->second * 0.85) + (rttMs * 0.15);
        };
        updateRtt(senderKey);
        updateRtt(canonicalSenderKey);
        return;
    }
    if (type == "midiRelay")
    {
        juce::String payloadUserId;
        MidiControllerEvent event;
        const juce::var parsed = juce::JSON::parse(payload);
        if (auto* obj = parsed.getDynamicObject())
        {
            if (obj->hasProperty("userId")) payloadUserId = obj->getProperty("userId").toString();
            if (obj->hasProperty("isController")) event.isController = (bool)obj->getProperty("isController");
            if (obj->hasProperty("midiChannel")) event.midiChannel = (int)obj->getProperty("midiChannel");
            if (obj->hasProperty("number")) event.number = (int)obj->getProperty("number");
            if (obj->hasProperty("value")) event.value = (int)obj->getProperty("value");
            if (obj->hasProperty("normalized")) event.normalized = (float)(double)obj->getProperty("normalized");
            if (obj->hasProperty("isNoteOn")) event.isNoteOn = (bool)obj->getProperty("isNoteOn");
        }

        const juce::String senderKey = normaliseOpusPeerId(payloadUserId.isNotEmpty() ? payloadUserId : sender);
        const juce::String localUserKey = normaliseOpusPeerId(currentUser);
        if (senderKey.isEmpty() || senderKey == localUserKey)
            return;

        event.midiChannel = juce::jlimit(1, 16, event.midiChannel);
        event.number = juce::jlimit(0, 127, event.number);
        event.value = juce::jlimit(0, 127, event.value);
        event.normalized = juce::jlimit(0.0f, 1.0f, event.normalized);

        const juce::SpinLock::ScopedLockType lock(inboundMidiRelayQueueLock);
        pendingInboundMidiRelayEvents.push_back(event);
        if (pendingInboundMidiRelayEvents.size() > 512)
            pendingInboundMidiRelayEvents.erase(pendingInboundMidiRelayEvents.begin(), pendingInboundMidiRelayEvents.begin() + (long long)(pendingInboundMidiRelayEvents.size() - 512));
        return;
    }
    if (type == "localInputSelect")
    {
        const juce::var parsed = juce::JSON::parse(payload);
        if (auto* obj = parsed.getDynamicObject())
        {
            const int channel = obj->hasProperty("channel") ? (int)obj->getProperty("channel") : -1;
            const int inputIndex = obj->hasProperty("inputIndex") ? (int)obj->getProperty("inputIndex") : 0;
            if (channel >= 0 && channel < maxLocalChannels)
                setLocalChannelInput(channel, inputIndex);
        }
        return;
    }
    if (type == "intervals")
    {
        // payload is expected to be either an array of objects or a single object
        if (videoHelperRunning.load() && !intervalJsonFile.getFullPathName().isEmpty())
        {
            // write incoming payload to the helper file path
            intervalJsonFile.replaceWithText(payload);
            vlogStr("[INTSYNC] Received intervals payload from=" + sender + " written to " + intervalJsonFile.getFullPathName());
        }
        return;
    }
    if (type == "intervalSyncTag")
    {
        juce::String tag;
        juce::String payloadUserId;
        int remoteInterval = -1;
        int remoteIntervalAbsolute = -1;
        int remoteBpi = 0;
        int remoteBeat = -1;
        const juce::var parsed = juce::JSON::parse(payload);
        if (auto* obj = parsed.getDynamicObject())
        {
            if (obj->hasProperty("tag"))
                tag = obj->getProperty("tag").toString();
            if (obj->hasProperty("userId"))
                payloadUserId = obj->getProperty("userId").toString();
            if (obj->hasProperty("intervalIndex"))
                remoteInterval = (int)obj->getProperty("intervalIndex");
            if (obj->hasProperty("intervalAbsolute"))
                remoteIntervalAbsolute = (int)obj->getProperty("intervalAbsolute");
            if (obj->hasProperty("bpi"))
                remoteBpi = (int)obj->getProperty("bpi");
            if (obj->hasProperty("beatIndex"))
                remoteBeat = (int)obj->getProperty("beatIndex");
        }
        const int localInterval = getIntervalIndex();
        juce::String status = "Interval Tag " + sender;
        if (remoteInterval >= 0)
        {
            const int delta = remoteInterval - localInterval;
            status << " remoteInt " << juce::String(remoteInterval)
                   << " localInt " << juce::String(localInterval)
                   << " d=" << juce::String(delta);
        }
        if (remoteBeat >= 0 && remoteBpi > 0)
            status << " beat " << juce::String(remoteBeat + 1) << "/" << juce::String(remoteBpi);
        if (tag.isNotEmpty())
            status << " tag " << tag;
        setIntervalSyncStatusText(status);

        if (remoteInterval >= 0 && remoteBeat == 0)
        {
            const juce::String localUserKey = normaliseOpusPeerId(currentUser);
            const juce::String senderKey = normaliseOpusPeerId(payloadUserId.isNotEmpty() ? payloadUserId : sender);
            if (senderKey.isNotEmpty() && senderKey != localUserKey)
            {
                const int localBpi = juce::jmax(1, getBPI());
                const bool bpiMatches = (remoteBpi <= 0 || remoteBpi == localBpi);
                if (!bpiMatches)
                    return;
                bool shouldStorePending = false;
                const juce::String displaySender = sender.isNotEmpty() ? sender : (payloadUserId.isNotEmpty() ? payloadUserId : senderKey);
                const long long receivedSampleCount = intervalSyncSampleCounter.load(std::memory_order_relaxed);
                {
                    const juce::ScopedLock lock(intervalSyncAnnouncementLock);
                    auto it = lastAnnouncedRemoteIntervalByUser.find(senderKey);
                    if (it != lastAnnouncedRemoteIntervalByUser.end() && remoteInterval + 1 < it->second)
                        remoteLatencyAverageByUser.erase(senderKey);
                    if (it == lastAnnouncedRemoteIntervalByUser.end() || it->second != remoteInterval)
                    {
                        lastAnnouncedRemoteIntervalByUser[senderKey] = remoteInterval;
                        shouldStorePending = true;
                    }
                }

                if (shouldStorePending)
                {
                    const juce::ScopedLock lock(intervalSyncAnnouncementLock);
                    auto& pending = pendingRemoteIntervalStartsByUser[senderKey];
                    pending.remoteInterval = remoteInterval;
                    pending.remoteIntervalAbsolute = remoteIntervalAbsolute;
                    pending.displaySender = displaySender;
                    pending.receivedSampleCount = receivedSampleCount;
                    vlogStr("[INTTAG] Pending stored from=" + sender + " userId=" + (payloadUserId.isNotEmpty() ? payloadUserId : sender) + " senderKey=" + senderKey + " remoteInterval=" + juce::String(remoteInterval) + " remoteAbs=" + juce::String(remoteIntervalAbsolute) + " samples=" + juce::String(receivedSampleCount));
                }
            }
        }
        return;
    }
}

void NinjamVst3AudioProcessor::ChatMessage_Callback(void* userData, NJClient* inst, const char** parms, int nparms)
{
    auto* self = static_cast<NinjamVst3AudioProcessor*>(userData);
    auto processOpusSyncSupport = [self](const juce::String& sender, const juce::String& payload, juce::String* outEventId) -> bool
    {
        juce::Logger::writeToLog("processOpusSyncSupport from sender='" + sender + "'");
        vlogStr("processOpusSyncSupport from sender='" + sender + "' payload=" + payload.substring(0, 80));
        juce::var parsed = juce::JSON::parse(payload);
        bool supportsOpus = false;
        bool multiChanEnabled = false;
        int peerNumChannels = 1;
        juce::String userId = normaliseOpusPeerId(sender);
        juce::String clientId;
        juce::String appFamily;
        int handshakeVersion = 0;
        juce::String runtimeFormat;
        juce::String pluginVersion;
        if (auto* obj = parsed.getDynamicObject())
        {
            const juce::String supports = obj->getProperty("supportsOpus").toString();
            supportsOpus = supports == "1" || supports.equalsIgnoreCase("true");
            const juce::String enabledStr = obj->getProperty("enabled").toString();
            multiChanEnabled = enabledStr == "1" || enabledStr.equalsIgnoreCase("true");
            const juce::var numChVar = obj->getProperty("numChannels");
            if (!numChVar.isVoid()) peerNumChannels = juce::jmax(1, (int)numChVar);
            juce::String payloadUserId = obj->getProperty("userId").toString();
            if (payloadUserId.isNotEmpty())
                userId = normaliseOpusPeerId(payloadUserId);
            clientId = obj->getProperty("clientId").toString().trim();
            appFamily = obj->getProperty("appFamily").toString().trim();
            handshakeVersion = (int)obj->getProperty("handshakeVersion");
            runtimeFormat = obj->getProperty("runtimeFormat").toString().trim();
            pluginVersion = obj->getProperty("pluginVersion").toString().trim();
            if (outEventId != nullptr)
                *outEventId = obj->getProperty("eventId").toString();
        }
        else
            return false;

        const bool isLocalClient = clientId.isNotEmpty() ? (clientId == self->opusSyncInstanceId)
                                                          : (userId == normaliseOpusPeerId(self->currentUser));
        const bool sameAppFamily = appFamily.isEmpty() || appFamily == opusSyncAppFamily;
        const bool compatibleHandshake = handshakeVersion <= 0 || handshakeVersion == opusSyncHandshakeVersion;
        const juce::String peerKey = clientId.isNotEmpty() ? clientId : userId;
        if (peerKey.isNotEmpty() && userId.isNotEmpty() && !isLocalClient)
        {
            bool recognizedNow = false;
            juce::String recognizedMessage;
            {
                juce::ScopedLock lock(self->opusSyncPeerLock);
                if (supportsOpus && sameAppFamily && compatibleHandshake)
                {
                    const bool wasKnown = self->opusSyncPeers.find(peerKey) != self->opusSyncPeers.end();
                    auto& peer = self->opusSyncPeers[peerKey];
                    const bool wasMultiChan = peer.multiChanEnabled;
                    peer.userId = userId;
                    peer.supportsOpus = true;
                    peer.multiChanEnabled = multiChanEnabled;
                    peer.numChannels = peerNumChannels;
                    peer.appFamily = appFamily;
                    peer.handshakeVersion = handshakeVersion;
                    peer.runtimeFormat = runtimeFormat;
                    peer.pluginVersion = pluginVersion;
                    peer.lastSeenMs = juce::Time::getMillisecondCounterHiRes();
                    juce::String peerLabel = sender.isNotEmpty() ? sender : userId;
                    if (!wasKnown)
                    {
                        juce::String peerInfo = peer.runtimeFormat;
                        if (peer.pluginVersion.isNotEmpty())
                        {
                            if (peerInfo.isNotEmpty())
                                peerInfo << " ";
                            peerInfo << peer.pluginVersion;
                        }
                        recognizedMessage = "Multi Client Detected: " + peerLabel;
                        if (peerInfo.isNotEmpty())
                            recognizedMessage << " (" << peerInfo << ")";
                        if (multiChanEnabled)
                            recognizedMessage << " [MultiChannel ON]";
                        recognizedNow = true;
                    }
                    else if (multiChanEnabled && !wasMultiChan)
                    {
                        recognizedMessage = "MultiChannel Detected: " + peerLabel;
                        recognizedNow = true;
                    }
                    else if (!multiChanEnabled && wasMultiChan)
                    {
                        recognizedMessage = "MultiChannel Off: " + peerLabel;
                        recognizedNow = true;
                    }
                }
                else
                    self->opusSyncPeers.erase(peerKey);
            }
            if (recognizedNow)
            {
                juce::ScopedLock lock(self->chatLock);
                self->chatHistory.add(recognizedMessage);
                self->chatSenders.add("");
                if (self->chatHistory.size() > 100)
                {
                    self->chatHistory.removeRange(0, self->chatHistory.size() - 100);
                    self->chatSenders.removeRange(0, juce::jmax(0, self->chatSenders.size() - 100));
                }
            }
            vlogStr("[MCRefresh] processOpusSyncSupport calling refresh. sender='" + sender + "' userId='" + userId + "' multiChanEnabled=" + juce::String(multiChanEnabled ? 1 : 0) + " nCh=" + juce::String(peerNumChannels));
            self->refreshOpusSyncAvailabilityFromUsers();
        }
        return true;
    };
    auto processInboundSideSignal = [self, &processOpusSyncSupport](const juce::String& sender, const juce::String& type, const juce::String& payload, juce::String* outEventId) -> bool
    {
        if (type == "opusSyncSupport")
            return processOpusSyncSupport(sender, payload, outEventId);
        juce::ignoreUnused(outEventId);
        self->processSyncSignal(sender, type, payload);
        return true;
    };
    // nparms is the static array size (always 5); count only non-null entries
    {
        int actualNparms = 0;
        while (actualNparms < nparms && parms[actualNparms] != nullptr)
            ++actualNparms;
        nparms = actualNparms;
    }
    if (nparms > 0)
    {
        juce::String cmd = parms[0];
        vlogStr("ChatMsg cmd=" + cmd + " nparms=" + juce::String(nparms));
        juce::Logger::writeToLog("ChatMsg cmd=" + cmd + " nparms=" + juce::String(nparms));
        auto applyServerCaps = [self](const juce::String& capsText)
        {
            juce::Logger::writeToLog("SERVER_CAPS received: " + capsText);
            const juce::String caps = capsText.toLowerCase();
            const bool hasOpusSyncCap = caps.contains("opus_sync_v2")
                                     || caps.contains("hd_audio_v2")
                                     || caps.contains("hd_sync_v2");
            self->opusSyncServerSupported.store(hasOpusSyncCap);
            juce::Logger::writeToLog("opusSyncServerSupported -> " + juce::String(hasOpusSyncCap ? "true" : "false"));
            if (!hasOpusSyncCap)
                self->refreshOpusSyncAvailabilityFromUsers();
        };
        juce::String line;
        if (cmd == "SERVER_CAPS" && nparms >= 2)
        {
            applyServerCaps(juce::String(parms[1]));
            return;
        }
        bool isSideSignalCmd = (cmd == "SIDE_SIGNAL_FROM" && nparms >= 4)
                               || (cmd == "SIDE_SIGNAL" && nparms >= 4)
                               || (cmd == "VIDEO_SIGNAL_FROM" && nparms >= 4)
                               || (cmd == "VIDEO_SIGNAL" && nparms >= 4);
        if (isSideSignalCmd)
        {
            juce::String sender;
            juce::String type;
            juce::String payload;
            juce::String signalEventId;
            sender = nparms >= 2 ? juce::String(parms[1]) : juce::String();
            type = nparms >= 3 ? juce::String(parms[nparms - 2]) : juce::String();
            payload = nparms >= 2 ? juce::String(parms[nparms - 1]) : juce::String();
            if (type.isEmpty() || payload.isEmpty())
                return;

            processInboundSideSignal(sender, type, payload, &signalEventId);
            juce::String logLine = "NINJAM Side Signal From " + sender + " [" + type + "]";
            if (signalEventId.isNotEmpty())
                logLine += " eid=" + signalEventId;
            juce::Logger::writeToLog(logLine);
            return;
        }
        if ((cmd == "MSG" || cmd == "PRIVMSG") && nparms >= 3)
        {
            const juce::String sender = juce::String(parms[1]);
            const juce::String messageText = juce::String(parms[2]);
            if (messageText.startsWith(opusSyncChatPrefix))
            {
                vlogStr("MSG opusSyncChatPrefix MATCHED from " + sender);
                juce::Logger::writeToLog("MSG opusSyncChatPrefix received from " + sender);
            }
            const juce::String trimmedText = messageText.trim();
            if (sender == "*" && trimmedText.startsWithIgnoreCase("SERVER_CAPS"))
            {
                juce::String capsText = trimmedText.fromFirstOccurrenceOf("SERVER_CAPS", false, true).trim();
                if (capsText.startsWithChar(':'))
                    capsText = capsText.substring(1).trim();
                applyServerCaps(capsText);
                return;
            }
            if (messageText.startsWith(opusSyncChatPrefix))
            {
                juce::String signalEventId;
                const juce::String payload = messageText.fromFirstOccurrenceOf(opusSyncChatPrefix, false, false);
                if (processOpusSyncSupport(sender, payload, &signalEventId))
                {
                    juce::String logLine = "NINJAM Opus Sync Signal From " + sender + " [chat]";
                    if (signalEventId.isNotEmpty())
                        logLine += " eid=" + signalEventId;
                    juce::Logger::writeToLog(logLine);
                    return;
                }
            }
            bool isSideSignalChat = messageText.startsWith(sideSignalChatPrefix);
            if (isSideSignalChat)
            {
                const char* signalPrefix = sideSignalChatPrefix;
                const juce::String wrapperJson = messageText.fromFirstOccurrenceOf(signalPrefix, false, false);
                juce::var wrapped = juce::JSON::parse(wrapperJson);
                if (auto* wrappedObj = wrapped.getDynamicObject())
                {
                    const juce::String type = wrappedObj->getProperty("type").toString();
                    const juce::String payload = wrappedObj->getProperty("payload").toString();
                    if (type.isNotEmpty() && payload.isNotEmpty())
                    {
                        juce::String signalEventId;
                        if (processInboundSideSignal(sender, type, payload, &signalEventId))
                        {
                            juce::String logLine = "NINJAM Side Signal From " + sender + " [" + type + " chat]";
                            if (signalEventId.isNotEmpty())
                                logLine += " eid=" + signalEventId;
                            juce::Logger::writeToLog(logLine);
                            return;
                        }
                    }
                }
            }
        }

        auto cleanName = [](const char* raw) -> juce::String {
            return normaliseChatTargetNick(juce::String(raw));
        };

        juce::String lineSender;
        if (cmd == "MSG" && nparms >= 3)
        {
            // Suppress server echo of our own messages
            if (normaliseChatTargetNick(juce::String(parms[1])) == normaliseChatTargetNick(self->currentUser))
                return;
            juce::String name = cleanName(parms[1]);
            line = name + ": " + juce::String(parms[2]);
            lineSender = name;
        }
        else if (cmd == "PRIVMSG" && nparms >= 3)
        {
            juce::String name = cleanName(parms[1]);
            line = "(Private) " + name + ": " + juce::String(parms[2]);
            lineSender = name;
        }
        else if (cmd == "TOPIC" && nparms >= 2)
            line = "Topic: " + juce::String(parms[1]);
        else if (cmd == "JOIN" && nparms >= 2)
        {
             self->broadcastOpusSyncSupport(juce::String(parms[1]));
            self->broadcastIntervalSyncTag(juce::String(parms[1]));
             line = cleanName(parms[1]) + " has joined.";
        }
        else if (cmd == "PART" && nparms >= 2)
             line = cleanName(parms[1]) + " has left.";
        else
        {
            line = cmd;
            for (int i=1; i<nparms; ++i) 
                if (parms[i]) line += " " + juce::String(parms[i]);
        }
        {
            juce::String stored = self->translateText(line);
            juce::ScopedLock lock(self->chatLock);
            self->chatHistory.add(stored);
            self->chatSenders.add(lineSender);
            if (self->chatHistory.size() > 100)
            {
                self->chatHistory.removeRange(0, self->chatHistory.size() - 100);
                self->chatSenders.removeRange(0, juce::jmax(0, self->chatSenders.size() - 100));
            }
        }
        
        // Also log
        juce::Logger::writeToLog("NINJAM Chat: " + line);
    }
}

void NinjamVst3AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::AudioPlayHead::CurrentPositionInfo hostInfoAtBlock;
    bool gotHostPosition = false;
    if (auto* playHead = getPlayHead())
    {
        juce::AudioPlayHead::CurrentPositionInfo info;
        if (playHead->getCurrentPosition(info))
        {
            gotHostPosition = true;
            hostInfoAtBlock = info;
            const juce::ScopedLock lock(transportLock);
            lastHostPosition = info;
        }
    }
    int numSamples = buffer.getNumSamples();
    intervalSyncSampleCounter.fetch_add((long long)numSamples, std::memory_order_relaxed);
    const bool useHostMidiForLearn = getMidiLearnInputDeviceId().isEmpty();
    const bool useHostMidiForRelay = getMidiRelayInputDeviceId().isEmpty();
    {
        const juce::SpinLock::ScopedLockType midiQueueLock(midiEventQueueLock);
        const juce::SpinLock::ScopedLockType relayQueueLock(outboundMidiRelayQueueLock);
        for (const auto metadata : midiMessages)
        {
            const auto& msg = metadata.getMessage();
            if (msg.isController())
            {
                MidiControllerEvent event;
                event.isController = true;
                event.midiChannel = msg.getChannel();
                event.number = msg.getControllerNumber();
                event.value = msg.getControllerValue();
                event.normalized = (float)event.value / 127.0f;
                event.isNoteOn = event.value >= 64;
                if (useHostMidiForLearn)
                    pendingMidiControllerEvents.push_back(event);
                if (useHostMidiForRelay)
                    pendingOutboundMidiRelayEvents.push_back(event);
            }
            else if (msg.isNoteOnOrOff())
            {
                MidiControllerEvent event;
                event.isController = false;
                event.midiChannel = msg.getChannel();
                event.number = msg.getNoteNumber();
                event.value = msg.getVelocity();
                event.normalized = msg.isNoteOn() ? ((float)event.value / 127.0f) : 0.0f;
                event.isNoteOn = msg.isNoteOn();
                if (useHostMidiForLearn)
                    pendingMidiControllerEvents.push_back(event);
                if (useHostMidiForRelay)
                    pendingOutboundMidiRelayEvents.push_back(event);
            }
        }
        if (pendingMidiControllerEvents.size() > 512)
            pendingMidiControllerEvents.erase(pendingMidiControllerEvents.begin(), pendingMidiControllerEvents.begin() + (long long)(pendingMidiControllerEvents.size() - 512));
        if (pendingOutboundMidiRelayEvents.size() > 512)
            pendingOutboundMidiRelayEvents.erase(pendingOutboundMidiRelayEvents.begin(), pendingOutboundMidiRelayEvents.begin() + (long long)(pendingOutboundMidiRelayEvents.size() - 512));
    }
    injectInboundMidiRelayEvents(midiMessages);

    int totalInputChannels = 0;
    int numInputBuses = getBusCount(true);
    for (int bus = 0; bus < numInputBuses; ++bus)
    {
        int busChans = getChannelCountOfBus(true, bus);
        if (busChans <= 0)
            continue;
        totalInputChannels += busChans;
    }

    if (tempInputBuffer.getNumChannels() < totalInputChannels || tempInputBuffer.getNumSamples() < numSamples)
        tempInputBuffer.setSize(totalInputChannels, numSamples, false, false, true);

    int inputChanIndex = 0;
    for (int bus = 0; bus < numInputBuses; ++bus)
    {
        auto& busBuffer = getBusBuffer(buffer, true, bus);
        int busChans = busBuffer.getNumChannels();
        if (busChans <= 0)
            continue;
        for (int ch = 0; ch < busChans; ++ch)
        {
            if (inputChanIndex < totalInputChannels)
            {
                tempInputBuffer.copyFrom(inputChanIndex, 0, busBuffer, ch, 0, numSamples);
                ++inputChanIndex;
            }
        }
    }

    if (localChannelBuffer.getNumChannels() < maxLocalChannels || localChannelBuffer.getNumSamples() < numSamples)
        localChannelBuffer.setSize(maxLocalChannels, numSamples, false, false, true);

    int requestedLocal = numLocalChannels.load();
    int actualLocal = juce::jlimit(1, maxLocalChannels, requestedLocal);
    actualLocal = juce::jmin(actualLocal, totalInputChannels);
    std::array<int, maxLocalChannels> monitorSourceLeft{};
    std::array<int, maxLocalChannels> monitorSourceRight{};
    std::array<bool, maxLocalChannels> monitorStereo{};
    monitorSourceLeft.fill(-1);
    monitorSourceRight.fill(-1);
    monitorStereo.fill(false);

    float globalLocalMax = 0.0f;
    float globalLocalMaxL = 0.0f;
    float globalLocalMaxR = 0.0f;
    for (int ch = 0; ch < actualLocal; ++ch)
    {
        int srcIndex = localChannelInputs[(size_t)ch].load();
        int leftSource = -1;
        int rightSource = -1;

        if (srcIndex >= 0)
        {
            if (srcIndex >= totalInputChannels)
                srcIndex = juce::jlimit(0, totalInputChannels - 1, srcIndex);

            int left = juce::jlimit(0, juce::jmax(totalInputChannels - 1, 0), srcIndex);
            int right = left;

            localChannelBuffer.clear(ch, 0, numSamples);
            if (left < totalInputChannels)
                localChannelBuffer.copyFrom(ch, 0, tempInputBuffer, left, 0, numSamples);

            leftSource = left;
            rightSource = right;
        }
        else
        {
            int pairIndex = -1 - srcIndex;
            int left = pairIndex * 2;
            int right = left + 1;

            if (left < 0 || left >= totalInputChannels)
                left = juce::jlimit(0, juce::jmax(totalInputChannels - 1, 0), left);
            if (right < 0 || right >= totalInputChannels)
                right = left;

            localChannelBuffer.clear(ch, 0, numSamples);
            if (left < totalInputChannels)
                localChannelBuffer.addFrom(ch, 0, tempInputBuffer, left, 0, numSamples, 0.5f);
            if (right < totalInputChannels)
                localChannelBuffer.addFrom(ch, 0, tempInputBuffer, right, 0, numSamples, 0.5f);

            leftSource = left;
            rightSource = right;
            monitorStereo[(size_t)ch] = (right != left);
        }

        monitorSourceLeft[(size_t)ch] = leftSource;
        monitorSourceRight[(size_t)ch] = rightSource;

        float gain = localChannelGains[(size_t)ch].load();
        if (gain != 1.0f)
            localChannelBuffer.applyGain(ch, 0, numSamples, gain);

        const float* data = localChannelBuffer.getReadPointer(ch);
        float localMax = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            float a = std::abs(data[i]);
            if (a > localMax)
                localMax = a;
        }

        float localMaxL = 0.0f;
        float localMaxR = 0.0f;

        if (leftSource >= 0 && leftSource < totalInputChannels)
        {
            const float* leftData = tempInputBuffer.getReadPointer(leftSource);
            for (int i = 0; i < numSamples; ++i)
            {
                float a = std::abs(leftData[i] * gain);
                if (a > localMaxL)
                    localMaxL = a;
            }
        }

        if (rightSource >= 0 && rightSource < totalInputChannels)
        {
            const float* rightData = tempInputBuffer.getReadPointer(rightSource);
            for (int i = 0; i < numSamples; ++i)
            {
                float a = std::abs(rightData[i] * gain);
                if (a > localMaxR)
                    localMaxR = a;
            }
        }

        localChannelPeaks[(size_t)ch].store(localMax);
        localChannelPeaksL[(size_t)ch].store(localMaxL);
        localChannelPeaksR[(size_t)ch].store(localMaxR);
        if (localMax > globalLocalMax)
            globalLocalMax = localMax;
        if (localMaxL > globalLocalMaxL)
            globalLocalMaxL = localMaxL;
        if (localMaxR > globalLocalMaxR)
            globalLocalMaxR = localMaxR;
    }

    if (totalInputChannels > 0 && numSamples > 0)
    {
        const float* dev0 = tempInputBuffer.getReadPointer(0);
        const float* dev1 = tempInputBuffer.getNumChannels() > 1 ? tempInputBuffer.getReadPointer(1) : dev0;
        float devMax = 0.0f;
        float devMaxL = 0.0f;
        float devMaxR = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            float aL = std::abs(dev0[i]);
            float aR = std::abs(dev1[i]);
            float a = juce::jmax(aL, aR);
            if (a > devMax)
                devMax = a;
            if (aL > devMaxL)
                devMaxL = aL;
            if (aR > devMaxR)
                devMaxR = aR;
        }
        localChannelPeaks[0].store(devMax);
        localChannelPeaksL[0].store(devMaxL);
        localChannelPeaksR[0].store(devMaxR);
        if (devMax > globalLocalMax)
            globalLocalMax = devMax;
        if (devMaxL > globalLocalMaxL)
            globalLocalMaxL = devMaxL;
        if (devMaxR > globalLocalMaxR)
            globalLocalMaxR = devMaxR;
    }

    localPeak.store(globalLocalMax);
    localPeakL.store(globalLocalMaxL);
    localPeakR.store(globalLocalMaxR);

    const bool reverbOn = fxReverbEnabled.load();
    const bool delayOn = fxDelayEnabled.load();
    const bool fxSendActive = reverbOn || delayOn;

    if (fxTransmitBuffer.getNumSamples() < numSamples)
        fxTransmitBuffer.setSize(1, numSamples, false, true, true);
    if (fxReturnBuffer.getNumSamples() < numSamples)
        fxReturnBuffer.setSize(2, numSamples, false, true, true);
    fxTransmitBuffer.clear();
    fxReturnBuffer.clear();

    if (fxSendActive)
    {
        if (fxReverbInputBuffer.getNumSamples() < numSamples)
            fxReverbInputBuffer.setSize(1, numSamples, false, true, true);
        if (fxDelayInputBuffer.getNumSamples() < numSamples)
            fxDelayInputBuffer.setSize(1, numSamples, false, true, true);

        fxReverbInputBuffer.clear();
        fxDelayInputBuffer.clear();

        const int activeLocal = juce::jmin(actualLocal, numLocalChannels.load());
        for (int ch = 0; ch < activeLocal; ++ch)
        {
            const float reverbSend = localChannelReverbSends[(size_t)ch].load();
            const float delaySend = localChannelDelaySends[(size_t)ch].load();
            if (reverbSend <= 0.0001f && delaySend <= 0.0001f)
                continue;

            const float* src = localChannelBuffer.getReadPointer(ch);
            float* reverbDst = fxReverbInputBuffer.getWritePointer(0);
            float* delayDst = fxDelayInputBuffer.getWritePointer(0);
            for (int i = 0; i < numSamples; ++i)
            {
                const float v = src[i];
                if (reverbSend > 0.0001f)
                    reverbDst[i] += v * reverbSend;
                if (delaySend > 0.0001f)
                    delayDst[i] += v * delaySend;
            }
        }

        float* fxSendMono = fxTransmitBuffer.getWritePointer(0);
        float* fxLeft = fxReturnBuffer.getWritePointer(0);
        float* fxRight = fxReturnBuffer.getWritePointer(1);

        if (reverbOn)
        {
            juce::Reverb::Parameters params;
            params.roomSize = fxReverbRoomSize.load();
            params.damping = fxReverbDamping.load();
            params.width = 1.0f;
            params.wetLevel = 1.0f;
            params.dryLevel = 0.0f;
            params.freezeMode = 0.0f;
            fxReverb.setParameters(params);

            const float wetDryMix = fxReverbWetDryMix.load();
            const float earlyAmount = fxReverbEarlyReflections.load();
            const float tailAmount = fxReverbTail.load();
            const float* reverbIn = fxReverbInputBuffer.getReadPointer(0);
            float* revMono = fxReverbInputBuffer.getWritePointer(0);
            fxReverb.processMono(revMono, numSamples);
            for (int i = 0; i < numSamples; ++i)
            {
                const float early = reverbIn[i] * earlyAmount;
                const float tail = revMono[i] * tailAmount;
                const float wet = early + tail;
                const float mixed = wet * wetDryMix + reverbIn[i] * (1.0f - wetDryMix);
                const float out = mixed * 0.8f;
                fxLeft[i] += out;
                fxRight[i] += out;
                fxSendMono[i] += out * 0.5f;
            }
        }

        if (delayOn)
        {
            const int delayBufferSamples = fxDelayBuffer.getNumSamples();
            if (delayBufferSamples > 1)
            {
                const int division = fxDelayDivision.load();
                const double bpm = (double)getBPM();
                double targetDelaySeconds = fxDelayTimeMs.load() / 1000.0;
                if (fxDelaySyncToHost.load() && bpm > 1.0)
                    targetDelaySeconds = (60.0 / bpm) * (4.0 / (double)division);
                const int delaySamples = juce::jlimit(1, delayBufferSamples - 1, (int)std::round(targetDelaySeconds * processingSampleRate));

                const bool pingPong = fxDelayPingPong.load();
                const float feedback = juce::jlimit(0.0f, 0.95f, fxDelayFeedback.load());
                const float wetDryMix = juce::jlimit(0.0f, 1.0f, fxDelayWetDryMix.load());
                const float delayWet = wetDryMix * 0.8f;

                float* delayMemoryL = fxDelayBuffer.getWritePointer(0);
                float* delayMemoryR = fxDelayBuffer.getWritePointer(1);
                const float* delayIn = fxDelayInputBuffer.getReadPointer(0);

                int writePos = fxDelayWritePosition;
                for (int i = 0; i < numSamples; ++i)
                {
                    int readPos = writePos - delaySamples;
                    if (readPos < 0)
                        readPos += delayBufferSamples;

                    const float readL = delayMemoryL[readPos];
                    const float readR = delayMemoryR[readPos];
                    const float input = delayIn[i];
                    const float wetL = readL * delayWet;
                    const float wetR = readR * delayWet;

                    fxLeft[i] += wetL;
                    fxRight[i] += wetR;
                    fxSendMono[i] += (wetL + wetR) * 0.25f;

                    if (pingPong)
                    {
                        delayMemoryL[writePos] = input + readR * feedback;
                        delayMemoryR[writePos] = input + readL * feedback;
                    }
                    else
                    {
                        const float mono = 0.5f * (readL + readR);
                        delayMemoryL[writePos] = input + mono * feedback;
                        delayMemoryR[writePos] = input + mono * feedback;
                    }

                    ++writePos;
                    if (writePos >= delayBufferSamples)
                        writePos = 0;
                }
                fxDelayWritePosition = writePos;
            }
        }
    }

    // Determine active encoding mode:
    // - multiChanAuto: >1 local channels + VST3 peers → Vorbis mix on ch0, Opus per-ch on ch1..N
    // - otherwise:     Vorbis only, single channel (mix folded into ch0 above)
    const bool multiChanAuto = numLocalChannels.load() > 1 && opusSyncAvailable.load() && isTransmittingLocal();

    if (!multiChanAuto && actualLocal > 1)
    {
        float* dst = localChannelBuffer.getWritePointer(0);
        for (int ch = 1; ch < actualLocal; ++ch)
        {
            const float* src = localChannelBuffer.getReadPointer(ch);
            for (int s = 0; s < numSamples; ++s)
                dst[s] += src[s];
        }
    }

    if (!multiChanAuto && fxSendActive)
        localChannelBuffer.addFrom(0, 0, fxTransmitBuffer, 0, 0, numSamples);

    if (multiChanAuto)
    {
        if (localMixBuffer.getNumSamples() < numSamples)
            localMixBuffer.setSize(1, numSamples, false, true, true);
        float* mix = localMixBuffer.getWritePointer(0);
        const float* src0 = localChannelBuffer.getReadPointer(0);
        for (int s = 0; s < numSamples; ++s)
            mix[s] = src0[s];
        for (int ch = 1; ch < actualLocal; ++ch)
        {
            const float* src = localChannelBuffer.getReadPointer(ch);
            for (int s = 0; s < numSamples; ++s)
                mix[s] += src[s];
        }
        if (fxSendActive)
            localMixBuffer.addFrom(0, 0, fxTransmitBuffer, 0, 0, numSamples);
    }

    float* inputs[32] = {};
    int actualInputChannels;
    if (multiChanAuto)
    {
        const int n = juce::jmin(actualLocal, 30);
        for (int i = 0; i < n; ++i)
            inputs[i] = localChannelBuffer.getWritePointer(i);
        inputs[n] = fxTransmitBuffer.getWritePointer(0);
        inputs[n + 1] = localMixBuffer.getWritePointer(0);
        actualInputChannels = n + 2;
    }
    else
    {
        inputs[0] = localChannelBuffer.getWritePointer(0);
        actualInputChannels = 1;
    }

    float* outputs[32];
    int totalOutputChannels = 0;
    int numOutputBuses = getBusCount(false);
    for (int bus = 0; bus < numOutputBuses; ++bus)
    {
        int busChans = getChannelCountOfBus(false, bus);
        if (busChans <= 0)
            continue;
        totalOutputChannels += busChans;
    }

    int actualOutputChannels = juce::jmin(totalOutputChannels, 32);

    int outputChanIndex = 0;
    for (int bus = 0; bus < numOutputBuses; ++bus)
    {
        auto& busBuffer = getBusBuffer(buffer, false, bus);
        int busChans = busBuffer.getNumChannels();
        if (busChans <= 0)
            continue;
        for (int ch = 0; ch < busChans; ++ch)
        {
            if (outputChanIndex < actualOutputChannels)
            {
                outputs[outputChanIndex] = busBuffer.getWritePointer(ch);
                ++outputChanIndex;
            }
        }
    }

    for (int bus = 0; bus < numOutputBuses; ++bus)
    {
        auto& busBuffer = getBusBuffer(buffer, false, bus);
        int busChans = busBuffer.getNumChannels();
        if (busChans <= 0)
            continue;
        for (int ch = 0; ch < busChans; ++ch)
            busBuffer.clear(ch, 0, numSamples);
    }

    bool gateForSync = false;
    bool runMonitorOnly = false;
    if (isSyncToHostEnabled())
    {
        bool hostValid = gotHostPosition;
        bool hostPlaying = hostValid && hostInfoAtBlock.isPlaying;

        bool prev = hostWasPlaying.load();
        if (!hostValid || !hostPlaying)
        {
            hostWasPlaying.store(false);
            syncWaitForInterval.store(false);
            syncTargetInterval.store(-1);
            syncDisplayPositionOffset.store(0);
        }
        else if (!prev)
        {
            hostWasPlaying.store(true);
            ninjamClient.ResetTransportPhase();
            ninjamClient.ResetLocalBroadcastState();
            syncWaitForInterval.store(false);
            syncTargetInterval.store(-1);
            syncDisplayIntervalOffset.store(intervalIndex.load());
            syncDisplayPositionOffset.store(0);
        }

        if (!hostValid || !hostPlaying)
        {
            gateForSync = true;
        }
        runMonitorOnly = gateForSync;
    }
    else
    {
        hostWasPlaying.store(false);
        syncWaitForInterval.store(false);
        syncTargetInterval.store(-1);
        syncDisplayIntervalOffset.store(0);
        syncDisplayPositionOffset.store(0);
    }

    const bool monitorEnabled = localMonitorEnabled.load();
    const bool transmitEnabled = isTransmittingLocal();
    const bool allowEngineLocalInput = monitorEnabled || transmitEnabled;
    float** engineInputs = allowEngineLocalInput ? inputs : nullptr;
    int engineInputChannels = allowEngineLocalInput ? actualInputChannels : 0;
    ninjamClient.AudioProc(engineInputs, engineInputChannels, outputs, actualOutputChannels, numSamples, (int)getSampleRate(), runMonitorOnly);

    if (monitorEnabled && !transmitEnabled)
    {
        int numOutputBusesOut = getBusCount(false);
        if (numOutputBusesOut > 0)
        {
            auto& mainBus = getBusBuffer(buffer, false, 0);
            int outChans = mainBus.getNumChannels();
            int numLocal = juce::jmin(numLocalChannels.load(), maxLocalChannels);
            for (int ch = 0; ch < numLocal; ++ch)
            {
                const int outLeft = ch * 2;
                const int outRight = outLeft + 1;
                if (outChans <= 0)
                    break;
                const int sourceLeft = monitorSourceLeft[(size_t)ch];
                const int sourceRight = monitorSourceRight[(size_t)ch];
                const float gain = localChannelGains[(size_t)ch].load();
                if (sourceLeft < 0 || sourceLeft >= totalInputChannels)
                    continue;

                if (monitorStereo[(size_t)ch] && sourceRight >= 0 && sourceRight < totalInputChannels)
                {
                    if (outLeft < outChans)
                        mainBus.addFrom(outLeft, 0, tempInputBuffer, sourceLeft, 0, numSamples, gain);
                    if (outRight < outChans)
                        mainBus.addFrom(outRight, 0, tempInputBuffer, sourceRight, 0, numSamples, gain);
                    else if (outLeft == 0 && outChans == 1)
                    {
                        mainBus.addFrom(0, 0, tempInputBuffer, sourceLeft, 0, numSamples, gain * 0.5f);
                        mainBus.addFrom(0, 0, tempInputBuffer, sourceRight, 0, numSamples, gain * 0.5f);
                    }
                }
                else
                {
                    if (outLeft < outChans)
                        mainBus.addFrom(outLeft, 0, tempInputBuffer, sourceLeft, 0, numSamples, gain);
                    if (outRight < outChans)
                        mainBus.addFrom(outRight, 0, tempInputBuffer, sourceLeft, 0, numSamples, gain);
                    else if (outLeft == 0 && outChans == 1)
                        mainBus.addFrom(0, 0, tempInputBuffer, sourceLeft, 0, numSamples, gain);
                }
            }
        }
    }

    int mtcPos = 0;
    int mtcLength = 0;
    ninjamClient.GetPosition(&mtcPos, &mtcLength);
    emitMidiTimecode(midiMessages, numSamples, mtcPos, mtcLength);
    
    int numOutputBusesOut = getBusCount(false);

    if (gateForSync)
    {
        for (int bus = 0; bus < numOutputBusesOut; ++bus)
        {
            auto& busBuffer = getBusBuffer(buffer, false, bus);
            int busChans = busBuffer.getNumChannels();
            if (busChans <= 0)
                continue;
            for (int ch = 0; ch < busChans; ++ch)
                busBuffer.clear(ch, 0, numSamples);
        }
        masterPeak.store(0.0f);
        masterPeakL.store(0.0f);
        masterPeakR.store(0.0f);
        return;
    }

    if (numOutputBusesOut > 0 && fxSendActive)
    {
        auto& mainBus = getBusBuffer(buffer, false, 0);
        const int mainChans = mainBus.getNumChannels();
        if (mainChans >= 2)
        {
            mainBus.addFrom(0, 0, fxReturnBuffer, 0, 0, numSamples);
            mainBus.addFrom(1, 0, fxReturnBuffer, 1, 0, numSamples);
        }
        else if (mainChans == 1)
        {
            const float* l = fxReturnBuffer.getReadPointer(0);
            const float* r = fxReturnBuffer.getReadPointer(1);
            float* monoOut = mainBus.getWritePointer(0);
            for (int i = 0; i < numSamples; ++i)
                monoOut[i] += 0.5f * (l[i] + r[i]);
        }
    }

    float masterGain = masterOutputGain.load();
    if (masterGain != 1.0f)
    {
        for (int bus = 0; bus < numOutputBusesOut; ++bus)
        {
            auto& busBuffer = getBusBuffer(buffer, false, bus);
            int busChans = busBuffer.getNumChannels();
            for (int ch = 0; ch < busChans; ++ch)
                busBuffer.applyGain(ch, 0, numSamples, masterGain);
        }
    }

    bool limiter = dspLimiterEnabled.load() && (limiterThresholdDb.load() < 0.0f);
    if (limiter)
    {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        masterLimiter.process(context);
    }

    bool softClip = softLimiterEnabled.load();
    float maxSample = 0.0f;
    float maxSampleL = 0.0f;
    float maxSampleR = 0.0f;
    for (int bus = 0; bus < numOutputBusesOut; ++bus)
    {
        auto& busBuffer = getBusBuffer(buffer, false, bus);
        int busChans = busBuffer.getNumChannels();
        for (int ch = 0; ch < busChans; ++ch)
        {
            float* data = busBuffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
            {
                float v = data[i];
                if (softClip)
                    v = softClipSample(v);
                float a = std::abs(v);
                if (a > maxSample)
                    maxSample = a;
                if (bus == 0 && ch == 0 && a > maxSampleL)
                    maxSampleL = a;
                if (bus == 0 && ch == 1 && a > maxSampleR)
                    maxSampleR = a;
                data[i] = v;
            }
        }
    }
    if (numOutputBusesOut > 0)
    {
        auto& mainBus = getBusBuffer(buffer, false, 0);
        if (mainBus.getNumChannels() == 1)
            maxSampleR = maxSampleL;
        else if (mainBus.getNumChannels() == 0)
        {
            maxSampleL = maxSample;
            maxSampleR = maxSample;
        }
    }
    else
    {
        maxSampleL = maxSample;
        maxSampleR = maxSample;
    }
    masterPeak.store(maxSample);
    masterPeakL.store(maxSampleL);
    masterPeakR.store(maxSampleR);
}

// Called from NJClient::on_new_interval() in the AUDIO THREAD at sample-accurate timing.
void NinjamVst3AudioProcessor::NewIntervalCallback_cb(void* /*userData*/, NJClient* /*inst*/)
{
}

void NinjamVst3AudioProcessor::IntervalChunkCallback_cb(void* /*userData*/, NJClient* /*inst*/,
    const char* /*username*/, int /*chidx*/, unsigned int /*fourcc*/,
    const unsigned char* /*guid*/, const void* /*data*/, int /*dataLen*/, int /*flags*/)
{
}

void NinjamVst3AudioProcessor::IntervalMediaItem_Callback(void* userData, NJClient* /*inst*/,
    const char* username, int /*chidx*/, unsigned int fourcc,
    const unsigned char* /*guid*/, const void* data, int dataLen)
{
    if (!username || !data || dataLen <= 0) return;
    if (fourcc == kSyncSignalFourcc)
    {
        auto* self = static_cast<NinjamVst3AudioProcessor*>(userData);
        const juce::String sender = juce::String::fromUTF8(username);
        const juce::String msg    = juce::String::fromUTF8(static_cast<const char*>(data), dataLen);
        const juce::var parsed    = juce::JSON::parse(msg);
        if (auto* obj = parsed.getDynamicObject())
        {
            const juce::String type    = obj->getProperty("sig").toString();
            const juce::String payload = obj->getProperty("data").toString();
            if (type.isNotEmpty() && payload.isNotEmpty())
                self->processSyncSignal(sender, type, payload);
        }
        return;
    }
    if (fourcc != kOpusSyncFourcc) return;
    auto* self = static_cast<NinjamVst3AudioProcessor*>(userData);
    const juce::String sender = juce::String::fromUTF8(username);
    const juce::String payload = juce::String::fromUTF8(static_cast<const char*>(data), dataLen);
    vlogStr("IntervalMediaItem opusSyncFourcc from=" + sender);

    juce::var parsed = juce::JSON::parse(payload);
    bool supportsOpus = false;
    bool multiChanEnabled = false;
    int peerNumChannels = 1;
    juce::String userId = normaliseOpusPeerId(sender);
    juce::String clientId;
    juce::String appFamily;
    int handshakeVersion = 0;
    juce::String runtimeFormat;
    juce::String pluginVersion;
    if (auto* obj = parsed.getDynamicObject())
    {
        const juce::String supports = obj->getProperty("supportsOpus").toString();
        supportsOpus = supports == "1" || supports.equalsIgnoreCase("true");
        const juce::String enabledStr = obj->getProperty("enabled").toString();
        multiChanEnabled = enabledStr == "1" || enabledStr.equalsIgnoreCase("true");
        const juce::var numChVar = obj->getProperty("numChannels");
        if (!numChVar.isVoid()) peerNumChannels = juce::jmax(1, (int)numChVar);
        juce::String payloadUserId = obj->getProperty("userId").toString();
        if (payloadUserId.isNotEmpty())
            userId = normaliseOpusPeerId(payloadUserId);
        clientId = obj->getProperty("clientId").toString().trim();
        appFamily = obj->getProperty("appFamily").toString().trim();
        handshakeVersion = (int)obj->getProperty("handshakeVersion");
        runtimeFormat = obj->getProperty("runtimeFormat").toString().trim();
        pluginVersion = obj->getProperty("pluginVersion").toString().trim();
    }
    else { vlogStr("[MCExit1] JSON parse failed from=" + sender + " payloadLen=" + juce::String((int)payload.length()) + " first100=" + payload.substring(0, 100)); return; }

    const bool isLocalClient = clientId.isNotEmpty() ? (clientId == self->opusSyncInstanceId)
                                                      : (userId == normaliseOpusPeerId(self->currentUser));
    const bool sameAppFamily = appFamily.isEmpty() || appFamily == opusSyncAppFamily;
    const bool compatibleHandshake = handshakeVersion <= 0 || handshakeVersion == opusSyncHandshakeVersion;
    const juce::String peerKey = clientId.isNotEmpty() ? clientId : userId;
    vlogStr("[MCGuard] peerKey='" + peerKey + "' userId='" + userId + "' isLocal=" + juce::String(isLocalClient ? 1 : 0) + " sameFamily=" + juce::String(sameAppFamily ? 1 : 0) + " compatHS=" + juce::String(compatibleHandshake ? 1 : 0) + " supportsOpus=" + juce::String(supportsOpus ? 1 : 0));
    if (peerKey.isEmpty() || userId.isEmpty() || isLocalClient) return;

    bool recognizedNow = false;
    juce::String recognizedMessage;
    {
        juce::ScopedLock lock(self->opusSyncPeerLock);
        if (supportsOpus && sameAppFamily && compatibleHandshake)
        {
            const bool wasKnown = self->opusSyncPeers.find(peerKey) != self->opusSyncPeers.end();
            auto& peer = self->opusSyncPeers[peerKey];
            const bool wasMultiChan = peer.multiChanEnabled;
            peer.userId = userId;
            peer.supportsOpus = true;
            peer.multiChanEnabled = multiChanEnabled;
            peer.numChannels = peerNumChannels;
            peer.appFamily = appFamily;
            peer.handshakeVersion = handshakeVersion;
            peer.runtimeFormat = runtimeFormat;
            peer.pluginVersion = pluginVersion;
            peer.lastSeenMs = juce::Time::getMillisecondCounterHiRes();
            const juce::String peerLabel = sender.isNotEmpty() ? sender : userId;
            if (!wasKnown)
            {
                juce::String peerInfo = peer.runtimeFormat;
                if (peer.pluginVersion.isNotEmpty())
                {
                    if (peerInfo.isNotEmpty()) peerInfo << " ";
                    peerInfo << peer.pluginVersion;
                }
                recognizedMessage = "Multi Client Detected: " + peerLabel;
                if (peerInfo.isNotEmpty()) recognizedMessage << " (" << peerInfo << ")";
                if (multiChanEnabled) recognizedMessage << " [MultiChannel ON]";
                recognizedNow = true;
            }
            else if (multiChanEnabled && !wasMultiChan)
            {
                recognizedMessage = "MultiChannel Detected: " + peerLabel;
                recognizedNow = true;
            }
            else if (!multiChanEnabled && wasMultiChan)
            {
                recognizedMessage = "MultiChannel Off: " + peerLabel;
                recognizedNow = true;
            }
        }
        else
            self->opusSyncPeers.erase(peerKey);
    }
    if (recognizedNow)
    {
        juce::ScopedLock lock(self->chatLock);
        self->chatHistory.add(recognizedMessage);
        self->chatSenders.add("");
        if (self->chatHistory.size() > 100)
        {
            self->chatHistory.removeRange(0, self->chatHistory.size() - 100);
            self->chatSenders.removeRange(0, juce::jmax(0, self->chatSenders.size() - 100));
        }
    }
    vlogStr("[MCRefresh] IntervalMediaItem_Callback calling refresh. peerKey='" + peerKey + "' userId='" + userId + "' multiChanEnabled=" + juce::String(multiChanEnabled ? 1 : 0) + " nCh=" + juce::String(peerNumChannels));
    self->refreshOpusSyncAvailabilityFromUsers();
}

void NinjamVst3AudioProcessor::setSyncToHost(bool shouldSync)
{
    syncToHost = shouldSync;
    hostWasPlaying.store(false);
    syncWaitForInterval.store(false);
    syncTargetInterval.store(-1);
    syncDisplayIntervalOffset.store(intervalIndex.load());
    int pos = 0;
    int length = 0;
    ninjamClient.GetPosition(&pos, &length);
    syncDisplayPositionOffset.store(length > 0 ? pos : 0);
}

bool NinjamVst3AudioProcessor::isSyncToHostEnabled() const
{
    return syncToHost;
}

bool NinjamVst3AudioProcessor::getHostPosition(juce::AudioPlayHead::CurrentPositionInfo& info) const
{
    const juce::ScopedLock lock(transportLock);
    info = lastHostPosition;
    return true;
}

void NinjamVst3AudioProcessor::setMtcOutputEnabled(bool shouldEnable)
{
    mtcOutputEnabled.store(shouldEnable);
}

bool NinjamVst3AudioProcessor::isMtcOutputEnabled() const
{
    return mtcOutputEnabled.load();
}

void NinjamVst3AudioProcessor::setMtcFrameRate(int fps)
{
    int mapped = 30;
    if (fps == 24 || fps == 25 || fps == 30 || fps == 2997)
        mapped = fps;
    mtcFrameRateFps.store(mapped);
}

int NinjamVst3AudioProcessor::getMtcFrameRate() const
{
    return mtcFrameRateFps.load();
}

bool NinjamVst3AudioProcessor::isStandaloneInstance() const
{
    return isStandaloneWrapper();
}

std::vector<NinjamVst3AudioProcessor::MidiControllerEvent> NinjamVst3AudioProcessor::popPendingMidiControllerEvents()
{
    std::vector<MidiControllerEvent> events;
    const juce::SpinLock::ScopedLockType midiQueueLock(midiEventQueueLock);
    events.swap(pendingMidiControllerEvents);
    return events;
}

void NinjamVst3AudioProcessor::setMidiRelayTarget(const juce::String& targetUser)
{
    const juce::ScopedLock lock(midiRelayTargetLock);
    midiRelayTarget = targetUser.isNotEmpty() ? targetUser : "*";
}

juce::String NinjamVst3AudioProcessor::getMidiRelayTarget() const
{
    const juce::ScopedLock lock(midiRelayTargetLock);
    return midiRelayTarget.isNotEmpty() ? midiRelayTarget : "*";
}

void NinjamVst3AudioProcessor::setMidiLearnStateJson(const juce::String& json)
{
    const juce::ScopedLock lock(learnStateLock);
    midiLearnStateJson = json;
}

juce::String NinjamVst3AudioProcessor::getMidiLearnStateJson() const
{
    const juce::ScopedLock lock(learnStateLock);
    return midiLearnStateJson;
}

void NinjamVst3AudioProcessor::setOscLearnStateJson(const juce::String& json)
{
    const juce::ScopedLock lock(learnStateLock);
    oscLearnStateJson = json;
}

juce::String NinjamVst3AudioProcessor::getOscLearnStateJson() const
{
    const juce::ScopedLock lock(learnStateLock);
    return oscLearnStateJson;
}

void NinjamVst3AudioProcessor::setMidiLearnInputDeviceId(const juce::String& deviceId)
{
    const juce::ScopedLock lock(learnStateLock);
    midiLearnInputDeviceId = deviceId;
}

juce::String NinjamVst3AudioProcessor::getMidiLearnInputDeviceId() const
{
    const juce::ScopedLock lock(learnStateLock);
    return midiLearnInputDeviceId;
}

void NinjamVst3AudioProcessor::setMidiRelayInputDeviceId(const juce::String& deviceId)
{
    const juce::ScopedLock lock(learnStateLock);
    midiRelayInputDeviceId = deviceId;
}

juce::String NinjamVst3AudioProcessor::getMidiRelayInputDeviceId() const
{
    const juce::ScopedLock lock(learnStateLock);
    return midiRelayInputDeviceId;
}

void NinjamVst3AudioProcessor::enqueueExternalMidiControllerEvent(const MidiControllerEvent& event, bool forLearn, bool forRelay)
{
    if (forLearn)
    {
        const juce::SpinLock::ScopedLockType midiQueueLock(midiEventQueueLock);
        pendingMidiControllerEvents.push_back(event);
        if (pendingMidiControllerEvents.size() > 512)
            pendingMidiControllerEvents.erase(pendingMidiControllerEvents.begin(), pendingMidiControllerEvents.begin() + (long long)(pendingMidiControllerEvents.size() - 512));
    }

    if (forRelay)
    {
        const juce::SpinLock::ScopedLockType relayQueueLock(outboundMidiRelayQueueLock);
        pendingOutboundMidiRelayEvents.push_back(event);
        if (pendingOutboundMidiRelayEvents.size() > 512)
            pendingOutboundMidiRelayEvents.erase(pendingOutboundMidiRelayEvents.begin(), pendingOutboundMidiRelayEvents.begin() + (long long)(pendingOutboundMidiRelayEvents.size() - 512));
    }
}

void NinjamVst3AudioProcessor::flushOutboundMidiRelayEvents()
{
    std::vector<MidiControllerEvent> events;
    {
        const juce::SpinLock::ScopedLockType lock(outboundMidiRelayQueueLock);
        events.swap(pendingOutboundMidiRelayEvents);
    }

    if (events.empty())
        return;

    const juce::String target = getMidiRelayTarget();
    const juce::String userId = currentUser;
    for (const auto& event : events)
    {
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("userId", userId);
        obj->setProperty("isController", event.isController);
        obj->setProperty("midiChannel", event.midiChannel);
        obj->setProperty("number", event.number);
        obj->setProperty("value", event.value);
        obj->setProperty("normalized", event.normalized);
        obj->setProperty("isNoteOn", event.isNoteOn);
        sendSideSignal(target, "midiRelay", juce::JSON::toString(juce::var(obj.get())));
    }
}

void NinjamVst3AudioProcessor::injectInboundMidiRelayEvents(juce::MidiBuffer& midiMessages)
{
    std::vector<MidiControllerEvent> events;
    {
        const juce::SpinLock::ScopedLockType lock(inboundMidiRelayQueueLock);
        events.swap(pendingInboundMidiRelayEvents);
    }

    for (const auto& event : events)
    {
        if (event.isController)
            midiMessages.addEvent(juce::MidiMessage::controllerEvent(event.midiChannel, event.number, event.value), 0);
        else if (event.isNoteOn)
            midiMessages.addEvent(juce::MidiMessage::noteOn(event.midiChannel, event.number, (juce::uint8)event.value), 0);
        else
            midiMessages.addEvent(juce::MidiMessage::noteOff(event.midiChannel, event.number), 0);
    }
}

bool NinjamVst3AudioProcessor::isStandaloneWrapper() const
{
    return wrapperType == juce::AudioProcessor::wrapperType_Standalone;
}

int NinjamVst3AudioProcessor::getDisplayIntervalIndex() const
{
    const int absolute = intervalIndex.load();
    if (!isSyncToHostEnabled())
        return absolute;
    if (!hostWasPlaying.load())
        return 0;
    const int base = syncDisplayIntervalOffset.load();
    return juce::jmax(0, absolute - base);
}

void NinjamVst3AudioProcessor::emitMidiTimecode(juce::MidiBuffer& midiMessages, int numSamples, int pos, int length)
{
    const double sampleRate = getSampleRate();
    if (sampleRate <= 1.0 || numSamples <= 0)
        return;

    const bool mtcEnabled = isMtcOutputEnabled();
    const int fpsSetting = getMtcFrameRate();
    const double fps = fpsSetting == 2997 ? 29.97 : (double)fpsSetting;
    const juce::uint8 rateCode = fpsSetting == 24 ? 0x00 : fpsSetting == 25 ? 0x01 : fpsSetting == 2997 ? 0x02 : 0x03;

    const bool waitingForStart = isSyncToHostEnabled() && (!hostWasPlaying.load() || syncWaitForInterval.load());
    const bool shouldRun = (length > 0) && !waitingForStart;

    auto sendLocate = [&midiMessages, rateCode](int sampleOffset, int hours, int minutes, int seconds, int frames)
    {
        const juce::uint8 hr = (juce::uint8)(((rateCode & 0x03u) << 5) | ((juce::uint8)hours & 0x1Fu));
        const juce::uint8 sysex[] = { 0xF0, 0x7F, 0x7F, 0x01, 0x01,
                                      hr,
                                      (juce::uint8)minutes,
                                      (juce::uint8)seconds,
                                      (juce::uint8)frames,
                                      0xF7 };
        midiMessages.addEvent(juce::MidiMessage::createSysExMessage(sysex, (int)sizeof(sysex)), sampleOffset);
    };

    auto getTimecode = [sampleRate, fps](long long timelineSamples)
    {
        if (timelineSamples < 0)
            timelineSamples = 0;
        const double seconds = (double)timelineSamples / sampleRate;
        const long long totalFrames = (long long)std::floor(seconds * fps);
        const int frame = (int)(totalFrames % (long long)std::round(fps));
        const long long totalSeconds = (long long)std::floor((double)totalFrames / fps);
        const int second = (int)(totalSeconds % 60);
        const int minute = (int)((totalSeconds / 60) % 60);
        const int hour = (int)((totalSeconds / 3600) % 24);
        return std::array<int, 4> { hour, minute, second, frame };
    };

    if (!mtcEnabled)
    {
        if (mtcWasRunning)
        {
            midiMessages.addEvent(juce::MidiMessage::midiStop(), 0);
            sendLocate(0, 0, 0, 0, 0);
        }
        mtcWasRunning = false;
        mtcSamplesUntilNextQuarterFrame = 0.0;
        mtcQuarterFramePiece = 0;
        return;
    }

    if (mtcWasRunning && !shouldRun)
    {
        midiMessages.addEvent(juce::MidiMessage::midiStop(), 0);
        sendLocate(0, 0, 0, 0, 0);
        mtcSamplesUntilNextQuarterFrame = 0.0;
        mtcQuarterFramePiece = 0;
    }

    int displayInterval = getDisplayIntervalIndex();
    int timelinePos = 0;
    if (length > 0)
    {
        if (!waitingForStart)
            timelinePos = juce::jlimit(0, juce::jmax(0, length - 1), pos);
    }
    long long blockStartSamples = (long long)displayInterval * (long long)juce::jmax(0, length) + (long long)timelinePos;

    if (!mtcWasRunning && shouldRun)
    {
        const auto tc = getTimecode(blockStartSamples);
        sendLocate(0, tc[0], tc[1], tc[2], tc[3]);
        midiMessages.addEvent(juce::MidiMessage::midiStart(), 0);
        mtcSamplesUntilNextQuarterFrame = 0.0;
        mtcQuarterFramePiece = 0;
    }

    mtcWasRunning = shouldRun;
    if (!shouldRun)
        return;

    const double qfPerSecond = fps * 4.0;
    const double samplesPerQuarterFrame = sampleRate / qfPerSecond;
    double sampleCursor = mtcSamplesUntilNextQuarterFrame;
    if (sampleCursor <= 0.0)
        sampleCursor = samplesPerQuarterFrame;

    while (sampleCursor < (double)numSamples)
    {
        const int eventSample = juce::jlimit(0, numSamples - 1, (int)std::floor(sampleCursor));
        const long long eventTimelineSamples = blockStartSamples + (long long)eventSample;
        const auto tc = getTimecode(eventTimelineSamples);

        const int piece = mtcQuarterFramePiece & 0x07;
        int value = 0;
        switch (piece)
        {
            case 0: value = tc[3] & 0x0F; break;
            case 1: value = (tc[3] >> 4) & 0x01; break;
            case 2: value = tc[2] & 0x0F; break;
            case 3: value = (tc[2] >> 4) & 0x03; break;
            case 4: value = tc[1] & 0x0F; break;
            case 5: value = (tc[1] >> 4) & 0x03; break;
            case 6: value = tc[0] & 0x0F; break;
            case 7: value = ((tc[0] >> 4) & 0x01) | (0x03 << 1); break;
            default: break;
        }

        const juce::uint8 data = (juce::uint8)(((piece & 0x07) << 4) | (value & 0x0F));
        midiMessages.addEvent(juce::MidiMessage(0xF1, data), eventSample);
        mtcQuarterFramePiece = (piece + 1) & 0x07;
        sampleCursor += samplesPerQuarterFrame;
    }

    mtcSamplesUntilNextQuarterFrame = sampleCursor - (double)numSamples;
}

bool NinjamVst3AudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* NinjamVst3AudioProcessor::createEditor()
{
    return new NinjamVst3AudioProcessorEditor (*this);
}

void NinjamVst3AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state("NINJAM_STATE");
    state.setProperty("midiRelayTarget", getMidiRelayTarget(), nullptr);
    state.setProperty("midiLearnStateJson", getMidiLearnStateJson(), nullptr);
    state.setProperty("oscLearnStateJson", getOscLearnStateJson(), nullptr);
    state.setProperty("midiLearnInputDeviceId", getMidiLearnInputDeviceId(), nullptr);
    state.setProperty("midiRelayInputDeviceId", getMidiRelayInputDeviceId(), nullptr);
    state.setProperty("fxReverbWetDryMix", (double)getFxReverbWetDryMix(), nullptr);
    state.setProperty("fxDelayWetDryMix", (double)getFxDelayWetDryMix(), nullptr);
    for (int channel = 0; channel < maxLocalChannels; ++channel)
        state.setProperty("localInput" + juce::String(channel), getLocalChannelInput(channel), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void NinjamVst3AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState == nullptr)
        return;

    const juce::ValueTree state = juce::ValueTree::fromXml(*xmlState);
    if (!state.isValid())
        return;

    setMidiRelayTarget(state.getProperty("midiRelayTarget", "*").toString());
    setMidiLearnStateJson(state.getProperty("midiLearnStateJson", "").toString());
    setOscLearnStateJson(state.getProperty("oscLearnStateJson", "").toString());
    setMidiLearnInputDeviceId(state.getProperty("midiLearnInputDeviceId", "").toString());
    setMidiRelayInputDeviceId(state.getProperty("midiRelayInputDeviceId", "").toString());
    setFxReverbWetDryMix((float)(double)state.getProperty("fxReverbWetDryMix", 1.0));
    setFxDelayWetDryMix((float)(double)state.getProperty("fxDelayWetDryMix", 1.0));
    for (int channel = 0; channel < maxLocalChannels; ++channel)
        setLocalChannelInput(channel, (int)state.getProperty("localInput" + juce::String(channel), -1));
}

void NinjamVst3AudioProcessor::timerCallback()
{
    int loopCount = 0;
    while (!ninjamClient.Run() && loopCount < 50)
    {
        loopCount++;
    }

    int status = ninjamClient.GetStatus();
    if (status != lastStatus)
    {
        if (status == NJClient::NJC_STATUS_CANTCONNECT || status == NJClient::NJC_STATUS_INVALIDAUTH)
        {
            juce::String err = juce::String::fromUTF8(ninjamClient.GetErrorStr());
            juce::Logger::writeToLog("NINJAM Error (" + juce::String(status) + "): " + err);
        }
        else if (status == NJClient::NJC_STATUS_OK)
        {
            juce::Logger::writeToLog("NINJAM Connected Successfully");
            opusSyncServerSupported.store(false);
            juce::Logger::writeToLog("Sending VIDEO_CAP 1");
            ninjamClient.ChatMessage_Send("VIDEO_CAP", "1", nullptr, nullptr, nullptr);
            {
                const juce::ScopedLock lock(opusSyncPeerLock);
                opusSyncPeers.clear();
            }
            {
                const juce::ScopedLock lock(intervalSyncAnnouncementLock);
                lastAnnouncedRemoteIntervalByUser.clear();
                localIntervalStartMsByInterval.clear();
                pendingRemoteIntervalStartsByUser.clear();
                remoteTransportRttMsByUser.clear();
                pendingTransportProbeSentMsById.clear();
                remoteLatencyLastAppliedIntervalByUser.clear();
                remoteLatencyAverageByUser.clear();
                remoteLatencyFirmDelayMsByUser.clear();
            }
            opusSyncAvailable.store(false);
            opusSyncHasLegacyClients.store(false);
            lastOpusSupportBroadcastMs = 0.0;
            lastTransportProbeBroadcastMs = 0.0;
            if (!isSyncToHostEnabled())
            {
                syncWaitForInterval.store(false);
                syncTargetInterval.store(-1);
                intervalIndex.store(0);
                lastIntervalPos.store(0);
            }
            lastBroadcastIntervalTag.store(-1);
            setIntervalSyncStatusText({});
            syncLocalIntervalChannelConfig();
        }
        else if (lastStatus == NJClient::NJC_STATUS_OK)
        {
            opusSyncServerSupported.store(false);
            {
                const juce::ScopedLock lock(opusSyncPeerLock);
                opusSyncPeers.clear();
            }
            {
                const juce::ScopedLock lock(intervalSyncAnnouncementLock);
                lastAnnouncedRemoteIntervalByUser.clear();
                localIntervalStartMsByInterval.clear();
                pendingRemoteIntervalStartsByUser.clear();
                remoteTransportRttMsByUser.clear();
                pendingTransportProbeSentMsById.clear();
                remoteLatencyLastAppliedIntervalByUser.clear();
                remoteLatencyAverageByUser.clear();
                remoteLatencyFirmDelayMsByUser.clear();
            }
            opusSyncAvailable.store(false);
            opusSyncHasLegacyClients.store(false);
            setIntervalSyncStatusText({});
            lastBroadcastIntervalTag.store(-1);
            applyCodecPreference();
        }
        lastStatus = status;
    }

    if (status == NJClient::NJC_STATUS_OK)
    {
        refreshOpusSyncAvailabilityFromUsers();
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        {
            const juce::ScopedLock lock(intervalSyncAnnouncementLock);
            const int displayInterval = getDisplayIntervalIndex();
            if (localIntervalStartMsByInterval.find(displayInterval) == localIntervalStartMsByInterval.end())
                localIntervalStartMsByInterval[displayInterval] = nowMs;
        }
        if (nowMs - lastOpusSupportBroadcastMs >= 1500.0)
        {
            broadcastOpusSyncSupport();
            lastOpusSupportBroadcastMs = nowMs;
        }
        if (nowMs - lastTransportProbeBroadcastMs >= 5000.0)
        {
            broadcastTransportProbe();
            lastTransportProbeBroadcastMs = nowMs;
        }

        flushOutboundMidiRelayEvents();

        const int displayInterval = getDisplayIntervalIndex();
        if (lastBroadcastIntervalTag.load() != displayInterval)
        {
            broadcastIntervalSyncTag();
            lastBroadcastIntervalTag.store(displayInterval);
        }
    }

    int pos = 0;
    int length = 0;
    ninjamClient.GetPosition(&pos, &length);
    if (length > 0)
    {
        int last = lastIntervalPos.load();
        if (pos < last)
        {
            intervalIndex.fetch_add(1);
            const int localAbsoluteInterval = intervalIndex.load();
            const int localDisplayInterval = getDisplayIntervalIndex();
            const long long localIntervalStartSampleCount = intervalSyncSampleCounter.load(std::memory_order_relaxed);
            const double localIntervalStartMs = juce::Time::getMillisecondCounterHiRes();
            {
                const juce::ScopedLock lock(intervalSyncAnnouncementLock);
                localIntervalStartMsByInterval[localDisplayInterval] = localIntervalStartMs;
                const int minIntervalToKeep = localDisplayInterval - 64;
                for (auto it = localIntervalStartMsByInterval.begin(); it != localIntervalStartMsByInterval.end();)
                {
                    if (it->first < minIntervalToKeep)
                        it = localIntervalStartMsByInterval.erase(it);
                    else
                        ++it;
                }
            }
            if (status == NJClient::NJC_STATUS_OK)
            {
                const int localBpi = juce::jmax(1, getBPI());
                const double localBpm = juce::jmax(1.0, (double)getBPM());
                const double intervalDurationMs = (60.0 / localBpm) * (double)localBpi * 1000.0;
                for (;;)
                {
                    juce::String senderKey;
                    PendingRemoteIntervalStart pending;
                    {
                        const juce::ScopedLock lock(intervalSyncAnnouncementLock);
                        if (pendingRemoteIntervalStartsByUser.empty())
                            break;
                        for (auto staleIt = pendingRemoteIntervalStartsByUser.begin(); staleIt != pendingRemoteIntervalStartsByUser.end();)
                        {
                            const int targetAbsolute = staleIt->second.remoteIntervalAbsolute;
                            const int targetDisplay = staleIt->second.remoteInterval;
                            bool isStale = false;
                            if (targetAbsolute >= 0)
                                isStale = localAbsoluteInterval > (targetAbsolute + 2);
                            else if (targetDisplay >= 0)
                                isStale = localDisplayInterval > (targetDisplay + 2);
                            else
                                isStale = true;
                            if (isStale)
                                staleIt = pendingRemoteIntervalStartsByUser.erase(staleIt);
                            else
                                ++staleIt;
                        }
                        auto chosenIt = pendingRemoteIntervalStartsByUser.end();
                        for (auto it = pendingRemoteIntervalStartsByUser.begin(); it != pendingRemoteIntervalStartsByUser.end(); ++it)
                        {
                            const bool absoluteMatch = it->second.remoteIntervalAbsolute >= 0 && it->second.remoteIntervalAbsolute == localAbsoluteInterval;
                            const bool displayMatch = it->second.remoteIntervalAbsolute < 0 && it->second.remoteInterval >= 0 && it->second.remoteInterval == localDisplayInterval;
                            if (absoluteMatch || displayMatch)
                            {
                                chosenIt = it;
                                break;
                            }
                        }
                        if (chosenIt == pendingRemoteIntervalStartsByUser.end())
                            break;
                        senderKey = chosenIt->first;
                        pending = chosenIt->second;
                        pendingRemoteIntervalStartsByUser.erase(chosenIt);
                    }
                    if (pending.receivedSampleCount < 0)
                        continue;
                    const long long elapsedSamples = localIntervalStartSampleCount - pending.receivedSampleCount;
                    if (elapsedSamples < 0)
                        continue;
                    const double sampleRate = juce::jmax(1.0, getSampleRate());
                    const double elapsedToNextLocalBpi1Ms = ((double)elapsedSamples / sampleRate) * 1000.0;
                    const double outlierLimitMs = intervalDurationMs * 2.0;
                    if (!std::isfinite(elapsedToNextLocalBpi1Ms) || elapsedToNextLocalBpi1Ms < 0.0 || elapsedToNextLocalBpi1Ms > outlierLimitMs)
                        continue;
                    const int elapsedMs = (int)std::llround(juce::jlimit(0.0, intervalDurationMs, elapsedToNextLocalBpi1Ms));
                    int averageMs = -1;
                    int firmAverageMs = -1;
                    {
                        const juce::ScopedLock lock(intervalSyncAnnouncementLock);
                        auto& avgState = remoteLatencyAverageByUser[senderKey];
                        avgState.lastMeasurementMs = (double)elapsedMs;
                        bool includeInAverage = true;
                        if (avgState.sampleCount >= 3)
                        {
                            const double baselineMs = avgState.firmAverageMs > 0.0 ? avgState.firmAverageMs : avgState.averageMs;
                            const double deltaMs = std::abs((double)elapsedMs - baselineMs);
                            const double spikeThresholdMs = juce::jlimit(5.0, 20.0, baselineMs * 0.30 + 2.0);
                            if (deltaMs > spikeThresholdMs)
                                includeInAverage = false;
                        }
                        if (includeInAverage)
                        {
                            avgState.sampleCount += 1;
                            avgState.sumMs += (double)elapsedMs;
                            avgState.averageMs = avgState.sumMs / (double)juce::jmax(1, avgState.sampleCount);
                            if (avgState.sampleCount == 1)
                                avgState.firmAverageMs = (double)elapsedMs;
                            else
                                avgState.firmAverageMs = (avgState.firmAverageMs * 0.88) + ((double)elapsedMs * 0.12);
                        }
                        if (avgState.sampleCount >= 3)
                        {
                            averageMs = juce::jmax(0, (int)std::llround(avgState.averageMs));
                            firmAverageMs = juce::jmax(0, (int)std::llround(avgState.firmAverageMs));
                        }
                        else if (avgState.lastMeasurementMs >= 0.0)
                        {
                            averageMs = juce::jmax(0, (int)std::llround(avgState.lastMeasurementMs));
                        }
                    }
                    if (firmAverageMs >= 0 || averageMs >= 0)
                    {
                        // Subtract half RTT to compensate for network transit time
                        double halfRttMs = 0.0;
                        {
                            auto rttIt = remoteTransportRttMsByUser.find(senderKey);
                            if (rttIt != remoteTransportRttMsByUser.end() && rttIt->second > 0.0)
                                halfRttMs = rttIt->second * 0.5;
                            if (halfRttMs <= 0.0)
                            {
                                const juce::String csKey = canonicalDelayUserKey(senderKey);
                                if (csKey.isNotEmpty())
                                {
                                    auto canonicalRttIt = remoteTransportRttMsByUser.find(csKey);
                                    if (canonicalRttIt != remoteTransportRttMsByUser.end() && canonicalRttIt->second > 0.0)
                                        halfRttMs = canonicalRttIt->second * 0.5;
                                }
                            }
                        }
                        const double rawDelayMs = (double)(firmAverageMs >= 0 ? firmAverageMs : averageMs);
                        const int correctedDelayMs = juce::jmax(0, (int)std::llround(rawDelayMs - halfRttMs));
                        const int sourceInterval = pending.remoteIntervalAbsolute >= 0 ? pending.remoteIntervalAbsolute : pending.remoteInterval;
                        const juce::String canonicalSenderKey = canonicalDelayUserKey(senderKey);
                        const juce::ScopedLock lock(intervalSyncAnnouncementLock);
                        int priorAppliedInterval = std::numeric_limits<int>::min();
                        auto appliedIt = remoteLatencyLastAppliedIntervalByUser.find(senderKey);
                        if (appliedIt != remoteLatencyLastAppliedIntervalByUser.end())
                            priorAppliedInterval = appliedIt->second;
                        bool shouldApply = (appliedIt == remoteLatencyLastAppliedIntervalByUser.end());
                        auto currentDelayIt = remoteLatencyFirmDelayMsByUser.find(senderKey);
                        if (!shouldApply)
                        {
                            const int intervalDelta = sourceInterval - priorAppliedInterval;
                            const bool cadenceReached = intervalDelta >= remoteLatencyUpdateCadenceIntervals;
                            shouldApply = cadenceReached;
                        }
                        if (shouldApply)
                        {
                            remoteLatencyFirmDelayMsByUser[senderKey] = correctedDelayMs;
                            if (canonicalSenderKey.isNotEmpty())
                                remoteLatencyFirmDelayMsByUser[canonicalSenderKey] = correctedDelayMs;
                            remoteLatencyLastAppliedIntervalByUser[senderKey] = sourceInterval;
                            if (canonicalSenderKey.isNotEmpty())
                                remoteLatencyLastAppliedIntervalByUser[canonicalSenderKey] = sourceInterval;
                            vlogStr("[MCGuard] Applied firm delay for=" + senderKey + " canonical=" + canonicalSenderKey + " delayMs=" + juce::String(correctedDelayMs) + " rawMs=" + juce::String((int)std::llround(rawDelayMs)) + " halfRtt=" + juce::String((int)std::llround(halfRttMs)) + " sourceInterval=" + juce::String(sourceInterval) + " priorApplied=" + juce::String(priorAppliedInterval));
                        }
                    }
                    const juce::String displaySender = pending.displaySender.isNotEmpty() ? pending.displaySender : senderKey;
                    // Look up halfRtt for display (may not be in scope from block above if firmAverageMs < 0)
                    double displayHalfRttMs = 0.0;
                    {
                        auto rttDisplayIt = remoteTransportRttMsByUser.find(senderKey);
                        if (rttDisplayIt != remoteTransportRttMsByUser.end() && rttDisplayIt->second > 0.0)
                            displayHalfRttMs = rttDisplayIt->second * 0.5;
                        if (displayHalfRttMs <= 0.0)
                        {
                            const juce::String csDisplayKey = canonicalDelayUserKey(senderKey);
                            if (csDisplayKey.isNotEmpty())
                            {
                                auto cRttIt = remoteTransportRttMsByUser.find(csDisplayKey);
                                if (cRttIt != remoteTransportRttMsByUser.end() && cRttIt->second > 0.0)
                                    displayHalfRttMs = cRttIt->second * 0.5;
                            }
                        }
                    }
                    juce::String line = displaySender + " BPI1->our BPI1 " + juce::String(elapsedMs) + "ms";
                    if (firmAverageMs >= 0)
                        line << " avg " << juce::String(firmAverageMs) << "ms";
                    else if (averageMs >= 0)
                        line << " avg " << juce::String(averageMs) << "ms";
                    if (displayHalfRttMs > 0.0)
                        line << " rtt/2 " << juce::String((int)std::llround(displayHalfRttMs)) << "ms";
                    juce::DynamicObject::Ptr reportObj = new juce::DynamicObject();
                    reportObj->setProperty("line", line);
                    reportObj->setProperty("interval", pending.remoteIntervalAbsolute >= 0 ? pending.remoteIntervalAbsolute : pending.remoteInterval);
                    reportObj->setProperty("targetUserId", canonicalDelayUserKey(displaySender));
                    reportObj->setProperty("elapsedMs", elapsedMs);
                    if (averageMs >= 0)
                        reportObj->setProperty("avgMs", averageMs);
                    if (firmAverageMs >= 0)
                        reportObj->setProperty("firmMs", firmAverageMs);
                    if (displayHalfRttMs > 0.0)
                        reportObj->setProperty("halfRttMs", (int)std::llround(displayHalfRttMs));
                    reportObj->setProperty("eventId", "latencyReport:" + senderKey + ":" + juce::String(++sideSignalEventCounter));
                    const juce::String reportPayload = juce::JSON::toString(juce::var(reportObj.get()));
                    sendIntervalSignal("intervalLatencyReport", reportPayload);
                }
            }
            if (status == NJClient::NJC_STATUS_OK)
                lastBroadcastIntervalTag.store(-1);
        }
        lastIntervalPos.store(pos);
        if (status == NJClient::NJC_STATUS_OK)
            writeIntervalHelperJson(pos, length);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NinjamVst3AudioProcessor();
}
