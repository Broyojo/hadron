/* WMA decoder transform at the end of a stream. Decodes raw WMA packets through the WMA decoder
 * (CLSID_CWMADecMediaObject, what the source reader and XAudio2's xWMA support use), counts the
 * PCM it gives back with MFT_MESSAGE_COMMAND_DRAIN and FLUSH, follows the calls FAudio's
 * decode_wma() makes for two buffers queued on one voice, and times each call.
 *
 * Usage: wma-decoder-drain.exe <packets> <rate> <channels> <block align> <bytes per second> <expected> [codec data]
 *        wma-decoder-drain.exe reader <file.wma> <expected>
 *   <packets> is the WMA payload with nothing around it, as `ffmpeg -i x.wma -map 0:a -c copy
 *   -f rawvideo x.raw` writes it. <expected> is the samples per channel a whole stream decodes
 *   to, or 0 to check only that every pass gives what the first one did (for the reader: to
 *   print the count). GStreamer's avdec_wmav2 gives one frame per frame coded and keeps nothing
 *   back for a drain (gst-libav sets AV_CODEC_FLAG2_SKIP_MANUAL for WMA and does not collect
 *   FFmpeg's final frame), so the counts here are not FFmpeg's own, which drops two frames at the
 *   start and adds one at the end. [codec data] is the WAVEFORMATEX extra bytes in hex; the
 *   default is FFmpeg's wmav2 encoder's (encode options 1).
 * Exit status 0 when every check passes. */
#define COBJMACROS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <initguid.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mftransform.h>

DEFINE_GUID(CLSID_CWMADecMediaObject_, 0x2eeb4adf, 0x4578, 0x4d10, 0xbc, 0xa7, 0xbb, 0x95, 0x5f, 0x56, 0x32, 0x0a);

static BYTE *packets;
static DWORD packets_size, rate, channels, align, bytes_per_second;
static BYTE codec_data[32] = {0, 0, 0, 0, 1, 0, 0, 0, 0, 0};
static DWORD codec_data_size = 10;
static IMFSample *output_sample;
static int not_accepting;

/* Slowest single call of each kind, in milliseconds. */
static double max_input, max_output, max_message, create_time[2];
static int create_count;

static double now_ms( void )
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency( &f );
    QueryPerformanceCounter( &c );
    return c.QuadPart * 1000.0 / f.QuadPart;
}

static void message( IMFTransform *decoder, MFT_MESSAGE_TYPE type )
{
    double start = now_ms(), t;
    HRESULT hr = IMFTransform_ProcessMessage( decoder, type, 0 );
    if ((t = now_ms() - start) > max_message) max_message = t;
    if (FAILED(hr)) printf( "  ProcessMessage %#x: %#lx\n", type, hr );
}

