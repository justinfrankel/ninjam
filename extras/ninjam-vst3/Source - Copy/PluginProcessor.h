#pragma once

#include <JuceHeader.h>
#include <atomic>

// Disable min/max macros before including ninjam headers
#ifdef WIN32
#define NOMINMAX
#endif

#include "ninjam/njclient.h"

class NinjamVst3AudioProcessor : public juce::AudioProcessor,
                                 public juce::Timer
{
    friend class NinjamVst3AudioProcessorEditor;
public:
    NinjamVst3AudioProcessor();
    ~NinjamVst3AudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Timer callback for NINJAM client Run()
    void timerCallback() override;

    NJClient& getClient() { return ninjamClient; }

    // NINJAM actions
    void connectToServer(juce::String host, juce::String user, juce::String pass);
    void disconnectFromServer();
    void sendChatMessage(juce::String msg);
    
    // Metronome
    void setMetronomeVolume(float vol);
    float getMetronomeVolume() const;

    // Local Channel
    void setTransmitLocal(bool shouldTransmit);
    bool isTransmittingLocal() const;
    void setLocalBitrate(int bitrate);
    int getLocalBitrate() const;
    void setVoiceChatMode(bool enabled);
    bool isVoiceChatMode() const;

    // Chat
    juce::StringArray getChatMessages();
    void setAutoTranslateEnabled(bool shouldEnable);
    bool isAutoTranslateEnabled() const;
    void setTranslateTargetLang(const juce::String& langCode);
    juce::String getTranslateTargetLang() const;

    struct PublicServerInfo {
        juce::String host;
        int port;
        juce::String name;
        int bpi;
        float bpm;
        int userCount;
        int userMax;
    };

    std::vector<PublicServerInfo> getPublicServers() const;
    void refreshPublicServers();

    // User List
    struct UserInfo {
        int index;
        juce::String name;
        float volume;
        float pan;
        bool isMuted;
        int outputChannel; // 0=Main, 2=Out2, etc.
        int numChannels = 1;          // number of active NINJAM channels for this user
        bool isMultiChanPeer = false;  // has more than 1 NINJAM channel
        juce::StringArray channelNames; // name of each NINJAM channel (index 0..numChannels-1)
    };
    std::vector<UserInfo> getConnectedUsers();
    void setUserOutput(int userIndex, int outputChannelIndex);
    void setUserLevel(int userIndex, float volume, float pan, bool isMuted, bool isSolo);
    void setUserVolume(int userIndex, float volume);
    float getUserPeak(int userIndex, int channelIndex); // 0=L, 1=R
    float getUserChannelPeak(int userIndex, int njChanIdx, int lrSide); // per NINJAM channel L/R peak
    void setUserNjChannelVolume(int userIndex, int njChanIdx, float volume); // individual NINJAM channel volume

    void setMasterOutputGain(float gain);
    float getMasterOutputGain() const;
    float getMasterPeak() const;
    float getMasterPeakLeft() const;
    float getMasterPeakRight() const;
    void setSoftLimiterEnabled(bool shouldEnable);
    bool isSoftLimiterEnabled() const;
    void setUserClipEnabled(int userIndex, bool enabled);
    bool isUserClipEnabled(int userIndex) const;
    void setMasterLimiterEnabled(bool shouldEnable);
    bool isMasterLimiterEnabled() const;
    float getLimiterThreshold() const { return limiterThresholdDb.load(); }
    float getLimiterRelease() const { return limiterReleaseMs.load(); }
    void setLimiterThreshold(float db);
    void setLimiterRelease(float ms);
    void setLocalInputGain(float gain);
    float getLocalInputGain() const;
    static constexpr int maxLocalChannels = 8;
    void setNumLocalChannels(int num);
    int getNumLocalChannels() const;
    void setLocalChannelName(int channel, const juce::String& name);
    juce::String getLocalChannelName(int channel) const;
    void setLocalChannelGain(int channel, float gain);
    float getLocalChannelGain(int channel) const;
    NinjamVst3AudioProcessorEditor* getEditor() const { return (NinjamVst3AudioProcessorEditor*)getActiveEditor(); }
    void setLocalChannelInput(int channel, int inputIndex);
    int getLocalChannelInput(int channel) const;
    float getLocalChannelPeak(int channel) const;
    float getLocalChannelPeakLeft(int channel) const;
    float getLocalChannelPeakRight(int channel) const;
    void setLocalMonitorEnabled(bool enabled);
    bool isLocalMonitorEnabled() const;
    void setFxReverbEnabled(bool enabled);
    bool isFxReverbEnabled() const;
    void setFxDelayEnabled(bool enabled);
    bool isFxDelayEnabled() const;
    void setFxReverbRoomSize(float roomSize);
    float getFxReverbRoomSize() const;
    void setFxReverbDamping(float damping);
    float getFxReverbDamping() const;
    void setFxReverbWetDryMix(float wetDryMix);
    float getFxReverbWetDryMix() const;
    void setFxReverbEarlyReflections(float earlyReflections);
    float getFxReverbEarlyReflections() const;
    void setFxReverbTail(float tail);
    float getFxReverbTail() const;
    void setFxDelayTimeMs(float timeMs);
    float getFxDelayTimeMs() const;
    void setFxDelaySyncToHost(bool enabled);
    bool isFxDelaySyncToHost() const;
    void setFxDelayDivision(int division);
    int getFxDelayDivision() const;
    void setFxDelayPingPong(bool enabled);
    bool isFxDelayPingPong() const;
    void setFxDelayWetDryMix(float wetDryMix);
    float getFxDelayWetDryMix() const;
    void setFxDelayFeedback(float feedback);
    float getFxDelayFeedback() const;
    void setLocalChannelReverbSend(int channel, float send);
    float getLocalChannelReverbSend(int channel) const;
    void setLocalChannelDelaySend(int channel, float send);
    float getLocalChannelDelaySend(int channel) const;

    // NINJAM callbacks
    static int LicenseAgreementCallback(void* userData, const char* licensetext);
    static void ChatMessage_Callback(void* userData, NJClient* inst, const char** parms, int nparms);
    static void IntervalMediaItem_Callback(void* userData, NJClient* inst, const char* username, int chidx, unsigned int fourcc, const unsigned char* guid, const void* data, int dataLen);
    static void IntervalChunkCallback_cb(void* userData, NJClient* inst, const char* username, int chidx, unsigned int fourcc, const unsigned char* guid, const void* data, int dataLen, int flags);
    static void NewIntervalCallback_cb(void* userData, NJClient* inst);

    // Interval / BPI
    int getBPI();
    float getIntervalProgress();
    float getBPM();
    int getIntervalIndex() const;
    int getCodecMode() const;
    unsigned int getVorbisMask() const;
    unsigned int getOpusMask() const;

    float getLocalPeak() const;
    float getLocalPeakLeft() const;
    float getLocalPeakRight() const;

    void sendSideSignal(const juce::String& target, const juce::String& type, const juce::String& payload);
    void sendIntervalSignal(const juce::String& type, const juce::String& payload);
    void processSyncSignal(const juce::String& sender, const juce::String& type, const juce::String& payload);
    void launchVideoSession();

    void rememberUserVolume(int userIndex, float volume, const juce::String& name);

    void setSpreadOutputsEnabled(bool shouldEnable);
    bool isSpreadOutputsEnabled() const;

    void setSyncToHost(bool shouldSync);
    bool isSyncToHostEnabled() const;
    bool getHostPosition(juce::AudioPlayHead::CurrentPositionInfo& info) const;
    void setMtcOutputEnabled(bool shouldEnable);
    bool isMtcOutputEnabled() const;
    void setMtcFrameRate(int fps);
    int getMtcFrameRate() const;
    bool isStandaloneInstance() const;
    struct MidiControllerEvent
    {
        bool isController = false;
        int midiChannel = 1;
        int number = 0;
        int value = 0;
        float normalized = 0.0f;
        bool isNoteOn = false;
    };
    std::vector<MidiControllerEvent> popPendingMidiControllerEvents();
    void setMidiRelayTarget(const juce::String& targetUser);
    juce::String getMidiRelayTarget() const;
    void setMidiLearnStateJson(const juce::String& json);
    juce::String getMidiLearnStateJson() const;
    void setOscLearnStateJson(const juce::String& json);
    juce::String getOscLearnStateJson() const;
    void setMidiLearnInputDeviceId(const juce::String& deviceId);
    juce::String getMidiLearnInputDeviceId() const;
    void setMidiRelayInputDeviceId(const juce::String& deviceId);
    juce::String getMidiRelayInputDeviceId() const;
    void enqueueExternalMidiControllerEvent(const MidiControllerEvent& event, bool forLearn, bool forRelay);

    bool isOpusSyncAvailable() const;
    juce::String getIntervalSyncStatusText() const;

private:
    NJClient ninjamClient;
    juce::CriticalSection processLock;
    mutable juce::CriticalSection serverListLock;
    std::vector<PublicServerInfo> publicServers;
    
    // Chat storage
    juce::CriticalSection chatLock;
    juce::StringArray chatHistory;
    juce::StringArray chatSenders;  // parallel: "me", username, or "" for system
    bool autoTranslate = false;
    juce::String translateTargetLang = "en";
    
    // Local state
    bool isTransmitting = false;
    int localBitrate = 128;
    bool voiceChatMode = false;
    int lastStatus = 0;
    
    juce::AudioBuffer<float> tempInputBuffer;
    juce::AudioBuffer<float> localChannelBuffer;
    juce::AudioBuffer<float> localMixBuffer;   // 1-ch mix used by multiChanAuto Vorbis slot
    std::atomic<float> masterOutputGain { 1.0f };
    std::atomic<float> localInputGain { 1.0f };
    std::atomic<float> masterPeak { 0.0f };
    std::atomic<float> masterPeakL { 0.0f };
    std::atomic<float> masterPeakR { 0.0f };
    std::atomic<float> localPeak { 0.0f };
    std::atomic<float> localPeakL { 0.0f };
    std::atomic<float> localPeakR { 0.0f };
    std::array<std::atomic<float>, maxLocalChannels> localChannelGains;
    std::array<std::atomic<float>, maxLocalChannels> localChannelPeaks;
    std::array<std::atomic<float>, maxLocalChannels> localChannelPeaksL;
    std::array<std::atomic<float>, maxLocalChannels> localChannelPeaksR;
    std::array<std::atomic<int>, maxLocalChannels> localChannelInputs;
    std::array<std::atomic<float>, maxLocalChannels> localChannelReverbSends;
    std::array<std::atomic<float>, maxLocalChannels> localChannelDelaySends;
    juce::CriticalSection localChannelNamesLock;
    std::array<juce::String, maxLocalChannels> localChannelNames; // user-defined channel names
    std::atomic<int> numLocalChannels { 1 };
    std::atomic<bool> localMonitorEnabled { true };
    std::atomic<bool> fxReverbEnabled { true };
    std::atomic<bool> fxDelayEnabled { true };
    std::atomic<float> fxReverbRoomSize { 0.45f };
    std::atomic<float> fxReverbDamping { 0.45f };
    std::atomic<float> fxReverbWetDryMix { 1.0f };
    std::atomic<float> fxReverbEarlyReflections { 0.25f };
    std::atomic<float> fxReverbTail { 0.75f };
    std::atomic<float> fxDelayTimeMs { 320.0f };
    std::atomic<bool> fxDelaySyncToHost { true };
    std::atomic<int> fxDelayDivision { 8 };
    std::atomic<bool> fxDelayPingPong { false };
    std::atomic<float> fxDelayWetDryMix { 1.0f };
    std::atomic<float> fxDelayFeedback { 0.38f };
    juce::Reverb fxReverb;
    juce::AudioBuffer<float> fxDelayBuffer;
    juce::AudioBuffer<float> fxReverbInputBuffer;
    juce::AudioBuffer<float> fxDelayInputBuffer;
    juce::AudioBuffer<float> fxTransmitBuffer;
    juce::AudioBuffer<float> fxReturnBuffer;
    int fxDelayWritePosition = 0;
    double processingSampleRate = 44100.0;
    std::atomic<bool> spreadOutputsEnabled { false };
    std::atomic<bool> softLimiterEnabled { true };
    std::atomic<bool> dspLimiterEnabled { false };
    std::atomic<float> limiterThresholdDb { 0.0f };
    std::atomic<float> limiterReleaseMs { 100.0f };
    juce::dsp::Limiter<float> masterLimiter;

    std::map<int, bool> userClipEnabled;
    std::map<int, float> userPanOverrides;
    std::map<juce::String, int> userOutputAssignment;
    std::map<int, float> userBaseVolume;
    std::map<juce::String, float> userVolumeByName;

    bool syncToHost = false;
    std::atomic<bool> hostWasPlaying { false };
    std::atomic<bool> syncWaitForInterval { false };
    std::atomic<int> syncTargetInterval { -1 };
    std::atomic<int> syncDisplayIntervalOffset { 0 };
    std::atomic<int> syncDisplayPositionOffset { 0 };
    std::atomic<bool> mtcOutputEnabled { true };
    std::atomic<int> mtcFrameRateFps { 30 };
    bool mtcWasRunning = false;
    double mtcSamplesUntilNextQuarterFrame = 0.0;
    int mtcQuarterFramePiece = 0;
    mutable juce::CriticalSection transportLock;
    juce::AudioPlayHead::CurrentPositionInfo lastHostPosition;

    std::atomic<int> intervalIndex { 0 };
    std::atomic<int> lastIntervalPos { 0 };
    std::atomic<juce::uint64> sideSignalEventCounter { 0 };
    juce::String currentServer;
    juce::String currentUser;
    juce::File videoHelperRootDir;
    juce::File intervalJsonFile;
    std::atomic<bool> videoHelperRunning { false };
    std::unique_ptr<juce::ChildProcess> advancedVideoProcess;
    std::map<juce::String, int> remoteLatencyFirmDelayMsByUser;

    std::atomic<bool> opusSyncAvailable { false };
    std::atomic<bool> opusSyncHasLegacyClients { false };
    std::atomic<bool> opusSyncServerSupported { false };
    mutable juce::CriticalSection intervalSyncStatusLock;
    juce::String intervalSyncStatusText;
    std::atomic<int> lastBroadcastIntervalTag { -1 };
    juce::CriticalSection intervalSyncAnnouncementLock;
    std::map<juce::String, int> lastAnnouncedRemoteIntervalByUser;
    std::map<int, double> localIntervalStartMsByInterval;
    struct PendingRemoteIntervalStart
    {
        int remoteInterval = -1;
        int remoteIntervalAbsolute = -1;
        juce::String displaySender;
        long long receivedSampleCount = -1;
    };
    std::map<juce::String, PendingRemoteIntervalStart> pendingRemoteIntervalStartsByUser;
    std::map<juce::String, double> remoteTransportRttMsByUser;
    std::map<juce::String, double> pendingTransportProbeSentMsById;
    std::map<juce::String, int> remoteLatencyLastAppliedIntervalByUser;
    struct RemoteLatencyAverageState
    {
        int sampleCount = 0;
        double sumMs = 0.0;
        double averageMs = 0.0;
        double firmAverageMs = 0.0;
        double lastMeasurementMs = -1.0;
    };
    std::map<juce::String, RemoteLatencyAverageState> remoteLatencyAverageByUser;
    juce::CriticalSection opusSyncPeerLock;
    struct OpusSyncPeerState
    {
        juce::String userId;
        bool supportsOpus = false;
        bool multiChanEnabled = false;
        int numChannels = 1;           // number of local channels the peer is sending
        juce::String appFamily;
        int handshakeVersion = 0;
        juce::String runtimeFormat;
        juce::String pluginVersion;
        double lastSeenMs = 0.0;
    };
    std::map<juce::String, OpusSyncPeerState> opusSyncPeers;
    // Simple username→{isMultiChan, numChannels} snapshot updated by refreshOpusSyncAvailabilityFromUsers().
    // Keyed by normalised username (no @host, lowercase). Read without holding opusSyncPeerLock.
    struct PeerMultiChanInfo { bool isMultiChan = false; int numChannels = 1; };
    std::map<juce::String, PeerMultiChanInfo> peerMultiChanByName;
    juce::CriticalSection peerMultiChanLock;
    juce::String opusSyncInstanceId;
    double lastOpusSupportBroadcastMs = 0.0;
    std::atomic<juce::uint64> transportProbeCounter { 0 };
    double lastTransportProbeBroadcastMs = 0.0;
    std::atomic<long long> intervalSyncSampleCounter { 0 };
    juce::SpinLock midiEventQueueLock;
    std::vector<MidiControllerEvent> pendingMidiControllerEvents;
    juce::SpinLock outboundMidiRelayQueueLock;
    std::vector<MidiControllerEvent> pendingOutboundMidiRelayEvents;
    juce::SpinLock inboundMidiRelayQueueLock;
    std::vector<MidiControllerEvent> pendingInboundMidiRelayEvents;
    mutable juce::CriticalSection midiRelayTargetLock;
    juce::String midiRelayTarget { "*" };
    mutable juce::CriticalSection learnStateLock;
    juce::String midiLearnStateJson;
    juce::String oscLearnStateJson;
    juce::String midiLearnInputDeviceId;
    juce::String midiRelayInputDeviceId;

    juce::String translateText(const juce::String& text);
    bool isStandaloneWrapper() const;
    int getDisplayIntervalIndex() const;
    void emitMidiTimecode(juce::MidiBuffer& midiMessages, int numSamples, int pos, int length);
    void broadcastOpusSyncSupport(const juce::String& target = "*");
    void refreshOpusSyncAvailabilityFromUsers();
    void applyCodecPreference();
    void setIntervalSyncStatusText(const juce::String& text);
    void broadcastIntervalSyncTag(const juce::String& target = "*");
    void broadcastTransportProbe(const juce::String& target = "*");
    juce::String buildIntervalSyncTag(int interval, int length) const;
    juce::File resolveVideoHelperRootDir() const;
    bool isAdvancedVideoClientAvailable() const;
    bool ensureAdvancedVideoClientStarted();
    void stopAdvancedVideoClient();
    void writeIntervalHelperJson(int pos, int length);
    void syncLocalIntervalChannelConfig();
    void flushOutboundMidiRelayEvents();
    void injectInboundMidiRelayEvents(juce::MidiBuffer& midiMessages);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NinjamVst3AudioProcessor)
};
