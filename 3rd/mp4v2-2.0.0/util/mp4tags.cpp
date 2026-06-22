/* mp4tags -- tool to set iTunes-compatible metadata tags
 *
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
 * Contributed to MPEG4IP
 * by Christopher League <league@contrapunctus.net>
 */

#include "util/impl.h"

using namespace mp4v2::util;

///////////////////////////////////////////////////////////////////////////////

/* One-letter options -- if you want to rearrange these, change them
   here, immediately below in OPT_STRING, and in the help text. */
#define OPT_HELP         0x01ff
#define OPT_VERSION      0x02ff
#define OPT_REMOVE       'r'
#define OPT_ALBUM        'A'
#define OPT_ARTIST       'a'
#define OPT_TEMPO        'b'
#define OPT_COMMENT      'c'
#define OPT_COPYRIGHT    'C'
#define OPT_DISK         'd'
#define OPT_DISKS        'D'
#define OPT_ENCODEDBY    'e'
#define OPT_TOOL         'E'
#define OPT_GENRE        'g'
#define OPT_GROUPING     'G'
#define OPT_HD           'H'
#define OPT_MEDIA_TYPE   'i'
#define OPT_CONTENTID    'I'
#define OPT_LONGDESC     'l'
#define OPT_GENREID      'j'
#define OPT_LYRICS       'L'
#define OPT_DESCRIPTION  'm'
#define OPT_TVEPISODE    'M'
#define OPT_TVSEASON     'n'
#define OPT_TVNETWORK    'N'
#define OPT_TVEPISODEID  'o'
#define OPT_CATEGORY     'O'
#define OPT_PLAYLISTID   'p'
#define OPT_PICTURE      'P'
#define OPT_PODCAST      'B'
#define OPT_ALBUM_ARTIST 'R'
#define OPT_NAME         's'
#define OPT_TVSHOW       'S'
#define OPT_TRACK        't'
#define OPT_TRACKS       'T'
#define OPT_XID          'x'
#define OPT_RATING       'X'
#define OPT_COMPOSER     'w'
#define OPT_RELEASEDATE  'y'
#define OPT_ARTISTID     'z'
#define OPT_COMPOSERID   'Z'

#define OPT_STRING  "r:A:a:b:c:C:d:D:e:E:g:G:H:i:I:j:l:L:m:M:n:N:o:O:p:P:B:R:s:S:t:T:x:X:w:y:z:Z:"

#define ELEMENT_OF(x,i) x[int(i)]

static const char* const help_text =
    "OPTION... FILE...\n"
    "Adds or modifies iTunes-compatible tags on MP4 files.\n"
    "\n"
    "      -help            Display this help text and exit\n"
    "      -version         Display version information and exit\n"
    "  -A, -album       STR  Set the album title\n"
    "  -a, -artist      STR  Set the artist information\n"
    "  -b, -tempo       NUM  Set the tempo (beats per minute)\n"
    "  -c, -comment     STR  Set a general comment\n"
    "  -C, -copyright   STR  Set the copyright information\n"
    "  -d, -disk        NUM  Set the disk number\n"
    "  -D, -disks       NUM  Set the number of disks\n"
    "  -e, -encodedby   STR  Set the name of the person or company who encoded the file\n"
    "  -E, -tool        STR  Set the software used for encoding\n"
    "  -g, -genre       STR  Set the genre name\n"
    "  -G, -grouping    STR  Set the grouping name\n"
    "  -H, -hdvideo     NUM  Set the HD flag (1\\0)\n"
    "  -i, -type        STR  Set the Media Type(tvshow, movie, music, ...)\n"
    "  -I, -contentid   NUM  Set the content ID\n"
    "  -j, -genreid     NUM  Set the genre ID\n"
    "  -l, -longdesc    STR  Set the long description\n"
    "  -L, -lyrics      NUM  Set the lyrics\n"
    "  -m, -description STR  Set the short description\n"
    "  -M, -episode     NUM  Set the episode number\n"
    "  -n, -season      NUM  Set the season number\n"
    "  -N, -network     STR  Set the TV network\n"
    "  -o, -episodeid   STR  Set the TV episode ID\n"
	"  -O, -category    STR  Set the category\n"
    "  -p, -playlistid  NUM  Set the playlist ID\n"
    "  -P, -picture     PTH  Set the picture as a .png\n"
    "  -B, -podcast     NUM  Set the podcast flag.\n"
    "  -R, -albumartist STR  Set the album artist\n"
    "  -s, -song        STR  Set the song title\n"
    "  -S  -show        STR  Set the TV show\n"
    "  -t, -track       NUM  Set the track number\n"
    "  -T, -tracks      NUM  Set the number of tracks\n"
    "  -x, -xid         STR  Set the globally-unique xid (vendor:scheme:id)\n"
	"  -X, -rating      STR  Set the Rating(none, clean, explicit)\n"
    "  -w, -writer      STR  Set the composer information\n"
    "  -y, -year        NUM  Set the release date\n"
    "  -z, -artistid    NUM  Set the artist ID\n"
    "  -Z, -composerid  NUM  Set the composer ID\n"
    "  -r, -remove      STR  Remove tags by code (e.g. \"-r cs\"\n"
    "                        removes the comment and song tags)";