static IMFTransform *create_decoder( void )
{
    double start = now_ms(), t;
    MFT_OUTPUT_STREAM_INFO info = {0};
    IMFMediaBuffer *buffer;
    IMFTransform *decoder;
    IMFMediaType *type;
    GUID subtype;
    UINT32 value;
    HRESULT hr;
    DWORD i;

    if (FAILED(hr = CoCreateInstance( &CLSID_CWMADecMediaObject_, NULL, CLSCTX_INPROC_SERVER,
                                      &IID_IMFTransform, (void **)&decoder )))
    { printf( "no WMA decoder: %#lx\n", hr ); exit( 1 ); }

    MFCreateMediaType( &type );
    IMFMediaType_SetGUID( type, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio );
    IMFMediaType_SetGUID( type, &MF_MT_SUBTYPE, &MFAudioFormat_WMAudioV8 );
    IMFMediaType_SetBlob( type, &MF_MT_USER_DATA, codec_data, codec_data_size );
    IMFMediaType_SetUINT32( type, &MF_MT_AUDIO_BLOCK_ALIGNMENT, align );
    IMFMediaType_SetUINT32( type, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16 );
    IMFMediaType_SetUINT32( type, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, bytes_per_second );
    IMFMediaType_SetUINT32( type, &MF_MT_AUDIO_NUM_CHANNELS, channels );
    IMFMediaType_SetUINT32( type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, rate );
    if (FAILED(hr = IMFTransform_SetInputType( decoder, 0, type, 0 )))
    { printf( "SetInputType: %#lx\n", hr ); exit( 1 ); }
    IMFMediaType_Release( type );

    for (i = 0; SUCCEEDED(IMFTransform_GetOutputAvailableType( decoder, 0, i, &type )); i++)
    {
        if (SUCCEEDED(IMFMediaType_GetGUID( type, &MF_MT_SUBTYPE, &subtype )) && IsEqualGUID( &subtype, &MFAudioFormat_Float )
                && SUCCEEDED(IMFMediaType_GetUINT32( type, &MF_MT_AUDIO_BITS_PER_SAMPLE, &value )) && value == 32)
            break;
        IMFMediaType_Release( type );
        type = NULL;
    }
    if (!type) { printf( "no float output type\n" ); exit( 1 ); }
    if (FAILED(hr = IMFTransform_SetOutputType( decoder, 0, type, 0 )))
    { printf( "SetOutputType: %#lx\n", hr ); exit( 1 ); }
    IMFMediaType_Release( type );

    if (!output_sample)
    {
        IMFTransform_GetOutputStreamInfo( decoder, 0, &info );
        MFCreateSample( &output_sample );
        MFCreateMemoryBuffer( info.cbSize, &buffer );
        IMFSample_AddBuffer( output_sample, buffer );
        IMFMediaBuffer_Release( buffer );
    }
    message( decoder, MFT_MESSAGE_NOTIFY_BEGIN_STREAMING );
    t = now_ms() - start;
    if (create_count < 2) create_time[create_count++] = t;
    return decoder;
}

/* Pushes the packet at *pos; S_FALSE when there are none left. */
static HRESULT push( IMFTransform *decoder, DWORD *pos )
{
    IMFMediaBuffer *buffer;
    IMFSample *sample;
    double start, t;
    BYTE *data;
    HRESULT hr;

    if (*pos >= packets_size) return S_FALSE;
    MFCreateSample( &sample );
    MFCreateMemoryBuffer( align, &buffer );
    IMFMediaBuffer_Lock( buffer, &data, NULL, NULL );
    memcpy( data, packets + *pos, align );
    IMFMediaBuffer_Unlock( buffer );
    IMFMediaBuffer_SetCurrentLength( buffer, align );
    IMFSample_AddBuffer( sample, buffer );
    IMFMediaBuffer_Release( buffer );
    start = now_ms();
    hr = IMFTransform_ProcessInput( decoder, 0, sample, 0 );
    if ((t = now_ms() - start) > max_input) max_input = t;
    IMFSample_Release( sample );
    if (hr == MF_E_NOTACCEPTING)
    {
        if (++not_accepting == 1000) { printf( "  ProcessInput refuses input and ProcessOutput has none\n" ); exit( 3 ); }
        return S_OK;
    }
    if (FAILED(hr)) { printf( "ProcessInput: %#lx\n", hr ); exit( 1 ); }
    not_accepting = 0;
    *pos += align;
    return S_OK;
}

/* Pulls output until the decoder wants input; returns the samples per channel it got. */
static DWORD pull( IMFTransform *decoder )
{
    DWORD total = 0, status, length;
    MFT_OUTPUT_DATA_BUFFER output;
    IMFMediaBuffer *buffer;
    double start, t;
    HRESULT hr;

    for (int tries = 0;; tries++)
    {
        if (tries == 1000) { printf( "  ProcessOutput keeps giving output\n" ); exit( 3 ); }
        memset( &output, 0, sizeof(output) );
        output.pSample = output_sample;
        IMFSample_GetBufferByIndex( output_sample, 0, &buffer );
        IMFMediaBuffer_SetCurrentLength( buffer, 0 );
        start = now_ms();
        hr = IMFTransform_ProcessOutput( decoder, 0, 1, &output, &status );
        if ((t = now_ms() - start) > max_output) max_output = t;
        if (output.pEvents) IMFCollection_Release( output.pEvents );
        IMFMediaBuffer_GetCurrentLength( buffer, &length );
        IMFMediaBuffer_Release( buffer );
        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) break;
        if (FAILED(hr)) { printf( "ProcessOutput: %#lx\n", hr ); exit( 1 ); }
        total += length / (4 * channels);
    }
    return total;
}

