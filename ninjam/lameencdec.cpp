#include <windows.h>
#include <stdio.h>

#pragma pack(push)
#pragma pack(1)

#ifdef	__cplusplus
extern "C" {
#endif

/* encoding formats */

#define		BE_CONFIG_MP3			0										
#define		BE_CONFIG_LAME			256		

/* type definitions */

typedef		unsigned long			HBE_STREAM;
typedef		HBE_STREAM				*PHBE_STREAM;
typedef		unsigned long			BE_ERR;

/* error codes */

#define		BE_ERR_SUCCESSFUL					0x00000000
#define		BE_ERR_INVALID_FORMAT				0x00000001
#define		BE_ERR_INVALID_FORMAT_PARAMETERS	0x00000002
#define		BE_ERR_NO_MORE_HANDLES				0x00000003
#define		BE_ERR_INVALID_HANDLE				0x00000004
#define		BE_ERR_BUFFER_TOO_SMALL				0x00000005

/* other constants */

#define		BE_MAX_HOMEPAGE			128

/* format specific variables */

#define		BE_MP3_MODE_STEREO		0
#define		BE_MP3_MODE_JSTEREO		1
#define		BE_MP3_MODE_DUALCHANNEL	2
#define		BE_MP3_MODE_MONO		3



#define		MPEG1	1
#define		MPEG2	0

#ifdef _BLADEDLL
#undef FLOAT
	#include <Windows.h>
#endif

#define CURRENT_STRUCT_VERSION 1
#define CURRENT_STRUCT_SIZE sizeof(BE_CONFIG)	// is currently 331 bytes

/* OBSOLETE, VALUES STILL WORK
typedef enum 
{
	NORMAL_QUALITY=0,
	LOW_QUALITY,
	HIGH_QUALITY,
	VOICE_QUALITY
} LAME_QUALTIY_PRESET;

*/


typedef enum
{
	VBR_METHOD_NONE			= -1,
	VBR_METHOD_DEFAULT		=  0,
	VBR_METHOD_OLD			=  1,
	VBR_METHOD_NEW			=  2,
	VBR_METHOD_MTRH			=  3,
	VBR_METHOD_ABR			=  4
} VBRMETHOD;

typedef enum 
{
	LQP_NOPRESET=-1,

	// QUALITY PRESETS
	LQP_NORMAL_QUALITY=0,
	LQP_LOW_QUALITY,
	LQP_HIGH_QUALITY,
	LQP_VOICE_QUALITY,
	LQP_R3MIX_QUALITY,
	LQP_VERYHIGH_QUALITY,

	// NEW PRESET VALUES
	LQP_PHONE	=1000,
	LQP_SW		=2000,
	LQP_AM		=3000,
	LQP_FM		=4000,
	LQP_VOICE	=5000,
	LQP_RADIO	=6000,
	LQP_TAPE	=7000,
	LQP_HIFI	=8000,
	LQP_CD		=9000,
	LQP_STUDIO	=10000

} LAME_QUALTIY_PRESET;



typedef struct	{
	DWORD	dwConfig;			// BE_CONFIG_XXXXX
								// Currently only BE_CONFIG_MP3 is supported
	union	{

		struct	{

			DWORD	dwSampleRate;	// 48000, 44100 and 32000 allowed.  RG note: also seems to support 16000, 22050, 24000.
			BYTE	byMode;			// BE_MP3_MODE_STEREO, BE_MP3_MODE_DUALCHANNEL, BE_MP3_MODE_MONO
			WORD	wBitrate;		// 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256 and 320 allowed.  RG note: also seems to support 8,16,24.
			BOOL	bPrivate;		
			BOOL	bCRC;
			BOOL	bCopyright;
			BOOL	bOriginal;

			} mp3;					// BE_CONFIG_MP3

			struct
			{
			// STRUCTURE INFORMATION
			DWORD			dwStructVersion;	
			DWORD			dwStructSize;

			// BASIC ENCODER SETTINGS
			DWORD			dwSampleRate;	// SAMPLERATE OF INPUT FILE
			DWORD			dwReSampleRate;	// DOWNSAMPLERATE, 0=ENCODER DECIDES  
			LONG			nMode;			// BE_MP3_MODE_STEREO, BE_MP3_MODE_DUALCHANNEL, BE_MP3_MODE_MONO
			DWORD			dwBitrate;		// CBR bitrate, VBR min bitrate
			DWORD			dwMaxBitrate;	// CBR ignored, VBR Max bitrate
			LONG			nPreset;		// Quality preset, use one of the settings of the LAME_QUALITY_PRESET enum
			DWORD			dwMpegVersion;	// FUTURE USE, MPEG-1 OR MPEG-2
			DWORD			dwPsyModel;		// FUTURE USE, SET TO 0
			DWORD			dwEmphasis;		// FUTURE USE, SET TO 0

			// BIT STREAM SETTINGS
			BOOL			bPrivate;		// Set Private Bit (TRUE/FALSE)
			BOOL			bCRC;			// Insert CRC (TRUE/FALSE)
			BOOL			bCopyright;		// Set Copyright Bit (TRUE/FALSE)
			BOOL			bOriginal;		// Set Original Bit (TRUE/FALSE)
			
			// VBR STUFF
			BOOL			bWriteVBRHeader;	// WRITE XING VBR HEADER (TRUE/FALSE)
			BOOL			bEnableVBR;			// USE VBR ENCODING (TRUE/FALSE)
			INT				nVBRQuality;		// VBR QUALITY 0..9
			DWORD			dwVbrAbr_bps;		// Use ABR in stead of nVBRQuality
			VBRMETHOD		nVbrMethod;
			BOOL			bNoRes;				// Disable Bit resorvoir

			BYTE			btReserved[255-3*sizeof(DWORD)];	// FUTURE USE, SET TO 0

			} LHV1;					// LAME header version 1

		struct	{

			DWORD	dwSampleRate;
			BYTE	byMode;
			WORD	wBitrate;
			BYTE	byEncodingMethod;

		} aac;

	} format;
		
} BE_CONFIG, *PBE_CONFIG;


typedef struct	{

	// BladeEnc DLL Version number

	BYTE	byDLLMajorVersion;
	BYTE	byDLLMinorVersion;

	// BladeEnc Engine Version Number

	BYTE	byMajorVersion;
	BYTE	byMinorVersion;

	// DLL Release date

	BYTE	byDay;
	BYTE	byMonth;
	WORD	wYear;

	// BladeEnc	Homepage URL

	CHAR	zHomepage[BE_MAX_HOMEPAGE + 1];	

	BYTE	byAlphaLevel;
	BYTE	byBetaLevel;
	BYTE	byMMXEnabled;

	BYTE	btReserved[125];


} BE_VERSION, *PBE_VERSION;			

#ifndef _BLADEDLL

typedef BE_ERR	(*BEINITSTREAM)			(PBE_CONFIG, PDWORD, PDWORD, PHBE_STREAM);
typedef BE_ERR	(*BEENCODECHUNK)		(HBE_STREAM, DWORD, PSHORT, PBYTE, PDWORD);
// added for floating point audio  -- DSPguru, jd
typedef BE_ERR	(*BEENCODECHUNKFLOATS16NI)	(HBE_STREAM, DWORD, PFLOAT, PFLOAT, PBYTE, PDWORD);
typedef BE_ERR	(*BEDEINITSTREAM)		(HBE_STREAM, PBYTE, PDWORD);
typedef BE_ERR	(*BECLOSESTREAM)		(HBE_STREAM);
typedef VOID	(*BEVERSION)			(PBE_VERSION);
typedef VOID	(*BEWRITEVBRHEADER)		(LPCSTR);

#define	TEXT_BEINITSTREAM		"beInitStream"
#define	TEXT_BEENCODECHUNK		"beEncodeChunk"
#define	TEXT_BEENCODECHUNKFLOATS16NI	"beEncodeChunkFloatS16NI"
#define	TEXT_BEDEINITSTREAM		"beDeinitStream"
#define	TEXT_BECLOSESTREAM		"beCloseStream"
#define	TEXT_BEVERSION			"beVersion"
#define	TEXT_BEWRITEVBRHEADER	"beWriteVBRHeader"



#define MP3_ERR -1
#define MP3_OK  0
#define MP3_NEED_MORE 1

// void *InitMP3_Create();
// void ExitMP3_Delete(void *);

// int decodeMP3_unclipped(void *,unsigned char *inmemory,int inmemsize,char *outmemory,int outmemsize,int *done);
//    always uses all of inmemory, returns MP3_NEED_MORE if it needs more, and done is the output size.
//    it appears outmemory is interleaved. outmemory is doubles, too. ick.
//    is 'done' bytes or samples? inmemsize should be 1152*2*sizeof(double)

// void get_decode_info(void *,int *nch, int *srate);//JF> added for querying the decode stats
// void remove_buf(void *);



#else

__declspec(dllexport) BE_ERR	beInitStream(PBE_CONFIG pbeConfig, PDWORD dwSamples, PDWORD dwBufferSize, PHBE_STREAM phbeStream);
__declspec(dllexport) BE_ERR	beEncodeChunk(HBE_STREAM hbeStream, DWORD nSamples, PSHORT pSamples, PBYTE pOutput, PDWORD pdwOutput);
// added for floating point audio  -- DSPguru, jd
__declspec(dllexport) BE_ERR	beEncodeChunkFloatS16NI(HBE_STREAM hbeStream, DWORD nSamples, PFLOAT buffer_l, PFLOAT buffer_r, PBYTE pOutput, PDWORD pdwOutput);
__declspec(dllexport) BE_ERR	beDeinitStream(HBE_STREAM hbeStream, PBYTE pOutput, PDWORD pdwOutput);
__declspec(dllexport) BE_ERR	beCloseStream(HBE_STREAM hbeStream);
__declspec(dllexport) VOID		beVersion(PBE_VERSION pbeVersion);
__declspec(dllexport) BE_ERR	beWriteVBRHeader(LPCSTR lpszFileName);

#endif

#pragma pack(pop)

#ifdef	__cplusplus
}
#endif



