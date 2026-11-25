/* An xWMA voice through XAudio2 (FAudio and the WMA decoder transform, findings #63). Plays raw
 * WMA packets on a source voice declared as WAVE_FORMAT_WMAUDIO2, the way a game does, with a
 * volume meter on the voice, and says whether any sound came out. The mastering voice is muted.
 * Two voices are made one after the other, each with the packets queued twice, so that a decoding
 * failure shows as one report per voice: Wine's FAudio (patch 0053) then logs, at the ERR level,
 * four lines for each voice that start with "err:faudio:report_failure voice <FAudio's voice>",
 * and a buffer that decodes logs nothing.
 *
 * Usage: xwma-voice.exe <packets> <rate> <channels> <block align> <bytes per second> <samples> <sound|silence>
 *   <packets> as for wma-decoder-drain.exe: the WMA payload with nothing around it, a whole number
 *   of <block align> packets. <samples> is the samples per channel the packets decode to, for the
 *   buffer's decoded packet sizes (XAUDIO2_BUFFER_WMA). The last argument is what the volume meter
 *   should see. Wine's dlls/mf/tests/wmaencdata.bin is 22050 Hz stereo, 1487-byte packets, 4003
 *   bytes a second, 23552 samples.
 * Build: toolchains/llvm-mingw/bin/i686-w64-mingw32-clang -O1 -o xwma-voice.exe tests/win/xwma-voice.c -lole32
 *   (or aarch64-w64-mingw32-clang)
 * Exit status 0 when the meter saw what was expected. */
#define COBJMACROS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <mmreg.h>
#include <xaudio2.h>
#include <xaudio2fx.h>

/* Not in mingw-w64's xaudio2fx.h; packed, as XAudio2's structures are. */
#pragma pack(push, 1)
typedef struct
{
    float *pPeakLevels;
    float *pRMSLevels;
    UINT32 ChannelCount;
} XAUDIO2FX_VOLUMEMETER_LEVELS;
#pragma pack(pop)

static HRESULT (WINAPI *pXAudio2Create)( IXAudio2 **, UINT32, XAUDIO2_PROCESSOR );
static HRESULT (WINAPI *pCreateAudioVolumeMeter)( IUnknown ** );

static BYTE *packets;
static DWORD packets_size;

/* Plays the packets twice on a new voice, returns the loudest peak the meter saw. The voice is
 * kept, so that the next one is not made at the same address. */
static float play( IXAudio2 *xaudio, const WAVEFORMATEX *format, DWORD samples, IXAudio2SourceVoice **ret )
{
    XAUDIO2FX_VOLUMEMETER_LEVELS levels;
    XAUDIO2_EFFECT_DESCRIPTOR effect;
    XAUDIO2_EFFECT_CHAIN chain;
    IXAudio2SourceVoice *voice;
    XAUDIO2_BUFFER_WMA wma;
    XAUDIO2_BUFFER buffer;
    XAUDIO2_VOICE_STATE state;
    float peak[8], rms[8], loudest = 0;
    UINT32 *sizes, count = packets_size / format->nBlockAlign, i, j;
    IUnknown *meter;
    HRESULT hr;

    if (FAILED(hr = pCreateAudioVolumeMeter( &meter ))) { printf( "CreateAudioVolumeMeter: %#lx\n", hr ); exit( 1 ); }
    effect.pEffect = meter;
    effect.InitialState = TRUE;
    effect.OutputChannels = format->nChannels;
    chain.EffectCount = 1;
    chain.pEffectDescriptors = &effect;
    if (FAILED(hr = IXAudio2_CreateSourceVoice( xaudio, &voice, format, 0, 2.0f, NULL, NULL, &chain )))
    { printf( "CreateSourceVoice: %#lx\n", hr ); exit( 1 ); }
    IUnknown_Release( meter );
    printf( "voice %p\n", voice );

    /* The decoded size of each packet is not known here; spread the samples over them evenly. */
    sizes = malloc( count * sizeof(*sizes) );
    for (i = 0; i < count; i++)
        sizes[i] = (UINT32)((ULONGLONG)samples * (i + 1) / count) * format->nChannels * 2;
    memset( &buffer, 0, sizeof(buffer) );
    buffer.AudioBytes = count * format->nBlockAlign;
    buffer.pAudioData = packets;
    wma.pDecodedPacketCumulativeBytes = sizes;
    wma.PacketCount = count;
    for (i = 0; i < 2; i++)
    {
        if (i) buffer.Flags = XAUDIO2_END_OF_STREAM;
        if (FAILED(hr = IXAudio2SourceVoice_SubmitSourceBuffer( voice, &buffer, &wma )))
        { printf( "SubmitSourceBuffer: %#lx\n", hr ); exit( 1 ); }
    }
    IXAudio2SourceVoice_Start( voice, 0, XAUDIO2_COMMIT_NOW );

    for (i = 0; i < 1000; i++)
    {
        Sleep( 10 );
        levels.pPeakLevels = peak;
        levels.pRMSLevels = rms;
        levels.ChannelCount = format->nChannels;
        IXAudio2SourceVoice_GetEffectParameters( voice, 0, &levels, sizeof(levels) );
        for (j = 0; j < format->nChannels; j++) if (peak[j] > loudest) loudest = peak[j];
        IXAudio2SourceVoice_GetState( voice, &state, 0 );
        if (!state.BuffersQueued) break;
    }
    if (state.BuffersQueued) printf( "  still %u buffers queued after 10 s\n", state.BuffersQueued );
    free( sizes );
    *ret = voice;
    return loudest;
}

