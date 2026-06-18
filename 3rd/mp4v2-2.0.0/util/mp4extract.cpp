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

// forward declaration
void ExtractTrack( MP4FileHandle mp4File, MP4TrackId trackId,
                   bool sampleMode, MP4SampleId sampleId, char* dstFileName = NULL );

extern "C" int main( int argc, char** argv )
{
    const char* const usageString =
        "[-l] [-t <track-id>] [-s <sample-id>] [-v [<level>]] <file-name>";
    bool doList = false;
    bool doSamples = false;
    MP4TrackId trackId = MP4_INVALID_TRACK_ID;
    MP4SampleId sampleId = MP4_INVALID_SAMPLE_ID;
    char* dstFileName = NULL;
    MP4LogLevel verbosity = MP4_LOG_ERROR;

    /* begin processing command line */
    ProgName = argv[0];
    while ( true ) {
        int c = -1;
        int option_index = 0;
        static const prog::Option long_options[] = {
            { "list",    prog::Option::NO_ARG,       0, 'l' },
            { "track",   prog::Option::REQUIRED_ARG, 0, 't' },
            { "sample",  prog::Option::OPTIONAL_ARG, 0, 's' },
            { "verbose", prog::Option::OPTIONAL_ARG, 0, 'v' },
            { "version", prog::Option::NO_ARG,       0, 'V' },
            { NULL, prog::Option::NO_ARG, 0, 0 }
        };

        c = prog::getOptionSingle( argc, argv, "lt:s::v::V", long_options, &option_index );

        if ( c == -1 )
            break;

        switch ( c ) {
            case 'l':
                doList = true;
                break;
            case 's':
                doSamples = true;
                if ( prog::optarg ) {
                    if ( sscanf( prog::optarg, "%u", &sampleId ) != 1 ) {
                        fprintf( stderr,
                                 "%s: bad sample-id specified: %s\n",
                                 ProgName, prog::optarg );
                    }
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
                fprintf( stderr, "%s - %s\n", ProgName, MP4V2_PROJECT_name_formal );
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

    /* get dest file name for a single track */
    if ( trackId && ( argc - prog::optind ) > 0 ) {
        dstFileName = argv[prog::optind++];
    }

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

    if ( doList ) {
        MP4Info( mp4File );
        exit( 0 );
    }

    if ( trackId == 0 ) {
        uint32_t numTracks = MP4GetNumberOfTracks( mp4File );

        for ( uint32_t i = 0; i < numTracks; i++ ) {
            trackId = MP4FindTrackId( mp4File, i );
            ExtractTrack( mp4File, trackId, doSamples, sampleId );
        }
    }
    else {
        ExtractTrack( mp4File, trackId, doSamples, sampleId, dstFileName );
    }

    MP4Close( mp4File );

    return( 0 );
}

void ExtractTrack( MP4FileHandle mp4File, MP4TrackId trackId,
                   bool sampleMode, MP4SampleId sampleId, char* dstFileName )
{
    static string outName;
    File out;

    if( !sampleMode ) {
        if( !dstFileName ) {
            stringstream ss;
            ss << Mp4FileName << ".t" << trackId;
            outName = ss.str();
        } else {
            outName = dstFileName;
        }

        if( out.open( outName.c_str(), File::MODE_CREATE )) {
            fprintf( stderr, "%s: can't open %s: %s\n", ProgName, outName.c_str(), sys::getLastErrorStr() );
            return;
        }
    }

    MP4SampleId numSamples;

    if ( sampleMode && sampleId != MP4_INVALID_SAMPLE_ID ) {
        numSamples = sampleId;
    }
    else {
        sampleId = 1;
        numSamples = MP4GetTrackNumberOfSamples( mp4File, trackId );
    }

    for ( ; sampleId <= numSamples; sampleId++ ) {
        // signals to ReadSample() that it should malloc a buffer for us
        uint8_t* pSample = NULL;
        uint32_t sampleSize = 0;

        if( !MP4ReadSample( mp4File, trackId, sampleId, &pSample, &sampleSize )) {
            fprintf( stderr, "%s: read sample %u for %s failed\n", ProgName, sampleId, outName.c_str() );
            break;
        }

        if ( sampleMode ) {
            stringstream ss;
            ss << Mp4FileName << ".t" << trackId << ".s" << sampleId;
            outName = ss.str();

            if( out.open( outName.c_str(), File::MODE_CREATE )) {
                fprintf( stderr, "%s: can't open %s: %s\n", ProgName, outName.c_str(), sys::getLastErrorStr() );
                break;
            }
        }

        File::Size nout;
        if( out.write( pSample, sampleSize, nout )) {
            fprintf( stderr, "%s: write to %s failed: %s\n", ProgName, outName.c_str(), sys::getLastErrorStr() );
            break;
        }

        free( pSample );

        if( sampleMode )
            out.close();
    }

    out.close();
}
