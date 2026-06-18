///////////////////////////////////////////////////////////////////////////////
//
//  The contents of this file are subject to the Mozilla Public License
//  Version 1.1 (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//  http://www.mozilla.org/MPL/
//
//  Software distributed under the License is distributed on an "AS IS"
//  basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
//  License for the specific language governing rights and limitations
//  under the License.
// 
//  The Original Code is MP4v2.
// 
//  The Initial Developer of the Original Code is Ullrich Pollaehne.
//  Portions created by Kona Blend are Copyright (C) 2008.
//  Portions created by David Byron are Copyright (C) 2010.
//  All Rights Reserved.
//
//  Contributors:
//      Kona Blend, kona8lend@@gmail.com
//      Ullrich Pollaehne, u.pollaehne@@gmail.com
//      David Byron, dbyron@dbyron.com
//
///////////////////////////////////////////////////////////////////////////////
#include "util/impl.h"

namespace mp4v2 { namespace util {

///////////////////////////////////////////////////////////////////////////////
///
/// Chapter utility program class.
///
/// This class provides an implementation for a QuickTime/Nero chapter utility which
/// allows to add, delete, convert export or import QuickTime and Nero chapters
/// in MP4 container files.
///
///
/// @see Utility
///
///////////////////////////////////////////////////////////////////////////////
class ChapterUtility : public Utility
{
private:
    static const double CHAPTERTIMESCALE; //!< the timescale used for chapter tracks (1000)

    enum FileLongCode {
        LC_CHPT_ANY = _LC_MAX,
        LC_CHPT_QT,
        LC_CHPT_NERO,
        LC_CHPT_COMMON,
        LC_CHP_LIST,
        LC_CHP_CONVERT,
        LC_CHP_EVERY,
        LC_CHP_EXPORT,
        LC_CHP_IMPORT,
        LC_CHP_REMOVE
    };

    enum ChapterFormat {
        CHPT_FMT_NATIVE,
        CHPT_FMT_COMMON
    };

    enum FormatState {
        FMT_STATE_INITIAL,
        FMT_STATE_TIME_LINE,
        FMT_STATE_TITLE_LINE,
        FMT_STATE_FINISH
    };

public:
    ChapterUtility( int, char** );

protected:
    // delegates implementation
    bool utility_option( int, bool& );
    bool utility_job( JobContext& );

private:
    bool actionList    ( JobContext& ); 
    bool actionConvert ( JobContext& );
    bool actionEvery   ( JobContext& );
    bool actionExport  ( JobContext& );
    bool actionImport  ( JobContext& );
    bool actionRemove  ( JobContext& );

private:
    Group  _actionGroup;
    Group  _parmGroup;

    bool        (ChapterUtility::*_action)( JobContext& );
    void        fixQtScale(MP4FileHandle );
    MP4TrackId  getReferencingTrack( MP4FileHandle, bool& );
    string      getChapterTypeName( MP4ChapterType ) const;
    bool        parseChapterFile( const string, vector<MP4Chapter_t>&, Timecode::Format& );
    bool        readChapterFile( const string, char**, File::Size& );
    MP4Duration convertFrameToMillis( MP4Duration, uint32_t );