#include "lameencdec.h"



static HINSTANCE hlamedll;
static BEINITSTREAM     beInitStream;
static BECLOSESTREAM    beCloseStream;
static BEENCODECHUNKFLOATS16NI    beEncodeChunkFloatS16NI;
static BEDEINITSTREAM   beDeinitStream;
static BEVERSION        beVersion;

static void initdll()
{
  if (!hlamedll) 
  {
    char me[1024];
    GetModuleFileName(NULL,me,sizeof(me)-1);

    char *p=me;
    while (*p) p++;
    while(p>=me && *p!='\\') p--;

    strcpy(++p,"lame_enc.dll");

    hlamedll=LoadLibrary(me);
    if (hlamedll)
    {
		  beInitStream	 = (BEINITSTREAM)     GetProcAddress(hlamedll, TEXT_BEINITSTREAM);
		  beCloseStream	 = (BECLOSESTREAM)    GetProcAddress(hlamedll, TEXT_BECLOSESTREAM);
		  beEncodeChunkFloatS16NI	 = (BEENCODECHUNKFLOATS16NI)    GetProcAddress(hlamedll, TEXT_BEENCODECHUNKFLOATS16NI);
		  beDeinitStream	 = (BEDEINITSTREAM)   GetProcAddress(hlamedll, TEXT_BEDEINITSTREAM);
		  beVersion        = (BEVERSION)        GetProcAddress(hlamedll, TEXT_BEVERSION);
    }

  }
}

