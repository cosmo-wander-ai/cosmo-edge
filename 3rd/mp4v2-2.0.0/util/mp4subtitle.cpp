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
//  The Initial Developer of the Original Code is Kona Blend.
//  Portions created by Kona Blend are Copyright (C) 2008.
//  All Rights Reserved.
//
//  Contributors:
//      Kona Blend, kona8lend@@gmail.com
//      Edward Groenendaal, egroenen@@cisco.com
//
///////////////////////////////////////////////////////////////////////////////

#include "util/impl.h"

namespace mp4v2 { namespace util {

///////////////////////////////////////////////////////////////////////////////

class SubtitleUtility : public Utility
{
private:
    enum SubtitleLongCode {
        LC_LIST = _LC_MAX,
        LC_EXPORT,
        LC_IMPORT,
        LC_REMOVE,
    };

public:
    SubtitleUtility( int, char** );

protected:
    // delegates implementation
    bool utility_option( int, bool& );
    bool utility_job( JobContext& );

private:
    bool actionList   ( JobContext& );
    bool actionExport ( JobContext& );
    bool actionImport ( JobContext& );
    bool actionRemove ( JobContext& );

private:
    Group  _actionGroup;

    bool (SubtitleUtility::*_action)( JobContext& );

    string _stTextFile;
};

///////////////////////////////////////////////////////////////////////////////

SubtitleUtility::SubtitleUtility( int argc, char** argv )
    : Utility      ( "mp4subtitle", argc, argv )
    , _actionGroup ( "ACTIONS" )
    , _action      ( NULL )
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

    _actionGroup.add( "list",   false, LC_LIST,   "list available subtitles" );
    _actionGroup.add( "export", true,  LC_EXPORT, "export subtitles to TXT", "TXT" );
    _actionGroup.add( "import", true,  LC_IMPORT, "import subtitles from TXT", "TXT" );
    _actionGroup.add( "remove", false, LC_REMOVE, "remove all subtitles" );
    _groups.push_back( &_actionGroup );

    _usage = "[OPTION]... ACTION file...";
    _description =
        // 79-cols, inclusive, max desired width
        // |----------------------------------------------------------------------------|
        "\nFor each mp4 file specified, perform the specified ACTION. An action must be"
        "\nspecified. Some options are not applicable to some actions.";
}

///////////////////////////////////////////////////////////////////////////////

bool
SubtitleUtility::actionExport( JobContext& job )
{
    job.fileHandle = MP4Read( job.file.c_str() );
    if( job.fileHandle == MP4_INVALID_FILE_HANDLE )
        return herrf( "unable to open for read: %s\n", job.file.c_str() );

    verbose1f( "NOT IMPLEMENTED\n" );
    return FAILURE;
}

///////////////////////////////////////////////////////////////////////////////

bool
SubtitleUtility::actionImport( JobContext& job )
{
    job.fileHandle = MP4Modify( job.file.c_str() );
    if( job.fileHandle == MP4_INVALID_FILE_HANDLE )
        return herrf( "unable to open for write: %s\n", job.file.c_str() );

    verbose1f( "NOT IMPLEMENTED\n" );
    return FAILURE;
}

///////////////////////////////////////////////////////////////////////////////

bool
SubtitleUtility::actionList( JobContext& job )
{
    job.fileHandle = MP4Read( job.file.c_str() );
    if( job.fileHandle == MP4_INVALID_FILE_HANDLE )
        return herrf( "unable to open for read: %s\n", job.file.c_str() );

    verbose1f( "NOT IMPLEMENTED\n" );
    return FAILURE;
}

///////////////////////////////////////////////////////////////////////////////

bool
SubtitleUtility::actionRemove( JobContext& job )
{
    job.fileHandle = MP4Modify( job.file.c_str() );
    if( job.fileHandle == MP4_INVALID_FILE_HANDLE )
        return herrf( "unable to open for write: %s\n", job.file.c_str() );

    verbose1f( "NOT IMPLEMENTED" );
    return FAILURE;
}

///////////////////////////////////////////////////////////////////////////////

bool
SubtitleUtility::utility_job( JobContext& job )
{
    if( !_action )
        return herrf( "no action specified\n" );

    return (this->*_action)( job );
}

///////////////////////////////////////////////////////////////////////////////

bool
SubtitleUtility::utility_option( int code, bool& handled )
{
    handled = true;

    switch( code ) {
        case LC_LIST:
            _action = &SubtitleUtility::actionList;
            break;

        case LC_EXPORT:
            _action = &SubtitleUtility::actionExport;
            _stTextFile = prog::optarg;
            if( _stTextFile.empty() )
                return herrf( "invalid TXT file: empty-string\n" );
            break;

        case LC_IMPORT:
            _action = &SubtitleUtility::actionImport;
            _stTextFile = prog::optarg;
            if( _stTextFile.empty() )
                return herrf( "invalid TXT file: empty-string\n" );
            break;

        case LC_REMOVE:
            _action = &SubtitleUtility::actionRemove;
            break;

        default:
            handled = false;
            break;
    }

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////

}} // namespace mp4v2::util

///////////////////////////////////////////////////////////////////////////////

extern "C"
int main( int argc, char** argv )
{
    mp4v2::util::SubtitleUtility util( argc, argv );
    return util.process();
}