/* A whole stream the way Media Foundation clients end one: every packet, then
 * NOTIFY_END_OF_STREAM and COMMAND_DRAIN. */
static DWORD decode_stream( IMFTransform *decoder, DWORD *before_drain )
{
    DWORD got, pos;

    message( decoder, MFT_MESSAGE_NOTIFY_START_OF_STREAM );
    for (pos = 0, got = 0; push( decoder, &pos ) == S_OK;) got += pull( decoder );
    message( decoder, MFT_MESSAGE_NOTIFY_END_OF_STREAM );
    got += pull( decoder );
    if (before_drain) *before_drain = got;
    message( decoder, MFT_MESSAGE_COMMAND_DRAIN );
    return got + pull( decoder );
}

static int check( const char *what, DWORD got, DWORD expected )
{
    printf( "%-50s %6lu of %6lu samples%s\n", what, got, expected, got == expected ? "" : "  WRONG" );
    return got == expected;
}

int main( int argc, char **argv )
{
    DWORD expected, got, before, pos;
    IMFTransform *decoder;
    double destroy;
    int ok = 1;
    FILE *f;

    setvbuf( stdout, NULL, _IONBF, 0 );
    CoInitializeEx( NULL, COINIT_MULTITHREADED );
    MFStartup( MF_VERSION, MFSTARTUP_FULL );

    if (argc == 4 && !strcmp( argv[1], "reader" ))
    {
        double start, t, max_read = 0, total_read = 0, open;
        IMFSourceReader *reader;
        IMFMediaType *type;
        WCHAR path[MAX_PATH];
        HRESULT hr;

        expected = strtoul( argv[3], NULL, 0 );
        MultiByteToWideChar( CP_ACP, 0, argv[2], -1, path, MAX_PATH );
        start = now_ms();
        if (FAILED(hr = MFCreateSourceReaderFromURL( path, NULL, &reader )))
        { printf( "MFCreateSourceReaderFromURL: %#lx\n", hr ); return 1; }
        MFCreateMediaType( &type );
        IMFMediaType_SetGUID( type, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio );
        IMFMediaType_SetGUID( type, &MF_MT_SUBTYPE, &MFAudioFormat_Float );
        if (FAILED(hr = IMFSourceReader_SetCurrentMediaType( reader, MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, type )))
        { printf( "SetCurrentMediaType: %#lx\n", hr ); return 1; }
        IMFMediaType_Release( type );
        open = now_ms() - start;
        IMFSourceReader_GetCurrentMediaType( reader, MF_SOURCE_READER_FIRST_AUDIO_STREAM, &type );
        IMFMediaType_GetUINT32( type, &MF_MT_AUDIO_NUM_CHANNELS, (UINT32 *)&channels );
        IMFMediaType_Release( type );
        for (got = 0;;)
        {
            IMFMediaBuffer *buffer;
            IMFSample *sample = NULL;
            DWORD flags = 0, length;
            LONGLONG time;

            start = now_ms();
            hr = IMFSourceReader_ReadSample( reader, MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, NULL, &flags, &time, &sample );
            t = now_ms() - start;
            total_read += t;
            if (t > max_read) max_read = t;
            if (FAILED(hr)) { printf( "ReadSample: %#lx\n", hr ); return 1; }
            if (sample)
            {
                IMFSample_ConvertToContiguousBuffer( sample, &buffer );
                IMFMediaBuffer_GetCurrentLength( buffer, &length );
                got += length / (4 * channels);
                IMFMediaBuffer_Release( buffer );
                IMFSample_Release( sample );
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        }
        if (expected) ok = check( "source reader, read to end of stream", got, expected );
        else printf( "%-50s %6lu samples\n", "source reader, read to end of stream", got );
        printf( "  open %.1f ms, ReadSample %.1f ms in all, slowest %.1f ms\n", open, total_read, max_read );
        IMFSourceReader_Release( reader );
        MFShutdown();
        TerminateProcess( GetCurrentProcess(), !ok );
        return !ok;
    }

    if (argc == 8)
    {
        for (codec_data_size = 0; codec_data_size < sizeof(codec_data) && argv[7][2 * codec_data_size]; codec_data_size++)
            sscanf( argv[7] + 2 * codec_data_size, "%2hhx", &codec_data[codec_data_size] );
    }
    else if (argc != 7)
    {
        printf( "usage: %s <packets> <rate> <channels> <block align> <bytes per second> <expected> [codec data]\n"
                "       %s reader <file.wma> <expected>\n", argv[0], argv[0] );
        return 2;
    }
    if (!(f = fopen( argv[1], "rb" ))) { printf( "cannot open %s\n", argv[1] ); return 1; }
    fseek( f, 0, SEEK_END );
    packets_size = ftell( f );
    fseek( f, 0, SEEK_SET );
    packets = malloc( packets_size );
    fread( packets, 1, packets_size, f );
    fclose( f );
    rate = strtoul( argv[2], NULL, 0 );
    channels = strtoul( argv[3], NULL, 0 );
    align = strtoul( argv[4], NULL, 0 );
    bytes_per_second = strtoul( argv[5], NULL, 0 );
    expected = strtoul( argv[6], NULL, 0 );
    printf( "%lu packets of %lu bytes, %lu Hz, %lu channels\n", packets_size / align, align, rate, channels );

    /* 1. The way Media Foundation clients end a stream; then the same decoder again. */
    decoder = create_decoder();
    got = decode_stream( decoder, &before );
    printf( "  %lu samples before COMMAND_DRAIN, %lu after\n", before, got );
    if (!expected) expected = got;
    ok &= check( "all packets, END_OF_STREAM, COMMAND_DRAIN", got, expected );
    ok &= check( "the same decoder again after the drain", decode_stream( decoder, NULL ), expected );

    /* 2. A flush drops what the decoder holds: the next stream must not start with it. */
    message( decoder, MFT_MESSAGE_NOTIFY_START_OF_STREAM );
    for (pos = 0; pos < packets_size / 2 && push( decoder, &pos ) == S_OK;) pull( decoder );
    message( decoder, MFT_MESSAGE_COMMAND_FLUSH );
    ok &= check( "  left after half a stream and COMMAND_FLUSH", pull( decoder ), 0 );
    ok &= check( "a whole stream after the flush", decode_stream( decoder, NULL ), expected );
    IMFTransform_Release( decoder );

    /* 3. FAudio's decode_wma() (dlls/xaudio2_7 through libs/faudio): per buffer
     * NOTIFY_START_OF_STREAM, a packet in, output until it wants input, the next packet, and
     * NOTIFY_END_OF_STREAM when they run out; no drain until the voice is destroyed. Two
     * buffers on one voice, as a game queues sounds. Informational: this follows FAudio, not a
     * contract. */
    decoder = create_decoder();
    for (int buffer = 1; buffer <= 2; buffer++)
    {
        message( decoder, MFT_MESSAGE_NOTIFY_START_OF_STREAM );
        pos = 0;
        push( decoder, &pos );
        for (got = 0;;)
        {
            got += pull( decoder );
            if (push( decoder, &pos ) == S_OK) continue;
            message( decoder, MFT_MESSAGE_NOTIFY_END_OF_STREAM );
            got += pull( decoder );
            break;
        }
        printf( "%-50s %6lu samples\n", buffer == 1 ? "FAudio's calls, buffer 1" : "FAudio's calls, buffer 2", got );
    }
    /* FAudio_WMADEC_free(), which runs with the engine's source lock held. */
    destroy = now_ms();
    message( decoder, MFT_MESSAGE_NOTIFY_END_OF_STREAM );
    message( decoder, MFT_MESSAGE_COMMAND_DRAIN );
    IMFTransform_Release( decoder );
    destroy = now_ms() - destroy;

    printf( "  create %.1f ms (first) and %.1f ms (second), destroy %.2f ms; slowest ProcessInput %.2f ms,"
            " ProcessOutput %.2f ms, ProcessMessage %.2f ms\n", create_time[0], create_time[1], destroy,
            max_input, max_output, max_message );
    IMFSample_Release( output_sample );
    MFShutdown();
    /* Process exit sometimes hangs after this in Hadron 0.3.3 (one in four runs), with nothing
     * of this test left running; end without the DLL detach so that a run reports its result. */
    TerminateProcess( GetCurrentProcess(), !ok );
    return !ok;
}