extern "C" int
    main( int argc, char** argv )
{
    const prog::Option long_options[] = {
        { "help",        prog::Option::NO_ARG,       0, OPT_HELP         },
        { "version",     prog::Option::NO_ARG,       0, OPT_VERSION      },
        { "album",       prog::Option::REQUIRED_ARG, 0, OPT_ALBUM        },
        { "artist",      prog::Option::REQUIRED_ARG, 0, OPT_ARTIST       },
        { "comment",     prog::Option::REQUIRED_ARG, 0, OPT_COMMENT      },
        { "copyright",   prog::Option::REQUIRED_ARG, 0, OPT_COPYRIGHT    },
        { "disk",        prog::Option::REQUIRED_ARG, 0, OPT_DISK         },
        { "disks",       prog::Option::REQUIRED_ARG, 0, OPT_DISKS        },
        { "encodedby",   prog::Option::REQUIRED_ARG, 0, OPT_ENCODEDBY    },
        { "tool",        prog::Option::REQUIRED_ARG, 0, OPT_TOOL         },
        { "genre",       prog::Option::REQUIRED_ARG, 0, OPT_GENRE        },
        { "grouping",    prog::Option::REQUIRED_ARG, 0, OPT_GROUPING     },
        { "hdvideo",     prog::Option::REQUIRED_ARG, 0, OPT_HD           },
        { "type",        prog::Option::REQUIRED_ARG, 0, OPT_MEDIA_TYPE   },
        { "contentid",   prog::Option::REQUIRED_ARG, 0, OPT_CONTENTID    },
        { "longdesc",    prog::Option::REQUIRED_ARG, 0, OPT_LONGDESC     },
        { "genreid",     prog::Option::REQUIRED_ARG, 0, OPT_GENREID      },
        { "lyrics",      prog::Option::REQUIRED_ARG, 0, OPT_LYRICS       },
        { "description", prog::Option::REQUIRED_ARG, 0, OPT_DESCRIPTION  },
        { "episode",     prog::Option::REQUIRED_ARG, 0, OPT_TVEPISODE    },
        { "season",      prog::Option::REQUIRED_ARG, 0, OPT_TVSEASON     },
        { "network",     prog::Option::REQUIRED_ARG, 0, OPT_TVNETWORK    },
        { "episodeid",   prog::Option::REQUIRED_ARG, 0, OPT_TVEPISODEID  },
        { "playlistid",  prog::Option::REQUIRED_ARG, 0, OPT_PLAYLISTID   },
        { "picture",     prog::Option::REQUIRED_ARG, 0, OPT_PICTURE      },
        { "podcast",     prog::Option::REQUIRED_ARG, 0, OPT_PODCAST      },
        { "song",        prog::Option::REQUIRED_ARG, 0, OPT_NAME         },
        { "show",        prog::Option::REQUIRED_ARG, 0, OPT_TVSHOW       },
        { "tempo",       prog::Option::REQUIRED_ARG, 0, OPT_TEMPO        },
        { "track",       prog::Option::REQUIRED_ARG, 0, OPT_TRACK        },
        { "tracks",      prog::Option::REQUIRED_ARG, 0, OPT_TRACKS       },
        { "xid",         prog::Option::REQUIRED_ARG, 0, OPT_XID          },
        { "writer",      prog::Option::REQUIRED_ARG, 0, OPT_COMPOSER     },
        { "year",        prog::Option::REQUIRED_ARG, 0, OPT_RELEASEDATE  },
        { "artistid",    prog::Option::REQUIRED_ARG, 0, OPT_ARTISTID     },
        { "composerid",  prog::Option::REQUIRED_ARG, 0, OPT_COMPOSERID   },
        { "remove",      prog::Option::REQUIRED_ARG, 0, OPT_REMOVE       },
        { "albumartist", prog::Option::REQUIRED_ARG, 0, OPT_ALBUM_ARTIST },
        { "category",    prog::Option::REQUIRED_ARG, 0, OPT_CATEGORY },
        { "rating",      prog::Option::REQUIRED_ARG, 0, OPT_RATING },
        { NULL, prog::Option::NO_ARG, 0, 0 }
    };

    /* Sparse arrays of tag data: some space is wasted, but it's more
       convenient to say tags[OPT_SONG] than to enumerate all the
       metadata types (again) as a struct. */
    const char *tags[UCHAR_MAX];
    uint64_t nums[UCHAR_MAX];

    memset( tags, 0, sizeof( tags ) );
    memset( nums, 0, sizeof( nums ) );

    /* Any modifications requested? */
    int mods = 0;

    /* Option-processing loop. */
    int c = prog::getOptionSingle( argc, argv, OPT_STRING, long_options, NULL );
    while ( c != -1 ) {
        int r = 2;
        switch ( c ) {
                /* getopt() returns '?' if there was an error.  It already
                   printed the error message, so just return. */
            case '?':
                return 1;

                /* Help and version requests handled here. */
            case OPT_HELP:
                fprintf( stderr, "usage %s %s\n", argv[0], help_text );
                return 0;
            case OPT_VERSION:
                fprintf( stderr, "%s - %s\n", argv[0], MP4V2_PROJECT_name_formal );
                return 0;

                /* Integer arguments: convert them using sscanf(). */
            case OPT_TEMPO:
            case OPT_DISK:
            case OPT_DISKS:
            case OPT_HD:
            case OPT_CONTENTID:
            case OPT_GENREID:
            case OPT_TVEPISODE:
            case OPT_TVSEASON:
            case OPT_PLAYLISTID:
            case OPT_TRACK:
            case OPT_TRACKS:
            case OPT_ARTISTID:
            case OPT_COMPOSERID:
            case OPT_PODCAST:
                if ( c == OPT_PLAYLISTID ) {
                    r = sscanf(prog::optarg, "%" PRIu64 "", &nums[c]);
                } else {
                    unsigned int n;
                    r = sscanf( prog::optarg, "%u", &n );
                    if ( r >= 1 )
                    {
                        nums[c] = static_cast<uint64_t>( n );
                    }
                }
                if ( r < 1 ) {
                    fprintf( stderr, "%s: option requires integer argument -- %c\n",
                             argv[0], c );
                    return 2;
                }
                /* Break not, lest ye be broken.  :) */
                /* All arguments: all valid options end up here, and we just
                   stuff the string pointer into the tags[] array. */
            default:
                tags[c] = prog::optarg;
                mods++;
        } /* end switch */

        c = prog::getOptionSingle( argc, argv, OPT_STRING, long_options, NULL );
    } /* end while */

    /* Check that we have at least one non-option argument */
    if ( ( argc - prog::optind ) < 1 ) {
        fprintf( stderr,
                 "%s: You must specify at least one MP4 file.\n",
                 argv[0] );
        fprintf( stderr, "usage %s %s\n", argv[0], help_text );
        return 3;
    }

    /* Check that we have at least one requested modification.  Probably
       it's useful instead to print the metadata if no modifications are
       requested? */
    if ( !mods ) {
        fprintf( stderr,
                 "%s: You must specify at least one tag modification.\n",
                 argv[0] );
        fprintf( stderr, "usage %s %s\n", argv[0], help_text );
        return 4;
    }

    /* Loop through the non-option arguments, and modify the tags as
       requested. */
    while ( prog::optind < argc ) {
        char *mp4 = argv[prog::optind++];

        MP4FileHandle h = MP4Modify( mp4 );
        if ( h == MP4_INVALID_FILE_HANDLE ) {
            fprintf( stderr, "Could not open '%s'... aborting\n", mp4 );
            return 5;
        }
        /* Read out the existing metadata */
        const MP4Tags* mdata = MP4TagsAlloc();
        MP4TagsFetch( mdata, h );

        /* Remove any tags */
        if ( ELEMENT_OF(tags,OPT_REMOVE) ) {
            for ( const char *p = ELEMENT_OF(tags,OPT_REMOVE); *p; p++ ) {
                switch ( *p ) {
                    case OPT_ALBUM:
                        MP4TagsSetAlbum( mdata, NULL );
                        break;
                    case OPT_ARTIST:
                        MP4TagsSetArtist( mdata, NULL );
                        break;
                    case OPT_TEMPO:
                        MP4TagsSetTempo( mdata, NULL );
                        break;
                    case OPT_COMMENT:
                        MP4TagsSetComments( mdata, NULL );
                        break;
                    case OPT_COPYRIGHT:
                        MP4TagsSetCopyright( mdata, NULL );
                        break;
                    case OPT_DISK:
                        MP4TagsSetDisk( mdata, NULL );
                        break;
                    case OPT_DISKS:
                        MP4TagsSetDisk( mdata, NULL );
                        break;
                    case OPT_ENCODEDBY:
                        MP4TagsSetEncodedBy( mdata, NULL );
                        break;
                    case OPT_TOOL:
                        MP4TagsSetEncodingTool( mdata, NULL );
                        break;
                    case OPT_GENRE:
                        MP4TagsSetGenre( mdata, NULL );
                        break;
                    case OPT_GROUPING:
                        MP4TagsSetGrouping( mdata, NULL );
                        break;
                    case OPT_HD:
                        MP4TagsSetHDVideo( mdata, NULL );
                        break;
                    case OPT_MEDIA_TYPE:
                        MP4TagsSetMediaType( mdata, NULL );
                        break;
                    case OPT_CONTENTID:
                        MP4TagsSetContentID( mdata, NULL );
                        break;
                    case OPT_LONGDESC:
                        MP4TagsSetLongDescription( mdata, NULL );
                        break;
                    case OPT_GENREID:
                        MP4TagsSetGenreID( mdata, NULL );
                        break;
                    case OPT_LYRICS:
                        MP4TagsSetLyrics( mdata, NULL );
                        break;
                    case OPT_DESCRIPTION:
                        MP4TagsSetDescription( mdata, NULL );
                        break;
                    case OPT_TVEPISODE:
                        MP4TagsSetTVEpisode( mdata, NULL );
                        break;
                    case OPT_TVSEASON:
                        MP4TagsSetTVSeason( mdata, NULL );
                        break;
                    case OPT_TVNETWORK:
                        MP4TagsSetTVNetwork( mdata, NULL );
                        break;
                    case OPT_TVEPISODEID:
                        MP4TagsSetTVEpisodeID( mdata, NULL );
                        break;
                    case OPT_PLAYLISTID:
                        MP4TagsSetPlaylistID( mdata, NULL );
                        break;
                    case OPT_PICTURE:
                        if( mdata->artworkCount )
                            MP4TagsRemoveArtwork( mdata, 0 );
                        break;
                    case OPT_ALBUM_ARTIST:
                        MP4TagsSetAlbumArtist( mdata, NULL );
                        break ;
                    case OPT_NAME:
                        MP4TagsSetName( mdata, NULL );
                        break;
                    case OPT_TVSHOW:
                        MP4TagsSetTVShow( mdata, NULL );
                        break;
                    case OPT_TRACK:
                        MP4TagsSetTrack( mdata, NULL );
                        break;
                    case OPT_TRACKS:
                        MP4TagsSetTrack( mdata, NULL );
                        break;
                    case OPT_XID:
                        MP4TagsSetXID( mdata, NULL );
                        break;
                    case OPT_COMPOSER:
                        MP4TagsSetComposer( mdata, NULL );
                        break;
                    case OPT_RELEASEDATE:
                        MP4TagsSetReleaseDate( mdata, NULL );
                        break;
                    case OPT_ARTISTID:
                        MP4TagsSetArtistID( mdata, NULL );
                        break;
                    case OPT_COMPOSERID:
                        MP4TagsSetComposerID( mdata, NULL );
                        break;
                    case OPT_PODCAST:
                        MP4TagsSetPodcast(mdata, NULL);
                        break;
                    case OPT_CATEGORY:
                        MP4TagsSetCategory(mdata, NULL);
                        break;
                    case OPT_RATING:
                        MP4TagsSetContentRating(mdata, NULL);
                        break;
                }
            }
        }

        /* Track/disk numbers need to be set all at once, but we'd like to
           allow users to just specify -T 12 to indicate that all existing
           track numbers are out of 12.  This means we need to look up the
           current info if it is not being set. */

        if ( ELEMENT_OF(tags,OPT_TRACK) || ELEMENT_OF(tags,OPT_TRACKS) ) {
            MP4TagTrack tt;
            tt.index = 0;
            tt.total = 0;

            if( mdata->track ) {
                tt.index = mdata->track->index;
                tt.total = mdata->track->total;
            }

            if( ELEMENT_OF(tags,OPT_TRACK) )
                tt.index = static_cast<uint16_t>( ELEMENT_OF(nums,OPT_TRACK) );
            if( ELEMENT_OF(tags,OPT_TRACKS) )
                tt.total = static_cast<uint16_t>( ELEMENT_OF(nums,OPT_TRACKS) );

            MP4TagsSetTrack( mdata, &tt );
        }

        if ( ELEMENT_OF(tags,OPT_DISK) || ELEMENT_OF(tags,OPT_DISKS) ) {
            MP4TagDisk td;
            td.index = 0;
            td.total = 0;

            if( mdata->disk ) {
                td.index = mdata->disk->index;
                td.total = mdata->disk->total;
            }

            if( ELEMENT_OF(tags,OPT_DISK) )
                td.index = static_cast<uint16_t>( ELEMENT_OF(nums,OPT_DISK) );
            if( ELEMENT_OF(tags,OPT_DISKS) )
                td.total = static_cast<uint16_t>( ELEMENT_OF(nums,OPT_DISKS) );

            MP4TagsSetDisk( mdata, &td );
        }

        /* Set the other relevant attributes */
        for ( int i = 0;  i < UCHAR_MAX;  i++ ) {
            if ( tags[i] ) {
                switch ( i ) {
                    case OPT_ALBUM:
                        MP4TagsSetAlbum( mdata, tags[i] );
                        break;
                    case OPT_ARTIST:
                        MP4TagsSetArtist( mdata, tags[i] );
                        break;
                    case OPT_TEMPO:
                    {
                        uint16_t value = static_cast<uint16_t>( nums[i] );
                        MP4TagsSetTempo( mdata, &value );
                        break;
                    }
                    case OPT_COMMENT:
                        MP4TagsSetComments( mdata, tags[i] );
                        break;
                    case OPT_COPYRIGHT:
                        MP4TagsSetCopyright( mdata, tags[i] );
                        break;
                    case OPT_ENCODEDBY:
                        MP4TagsSetEncodedBy( mdata, tags[i] );
                        break;
                    case OPT_TOOL:
                        MP4TagsSetEncodingTool( mdata, tags[i] );
                        break;
                    case OPT_GENRE:
                        MP4TagsSetGenre( mdata, tags[i] );
                        break;
                    case OPT_GROUPING:
                        MP4TagsSetGrouping( mdata, tags[i] );
                        break;
                    case OPT_HD:
                    {
                        uint8_t value = static_cast<uint8_t>( nums[i] );
                        MP4TagsSetHDVideo( mdata, &value );
                        break;
                    }
                    case OPT_MEDIA_TYPE:
                    {
                        uint8_t st = static_cast<uint8_t>( itmf::enumStikType.toType( tags[i] ) ) ;
                        MP4TagsSetMediaType( mdata, &st );
                        break;
                    }
                    case OPT_CONTENTID:
                    {
                        uint32_t value = static_cast<uint32_t>( nums[i] );
                        MP4TagsSetContentID( mdata, &value );
                        break;
                    }
                    case OPT_LONGDESC:
                        MP4TagsSetLongDescription( mdata, tags[i] );
                        break;
                    case OPT_GENREID:
                    {
                        uint32_t value = static_cast<uint32_t>( nums[i] );
                        MP4TagsSetGenreID( mdata, &value );
                        break;
                    }
                    case OPT_LYRICS:
                        MP4TagsSetLyrics( mdata, tags[i] );
                        break;
                    case OPT_DESCRIPTION:
                        MP4TagsSetDescription( mdata, tags[i] );
                        break;
                    case OPT_TVEPISODE:
                    {
                        uint32_t value = static_cast<uint32_t>( nums[i] );
                        MP4TagsSetTVEpisode( mdata, &value );
                        break;
                    }
                    case OPT_TVSEASON:
                    {
                        uint32_t value = static_cast<uint32_t>( nums[i] );
                        MP4TagsSetTVSeason( mdata, &value );
                        break;
                    }
                    case OPT_TVNETWORK:
                        MP4TagsSetTVNetwork( mdata, tags[i] );
                        break;
                    case OPT_TVEPISODEID:
                        MP4TagsSetTVEpisodeID( mdata, tags[i] );
                        break;
                    case OPT_PLAYLISTID:
                    {
                        uint64_t value = static_cast<uint64_t>( nums[i] );
                        MP4TagsSetPlaylistID( mdata, &value );
                        break;
                    }
                    case OPT_PICTURE:
                    {
                        File in( tags[i], File::MODE_READ );
                        if( !in.open() ) {
                            MP4TagArtwork art;
                            art.size = (uint32_t)in.size;
                            art.data = malloc( art.size );
                            art.type = MP4_ART_UNDEFINED;

                            File::Size nin;
                            if( !in.read( art.data, art.size, nin ) && nin == art.size ) {
                                if( mdata->artworkCount )
                                    MP4TagsRemoveArtwork( mdata, 0 );
                                MP4TagsAddArtwork( mdata, &art ); 
                            }

                            free( art.data );
                            in.close();
                        }
                        else {
                            fprintf( stderr, "Art file %s not found\n", tags[i] );
                        }
                    }
                    case OPT_ALBUM_ARTIST:
                        MP4TagsSetAlbumArtist( mdata, tags[i] );
                        break;
                    case OPT_NAME:
                        MP4TagsSetName( mdata, tags[i] );
                        break;
                    case OPT_TVSHOW:
                        MP4TagsSetTVShow( mdata, tags[i] );
                        break;
                    case OPT_XID:
                        MP4TagsSetXID( mdata, tags[i] );
                        break;
                    case OPT_COMPOSER:
                        MP4TagsSetComposer( mdata, tags[i] );
                        break;
                    case OPT_RELEASEDATE:
                        MP4TagsSetReleaseDate( mdata, tags[i] );
                        break;
                    case OPT_ARTISTID:
                    {
                        uint32_t value = static_cast<uint32_t>( nums[i] );
                        MP4TagsSetArtistID( mdata, &value );
                        break;
                    }
                    case OPT_COMPOSERID:
                    {
                        uint32_t value = static_cast<uint32_t>( nums[i] );
                        MP4TagsSetComposerID( mdata, &value );
                        break;
                    }
                    case OPT_PODCAST:
                    {
                        uint8_t value = static_cast<uint8_t>( nums[i] );
                        MP4TagsSetPodcast(mdata, &value);
                        break;
                    }
                    case OPT_CATEGORY:
                    {
                        MP4TagsSetCategory(mdata, tags[i]);
                        break;
                    }
                    case OPT_RATING:
                    {
                        uint8_t rating = static_cast<uint8_t>( itmf::enumContentRating.toType( tags[i] ) ) ;
                        MP4TagsSetContentRating(mdata, &rating);
                        break;
                    }
                }
            }
        }
        /* Write out all tag modifications, free and close */
        MP4TagsStore( mdata, h );
        MP4TagsFree( mdata );
        MP4Close( h );
    } /* end while optind < argc */
    return 0;
}