LameEncoder::LameEncoder(int srate, int nch, int bitrate)
{
  errorstat=0;
  initdll();

  if (!beInitStream||!beCloseStream||!beEncodeChunkFloatS16NI||!beDeinitStream||!beVersion)
  {
    errorstat=1;
    return;
  }
  hbeStream=0;
  m_nch=nch;

  BE_CONFIG beConfig;
  memset(&beConfig,0,sizeof(beConfig));
	beConfig.dwConfig = BE_CONFIG_LAME;

	beConfig.format.LHV1.dwStructVersion	= 1;
	beConfig.format.LHV1.dwStructSize		= sizeof(beConfig);		
	beConfig.format.LHV1.dwSampleRate		= srate;
  if (nch == 1) beConfig.format.LHV1.nMode = BE_MP3_MODE_MONO;
  else beConfig.format.LHV1.nMode = BE_MP3_MODE_JSTEREO;

  beConfig.format.LHV1.dwBitrate=bitrate;
  beConfig.format.LHV1.dwMaxBitrate=bitrate;
  beConfig.format.LHV1.dwReSampleRate	= srate;

  // if mpeg 1, and bitrate is less than 48kbps per channel, switch to mpeg 2
  if (beConfig.format.LHV1.dwReSampleRate >= 32000 && bitrate < 48*nch)
    beConfig.format.LHV1.dwReSampleRate/=2;


  beConfig.format.LHV1.dwMpegVersion		= beConfig.format.LHV1.dwReSampleRate < 32000 ? MPEG2 : MPEG1;

  beConfig.format.LHV1.nPreset=LQP_NORMAL_QUALITY;

  beConfig.format.LHV1.bNoRes	= 1;

  int out_size_bytes=0;
  if (beInitStream(&beConfig, (DWORD*)&in_size_samples, (DWORD*)&out_size_bytes, (PHBE_STREAM) &hbeStream) != BE_ERR_SUCCESSFUL)
  {
    errorstat=2;
    printf("error init stream\n");
    return;
  }
  outtmp.Resize(out_size_bytes);

  printf("we're in good shape\n");
}

void LameEncoder::Encode(float *in, int in_spls)
{

  if (errorstat) return;

  if (m_nch > 1) // deinterleave
  {
    int x;
    for (x = 0; x < in_spls; x ++)
    {
      float f=in[x+x];
      f*=32767.0f;
      spltmp[0].Add(&f,sizeof(float));

      f=in[x+x+1];
      f*=32767.0f;
      spltmp[1].Add(&f,sizeof(float));
    }
  }
  else 
  {
    int x;
    for (x = 0; x < in_spls; x ++)
    {
      float f=in[x];
      f*=32767.0f;
      spltmp[0].Add(&f,sizeof(float));
    }
  }


  while (spltmp[0].Available() >= (int) (in_size_samples*sizeof(float)))
  {
    DWORD dwo=0;
    if (beEncodeChunkFloatS16NI(hbeStream, in_size_samples, (float*)spltmp[0].Get(), (float*)spltmp[m_nch > 1].Get(), 
                        (unsigned char*)outtmp.Get(), &dwo) != BE_ERR_SUCCESSFUL)
    {
      printf("error calling encode\n");
      errorstat=3;
      return;
    }
    outqueue.Add(outtmp.Get(),dwo);
    spltmp[0].Advance(in_size_samples*sizeof(float));
    if (m_nch > 1) spltmp[1].Advance(in_size_samples*sizeof(float));
  }


  spltmp[0].Compact();
  spltmp[1].Compact();

}

LameEncoder::~LameEncoder()
{
  if (hbeStream) beCloseStream(hbeStream);
}