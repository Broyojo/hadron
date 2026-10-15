/* Media Foundation playback: reads a media file through a source reader, as Unity's and Unreal's
 * video players do on Windows, and counts the video frames and audio samples it decodes.
 * Usage: mfplay.exe <file>   (exit status 0 when both decode) */
#define COBJMACROS
#include <stdio.h>
#include <windows.h>
#include <initguid.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

static int count( IMFSourceReader *reader, DWORD stream, const GUID *subtype, const char *what )
{
    IMFMediaType *type;
    HRESULT hr;
    int n = 0;

    MFCreateMediaType( &type );
    IMFMediaType_SetGUID( type, &MF_MT_MAJOR_TYPE, stream == (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM
                          ? &MFMediaType_Video : &MFMediaType_Audio );
    IMFMediaType_SetGUID( type, &MF_MT_SUBTYPE, subtype );
    if (FAILED(hr = IMFSourceReader_SetCurrentMediaType( reader, stream, NULL, type )))
    { printf( "%s: no decoder to %s output: %#lx\n", what, subtype == &MFVideoFormat_NV12 ? "NV12" : "PCM", hr ); return 0; }
    for (;;)
    {
        DWORD flags = 0;
        LONGLONG time;
        IMFSample *sample = NULL;

        if (FAILED(hr = IMFSourceReader_ReadSample( reader, stream, 0, NULL, &flags, &time, &sample )))
        { printf( "%s: ReadSample failed: %#lx\n", what, hr ); break; }
        if (sample) { n++; IMFSample_Release( sample ); }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
    }
    printf( "%s: %d samples decoded\n", what, n );
    return n;
}

int main( int argc, char **argv )
{
    IMFSourceReader *reader;
    WCHAR path[MAX_PATH];
    int video, audio;
    HRESULT hr;

    setvbuf( stdout, NULL, _IONBF, 0 );
    if (argc < 2) { printf( "usage: mfplay.exe <file>\n" ); return 2; }
    MultiByteToWideChar( CP_ACP, 0, argv[1], -1, path, MAX_PATH );
    CoInitializeEx( NULL, COINIT_MULTITHREADED );
    MFStartup( MF_VERSION, MFSTARTUP_FULL );
    if (FAILED(hr = MFCreateSourceReaderFromURL( path, NULL, &reader )))
    { printf( "MFCreateSourceReaderFromURL failed: %#lx\n", hr ); return 1; }
    video = count( reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, &MFVideoFormat_NV12, "video" );
    audio = count( reader, MF_SOURCE_READER_FIRST_AUDIO_STREAM, &MFAudioFormat_PCM, "audio" );
    IMFSourceReader_Release( reader );
    MFShutdown();
    return video && audio ? 0 : 1;
}
