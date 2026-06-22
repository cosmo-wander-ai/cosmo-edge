/*
 * The contents of this file are subject to the Mozilla Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is MPEG4IP.
 *
 * The Initial Developer of the Original Code is Cisco Systems Inc.
 * Portions created by Cisco Systems Inc. are
 * Copyright (C) Cisco Systems Inc. 2001.  All Rights Reserved.
 *
 * Contributor(s):
 *      Dave Mackie     dmackie@cisco.com
 */

// N.B. mp4extract just extracts tracks/samples from an mp4 file
// For many track types this is insufficient to reconsruct a valid
// elementary stream (ES). Use "mp4creator -extract=<trackId>" if
// you need the ES reconstructed.

#include "util/impl.h"

using namespace mp4v2::util;

char* ProgName;
char* Mp4PathName;
char* Mp4FileName;

static void DumpTrack ( MP4FileHandle mp4file, MP4TrackId tid )
{
    uint32_t numSamples;
    MP4SampleId sid;
    MP4Duration time;
    uint32_t timescale;
    uint64_t msectime;

    uint64_t sectime, mintime, hrtime;

    numSamples = MP4GetTrackNumberOfSamples( mp4file, tid );
    timescale = MP4GetTrackTimeScale( mp4file, tid );
    printf( "mp4file %s, track %d, samples %d, timescale %d\n",
            Mp4FileName, tid, numSamples, timescale );

    for ( sid = 1; sid <= numSamples; sid++ ) {
        time = MP4GetSampleTime( mp4file, tid, sid );
        msectime = time;
        msectime *= UINT64_C( 1000 );
        msectime /= timescale;
        if ( msectime == 0 ) {
            hrtime = mintime = sectime = UINT64_C( 0 );
        }
        else {
            hrtime = msectime / UINT64_C( 3600000 ); // 3600 * 1000
            msectime -= hrtime * UINT64_C( 3600000 );// 3600 * 1000
            mintime = msectime / UINT64_C( 60000 );// 60 * 1000
            msectime -= ( mintime * UINT64_C( 60000 ) );// 60 * 1000
            sectime = msectime / UINT64_C( 1000 );
            msectime -= sectime * UINT64_C( 1000 );
        }

        printf( "sampleId %6d, size %5u duration %8" PRIu64 " time %8" PRIu64 " %02" PRIu64 ":%02" PRIu64 ":%02" PRIu64 ".%03" PRIu64 " %c\n",
                sid,  MP4GetSampleSize( mp4file, tid, sid ),
                MP4GetSampleDuration( mp4file, tid, sid ),
                time, hrtime, mintime, sectime, msectime,
                MP4GetSampleSync( mp4file, tid, sid ) == 1 ? 'S' : ' ' );
    }
}

