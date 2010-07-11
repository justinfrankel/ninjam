#ifdef _WIN32
#include <windows.h>
#else
#include <stdlib.h>
#include <string.h>
#endif

#include <ctype.h>

#include "projectmode.h"

/********
todo: 
messages for:

  client->server:
    put data
    request data (optional revision#)
    request list of data
    request revision history of data
    request project switch?

  server->client
    put data reply (ok?)
    request data reply (data...)
    request list of data reply (list of items, hashes, timestamps, lengths)
    revision history request reply
    notify client of new revision (includes revision data)


  client on connect, would go and request everything, detect what was different, request it
    -- detect conflicts, rename old tracks to XXXconflict - (private), auto-ignore tracks with (private), also auto-ignore unnamed tracks

  subsequently server would notify if other users modded things



  // dir format:

  .tmp.* has uploading data (will get appended atomically to appropriate place)

  track_guid(_items|_cfg|_fx), project_misc

  media_whatever_filename.ext(.ogg)


  each file(database) consists of:

  <10 bytes magic>
  <4 bytes data length> 
  <2 bytes trailing header length>

  <data> 

  trailing header, last 128 bytes are:
     16 byte magic
     4 byte start of data offset from magic
     4 byte data length ( can be less if we want more data stuffed before this block)
     20 byte SHA1
     4 byte timestamp
     32 byte user
     8 byte revision count
     (40) byte pad/future
  


  todo: proxy PCM source that will use xxx.ext-proxy.ogg but serialize as xxx.ext, if xxx.ext is not found
*********/