    MP4ChapterType _ChapterType;
    ChapterFormat  _ChapterFormat;
    uint32_t       _ChaptersEvery;
    string         _ChapterFile;
};

///////////////////////////////////////////////////////////////////////////////

const double ChapterUtility::CHAPTERTIMESCALE = 1000.0;

///////////////////////////////////////////////////////////////////////////////

ChapterUtility::ChapterUtility( int argc, char** argv )
    : Utility        ( "mp4chaps", argc, argv )
    , _actionGroup   ( "ACTIONS" )
    , _parmGroup     ( "ACTION PARAMETERS" )
    , _action        ( NULL )
    , _ChapterType   ( MP4ChapterTypeAny )
    , _ChapterFormat ( CHPT_FMT_NATIVE )
    , _ChaptersEvery ( 0 )
{
    // add standard options which make sense for this utility
    _group.add( STD_OPTIMIZE );
    _group.add( STD_DRYRUN );
    _group.add( STD_KEEPGOING );
    _group.add( STD_OVERWRITE );
    _group.add( STD_FORCE );
    _group.add( STD_QUIET );
    _group.add( STD_DEBUG );
    _group.add( STD_VERBOSE );
    _group.add( STD_HELP );
    _group.add( STD_VERSION );
    _group.add( STD_VERSIONX );

    _parmGroup.add( 'A', false, "chapter-any",   false, LC_CHPT_ANY,    "act on any chapter type (default)" );
    _parmGroup.add( 'Q', false, "chapter-qt",    false, LC_CHPT_QT,     "act on QuickTime chapters" );
    _parmGroup.add( 'N', false, "chapter-nero",  false, LC_CHPT_NERO,   "act on Nero chapters" );
    _parmGroup.add( 'C', false, "format-common", false, LC_CHPT_COMMON, "export chapters in common format" );
    _groups.push_back( &_parmGroup );

    _actionGroup.add( 'l', false, "list",    false, LC_CHP_LIST,    "list available chapters" );
    _actionGroup.add( 'c', false, "convert", false, LC_CHP_CONVERT, "convert available chapters" );
    _actionGroup.add( 'e', true,  "every",   true,  LC_CHP_EVERY,   "create chapters every NUM seconds", "NUM" );
    _actionGroup.add( 'x', false, "export",  false, LC_CHP_EXPORT,  "export chapters to mp4file.chapters.txt", "TXT" );
    _actionGroup.add( 'i', false, "import",  false, LC_CHP_IMPORT,  "import chapters from mp4file.chapters.txt", "TXT" );
    _actionGroup.add( 'r', false, "remove",  false, LC_CHP_REMOVE,  "remove all chapters" );
    _groups.push_back( &_actionGroup );

    _usage = "[OPTION]... ACTION [ACTION PARAMETERS] mp4file...";
    _description =
        // 79-cols, inclusive, max desired width
        // |----------------------------------------------------------------------------|
        "\nFor each mp4 file specified, perform the specified ACTION. An action must be"
        "\nspecified. Some options are not applicable to some actions.";
}

///////////////////////////////////////////////////////////////////////////////

/** Action for listing chapters from <b>job.file</b>
 *
 *  
 *  @param job the job to process
 *  @return mp4v2::util::SUCCESS if successful, mp4v2::util::FAILURE otherwise
 */
bool
ChapterUtility::actionList( JobContext& job )
{
    job.fileHandle = MP4Read( job.file.c_str() );
    if( job.fileHandle == MP4_INVALID_FILE_HANDLE )
    {
        return herrf( "unable to open for read: %s\n", job.file.c_str() );
    }

    MP4Chapter_t * chapters = 0;
    uint32_t chapterCount = 0;

    // get the list of chapters
    MP4ChapterType chtp = MP4GetChapters(job.fileHandle, &chapters, &chapterCount, _ChapterType);
    if (0 == chapterCount)
    {
        verbose1f( "File \"%s\" does not contain chapters of type %s\n", job.file.c_str(),
                   getChapterTypeName( _ChapterType ).c_str() );
        return SUCCESS;
    }

    // start output (more or less like mp4box does)
    ostringstream report;
    report << getChapterTypeName( chtp ) << ' ' << "Chapters of " << '"' << job.file << '"' << endl;

    Timecode duration(0, CHAPTERTIMESCALE);
    duration.setFormat( Timecode::DECIMAL );
    for (uint32_t i = 0; i < chapterCount; ++i)
    {
        // print the infos
        report << '\t' << "Chapter #" << setw( 3 ) << setfill( '0' ) << i+1
               << " - " << duration.svalue << " - " << '"' << chapters[i].title << '"' << endl;

        // add the duration of this chapter to the sum (is the start time of the next chapter)
        duration += Timecode(chapters[i].duration, CHAPTERTIMESCALE);
    }

    verbose1f( "%s", report.str().c_str() );

    // free up the memory
    MP4Free(chapters);

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////

/** Action for converting chapters in <b>job.file</b>
 *
 *  
 *  @param job the job to process
 *  @return mp4v2::util::SUCCESS if successful, mp4v2::util::FAILURE otherwise
 */
bool
ChapterUtility::actionConvert( JobContext& job )
{
    MP4ChapterType sourceType;

    switch( _ChapterType )
    {
    case MP4ChapterTypeNero:
        sourceType = MP4ChapterTypeQt;
        break;
    case MP4ChapterTypeQt:
        sourceType = MP4ChapterTypeNero;
        break;
    default:
        return herrf( "invalid chapter type \"%s\" define the chapter type to convert to\n",
                      getChapterTypeName( _ChapterType ).c_str() );
    }

    ostringstream oss;
    oss << "converting chapters in file " << '"' << job.file << '"'
        << " from " << getChapterTypeName( sourceType ) << " to " << getChapterTypeName( _ChapterType ) << endl;

    verbose1f( "%s", oss.str().c_str() );
    if( dryrunAbort() )
    {
        return SUCCESS;
    }

    job.fileHandle = MP4Modify( job.file.c_str() );
    if( job.fileHandle == MP4_INVALID_FILE_HANDLE )
    {
        return herrf( "unable to open for write: %s\n", job.file.c_str() );
    }

    MP4ChapterType chtp = MP4ConvertChapters( job.fileHandle, _ChapterType );
    if( MP4ChapterTypeNone == chtp )
    {
        return herrf( "File %s does not contain chapters of type %s\n", job.file.c_str(),
                      getChapterTypeName( sourceType ).c_str() );
    }

    fixQtScale( job.fileHandle );
    job.optimizeApplicable = true;

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////

/** Action for setting chapters every n second in <b>job.file</b>
 *
 *  
 *  @param job the job to process
 *  @return mp4v2::util::SUCCESS if successful, mp4v2::util::FAILURE otherwise
 */
bool
ChapterUtility::actionEvery( JobContext& job )
{
    ostringstream oss;
    oss << "Setting " << getChapterTypeName( _ChapterType ) << " chapters every "
        << _ChaptersEvery << " seconds in file " << '"' << job.file << '"' << endl;

    verbose1f( "%s", oss.str().c_str() );
    if( dryrunAbort() )
    {
        return SUCCESS;
    }

    job.fileHandle = MP4Modify( job.file.c_str() );
    if( job.fileHandle == MP4_INVALID_FILE_HANDLE )
    {
        return herrf( "unable to open for write: %s\n", job.file.c_str() );
    }

    bool isVideoTrack = false;
    MP4TrackId refTrackId = getReferencingTrack( job.fileHandle, isVideoTrack );
    if( !MP4_IS_VALID_TRACK_ID(refTrackId) )
    {
        return herrf( "unable to find a video or audio track in file %s\n", job.file.c_str() );
    }

    Timecode refTrackDuration( MP4GetTrackDuration( job.fileHandle, refTrackId ), MP4GetTrackTimeScale( job.fileHandle, refTrackId ) );
    refTrackDuration.setScale( CHAPTERTIMESCALE );

    Timecode chapterDuration( _ChaptersEvery * 1000, CHAPTERTIMESCALE );
    chapterDuration.setFormat( Timecode::DECIMAL );
    vector<MP4Chapter_t> chapters;

    do
    {
        MP4Chapter_t chap;
        chap.duration = refTrackDuration.duration > chapterDuration.duration ? chapterDuration.duration : refTrackDuration.duration;
        sprintf(chap.title, "Chapter %lu", (unsigned long)chapters.size()+1);

        chapters.push_back( chap );
        refTrackDuration -= chapterDuration;
    }
    while( refTrackDuration.duration > 0 );

    if( 0 < chapters.size() )
    {
        MP4SetChapters(job.fileHandle, &chapters[0], (uint32_t)chapters.size(), _ChapterType);
    }

    fixQtScale( job.fileHandle );
    job.optimizeApplicable = true;

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////

/** Action for exporting chapters from the <b>job.file</b>
 *
 *  
 *  @param job the job to process
 *  @return mp4v2::util::SUCCESS if successful, mp4v2::util::FAILURE otherwise
 */
bool
ChapterUtility::actionExport( JobContext& job )
{
    job.fileHandle = MP4Read( job.file.c_str() );
    if( job.fileHandle == MP4_INVALID_FILE_HANDLE )
    {
        return herrf( "unable to open for read: %s\n", job.file.c_str() );
    }

    // get the list of chapters
    MP4Chapter_t*  chapters = 0;
    uint32_t       chapterCount = 0;
    MP4ChapterType chtp = MP4GetChapters( job.fileHandle, &chapters, &chapterCount, _ChapterType );
    if (0 == chapterCount)
    {
        return herrf( "File \"%s\" does not contain chapters of type %s\n", job.file.c_str(),
                      getChapterTypeName( chtp ).c_str() );
    }

    // build the filename
    string outName = job.file;
    if( _ChapterFile.empty() )
    {
        FileSystem::pathnameStripExtension( outName );
        outName.append( ".chapters.txt" );
    }
    else
    {
        outName = _ChapterFile;
    }

    ostringstream oss;
    oss << "Exporting " << chapterCount << " " << getChapterTypeName( chtp );
    oss << " chapters from file " << '"' << job.file << '"' << " into chapter file " << '"' << outName << '"' << endl;

    verbose1f( "%s", oss.str().c_str() );
    if( dryrunAbort() )
    {
        // free up the memory
        MP4Free(chapters);

        return SUCCESS;
    }

    // open the file
    File out( outName, File::MODE_CREATE );
    if( openFileForWriting( out ) )
    {
        // free up the memory
        MP4Free(chapters);

        return FAILURE;
    }

    // write the chapters
#if defined( _WIN32 )
    static const char* LINEND = "\r\n";
#else
    static const char* LINEND = "\n";
#endif
    File::Size nout;
    bool failure = SUCCESS;
    int width = 2;
    if( CHPT_FMT_COMMON == _ChapterFormat && (chapterCount / 100) >= 1 )
    {
        width = 3;
    }
    Timecode duration( 0, CHAPTERTIMESCALE );
    duration.setFormat( Timecode::DECIMAL );
    for( uint32_t i = 0; i < chapterCount; ++i )
    {
        // print the infos
        ostringstream oss;
        switch( _ChapterFormat )
        {
            case CHPT_FMT_COMMON:
                oss << "CHAPTER" << setw( width ) << setfill( '0' ) << i+1 <<     '=' << duration.svalue << LINEND
                    << "CHAPTER" << setw( width ) << setfill( '0' ) << i+1 << "NAME=" << chapters[i].title << LINEND;
                break;
            case CHPT_FMT_NATIVE:
            default:
                oss << duration.svalue << ' ' << chapters[i].title << LINEND;
        }

        string str = oss.str();
        if( out.write( str.c_str(), str.size(), nout ) )
        {
            failure = herrf( "write to %s failed: %s\n", outName.c_str(), sys::getLastErrorStr() );
            break;
        }

        // add the duration of this chapter to the sum (the start time of the next chapter)
        duration += Timecode(chapters[i].duration, CHAPTERTIMESCALE);
    }
    out.close();
    if( failure )
    {
        verbose1f( "removing file %s\n", outName.c_str() );
        ::remove( outName.c_str() );
    }

    // free up the memory
    MP4Free(chapters);

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////

/** Action for importing chapters into the <b>job.file</b>
 *
 *  
 *  @param job the job to process
 *  @return mp4v2::util::SUCCESS if successful, mp4v2::util::FAILURE otherwise
 */
bool
ChapterUtility::actionImport( JobContext& job )
{
    vector<MP4Chapter_t> chapters;
    Timecode::Format format;

    // create the chapter file name
    string inName = job.file;
    if( _ChapterFile.empty() )
    {
        FileSystem::pathnameStripExtension( inName );
        inName.append( ".chapters.txt" );
    }
    else
    {
        inName = _ChapterFile;
    }

    if( parseChapterFile( inName, chapters, format ) )
    {
        return FAILURE;
    }

    ostringstream oss;
    oss << "Importing " << chapters.size() << " " << getChapterTypeName( _ChapterType );
    oss << " chapters from file " << inName << " into file " << '"' << job.file << '"' << endl;

    verbose1f( "%s", oss.str().c_str() );
    if( dryrunAbort() )
    {
        return SUCCESS;
    }

    if( 0 == chapters.size() )
    {
        return herrf( "No chapters found in file %s\n", inName.c_str() );
    }

    job.fileHandle = MP4Modify( job.file.c_str() );
    if( job.fileHandle == MP4_INVALID_FILE_HANDLE )
    {
        return herrf( "unable to open for write: %s\n", job.file.c_str() );
    }

    bool isVideoTrack = false;
    MP4TrackId refTrackId = getReferencingTrack( job.fileHandle, isVideoTrack );
    if( !MP4_IS_VALID_TRACK_ID(refTrackId) )
    {
        return herrf( "unable to find a video or audio track in file %s\n", job.file.c_str() );
    }
    if( Timecode::FRAME == format && !isVideoTrack )
    {
        // we need a video track for this
        return herrf( "unable to find a video track in file %s but chapter file contains frame timestamps\n", job.file.c_str() );
    }

    // get duration and recalculate scale
    Timecode refTrackDuration( MP4GetTrackDuration( job.fileHandle, refTrackId ),
                               MP4GetTrackTimeScale( job.fileHandle, refTrackId ) );
    refTrackDuration.setScale( CHAPTERTIMESCALE );

    // check for chapters starting after duration of reftrack
    for( vector<MP4Chapter_t>::iterator it = chapters.begin(); it != chapters.end(); )
    {
        Timecode curr( (*it).duration, CHAPTERTIMESCALE );
        if( refTrackDuration <= curr )
        {
            hwarnf( "Chapter '%s' start: %s, playlength of file: %s, chapter cannot be set\n",
                    (*it).title, curr.svalue.c_str(), refTrackDuration.svalue.c_str() );
            it = chapters.erase( it );
        }
        else
        {
            ++it;
        }
    }
    if( 0 == chapters.size() )
    {
        return SUCCESS;
    }

    // convert start time into duration
	uint32_t framerate = static_cast<uint32_t>( CHAPTERTIMESCALE );
    if( Timecode::FRAME == format )
    {
        // get the framerate
        MP4SampleId sampleCount = MP4GetTrackNumberOfSamples( job.fileHandle, refTrackId );
        Timecode tmpcd( refTrackDuration.svalue, CHAPTERTIMESCALE );
		framerate = static_cast<uint32_t>( std::ceil( ((double)sampleCount / (double)tmpcd.duration) * CHAPTERTIMESCALE ) );
    }

    for( vector<MP4Chapter_t>::iterator it = chapters.begin(); it != chapters.end(); ++it )
    {
        MP4Duration currDur = (*it).duration;
        MP4Duration nextDur =  chapters.end() == it+1 ? refTrackDuration.duration : (*(it+1)).duration;

        if( Timecode::FRAME == format )
        {
            // convert from frame nr to milliseconds
            currDur = convertFrameToMillis( (*it).duration, framerate );

            if( chapters.end() != it+1 )
            {
                nextDur = convertFrameToMillis( (*(it+1)).duration, framerate );
            }
        }

        (*it).duration = nextDur - currDur;
    }

    // now set the chapters
    MP4SetChapters( job.fileHandle, &chapters[0], (uint32_t)chapters.size(), _ChapterType );

    fixQtScale( job.fileHandle );
    job.optimizeApplicable = true;

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////

/** Action for removing chapters from the <b>job.file</b>
 *
 *  
 *  @param job the job to process
 *  @return mp4v2::util::SUCCESS if successful, mp4v2::util::FAILURE otherwise
 */
bool
ChapterUtility::actionRemove( JobContext& job )
{
    ostringstream oss;
    oss << "Deleting " << getChapterTypeName( _ChapterType ) << " chapters from file " << '"' << job.file << '"' << endl;

    verbose1f( "%s", oss.str().c_str() );
    if( dryrunAbort() )
    {
        return SUCCESS;
    }

    job.fileHandle = MP4Modify( job.file.c_str() );
    if( job.fileHandle == MP4_INVALID_FILE_HANDLE )
    {
        return herrf( "unable to open for write: %s\n", job.file.c_str() );
    }

    MP4ChapterType chtp = MP4DeleteChapters( job.fileHandle, _ChapterType );
    if( MP4ChapterTypeNone == chtp )
    {
        return FAILURE;
    }

    fixQtScale( job.fileHandle );
    job.optimizeApplicable = true;

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////

/** process positional argument
 *
 *  @see Utility::utility_job( JobContext& )
 */
bool
ChapterUtility::utility_job( JobContext& job )
{
    if( !_action )
    {
        return herrf( "no action specified\n" );
    }

    return (this->*_action)( job );
}

///////////////////////////////////////////////////////////////////////////////

/** process command-line option
 *
 *  @see Utility::utility_option( int, bool& )
 */
bool
ChapterUtility::utility_option( int code, bool& handled )
{
    handled = true;

    switch( code ) {
        case 'A':
        case LC_CHPT_ANY:
            _ChapterType = MP4ChapterTypeAny;
            break;

        case 'Q':
        case LC_CHPT_QT:
            _ChapterType = MP4ChapterTypeQt;
            break;

        case 'N':
        case LC_CHPT_NERO:
            _ChapterType = MP4ChapterTypeNero;
            break;

        case 'C':
        case LC_CHPT_COMMON:
            _ChapterFormat = CHPT_FMT_COMMON;
            break;

        case 'l':
        case LC_CHP_LIST:
            _action = &ChapterUtility::actionList;
            break;

        case 'e':
        case LC_CHP_EVERY:
        {
            istringstream iss( prog::optarg );
            iss >> _ChaptersEvery;
            if( iss.rdstate() != ios::eofbit )
            {
                return herrf( "invalid number of seconds: %s\n", prog::optarg );
            }
            _action = &ChapterUtility::actionEvery;
            break;
        }

        case 'x':
            _action = &ChapterUtility::actionExport;
            break;

        case LC_CHP_EXPORT:
            _action = &ChapterUtility::actionExport;
            /* currently not supported since the chapters of n input files would be written to one chapter file
            _ChapterFile = prog::optarg;
            if( _ChapterFile.empty() )
            {
                return herrf( "invalid TXT file: empty-string\n" );
            }
            */
            break;

        case 'i':
            _action = &ChapterUtility::actionImport;
            break;

        case LC_CHP_IMPORT:
            _action = &ChapterUtility::actionImport;
            /* currently not supported since the chapters of n input files would be read from one chapter file
            _ChapterFile = prog::optarg;
            if( _ChapterFile.empty() )
            {
                return herrf( "invalid TXT file: empty-string\n" );
            }
            */
            break;

        case 'c':
        case LC_CHP_CONVERT:
            _action = &ChapterUtility::actionConvert;
            break;

        case 'r':
        case LC_CHP_REMOVE:
            _action = &ChapterUtility::actionRemove;
            break;

        default:
            handled = false;
            break;
    }

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////

/** Fix a QuickTime/iPod issue with long audio files.
 *
 *  This function checks if the <b>file</b> is a long audio file (more than
 *  about 6 1/2 hours) and modifies the timescale if necessary to allow
 *  playback of the file in QuickTime player and on some iPod models.
 *
 *  @param file the opened MP4 file
 */
void
ChapterUtility::fixQtScale(MP4FileHandle file)
{
    // get around a QuickTime/iPod issue with storing the number of samples in a signed 32Bit value
    if( INT_MAX < MP4GetDuration(file))
    {
        bool isVideoTrack = false;
        if( MP4_IS_VALID_TRACK_ID(getReferencingTrack( file, isVideoTrack )) & isVideoTrack )
        {
            // if it is a video, everything is different
            return;
        }

        // timescale too high, lower it
        MP4ChangeMovieTimeScale(file, 1000);
    }
}

///////////////////////////////////////////////////////////////////////////////

/** Finds a suitable track that can reference a chapter track.
 *
 *  This function returns the first video or audio track that is found
 *  in the <b>file</b>.
 *  This track ca be used to reference the QuickTime chapter track.
 *
 *  @param file the opened MP4 file
 *  @param isVideoTrack receives true if the found track is video, false otherwise
 *  @return the <b>MP4TrackId</b> of the found track
 */
MP4TrackId
ChapterUtility::getReferencingTrack( MP4FileHandle file, bool& isVideoTrack )
{
    isVideoTrack = false;

    uint32_t trackCount = MP4GetNumberOfTracks( file );
    if( 0 == trackCount )
    {
        return MP4_INVALID_TRACK_ID;
    }

    MP4TrackId refTrackId = MP4_INVALID_TRACK_ID;
    for( uint32_t i = 0; i < trackCount; ++i )
    {
        MP4TrackId    id = MP4FindTrackId( file, i );
        const char* type = MP4GetTrackType( file, id );
        if( MP4_IS_VIDEO_TRACK_TYPE( type ) )
        {
            refTrackId = id;
            isVideoTrack = true;
            break;
        }
        else if( MP4_IS_AUDIO_TRACK_TYPE( type ) )
        {
            refTrackId = id;
            break;
        }
    }

    return refTrackId;
}

///////////////////////////////////////////////////////////////////////////////

/** Return a human readable representation of a <b>MP4ChapterType</b>.
 *
 *  @param chapterType the chapter type
 *  @return a string representing the chapter type
 */
string
ChapterUtility::getChapterTypeName( MP4ChapterType chapterType) const
{
    switch( chapterType )
    {
    case MP4ChapterTypeQt:
        return string( "QuickTime" );
        break;

    case MP4ChapterTypeNero:
        return string( "Nero" );
        break;

    case MP4ChapterTypeAny:
        return string( "QuickTime and Nero" );
        break;

    default:
        return string( "Unknown" );
    }
}

///////////////////////////////////////////////////////////////////////////////

/** Read a file into a buffer.
 *
 *  This function reads the file named by <b>filename</b> into a buffer allocated
 *  by malloc and returns the pointer to this buffer in <b>buffer</b> and the size
 *  of this buffer in <b>fileSize</b>.
 *
 *  @param filename  the name of the file.
 *  @param buffer    receives a pointer to the created buffer
 *  @param fileSize  reference to a <b>io::StdioFile::Size</b> that receives the size of the file
 *  @return true if there was an error, false otherwise
 */
bool
ChapterUtility::readChapterFile( const string filename, char** buffer, File::Size& fileSize )
{
    // open the file
    File in( filename, File::MODE_READ );
    File::Size nin;
    if( in.open() ) {
        return herrf( "opening chapter file '%s' failed: %s\n", filename.c_str(), sys::getLastErrorStr() );
    }

    // get the file size
    fileSize = in.size;
    if( 0 >= fileSize )
    {
        in.close();
        return herrf( "getting size of chapter file '%s' failed: %s\n", filename.c_str(), sys::getLastErrorStr() );
    }

    // allocate a buffer for the file and read the content
    char* inBuf = static_cast<char*>( malloc( fileSize+1 ) );
    if( in.read( inBuf, fileSize, nin ) )
    {
        in.close();
        return herrf( "reading chapter file '%s' failed: %s\n", filename.c_str(), sys::getLastErrorStr() );
    }
    in.close();
    inBuf[fileSize] = 0;

    *buffer = inBuf;

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////

/** Read and parse a chapter file.
 *
 *  This function reads and parses a chapter file and returns a vector of
 *  <b>MP4Chapter_t</b> elements.
 *
 *  @param filename the name of the file.
 *  @param vector   receives a vector of chapters
 *  @param format   receives the <b>Timecode::Format</b> of the timestamps
 *  @return true if there was an error, false otherwise
 */
bool
ChapterUtility::parseChapterFile( const string filename, vector<MP4Chapter_t>& chapters, Timecode::Format& format )
{
    // get the content
    char * inBuf;
    File::Size fileSize;
    if( readChapterFile( filename, &inBuf, fileSize ) )
    {
        return FAILURE;
    }

    // separate the text lines
    char* pos = inBuf;
    while (pos < inBuf + fileSize)
    {
        if (*pos == '\n' || *pos == '\r')
        {
            *pos = 0;
            if (pos > inBuf)
            {
                // remove trailing whitespace
                char* tmp = pos-1;
                while ((*tmp == ' ' || *tmp == '\t') && tmp > inBuf)
                {
                    *tmp = 0;
                    tmp--;
                }
            }
        }
        pos++;
    }
    pos = inBuf;

    // check for a BOM
    char bom[5] = {0};
    // int bomLen = 0;
    const unsigned char* uPos = reinterpret_cast<unsigned char*>( pos );
    if( 0xEF == *uPos && 0xBB == *(uPos+1) && 0xBF == *(uPos+2) )
    {
        // UTF-8 (we do not need the BOM)
        pos += 3;
    }
    else if(   ( 0xFE == *uPos && 0xFF == *(uPos+1) )   // UTF-16 big endian
            || ( 0xFF == *uPos && 0xFE == *(uPos+1) ) ) // UTF-16 little endian
    {
        // store the BOM to prepend the title strings
        bom[0] = *pos++;
        bom[1] = *pos++;
        // bomLen = 2;
        return herrf( "chapter file '%s' has UTF-16 encoding which is not supported (only UTF-8 is allowed)\n",
                      filename.c_str() );
    }
    else if(   ( 0x0 == *uPos && 0x0 == *(uPos+1) && 0xFE == *(uPos+2) && 0xFF == *(uPos+3) )   // UTF-32 big endian
            || ( 0xFF == *uPos && *(uPos+1) == 0xFE && *(uPos+2) == 0x0 && 0x0 == *(uPos+3) ) ) // UTF-32 little endian
    {
        // store the BOM to prepend the title strings
        bom[0] = *pos++;
        bom[1] = *pos++;
        bom[2] = *pos++;
        bom[3] = *pos++;
        // bomLen = 4;
        return herrf( "chapter file '%s' has UTF-32 encoding which is not supported (only UTF-8 is allowed)\n",
                      filename.c_str() );
    }
    (void)bom;

    // parse the lines
    bool failure = false;
    uint32_t currentChapter = 0;
    FormatState formatState = FMT_STATE_INITIAL;
    char* titleStart = 0;
    uint32_t titleLen = 0;
    char* timeStart = 0;
    while( pos < inBuf + fileSize )
    {
        if( 0 == *pos || ' ' == *pos || '\t' == *pos )
        {
            // uninteresting chars
            pos++;
            continue;
        }
        else if( '#' == *pos )
        {
            // comment line
            pos += strlen( pos );
            continue;
        }
        else if( isdigit( *pos ) )
        {
            // mp4chaps native format: hh:mm:ss.sss <title>

            timeStart = pos;

            // read the title if there is one
            titleStart = strchr( timeStart, ' ' );
            if( NULL == titleStart )
            {
                titleStart = strchr( timeStart, '\t' );
            }

            if( NULL != titleStart )
            {
                *titleStart = 0;
                pos = ++titleStart;

                while( ' ' == *titleStart || '\t' == *titleStart )
                {
                    titleStart++;
                }

                titleLen = (uint32_t)strlen( titleStart );

                // advance to the end of the line
                pos = titleStart + 1 + titleLen;
            }
            else
            {
                // advance to the end of the line
                pos += strlen( pos );
            }

            formatState = FMT_STATE_FINISH;
        }
#if defined( _MSC_VER )
        else if( 0 == strnicmp( pos, "CHAPTER", 7 ) )
#else
        else if( 0 == strncasecmp( pos, "CHAPTER", 7 ) )
#endif
        {
            // common format: CHAPTERxx=hh:mm:ss.sss\nCHAPTERxxNAME=<title>

            char* equalsPos = strchr( pos+7, '=' );
            if( NULL == equalsPos )
            {
                herrf( "Unable to parse line \"%s\"\n", pos );
                failure = true;
                break;
            }

            *equalsPos = 0;

            char* tlwr = pos;
            while( equalsPos != tlwr )
            {
                *tlwr = tolower( *tlwr );
                tlwr++;
            }

            if( NULL != strstr( pos, "name" ) )
            {
                // mark the chapter title
                uint32_t chNr = 0;
                sscanf( pos, "chapter%dname", &chNr );
                if( chNr != currentChapter )
                {
                    // different chapter number => different chapter definition pair
                    if( FMT_STATE_INITIAL != formatState )
                    {
                        herrf( "Chapter lines are not consecutive before line \"%s\"\n", pos );
                        failure = true;
                        break;
                    }

                    currentChapter = chNr;
                }
                formatState = FMT_STATE_TIME_LINE == formatState ? FMT_STATE_FINISH
                                                                 : FMT_STATE_TITLE_LINE;

                titleStart = equalsPos + 1;
                titleLen = (uint32_t)strlen( titleStart );

                // advance to the end of the line
                pos = titleStart + titleLen;
            }
            else
            {
                // mark the chapter start time
                uint32_t chNr = 0;
                sscanf( pos, "chapter%d", &chNr );
                if( chNr != currentChapter )
                {
                    // different chapter number => different chapter definition pair
                    if( FMT_STATE_INITIAL != formatState )
                    {
                        herrf( "Chapter lines are not consecutive at line \"%s\"\n", pos );
                        failure = true;
                        break;
                    }

                    currentChapter = chNr;
                }
                formatState = FMT_STATE_TITLE_LINE == formatState ? FMT_STATE_FINISH 
                                                                  : FMT_STATE_TIME_LINE;

                timeStart = equalsPos + 1;

                // advance to the end of the line
                pos = timeStart + strlen( timeStart );
            }
        }

        if( FMT_STATE_FINISH == formatState )
        {
            // now we have title and start time
            MP4Chapter_t chap;

            strncpy( chap.title, titleStart, min( titleLen, (uint32_t)MP4V2_CHAPTER_TITLE_MAX ) );
            chap.title[titleLen] = 0;

            Timecode tc( 0, CHAPTERTIMESCALE );
            string tm( timeStart );
            if( tc.parse( tm ) )
            {
                herrf( "Unable to parse time code from \"%s\"\n", tm.c_str() );
                failure = true;
                break;
            }
            chap.duration = tc.duration;
            format = tc.format;

            // ad the chapter to the list
            chapters.push_back( chap );

            // re-initialize
            formatState = FMT_STATE_INITIAL;
            titleStart = timeStart = NULL;
            titleLen = 0;
        }
    }
    free( inBuf );
    if( failure )
    {
        return failure;
    }

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////

/** Convert from frame to millisecond timestamp.
 *
 *  This function converts a timestamp from hh:mm:ss:ff to hh:mm:ss.sss
 *
 *  @param duration  the timestamp in hours:minutes:seconds:frames.
 *  @param framerate the frames per second
 *  @return the timestamp in milliseconds
 */
MP4Duration
ChapterUtility::convertFrameToMillis( MP4Duration duration, uint32_t framerate )
{
    Timecode tc( duration, CHAPTERTIMESCALE );
    if( framerate < tc.subseconds )
    {
        uint64_t seconds = tc.subseconds / framerate;
        tc.setSeconds( tc.seconds + seconds );
        tc.setSubseconds( (tc.subseconds - (seconds * framerate)) * framerate );
    }
    else
    {
        tc.setSubseconds( tc.subseconds * framerate );
    }

    return tc.duration;
}

///////////////////////////////////////////////////////////////////////////////

}} // namespace mp4v2::util

///////////////////////////////////////////////////////////////////////////////

extern "C"
int main( int argc, char** argv )
{
    mp4v2::util::ChapterUtility util( argc, argv );
    return util.process();
}