extern "C" int main( int argc, char** argv )
{
    const char* const usageString =
        "[-l] [-t <track-id>] [-s <sample-id>] [-v [<level>]] <file-name>";
    MP4TrackId trackId = MP4_INVALID_TRACK_ID;
    MP4SampleId sampleId = MP4_INVALID_SAMPLE_ID;
    MP4LogLevel verbosity = MP4_LOG_ERROR;

    /* begin processing command line */
    ProgName = argv[0];
    while ( true ) {
        int c = -1;
        int option_index = 0;
        static const prog::Option long_options[] = {
            { "track",   prog::Option::REQUIRED_ARG, 0, 't' },
            { "sample",  prog::Option::REQUIRED_ARG, 0, 's' },
            { "verbose", prog::Option::OPTIONAL_ARG, 0, 'v' },
            { "version", prog::Option::NO_ARG,       0, 'V' },
            { NULL, prog::Option::NO_ARG, 0, 0 }
        };

        c = prog::getOptionSingle( argc, argv, "t:v::V", long_options, &option_index );

        if ( c == -1 )
            break;

        switch ( c ) {
            case 's':
                if ( sscanf( prog::optarg, "%u", &sampleId ) != 1 ) {
                    fprintf( stderr, "%s: bad sample-id specified: %s\n",
                             ProgName, prog::optarg );
                    exit( 1 );
                }
                break;
            case 't':
                if ( sscanf( prog::optarg, "%u", &trackId ) != 1 ) {
                    fprintf( stderr,
                             "%s: bad track-id specified: %s\n",
                             ProgName, prog::optarg );
                    exit( 1 );
                }
                break;
            case 'v':
                verbosity = MP4_LOG_VERBOSE1;
                if ( prog::optarg ) {
                    uint32_t level;
                    if ( sscanf( prog::optarg, "%u", &level ) == 1 ) {
                        if ( level >= 2 ) {
                            verbosity = MP4_LOG_VERBOSE2;
                        }
                        if ( level >= 3 ) {
                            verbosity = MP4_LOG_VERBOSE3;
                        }
                        if ( level >= 4 ) {
                            verbosity = MP4_LOG_VERBOSE4;
                        }
                    }
                }
                break;
            case '?':
                fprintf( stderr, "usage: %s %s\n", ProgName, usageString );
                exit( 0 );
            case 'V':
                fprintf( stderr, "%s - %s\n",
                         ProgName, MP4V2_PROJECT_name_formal );
                exit( 0 );
            default:
                fprintf( stderr, "%s: unknown option specified, ignoring: %c\n",
                         ProgName, c );
        }
    }

    /* check that we have at least one non-option argument */
    if ( ( argc - prog::optind ) < 1 ) {
        fprintf( stderr, "usage: %s %s\n", ProgName, usageString );
        exit( 1 );
    }

    MP4LogSetLevel(verbosity);
    if ( verbosity ) {
        fprintf( stderr, "%s version %s\n", ProgName, MP4V2_PROJECT_version );
    }

    /* point to the specified file names */
    Mp4PathName = argv[prog::optind++];

    char* lastSlash = strrchr( Mp4PathName, '/' );
    if ( lastSlash ) {
        Mp4FileName = lastSlash + 1;
    }
    else {
        Mp4FileName = Mp4PathName;
    }

    /* warn about extraneous non-option arguments */
    if ( prog::optind < argc ) {
        fprintf( stderr, "%s: unknown options specified, ignoring: ", ProgName );
        while ( prog::optind < argc ) {
            fprintf( stderr, "%s ", argv[prog::optind++] );
        }
        fprintf( stderr, "\n" );
    }

    /* end processing of command line */


    MP4FileHandle mp4File = MP4Read( Mp4PathName );

    if ( !mp4File ) {
        exit( 1 );
    }

    if ( sampleId != MP4_INVALID_SAMPLE_ID ) {
        if ( trackId == 0 ) {
            fprintf( stderr, "%s: Must specify track for sample\n", ProgName );
            return -1;
        }
        if ( sampleId > MP4GetTrackNumberOfSamples( mp4File, trackId ) ) {
            fprintf( stderr, "%s: Sample number %u is past end %u\n",
                     ProgName, sampleId, MP4GetTrackNumberOfSamples( mp4File, trackId ) );
            return -1;
        }
        uint32_t sample_size = MP4GetTrackMaxSampleSize( mp4File, trackId );
        uint8_t *sample = ( uint8_t * )malloc( sample_size );
        MP4Timestamp sampleTime;
        MP4Duration sampleDuration, sampleRenderingOffset;
        uint32_t this_size = sample_size;
        bool isSyncSample;
        bool ret = MP4ReadSample( mp4File,
                                  trackId,
                                  sampleId,
                                  &sample,
                                  &this_size,
                                  &sampleTime,
                                  &sampleDuration,
                                  &sampleRenderingOffset,
                                  &isSyncSample );
        if ( ret == false ) {
            fprintf( stderr, "Sample read error\n" );
            return -1;
        }
        printf( "Track %u, Sample %u, Length %u\n",
                trackId, sampleId, this_size );

        for ( uint32_t ix = 0; ix < this_size; ix++ ) {
            if ( ( ix % 16 ) == 0 ) printf( "\n%04u ", ix );
            printf( "%02x ", sample[ix] );
        }
        printf( "\n" );
    }
    else {
        if ( trackId == 0 ) {
            uint32_t numTracks = MP4GetNumberOfTracks( mp4File );

            for ( uint32_t i = 0; i < numTracks; i++ ) {
                trackId = MP4FindTrackId( mp4File, i );
                DumpTrack( mp4File, trackId );
            }
        }
        else {
            DumpTrack( mp4File, trackId );
        }
    }

    MP4Close( mp4File );

    return( 0 );
}