int main( int argc, char **argv )
{
    IXAudio2SourceVoice *voices[2];
    IXAudio2MasteringVoice *master;
    WAVEFORMATEX format = {0};
    IXAudio2 *xaudio;
    DWORD samples;
    HMODULE module;
    int i, sound, ok = 1;
    float loudest;
    HRESULT hr;
    FILE *f;

    setvbuf( stdout, NULL, _IONBF, 0 );
    if (argc != 8 || (strcmp( argv[7], "sound" ) && strcmp( argv[7], "silence" )))
    {
        printf( "usage: %s <packets> <rate> <channels> <block align> <bytes per second> <samples> <sound|silence>\n", argv[0] );
        return 2;
    }
    if (!(f = fopen( argv[1], "rb" ))) { printf( "cannot open %s\n", argv[1] ); return 1; }
    fseek( f, 0, SEEK_END );
    packets_size = ftell( f );
    fseek( f, 0, SEEK_SET );
    packets = malloc( packets_size );
    fread( packets, 1, packets_size, f );
    fclose( f );
    format.wFormatTag = WAVE_FORMAT_WMAUDIO2;
    format.nSamplesPerSec = strtoul( argv[2], NULL, 0 );
    format.nChannels = strtoul( argv[3], NULL, 0 );
    format.nBlockAlign = strtoul( argv[4], NULL, 0 );
    format.nAvgBytesPerSec = strtoul( argv[5], NULL, 0 );
    format.wBitsPerSample = 16;
    samples = strtoul( argv[6], NULL, 0 );
    sound = !strcmp( argv[7], "sound" );

    CoInitializeEx( NULL, COINIT_MULTITHREADED );
    if (!(module = LoadLibraryA( "xaudio2_9.dll" ))) { printf( "no xaudio2_9.dll\n" ); return 1; }
    pXAudio2Create = (void *)GetProcAddress( module, "XAudio2Create" );
    pCreateAudioVolumeMeter = (void *)GetProcAddress( module, "CreateAudioVolumeMeter" );
    if (!pXAudio2Create) pXAudio2Create = (void *)GetProcAddress( module, MAKEINTRESOURCEA(1) );
    if (!pCreateAudioVolumeMeter) pCreateAudioVolumeMeter = (void *)GetProcAddress( module, MAKEINTRESOURCEA(3) );
    if (FAILED(hr = pXAudio2Create( &xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR ))) { printf( "XAudio2Create: %#lx\n", hr ); return 1; }
    if (FAILED(hr = IXAudio2_CreateMasteringVoice( xaudio, &master, XAUDIO2_DEFAULT_CHANNELS, XAUDIO2_DEFAULT_SAMPLERATE,
                                                   0, NULL, NULL, AudioCategory_GameEffects )))
    { printf( "CreateMasteringVoice: %#lx\n", hr ); return 1; }
    IXAudio2MasteringVoice_SetVolume( master, 0.0f, XAUDIO2_COMMIT_NOW );

    printf( "%lu bytes, %u packets of %u, %lu Hz, %u channels, %lu bytes/s, %lu samples\n", packets_size,
            (unsigned)(packets_size / format.nBlockAlign), format.nBlockAlign, format.nSamplesPerSec,
            format.nChannels, format.nAvgBytesPerSec, samples );
    for (i = 0; i < 2; i++)
    {
        loudest = play( xaudio, &format, samples, &voices[i] );
        printf( "  loudest peak %.4f: %s%s\n", loudest, loudest > 0 ? "sound" : "silence",
                (loudest > 0) == sound ? "" : "  WRONG" );
        if ((loudest > 0) != sound) ok = 0;
    }

    for (i = 0; i < 2; i++) IXAudio2SourceVoice_DestroyVoice( voices[i] );
    IXAudio2MasteringVoice_DestroyVoice( master );
    IXAudio2_Release( xaudio );
    /* As in wma-decoder-drain.c: do not wait for the process to exit by itself. */
    TerminateProcess( GetCurrentProcess(), !ok );
    return !ok;
}
