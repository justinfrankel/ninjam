# NINJAM Wire Protocol Specification

Reverse-engineered from the reference implementation in this repository
(`ninjam/` client library, `ninjam/server/` server). This document describes
everything an implementer needs to build an interoperable client or server
without reading the C++ source. Every claim cites the function or constant
in the reference tree that implements it, so citations stay valid as lines
move.

Source of truth:

| Area | Files |
|------|-------|
| Message framing | `ninjam/netmsg.h`, `ninjam/netmsg.cpp` |
| Message payloads (parse/build) | `ninjam/mpb.h`, `ninjam/mpb.cpp` |
| Message type IDs | `ninjam/mpb.h` (message-type classes) |
| Client state machine | `ninjam/njclient.cpp`, `ninjam/njclient.h` |
| Server connection handling | `ninjam/server/usercon.cpp`, `ninjam/server/usercon.h` |
| Server main loop / config | `ninjam/server/ninjamsrv.cpp` |
| Example server config | `ninjam/server/example.cfg` |

Exhaustiveness of the message-type list is proven in
[Appendix A](#appendix-a-exhaustiveness-of-the-message-set): every
`Send()`/receive site in the tree is enumerated, and no unlisted message type
exists.

---

## 1. Transport

* **Transport is plain TCP.** The client opens a `JNL_Connection` (JNetLib TCP)
  with 64 KiB send and receive buffers (`NJClient::Connect`); the
  server listens with `JNL_Listen` on the configured port and accepts with
  2×64 KiB / 64 KiB buffers (`main`).
* **Default port is 2049** (TCP): client default `NJ_PORT`, server config default
  (`ReadConfig`), example config `Port 2049`
  (`ninjam/server/example.cfg:3`). A client may override the port with
  `host:port` syntax (`NJClient::Connect`).
* **TCP_NODELAY is set** on every NINJAM connection
  (`Net_Connection`) — messages are latency-sensitive.
* There is no TLS and no proxy negotiation. The first bytes on the wire flow
  **server → client**: the server pushes an auth challenge the moment the
  connection is accepted (`User_Connection::User_Connection`, invoked by
  `User_Group::AddConnection`, which the accept loop calls from `main`).
* The protocol is byte-stream framed by NINJAM itself (section 2); TCP
  segmentation is irrelevant to parsing.

## 2. Message framing

Every message on the wire is:

```
+--------+---------------------+------------------------------+
| type   | size (4 × 8 bits)   | payload (exactly `size` bytes)|
| 1 byte | uint32 little-endian| (may be empty)               |
+--------+---------------------+------------------------------+
    1 byte        4 bytes              `size` bytes
```

* Header is **5 bytes**: a 1-byte type followed by the payload size as a
  **uint32 little-endian**. Encoding:
  `Net_Message::parseMessageHeader` (parse) and `Net_Message::makeMessageHeader` (build).
* **All multi-byte integers anywhere in the protocol are little-endian.**
  (`Net_Message::parseMessageHeader` for the header size; every payload field is
  serialized LSB-first, e.g. `mpb_server_auth_challenge::parse`, `mpb_client_auth_user::parse`.)
* Payload size limits:
  * `size` must satisfy `0 <= size <= 16384` (`NET_MESSAGE_MAX_SIZE`). A header with `size > 16384` is a framing error:
    the receiver aborts the connection (`Net_Message::parseMessageHeader`, `Net_Connection::Run`).
  * Type `0xFF` is `MESSAGE_INVALID` and is likewise rejected at the framing
    layer (`MESSAGE_INVALID`, `Net_Message::parseMessageHeader`).
  * Consequently the largest possible audio chunk in an interval-write
    message is `16384 - 17 = 16367` bytes (§ 5.8/§ 5.11).
* Messages are **self-delimiting**: a receiver reads 5 bytes, then exactly
  `size` bytes, then the next header. The reference receiver loops over
  available bytes, peeks, parses the header once, then consumes the payload
  (`Net_Connection::Run`). Partial headers/payloads are buffered until
  complete; the receive state machine never interleaves messages.
* There is no compression, no checksum, and no per-message acknowledgment.

### 2.1 Reserved type values

| Value | Name | Meaning |
|-------|------|---------|
| `0xFD` | `MESSAGE_KEEPALIVE` | Keepalive ping, sent automatically (sent automatically). See § 7. |
| `0xFE` | `MESSAGE_EXTENDED` | **Defined but never used anywhere in the tree** (sole reference in the tree). Treat as reserved: receivers ignore unknown types (§ 7.2), so do not rely on it. |
| `0xFF` | `MESSAGE_INVALID` | Illegal on the wire; framing error, disconnect (`MESSAGE_INVALID`, `Net_Message::parseMessageHeader`). |

### 2.2 String encoding

Strings are NUL-terminated byte strings, transmitted **including** the
terminating NUL. No character-set conversion is performed anywhere; text is
effectively UTF-8 pass-through. The server maps all received control
characters (`< 0x20`) in relayed chat/topic text to spaces before rebroadcast
(`sanitize_relay_text`, applied throughout `User_Group::onChatMessage`).
Usernames are further restricted: the
server rewrites any character that is not alphanumeric, `-`, `_`, `@`, or `.`
to `_`, and truncates at 128 bytes (`MAX_NICK_LEN`, applied in
`User_Connection::OnRunAuth`).

## 3. Protocol version and capability negotiation

* Protocol version range: `PROTO_VER_MIN = 0x00020000`,
  `PROTO_VER_MAX = 0x0002ffff`, `PROTO_VER_CUR = 0x00020000`
  (`ninjam/mpb.h`).
* The server advertises `PROTO_VER_CUR` in the auth challenge
  (`User_Connection::User_Connection`). The client requires
  `PROTO_VER_MIN <= version < PROTO_VER_MAX` and otherwise disconnects with
  error "server is incorrect protocol version"
  (`NJClient::Run`).
* The client sends its version in the auth user message; the server requires
  `PROTO_VER_MIN <= client_version <= PROTO_VER_MAX` and otherwise rejects
  with "incorrect client version" (`User_Connection::Run`). Note the reference client always sends
  `PROTO_VER_CUR` (`NJClient::Run`).
* Capability words:
  * **`server_caps`** (challenge): bit 0 = a license agreement string is
    appended to the challenge message (`mpb_server_auth_challenge::parse`, `mpb_server_auth_challenge::build`); **bits 8–15 = keepalive interval in seconds**
    (`NJClient::Run` reads `(caps>>8)&0xff`; the server sets
    `caps = keepalive<<8`, clamped to 0..255, at
    `User_Connection::User_Connection`). All other bits are unused;
    receivers must ignore unknown bits so future flags stay
    backwards-compatible.
  * **`client_caps`** (auth user): bit 0 = user agreed to the license
    (`mpb_client_auth_user`; client sets it at `NJClient::Run`; server
    checks it at `User_Connection::Run`). Bit 1 historically
    indicated "client_version field present" (`mpb_client_auth_user`), but in
    this codebase the version field is **always** present on the wire
    (`mpb_client_auth_user::build`) and always parsed by the
    server (`mpb_client_auth_user::parse`). The reference client sends
    `client_caps` of 0 or 1 only (`NJClient::Run`); servers
    must not require bit 1, and clients may set it or not without effect.

## 4. Handshake and authentication

### 4.1 Sequence

```
server                          client
  |                               |
  |-- 0x00 AUTH_CHALLENGE ------->|     (immediately on accept)
  |                               |  (optionally: user accepts license)
  |<-- 0x80 AUTH_USER ------------|
  |                               |
  |-- 0x01 AUTH_REPLY ----------->|     (success or failure)
  |-- 0x02 CONFIG_CHANGE_NOTIFY ->|     (BPM/BPI)
  |-- 0x03 USERINFO_CHANGE_NOTIFY-|     (current users, may be empty)
  |-- 0xC0 CHAT "TOPIC" --------->|
  |   (0xC0 CHAT "PRIVMSG" "*" -> |      MOTD lines, if configured)
  |                               |
  |<-- 0x82 SET_CHANNEL_INFO -----|     (client announces its channels)
  |                               |
  |-- 0xC0 CHAT "JOIN" ---------->|-> * (broadcast to *other* users)
```

The server constructs and sends the challenge in the `User_Connection`
constructor (`User_Connection::User_Connection`), so it is the first data
after the TCP handshake.

### 4.2 Challenge (server → client)

See § 5.1 for the byte layout. The 8-byte challenge is random, generated with
`WDL_RNG_bytes` (`User_Connection::User_Connection`).

### 4.3 Password hashing (client)

The client never sends the password. It computes (`NJClient::Run`):

```
inner  = SHA1( username_utf8 || ':' || password_utf8 )
reply  = SHA1( inner || challenge )        -- challenge = the 8 challenge bytes
```

and places `reply` (20 bytes) in the auth user message. The server stores
`SHA1(username + ":" + password)` per account
(`localUserInfoLookup`) and performs the identical second
hash over `SHA1(user:pass) || challenge`, comparing against the 20 bytes the
client sent (`User_Connection::OnRunAuth`). A mismatch (or unknown
user, when a password is required) is answered with `flag=0` "invalid
login/password" and disconnect (`User_Connection::OnRunAuth`).

### 4.4 Anonymous authentication

A username beginning with `anonymous` (`anonymous` exactly, or
`anonymous:desiredname`) is treated as anonymous **if the server allows it**
(`localUserInfoLookup`):

* `reqpass` is cleared, so the password hash is **not verified** — the client
  must still send a well-formed 20-byte hash field, but its contents are
  ignored (`User_Connection::OnRunAuth`).
* The effective username becomes `desiredname@<client-ip>` (or `anon@<ip>`
  when no name was requested). The desired name is truncated to 16
  characters and `@`/`.` are rewritten to `_`
  (`localUserInfoLookup`).
* If the server has `AnonymousMaskIP yes`, the last octet of the IP in the
  name is replaced with `x` (`localUserInfoLookup`).
* Anonymous users get the privileges configured by `AnonymousUsersCanChat`
  (chat permission), `AnonymousUsers multi` (multiple logins), and always
  `PRIV_VOTE`; channel count comes from `MaxChannels`'s anonymous value
  (`localUserInfoLookup`).

### 4.5 License agreement

If the server has a license configured (`ServerLicense`), the license text is
appended to the challenge message and `server_caps` bit 0 is set
(`User_Connection::User_Connection`, `mpb_server_auth_challenge::build`). The client
presents the text to the user (callback `LicenseAgreementCallback`); if the
user accepts, `client_caps` bit 0 is set in the auth user message
(`NJClient::Run`). A client that does not set the bit is
rejected with "license not agreed to" (`User_Connection::Run`).

While the license is pending, the connection's keepalive interval is relaxed
to 45 s on both sides so the user has time to read
(`NJClient::Run`, `User_Connection::User_Connection`).

### 4.6 Auth user timeout

The client must send `0x80` within **120 seconds** of connecting; afterwards
the server replies `flag=0` "authorization timeout" and disconnects
(`User_Connection::Run`). Caution for implementers: once the
challenge has been answered, this window closes; but note also that *any*
message that is not `0x80` received before authentication — including a
keepalive, if the client idles longer than its keepalive interval — is
treated as an invalid reply ("invalid authorization reply") and the
connection is killed (`User_Connection::Run`). Send the
auth user message promptly after receiving the challenge.

### 4.7 Auth reply (server → client)

Failure: `flag = 0`, followed by a NUL-terminated human-readable error
string. Known strings produced by the reference server
(`User_Connection::OnRunAuth`, `User_Connection::Run`):

* `invalid login/password`
* `server full` (account slots; `MaxUsers` minus hidden/reserved users,
  `User_Connection::OnRunAuth`)
* `authorization timeout`
* `invalid authorization reply` / `incorrect client version` /
  `license not agreed to`

The client displays the string and disconnects
(`NJClient::Run`).

Success: `flag = 1`, followed by a NUL-terminated string which is the
**effective username** assigned by the server (it may differ from what the
client sent: IP suffix, `-2` suffixes for multi-logins, sanitization —
`User_Connection::OnRunAuth`), followed by one byte `maxchan`: the
number of channels this user may use (clamped to `MAX_USER_CHANNELS` = 32;
**0 in lobby mode**,
`User_Connection::SendAuthReply`). The client stores the effective
username and `maxchan` (`NJClient::Run`).

### 4.8 Immediate post-auth server messages

After a successful auth the server sends, in order
(`User_Connection::OnRunAuth`):

1. `0x01` auth reply (§ 4.7);
2. `0x02` config change notify with current BPM/BPI
   (`User_Connection::OnRunAuth`, `User_Connection::SendConfigChangeNotify`);
3. `0x03` user info change notify listing every other active user's channels
   (skipped in lobby mode; § 5.4, `User_Connection::SendUserList`);
4. `0xC0` chat `PRIVMSG` from `"*"` per line of the MOTD file, if configured
   (`User_Connection::OnRunAuth`, `User_Connection::SendMOTDFile`);
5. `0xC0` chat `TOPIC` with the current topic
   (`User_Connection::OnRunAuth`);
6. (lobby only) `0xC0` chat `PRIVMSG` from `"*"` with lobby stats
   (`User_Connection::OnRunAuth`, `User_Connection::SendPrivateModeStats`);
7. broadcast `0xC0` chat `JOIN <username>` to all *other* users
   (`User_Connection::OnRunAuth`).

The client, upon success, immediately sends `0x82` set channel info
announcing its local channels (`NJClient::Run`, `NJClient::NotifyServerOfChannelChange`), which the server relays to everyone else as
`0x03` records (`User_Connection::Run`).

## 5. Message reference

Summary table (payload sizes exclude the 5-byte frame header):

| ID | Name | Direction | Min size | Notes |
|----|------|-----------|----------|-------|
| `0x00` | server_auth_challenge | S→C | 16 | + optional license text |
| `0x01` | server_auth_reply | S→C | 1 | + username/error, + maxchan |
| `0x02` | server_config_change_notify | S→C | 4 | exactly 4 |
| `0x03` | server_userinfo_change_notify | S→C | 0 | stream of records |
| `0x04` | server_download_interval_begin | S→C | 26 | + username |
| `0x05` | server_download_interval_write | S→C | 17 | + audio bytes |
| `0x80` | client_auth_user | C→S | 29 | 29 + len(username) |
| `0x81` | client_set_usermask | C→S | 0 | stream of records |
| `0x82` | client_set_channel_info | C→S | 2 | header + records |
| `0x83` | client_upload_interval_begin | C→S | 25 | exactly 25 |
| `0x84` | client_upload_interval_write | C→S | 17 | + audio bytes |
| `0xC0` | chat_message | both | 1 | up to 5 NUL strings |
| `0xFD` | keepalive | both | 0 | exactly 0 |

Minimum sizes are the values the parsers enforce (`mpb_server_auth_challenge::parse`,
`mpb_server_auth_reply::parse`, `mpb_server_config_change_notify::parse`, `mpb_server_userinfo_change_notify::parse`, `mpb_server_download_interval_begin::parse`, `mpb_server_download_interval_write::parse`, `mpb_client_auth_user::parse`, `mpb_client_set_usermask::parse`, `mpb_client_upload_interval_begin::parse`, `mpb_client_upload_interval_write::parse`, `mpb_chat_message::parse`
— noting `0x82`/`0x81`/`0x03` accept 0 and decode records lazily). `0xFD`
keepalive frames are 5 bytes total (empty payload).

### 5.1 `0x00` server_auth_challenge

`mpb_server_auth_challenge::parse`/`build`. Fields in order:

| Offset | Size | Type | Field |
|--------|------|------|-------|
| 0 | 8 | bytes | `challenge` — random nonce |
| 8 | 4 | uint32 LE | `server_caps` (§ 3) |
| 12 | 4 | uint32 LE | `protocol_version` |
| 16 | *n* | NUL string | license agreement text; **present iff `server_caps & 1`** |

### 5.2 `0x01` server_auth_reply

`mpb_server_auth_reply::parse`/`build`.

| Offset | Size | Type | Field |
|--------|------|------|-------|
| 0 | 1 | byte | `flag` — bit 0 set = success |
| 1 | *n* | NUL string | success: effective username; failure: error text. Omitted when the message is just 1 byte. |
| 1+n | 1 | byte | `maxchan` — max local channels (success only; present iff there is a byte after the string) |

### 5.3 `0x02` server_config_change_notify

`mpb_server_config_change_notify::parse`/`build`. Exactly 4 bytes:

| Offset | Size | Type | Field |
|--------|------|------|-------|
| 0 | 2 | uint16 LE | `beats_minute` — BPM |
| 2 | 2 | uint16 LE | `beats_interval` — BPI (beats per interval) |

Sent on join, on any BPM/BPI change (admin command, vote result, room
creation), and on config reload (`User_Connection::OnRunAuth`,
`User_Group::SetConfig`, `User_Group::onChatMessage`, `main`). The client recomputes its interval
length and starts audio on receipt
(`NJClient::Run`, `NJClient::updateBPMinfo`, `NJClient::AudioProc`).

### 5.4 `0x03` server_userinfo_change_notify

`mpb_server_userinfo_change_notify` (`parse`/`build` plus the record
codec `build_add_rec`/`parse_get_rec`). A stream of concatenated records;
empty payload = empty user list. Record layout:

| Offset | Size | Type | Field |
|--------|------|------|-------|
| 0 | 1 | byte | `active` — 1 = channel exists, 0 = channel removed |
| 1 | 1 | byte | `channelid` — 0..31 (clamped to 0..255 at build; clients must validate < 32) |
| 2 | 2 | int16 LE | `volume` — dB × 10 (e.g. 10 = +1 dB) |
| 4 | 1 | int8 | `pan` — −128..127 |
| 5 | 1 | byte | `flags` — see § 6.3 |
| 6 | *n* | NUL string | `username` |
| 6+n | *m* | NUL string | `channel name` |

Semantics (`NJClient::Run`): each record with `active=1`
creates or updates `(username, channelid)`; `active=0` removes it, and when a
user's last channel is removed the user disappears
(`NJClient::Run`). The server sends the full list on join
(`User_Connection::SendUserList`), deltas on channel-info changes
(`User_Connection::Run`), removals on user disconnect
(`User_Group::Run`), and additions on lobby→room
migration (`main`). If a user has no active
channels, the server synthesizes one placeholder record (channel 0, empty
name) so UIs show the user (`User_Connection::SendUserList`) unless
hidden users are allowed.

### 5.5 `0x04` server_download_interval_begin

`mpb_server_download_interval_begin::parse`/`build`. Announces the start of one user's audio transfer
for one interval:

| Offset | Size | Type | Field |
|--------|------|------|-------|
| 0 | 16 | bytes | `guid` — transfer ID |
| 16 | 4 | uint32 LE | `estsize` — estimated total bytes (informational; reference client always sends 0, `NJClient::Run`) |
| 20 | 4 | uint32 LE | `fourcc` — codec tag, `"OGGv"` (§ 6.2); 0 = special (§ 6.1) |
| 24 | 1 | byte | `chidx` — uploader's channel index |
| 25 | *n* | NUL string | `username` — uploader's effective name (added by the server, `User_Connection::Run`) |

### 5.6 `0x05` server_download_interval_write

`mpb_server_download_interval_write::parse`/`build`. One chunk of the transfer announced by § 5.5:

| Offset | Size | Type | Field |
|--------|------|------|-------|
| 0 | 16 | bytes | `guid` — matches the begin |
| 16 | 1 | byte | `flags` — bit 0 set on the **final** chunk |
| 17 | *n* | bytes | raw Ogg Vorbis stream data (§ 6.2) |

Byte-identical layout to `0x84` — the server forwards client uploads by
rewriting only the type byte (`User_Connection::Run`).

### 5.7 `0x80` client_auth_user

`mpb_client_auth_user::parse`/`build`.

| Offset | Size | Type | Field |
|--------|------|------|-------|
| 0 | 20 | bytes | `passhash` = SHA1(SHA1(user:pass) ‖ challenge) (§ 4.3); ignored for anonymous |
| 20 | *n* | NUL string | `username` (may be `anonymous:name`) |
| 20+n | 4 | uint32 LE | `client_caps` (§ 3) |
| 24+n | 4 | uint32 LE | `client_version` |

### 5.8 `0x81` client_set_usermask

`mpb_client_set_usermask` (`parse`/`build`/`build_add_rec`/`parse_get_rec`). Subscription state: one record per remote user the
client wants to hear. Record:

| Offset | Size | Type | Field |
|--------|------|------|-------|
| 0 | *n* | NUL string | `username` |
| n | 4 | uint32 LE | `channelmask` — bit *k* set = subscribe to channel *k* of that user |

Empty payload is valid. Sending a mask of 0 for an existing entry unsubscribes
(`User_Connection::Run`). The server routes interval data only
to subscribed users (§ 6.4). The reference client sends this when
auto-subscribing new users (`NJClient::Run`) and when the
local user toggles a subscription (`NJClient::SetUserChannelState`).

### 5.9 `0x82` client_set_channel_info

`mpb_client_set_channel_info` (`parse`/`build`/`build_add_rec`/`parse_get_rec`). Announces the sender's own local channels. Layout:

| Offset | Size | Type | Field |
|--------|------|------|-------|
| 0 | 2 | uint16 LE | `mpisize` — per-record info size; **4** in this protocol (`mpb_client_set_channel_info`, `mpb_client_set_channel_info::build_add_rec`) |
| 2 | … | records | concatenation of per-channel records |

Each record (`mpb_client_set_channel_info::build_add_rec`):

| Offset | Size | Type | Field |
|--------|------|------|-------|
| 0 | *n* | NUL string | `channel name` |
| n | 2 | int16 LE | `volume` — dB × 10 (client sends 0; `NJClient::NotifyServerOfChannelChange`) |
| n+2 | 1 | int8 | `pan` — −128..127 (client sends 0) |
| n+3 | 1 | byte | `flags` — bit 0x80 = **inactive/filler channel** (§ 6.3) |

The *i*-th record describes channel *i*. The server assigns `active = !(flags
& 0x80)` (`User_Connection::Run`), ignores records beyond the user's
`maxchan` (`User_Connection::Run`), treats all channels beyond the
last record as removed (`User_Connection::Run`), and broadcasts
the delta as `0x03` records to everyone else
(`User_Connection::Run`). The client re-sends this message after any
local channel change (`NJClient::DeleteLocalChannel` and friends call
`NJClient::NotifyServerOfChannelChange`).

### 5.10 `0x83` client_upload_interval_begin

`mpb_client_upload_interval_begin::parse`/`build`. Exactly 25 bytes — same first 25 bytes as § 5.5
but **without** the username (the server knows the sender):

| Offset | Size | Type | Field |
|--------|------|------|-------|
| 0 | 16 | bytes | `guid` |
| 16 | 4 | uint32 LE | `estsize` |
| 20 | 4 | uint32 LE | `fourcc` |
| 24 | 1 | byte | `chidx` |

### 5.11 `0x84` client_upload_interval_write

`mpb_client_upload_interval_write::parse`/`build`. Byte-identical layout to § 5.6.

### 5.12 `0xC0` chat_message

`mpb_chat_message::parse`/`build`. Payload is up to **5** NUL-terminated strings
concatenated (`parms[0..4]`, `mpb_chat_message`). The reference builder
always emits all five slots, so trailing unused parameters appear as bare
NUL bytes (`mpb_chat_message::build`); a parser must accept messages with
fewer parameters present (`mpb_chat_message::parse` stops at the end of the
payload). Commands are case-sensitive (dispatch comments on `NJClient`
in `ninjam/njclient.h`). See § 8 for
the full command set.

## 6. Audio transfer model (intervals)

### 6.1 Musical timing and the interval

* The server owns the tempo: BPM (beats/minute) and BPI (beats/interval),
  delivered via `0x02` (`User_Connection::SendConfigChangeNotify`). Defaults are
  120 BPM / 8 BPI unless configured (`ReadConfig`, `ninjam/server/example.cfg:39-40`); limits in the shipped config paths are
  `MIN_BPM 40, MAX_BPM 400, MIN_BPI 2, MAX_BPI 64` (`ninjam/server/usercon.h`,
  clamps in `ConfigOnToken`). (The `/bpm` and `/bpi` chat admin
  commands separately accept 20–400 BPM and 2–1024 BPI —
  `User_Group::onChatMessage` — an inconsistency in the reference
  server; treat 40–400 / 2–64 as the safe range.)
* Interval duration: `seconds = BPI × 60 / BPM`. The client computes the
  interval in samples as `bpi / (bpm/60) × samplerate`
  (`NJClient::AudioProc`); the server computes the same interval in
  wall-clock ms as `60 000 × bpi / bpm`
  (`User_Group::Run`).
* All users' intervals are aligned to the *server's* clock: the server merely
  logs interval ticks (`User_Group::Run`); clients align
  by starting their intervals when they receive/apply `0x02` and keeping time
  locally (`NJClient::AudioProc`). An interoperable client must
  upload each interval's audio *during* that interval (see § 6.5) and play
  downloads one interval later (that is the NINJAM latency-hiding trick; the
  client buffers decoded audio per channel, `NJClient::on_new_interval`).

### 6.2 Audio encoding: Ogg Vorbis inside interval messages

* The only codec in this protocol is Ogg Vorbis, tagged by fourcc
  `MAKE_NJ_FOURCC('O','G','G','v')` = `0x7647474F`
  (`NJ_ENCODER_FMT_TYPE`), which serialized
  little-endian appears on the wire as the four ASCII bytes `O G G v`
  (`4F 47 47 76`).
* The payload bytes of interval-write messages (§ 5.6/§ 5.11) are **raw,
  consecutive slices of a standard Ogg Vorbis bitstream**. The reference
  client encodes with libvorbis (VBR, quality derived from the channel
  bitrate, random stream serial number; `NJClient::Run`, encoder
  `VorbisEncoder` in `WDL/vorbisencdec.h`) and transmits the encoder output verbatim:
  the first chunks a receiver sees for a guid are the stream's Ogg pages
  (identification/comment/setup headers), followed by audio pages.
* **The server treats `fourcc` as opaque.** It relays whatever value the
  uploader sent to subscribers verbatim, and otherwise uses it only to
  derive the filename extension when archiving intervals
  (`User_Connection::Run`) and for the `fourcc = 0` special cases below.
  Interoperable peers may send other fourccs without breaking the relay —
  but note the reference client's decoder side ignores the announced
  fourcc entirely: `NJClient::start_decode` tries only `OGGv` regardless
  of what the begin message said (its `types[]` table has a single entry;
  see the `todo` there). Anything that is not Ogg Vorbis will relay fine
  but decode as garbage on reference clients.
* A receiver reconstructs the stream by **concatenating the payload bytes of
  all interval-write chunks with the same guid, in arrival order**, into one
  buffer/file, and handing it to any Ogg Vorbis decoder
  (`RemoteDownload::Write` appends to a
  file and/or an in-memory buffer that feeds the decoder). There is no extra
  per-chunk framing beyond the message header.
* Chunks are sized by the encoder's buffering: at least ~2 KiB per message in
  normal mode (`MIN_ENC_BLOCKSIZE`, hard cap `MAX_ENC_BLOCKSIZE = 9216`,
  `NJClient::Run`), smaller granularity in live/voicechat mode
  (`LIVE_PREBUFFER`/`LIVE_ENC_BLOCKSIZE1`/`LIVE_ENC_BLOCKSIZE2`,
  `NJClient::Run`). An interop implementation
  may chunk however it likes within the 16367-byte payload limit.
* **Special guids/fourccs** (`User_Connection::Run`,
  `NJClient::Run`):
  * `guid = 0` (16 zero bytes) **and** `fourcc = 0`: a *silence marker*. The
    client sends `0x83` with zero guid/fourcc when a channel stops
    broadcasting (`NJClient::Run`). The server relays the
    begin to subscribers but tracks no transfer
    (`User_Connection::Run`). A client receiving it
    schedules one silent interval for that channel
    (`NJClient::Run`).
  * `fourcc = 0` with nonzero guid: a reference to a previously transferred
    stream (session-mode replay); clients that have the guid cached re-use
    it (`NJClient::Run`). The server still relays it.
  * `guid != 0, fourcc != 0`: a normal audio transfer.

### 6.3 Channel flags

Bit field used in `0x03`/`0x82` records (`mpb_client_set_channel_info`):

| Bit | Meaning |
|-----|---------|
| `&1` | no default subscribe |
| `&2` | "instamode" (live/voicechat: fixed tiny intervals, no interval boundary flush) |
| `&4` | session mode (arbitrary-length chunks + `SESSION` chat messages, § 8) |
| `&0x80` | filler: channel is inactive (`User_Connection::Run`, `NJClient::NotifyServerOfChannelChange`) |

### 6.4 Transfer routing and subscriptions

The server relays uploads **only to subscribed users**
(`User_Connection::Run`): for each other user with a
`usermask` entry naming the uploader where `channelmask & (1<<chidx)`, the
server rewrites the `0x83` begin into a `0x04` begin with the uploader's
username appended and forwards it, registering a transfer state so subsequent
`0x84` writes are forwarded as `0x05` (`User_Connection::Run`).
Transfer state expires after 8 idle seconds (`TRANSFER_TIMEOUT`, `User_Connection::Run`); the
client similarly drops downloads idle for 8 s (`DOWNLOAD_TIMEOUT`, `NJClient::Run`).

### 6.5 Client upload lifecycle

For each broadcasting local channel, per interval
(`NJClient::Run`):

1. On the first encoded data of the interval, generate a fresh 16-byte
   random guid (`NJClient::Run`) and build the `0x83` begin
   (fourcc `OGGv`, `chidx`, `estsize` 0) — but hold it
   (`NJClient::Run`).
2. When the first chunk of encoded audio is available, send the held `0x83`,
   then `0x84` chunks with `flags = 0` as data becomes available
   (`NJClient::Run`).
3. At the interval boundary, flush the encoder and send the remaining chunks;
   the last one has `flags & 1` set (`NJClient::Run`).
   *An implementation must terminate every announced transfer with a
   flags&1 chunk* — otherwise transfer state lingers until the 8 s timeout.
4. When a channel goes silent (stops broadcasting), send one `0x83` with
   zero guid and fourcc 0 at the boundary (`NJClient::Run`,
   transition detection `NJClient::on_new_interval`).

The `0x82` channel-info message must be sent before/with the first uploads so
other users have the channel in their user list (client does it right after
auth success, `NJClient::Run`).

## 7. Keepalive and connection teardown

### 7.1 Keepalive

* If nothing has been sent for `keepalive` seconds, either side sends
  `0xFD` with an empty payload (`Net_Connection::Run`).
* The interval comes from the server: `server_caps` bits 8–15
  (`User_Connection::User_Connection` → `NJClient::Run`, applied
  at `NJClient::Run`); value 0 falls back to
  `NET_CON_KEEPALIVE_RATE = 3` seconds (`NET_CON_KEEPALIVE_RATE`, `Net_Connection::SetKeepAlive`). The server's value is configured by
  `SetKeepAlive` (`ConfigOnToken`, `ninjam/server/example.cfg:55-57`).
* Receiving **any** message (including `0xFD`) refreshes the peer's liveness
  timestamp (`Net_Connection::Run`).

### 7.2 Disconnect conditions

| Condition | Detected by | Cite |
|-----------|-------------|------|
| No inbound message for `3 × keepalive` seconds | both sides — connection error | `Net_Connection::Run` |
| Invalid frame header (type `0xFF`, size > 16384) | both sides | `Net_Message::parseMessageHeader`, `Net_Connection::Run` |
| Send queue overflow (> 512 queued messages) | both sides | `NET_CON_MAX_MESSAGES`, `Net_Connection::Send` |
| TCP error/close | both sides | `Net_Connection::GetStatus` |
| No auth within 120 s of connect | server | `User_Connection::Run` |
| Non-`0x80` message before auth (incl. keepalive) | server | `User_Connection::Run` |
| Failed auth / version / license / full | server → sends `0x01` flag=0 first | § 4.7 |

* Unknown message types **after** auth are silently ignored by both sides
  (`NJClient::Run`; `User_Connection::Run`).
  This includes `0xFD` keepalives on the application level — they are
  consumed by the connection layer's liveness logic.
* On user disconnect the server broadcasts `0xC0` `PART <username>` and
  `0x03` removal records for all the user's channels
  (`User_Group::Run`).

## 8. Chat subsystem (`0xC0`)

Parameters are positional (`parms[0]` first string). Commands are
case-sensitive; usernames are matched case-insensitively
(`User_Connection::Run`, `User_Group::onChatMessage`).

### 8.1 Client → server commands

| Command |Parms| Behavior |
|---------|-------|----------|
| `MSG` | `<text>` | Broadcast text to everyone. Text may begin with `!` for special commands (below). Requires chat privilege. Server relays as `MSG <sender> <text>` (`User_Group::onChatMessage`). |
| `PRIVMSG` | `<username>` `<text>` | Private message. Exact (case-insensitive) match first, then partial match if the prefix ends at an `@` or contains one (`User_Group::onChatMessage`). Relay: `PRIVMSG <sender> <text>`. |
| `SESSION` | `<guid-hex>` `<chidx>` `<start> <len>` | Session-mode block descriptor; relayed with the sender's name inserted as parm 1 (`User_Group::onChatMessage`). Sent by session-mode clients (`NJClient::Run`). |

`!`-commands inside `MSG` text (`User_Group::onChatMessage`):
`!topic` (query topic), `!vote bpm <n>` / `!vote bpi <n>` (voting system,
privilege-gated), and in lobby mode `!stat`, `!join <room>`; any other text
starting with `!` gets a command-list reply.

The reference server additionally documents `TOPIC <topic>` as a
client→server command (`mpb_chat_message`), but this server's dispatcher does
not handle it — topic changes go through the `ADMIN` command below; an
incoming `TOPIC` falls through to the unknown branch and is dropped
(`User_Group::onChatMessage`).

### 8.2 `ADMIN` (client → server)

`ADMIN <command ...>` (`User_Group::onChatMessage`). Commands:
`topic <text>` (priv `T`), `kick <user|prefix*>` (priv `K`), `bpm <n>` /
`bpi <n>` (priv `B`), `stat` (private-mode stats), `join <room>` (lobby
only). Errors are sent back as `MSG` from the empty username
(`User_Group::onChatMessage`).

### 8.3 Server → client messages

| Command | Parms | Meaning |
|---------|-------|---------|
| `MSG` | `<username>` `<text>` | Public message from `<username>`; empty username = server/system message (chat callback comments on `NJClient` in `ninjam/njclient.h`) |
| `PRIVMSG` | `<username>` `<text>` | Private message; username `"*"` = server notice (MOTD, lobby stats, lobby help) (`User_Connection::SendPrivateModeStats`, `User_Connection::SendMOTDFile`, `User_Group::onChatMessage`) |
| `TOPIC` | `<changed-by>` `<topic>` | Topic; empty `<changed-by>` on join/`!topic` query (`User_Connection::SendConnectInfo`, `User_Group::onChatMessage`) |
| `JOIN` | `<username>` | A user joined (`User_Connection::OnRunAuth`, `main`) |
| `PART` | `<username>` | A user left (`User_Group::Run`, `main`) |
| `USERCOUNT` | `<count>` `<max>` | Sent once to a joining (status) client (`User_Connection::SendConnectInfo`) |
| `SESSION` | `<username>` `<guid-hex>` `<chidx>` `<start> <len>` | Session descriptor from another user (`NJClient::Run`) |

## 9. Server configuration knobs that affect the wire

From `ConfigOnToken` and `ninjam/server/example.cfg`:

| Directive | Wire effect |
|-----------|-------------|
| `Port <n>` | TCP listen port (default 2049) (`ConfigOnToken`, `ReadConfig`) |
| `MaxUsers <n>` | Excess users rejected with `server full` (`ConfigOnToken`; `User_Connection::OnRunAuth`) |
| `MaxChannels <user> [anon]` | Bounds the `maxchan` byte of auth replies and per-user upload/channel limits (`ConfigOnToken`; `User_Connection::SendAuthReply`, `User_Connection::Run`) |
| `ServerLicense <file>` | License text appended to `0x00`; `server_caps` bit 0; clients must echo agreement in `client_caps` bit 0 (`ConfigOnToken`; `User_Connection::User_Connection`, `User_Connection::Run`) |
| `SetKeepAlive <s>` | Keepalive seconds, sent in `server_caps` bits 8–15 (`ConfigOnToken`; `User_Connection::User_Connection`) |
| `DefaultBPM` / `DefaultBPI` | Initial values of `0x02` (`ConfigOnToken`, `main`) |
| `DefaultTopic <text>` | Text of the `TOPIC` chat message on join (`ConfigOnToken`; `User_Connection::OnRunAuth`) |
| `MOTDFile <file>` | Lines sent as `PRIVMSG` from `"*"` on join (`ConfigOnToken`; `User_Connection::SendMOTDFile`) |
| `User <name> <pass> [privs]` | Account table; priv letters `*TBCKRMHVP` map to topic/bpm/chat/kick/reserve/multi-login/hidden/vote/show-private privileges (`ConfigOnToken`; flags `PRIV_*` in `ninjam/server/usercon.h`). Default privs: chat + vote (`ConfigOnToken`) |
| `AnonymousUsers no\|yes\|multi` | Enables anonymous auth; `multi` allows concurrent same-name logins (server appends `.N` to the name, `User_Connection::OnRunAuth`) (`ConfigOnToken`) |
| `AnonymousUsersCanChat yes\|no` | Grants chat privilege to anonymous users (`ConfigOnToken`, `localUserInfoLookup`) |
| `AnonymousMaskIP yes\|no` | Masks IP in anonymous effective names (`ConfigOnToken`, `localUserInfoLookup`) |
| `AllowHiddenUsers yes\|no` | Whether channel-less users appear (synthesized `0x03` record suppressed) (`ConfigOnToken`; `User_Connection::SendUserList`, `User_Connection::Run`) |
| `StatusUserPass <u> <p>` | Special status account: auths, receives user list + config/topic/usercount, then is disconnected — used by server-list pingers (`localUserInfoLookup`, `ConfigOnToken`; `User_Connection::OnRunAuth`) |
| `ACL <cidr> allow\|deny\|reserve` | Connection-level filtering/reserved slots; no message-level effect (`ConfigOnToken`, `main`) |
| `SetVotingThreshold <n>` / `SetVotingVoteTimeout <s>` | Governs `!vote` acceptance and the `0x02` broadcast on a successful vote (`ConfigOnToken`; `User_Group::onChatMessage`) |
| `PrivateGroupMode` / `PrivateGroupLobbySize` / `PrivateGroupAllowChat` / `PrivateGroupLobbyMOTDFile` / `PrivateGroupPublicPrefix` | Lobby mode: joining users get `maxchan = 0`, no user list, `!join <room>` migrates them to a new `User_Group` (fresh `0x01`/`0x02`/userlist/topic sequence) (`ConfigOnToken`, `main`; `User_Connection::SendAuthReply`, `User_Connection::SendUserList`) |
| `SessionArchive <path> <minutes>` | Server-side recording of interval uploads to disk; no wire effect (`ConfigOnToken`; `User_Connection::Run`) |

## 10. Worked transcript

A complete, byte-annotated session against a server configured with
`DefaultBPM 120`, `DefaultBPI 8`, `MaxChannels 32 2`, `AnonymousUsers no`,
one registered user, and one existing user **bob** (channel 0 "guitar") who
has subscribed to everyone. Client user **alice** / password **secret**
connects from `203.0.113.7` and is accepted as `alice@203.0.113.7`. HMAC-free
auth values below are real SHA-1 outputs:

```
SHA1("alice:secret")                        = 6985e52cea44a28695d5c440bd42f57e9f50b7b1
challenge                                   = 0123456789abcdef
SHA1(6985…b7b1 || 0123456789abcdef)         = 63fd01e27c12cc2ad826ae47b4d6b2c9fd20a42e
```

Notation: each frame is `type size` header then payload; comments show field
breakdown. Sizes are decimal.

### 10.1 Connect and authenticate

```
S>C  00 10 00 00 00                       AUTH_CHALLENGE, size 16
       01 23 45 67 89 AB CD EF            challenge (8)
       00 03 00 00                        server_caps = 0x00000300
                                          (keepalive 3 s in bits 8-15, no license bit)
       00 00 02 00                        protocol_version = 0x00020000

C>S  80 22 00 00 00                       AUTH_USER, size 34
       63 FD 01 E2 7C 12 CC 2A D8 26      passhash (20) =
       AE 47 B4 D6 B2 C9 FD 20 A4 2E        SHA1(SHA1("alice:secret")||challenge)
       61 6C 69 63 65 00                  username "alice\0"
       00 00 00 00                        client_caps = 0 (no license was offered)
       00 00 02 00                        client_version = 0x00020000

S>C  01 14 00 00 00                       AUTH_REPLY, size 20
       01                                 flag = success
       61 6C 69 63 65 40 32 30 33 2E      effective username "alice@203.0.113.7\0"
       30 2E 31 31 33 2E 37 00
       20                                 maxchan = 32 (MaxChannels' registered-user value)
```

### 10.2 Post-auth configuration

```
S>C  02 04 00 00 00                       CONFIG_CHANGE_NOTIFY, size 4
       78 00                              BPM = 120
       08 00                              BPI = 8
                                          (interval = 8 × 60 / 120 = 4.0 s)

S>C  03 11 00 00 00                       USERINFO_CHANGE_NOTIFY, size 17
       01                                 active = 1
       00                                 channelid = 0
       00 00                              volume = 0 (0.0 dB)
       00                                 pan = 0
       00                                 flags = 0
       62 6F 62 00                        username "bob\0"
       67 75 69 74 61 72 00               channel name "guitar\0"

S>C  C0 30 00 00 00                       CHAT_MESSAGE, size 48
       "TOPIC\0"                         parms[0]
       "\0"                              parms[1] (empty: not changed by a user)
       "Welcome to NINJAM. Please play nicely.\0"   parms[2]
       00 00                             parms[3], parms[4] (empty; the builder
                                          always emits all five slots)
```

### 10.3 Alice announces her channel

```
C>S  82 12 00 00 00                       SET_CHANNEL_INFO, size 18
       04 00                              mpisize = 4
       63 68 61 6E 6E 65 6C 20 6F 6E 65 00   channel name "channel one\0"
       00 00                              volume = 0
       00                                 pan = 0
       00                                 flags = 0 (active)

S>C(bob)  03 24 00 00 00                  USERINFO_CHANGE_NOTIFY relayed to bob, size 36
       01 00                              active, channelid
       00 00 00 00                        volume
       00 00                              pan, flags
       61 6C 69 63 65 40 32 30 33 2E      "alice@203.0.113.7\0"
       30 2E 31 31 33 2E 37 00
       63 68 61 6E 6E 65 6C 20 6F 6E 65 00   "channel one\0"

S>C(bob)  C0 1A 00 00 00                  CHAT_MESSAGE to bob, size 26
       "JOIN\0" "alice@203.0.113.7\0"     parms[0], parms[1]
       00 00 00                           parms[2..4] empty
                                          (bob's client may reply with a
                                           SET_USERMASK subscribing to alice;
                                           payload e.g.
                                           "alice@203.0.113.7\0" 01 00 00 00)
```

### 10.4 One full interval exchange (4 seconds, BPI 8 @ 120 BPM)

Alice broadcasts on channel 0. Her client uploads during the interval; the
server relays to bob (subscribed). Chunks marked `…` elide Ogg page bytes.

```
C>S  83 19 00 00 00                       UPLOAD_INTERVAL_BEGIN, size 25
       00 × 16                            guid = 0
       00 00 00 00                        estsize = 0
       00 00 00 00                        fourcc = 0
       00                                 chidx = 0
     (silence marker emitted at the boundary where her channel last went
      quiet; if she has been broadcasting continuously this frame is absent)

C>S  83 19 00 00 00                       UPLOAD_INTERVAL_BEGIN, size 25
       A1 B2 C3 D4 E5 F6 07 18            guid = a1b2c3d4e5f60718
       29 3A 4B 5C 6D 7E 8F 90              293a4b5c6d7e8f90 (random, 16 bytes)
       00 00 00 00                        estsize = 0
       4F 47 47 76                        fourcc "OGGv"
       00                                 chidx = 0

S>C(bob)  04 2B 00 00 00                  DOWNLOAD_INTERVAL_BEGIN to bob, size 43
       A1 B2 … 8F 90                      same guid (16)
       00 00 00 00                        estsize
       4F 47 47 76                        fourcc "OGGv"
       00                                 chidx
       61 6C 69 63 65 40 32 30 33 2E      "alice@203.0.113.7\0"   <- server adds
       30 2E 31 31 33 2E 37 00

C>S  84 45 00 00 00                       UPLOAD_INTERVAL_WRITE, size 69
       A1 B2 … 8F 90                      guid (16)
       00                                 flags = 0
       4F 67 67 53 …                      52 bytes of Ogg Vorbis stream
                                          (starts with "OggS" page: id header)

S>C(bob)  05 45 00 00 00                  DOWNLOAD_INTERVAL_WRITE to bob, size 69
       (identical payload; only the type byte changed)

C>S  84 25 00 00 00                       UPLOAD_INTERVAL_WRITE, size 37
       A1 B2 … 8F 90                      guid
       01                                 flags = 1  <- final chunk of interval
       … 20 bytes of Ogg Vorbis data …

S>C(bob)  05 25 00 00 00                  DOWNLOAD_INTERVAL_WRITE to bob
       (final chunk likewise)

S>C(alice) 04 2B 00 00 00                 bob's interval arrives for alice the
           …                              same way, with username "bob\0"
S>C(alice) 05 … 00 00 00                  (alice subscribed via auto-subscribe)
```

### 10.5 Chat

```
C>S  C0 16 00 00 00                       CHAT_MESSAGE, size 22
       "MSG\0" "hello everyone\0"         parms[0], parms[1]
       00 00 00                           parms[2..4] empty

S>C(bob)  C0 27 00 00 00                  CHAT_MESSAGE relayed, size 39
       "MSG\0"
       "alice@203.0.113.7\0"              sender inserted by server
       "hello everyone\0"
       00 00                              parms[3..4] empty

S>C(alice) C0 30 00 00 00                 (if someone changes the topic)
           "TOPIC\0" "bob\0" "<new topic>\0" + trailing empty slots
```

### 10.6 Steady state

After the exchange above the connection simply continues: each interval both
clients upload their channels (§ 6.5) and receive the channels they
subscribe to; either side may emit `0xFD 00 00 00 00` (5 bytes, empty
payload) whenever it has nothing else to send for one keepalive period (3 s
here).

## Appendix A: Exhaustiveness of the message set

All message IDs are defined in one place: the `mpb_*` classes in
`ninjam/mpb.h`, plus the framing-level types `MESSAGE_KEEPALIVE` /
`MESSAGE_EXTENDED` / `MESSAGE_INVALID` in `ninjam/netmsg.h`. Grepping the entire tree for
`MESSAGE_` usage outside the definitions yields exactly these files:
`ninjam/netmsg.cpp` (framing: invalid/keepalive), `ninjam/njclient.cpp`
(client dispatch), and `ninjam/server/usercon.cpp` (server dispatch). No other
translation unit constructs or inspects protocol messages.

### A.1 Client sends (`m_netcon->Send` / `ChatMessage_Send`)

| Function | Message |
|------|---------|
| `NJClient::Run` | `0x80` auth user |
| `NJClient::Run` | `0x81` usermask (auto-subscribe) |
| `NJClient::SetUserChannelState` | `0x81` usermask (subscribe toggle) |
| `NJClient::Run` | `0x83` begin — silence marker (zero guid/fourcc) |
| `NJClient::Run` | `0x83` begin — audio (built in `NJClient::Run`, held until first data) |
| `NJClient::Run` | `0x84` write |
| `NJClient::Run` → `NJClient::ChatMessage_Send` | `0xC0` chat (`SESSION`) |
| `NJClient::ChatMessage_Send` | `0xC0` chat (public API) |
| `Net_Connection::Run` | `0xFD` keepalive (automatic) |

### A.2 Client receives (`NJClient::Run`)

The dispatch switch in `NJClient::Run` handles `0x00` challenge, `0x01`
auth reply, `0x02` config change, `0x03` userinfo change, `0x04` download
begin, `0x05` download write and `0xC0` chat; everything else (including
`0xFD`) is ignored.

### A.3 Server sends

| Site | Message |
|------|---------|
| `User_Connection::User_Connection` | `0x00` challenge (on connect) |
| `User_Connection::SendAuthReply` | `0x01` auth success (reused for room migration at `main`) |
| `User_Connection::OnRunAuth`, `User_Connection::Run` | `0x01` auth failures |
| `User_Connection::SendConfigChangeNotify`, `User_Connection::SendConnectInfo` | `0x02` config |
| `User_Group::SetConfig`, `User_Group::onChatMessage` (votes, admin bpm/bpi) | `0x02` config broadcast |
| `User_Connection::SendUserList` | `0x03` user list (also `main`) |
| `User_Connection::Run`, `User_Group::Run`; `main` | `0x03` delta broadcasts |
| `User_Connection::Run` | `0x04` download begin (relay) |
| `User_Connection::Run` | `0x05` download write (relay; only the type byte is rewritten) |
| `User_Connection::SendConnectInfo`, `User_Connection::OnRunAuth` (join), `User_Group::onChatMessage` (`!topic`, `ADMIN topic`) | `0xC0` `TOPIC` |
| `User_Connection::SendConnectInfo` | `0xC0` `USERCOUNT` |
| `User_Connection::SendPrivateModeStats`, `User_Connection::SendMOTDFile`, `User_Connection::OnRunAuth`, `User_Group::onChatMessage` | `0xC0` `PRIVMSG` from `"*"` (MOTD, lobby stats/help) |
| `User_Connection::OnRunAuth`, `main` | `0xC0` `JOIN` |
| `User_Group::Run`, `main` | `0xC0` `PART` |
| `User_Group::onChatMessage`, `main` | `0xC0` `MSG` (broadcast/system) |
| `User_Group::onChatMessage` | `0xC0` `PRIVMSG` relay |
| `User_Group::onChatMessage` | `0xC0` `SESSION` relay |

### A.4 Server receives (`User_Connection::Run`)

`User_Connection::Run` dispatches `0x80` pre-auth (any other type before
auth is rejected), `0x82` channel info, `0x81` usermask, `0x83` upload
begin, `0x84` upload write and `0xC0` chat; everything else (including
`0xFD`) is ignored.

No sender or receiver for any type outside this list exists in the tree, and
no type is defined that is not accounted for above (`0xFE` `MESSAGE_EXTENDED`
is defined in `ninjam/netmsg.h` and referenced nowhere else).

## Appendix B: Implementer's checklist

1. TCP connect; read `0x00`; validate `16 <= protocol_version` bounds
   (§ 3) and note keepalive from caps bits 8–15.
2. Compute the double SHA-1 (§ 4.3); send `0x80` **immediately** (§ 4.6).
3. Expect `0x01`; on failure display error string and close; on success store
   effective username and `maxchan`.
4. Expect `0x02` (starts your musical clock), `0x03` (user list), `0xC0`
   `TOPIC`; optional `PRIVMSG`-`"*"` MOTD lines.
5. Send `0x82` with your channels (respect `maxchan`).
6. Auto-subscribe (optional): send `0x81` records.
7. Per interval: upload per § 6.5; parse `0x04`/`0x05` pairs per § 6.4,
   concatenating payloads by guid and decoding as Ogg Vorbis (§ 6.2).
8. Send `0xFD` when idle `keepalive` seconds; treat `3 × keepalive` of
   silence as a dead connection (§ 7).
9. Ignore unknown message types and unknown chat commands; never send text
   containing bytes < 0x20.
