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
//  Portions created by David Byron are Copyright (C) 2010.
//  All Rights Reserved.
//
//  Contributors:
//      Kona Blend, kona8lend@@gmail.com
//      David Byron, dbyron@dbyron.com
//
///////////////////////////////////////////////////////////////////////////////

#include "util/impl.h"

namespace mp4v2 { namespace util {
    using namespace itmf;

///////////////////////////////////////////////////////////////////////////////

class ArtUtility : public Utility
{
private:
    enum ArtLongCode {
        LC_ART_ANY = _LC_MAX,
        LC_ART_INDEX,
        LC_LIST,
        LC_ADD,
        LC_REMOVE,
        LC_REPLACE,
        LC_EXTRACT,
    };

public:
    ArtUtility( int, char** );

protected:
    // delegates implementation
    bool utility_option( int, bool& );
    bool utility_job( JobContext& );

private:
    struct ArtType {
        string         name;
        string         ext;
        vector<string> cwarns; // compatibility warnings
        string         cerror; // compatibility error
    };

    bool actionList    ( JobContext& );
    bool actionAdd     ( JobContext& );
    bool actionRemove  ( JobContext& );
    bool actionReplace ( JobContext& );
    bool actionExtract ( JobContext& );

    bool extractSingle( JobContext&, const CoverArtBox::Item&, uint32_t );

private:
    Group  _actionGroup;
    Group  _parmGroup;

    bool (ArtUtility::*_action)( JobContext& );

    string   _artImageFile;
    uint32_t _artFilter;
};

///////////////////////////////////////////////////////////////////////////////

ArtUtility::ArtUtility( int argc, char** argv )
    : Utility      ( "mp4art", argc, argv )
    , _actionGroup ( "ACTIONS" )
    , _parmGroup   ( "ACTION PARAMETERS" )
    , _action      ( NULL )
    , _artFilter   ( numeric_limits<uint32_t>::max() )
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

    _parmGroup.add( "art-any",   false, LC_ART_ANY,   "act on all covr-boxes (default)" );
    _parmGroup.add( "art-index", true,  LC_ART_INDEX, "act on covr-box index IDX", "IDX" );
    _groups.push_back( &_parmGroup );

    _actionGroup.add( "list",    false, LC_LIST,    "list all covr-boxes" );
    _actionGroup.add( "add",     true,  LC_ADD,     "add covr-box from IMG file", "IMG" );
    _actionGroup.add( "replace", true,  LC_REPLACE, "replace covr-box with IMG file", "IMG" );
    _actionGroup.add( "remove",  false, LC_REMOVE,  "remove covr-box" );
    _actionGroup.add( "extract", false, LC_EXTRACT, "extract covr-box" );
    _groups.push_back( &_actionGroup );

    _usage = "[OPTION]... ACTION file...";
    _description =
        // 79-cols, inclusive, max desired width
        // |----------------------------------------------------------------------------|
        "\nFor each mp4 (m4a) file specified, perform the specified ACTION. An action"
        "\nmust be specified. Some options are not applicable for some actions.";
}

///////////////////////////////////////////////////////////////////////////////

bool
ArtUtility::actionAdd( JobContext& job )
{
    File in( _artImageFile, File::MODE_READ );
    if( in.open() )
        return herrf( "unable to open %s for read: %s\n", _artImageFile.c_str(), sys::getLastErrorStr() );

    const uint32_t max = numeric_limits<uint32_t>::max();
    if( in.size > max )
        return herrf( "file too large: %s (exceeds %u bytes)\n", _artImageFile.c_str(), max );

    CoverArtBox::Item item;
    item.size     = static_cast<uint32_t>( in.size );
    item.buffer   = static_cast<uint8_t*>( malloc( item.size ));
    item.autofree = true;

    File::Size nin;
    if( in.read( item.buffer, item.size, nin ))
        return herrf( "read failed: %s\n", _artImageFile.c_str() );

    in.close();

    verbose1f( "adding %s -> %s\n", _artImageFile.c_str(), job.file.c_str() );
    if( dryrunAbort() )
        return SUCCESS;

    job.fileHandle = MP4Modify( job.file.c_str() );
    if( job.fileHandle == MP4_INVALID_FILE_HANDLE )
        return herrf( "unable to open for write: %s\n", job.file.c_str() );

    if( CoverArtBox::add( job.fileHandle, item ))
        return herrf( "unable to add covr-box\n" );

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////

bool
ArtUtility::actionExtract( JobContext& job )
{
    job.fileHandle = MP4Read( job.file.c_str() );
    if( job.fileHandle == MP4_INVALID_FILE_HANDLE )
        return herrf( "unable to open for read: %s\n", job.file.c_str() );

    // single-mode
    if( _artFilter != numeric_limits<uint32_t>::max() ) {
        CoverArtBox::Item item;
        if( CoverArtBox::get( job.fileHandle, item, _artFilter ))
            return herrf( "unable to retrieve covr-box (index=%d): %s\n", _artFilter, job.file.c_str() );

        return extractSingle( job, item, _artFilter );
    }

    // wildcard-mode
    CoverArtBox::ItemList items;
    if( CoverArtBox::list( job.fileHandle, items ))
        return herrf( "unable to fetch list of covr-box: %s\n", job.file.c_str() );

    bool onesuccess = false;
    const CoverArtBox::ItemList::size_type max = items.size();
    for( CoverArtBox::ItemList::size_type i = 0; i < max; i++ ) {
        bool rv = extractSingle( job, items[i], (uint32_t)i );
        if( !rv )
            onesuccess = true;
        if( !_keepgoing && rv )
            return FAILURE;
    }

    return _keepgoing ? onesuccess : SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////

bool
ArtUtility::actionList( JobContext& job )
{
    ostringstream report;

    const int widx = 3;
    const int wsize = 8;
    const int wtype = 9;
    const string sep = "  ";

    if( _jobCount == 0 ) {
        report << setw(widx) << right << "IDX" << left
               << sep << setw(wsize) << right << "BYTES" << left
               << sep << setw(8) << "CRC32"
               << sep << setw(wtype) << "TYPE"
               << sep << setw(0) << "FILE"
               << '\n';

        report << setfill('-') << setw(70) << "" << setfill(' ') << '\n';
    }

    job.fileHandle = MP4Read( job.file.c_str() );
    if( job.fileHandle == MP4_INVALID_FILE_HANDLE )
        return herrf( "unable to open for read: %s\n", job.file.c_str() );

    CoverArtBox::ItemList items;
    if( CoverArtBox::list( job.fileHandle, items ))
        return herrf( "unable to get list of covr-box: %s\n", job.file.c_str() );

    int line = 0;
    const CoverArtBox::ItemList::size_type max = items.size();
    for( CoverArtBox::ItemList::size_type i = 0; i < max; i++ ) {
        if( _artFilter != numeric_limits<uint32_t>::max() && _artFilter != i )
            continue;

        CoverArtBox::Item& item = items[i];
        const uint32_t crc = crc32( item.buffer, item.size );

        report << setw(widx) << right << i
               << sep << setw(wsize) << item.size
               << sep << setw(8) << setfill('0') << hex << crc << setfill(' ') << dec
               << sep << setw(wtype) << left << enumBasicType.toString( item.type );

        if( line++ == 0 )
            report << sep << setw(0) << job.file;

        report << '\n';
    }

    verbose1f( "%s", report.str().c_str() );
    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////

bool
ArtUtility::actionRemove( JobContext& job )
{
    job.fileHandle = MP4Modify( job.file.c_str() );
    if( job.fileHandle == MP4_INVALID_FILE_HANDLE )
        return herrf( "unable to open for write: %s\n", job.file.c_str() );

    if( _artFilter == numeric_limits<uint32_t>::max() )
        verbose1f( "removing covr-box (all) from %s\n", job.file.c_str() );
    else
        verbose1f( "removing covr-box (index=%d) from %s\n", _artFilter, job.file.c_str() );

    if( dryrunAbort() )
        return SUCCESS;

    if( CoverArtBox::remove( job.fileHandle, _artFilter ))
        return herrf( "remove failed\n" );

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////

bool
ArtUtility::actionReplace( JobContext& job )
{
    File in( _artImageFile, File::MODE_READ );
    if( in.open() )
        return herrf( "unable to open %s for read: %s\n", _artImageFile.c_str(), sys::getLastErrorStr() );

    const uint32_t max = numeric_limits<uint32_t>::max();
    if( in.size > max )
        return herrf( "file too large: %s (exceeds %u bytes)\n", _artImageFile.c_str(), max );

    CoverArtBox::Item item;
    item.size     = static_cast<uint32_t>( in.size );
    item.buffer   = static_cast<uint8_t*>( malloc( item.size ));
    item.autofree = true;

    File::Size nin;
    if( in.read( item.buffer, item.size, nin ))
        return herrf( "read failed: %s\n", _artImageFile.c_str() );

    in.close();

    if( _artFilter == numeric_limits<uint32_t>::max() )
        verbose1f( "replacing %s -> %s (all)\n", _artImageFile.c_str(), job.file.c_str() );
    else
        verbose1f( "replacing %s -> %s (index=%d)\n", _artImageFile.c_str(), job.file.c_str(), _artFilter );

    if( dryrunAbort() )
        return SUCCESS;

    job.fileHandle = MP4Modify( job.file.c_str() );
    if( job.fileHandle == MP4_INVALID_FILE_HANDLE )
        return herrf( "unable to open for write: %s\n", job.file.c_str() );

    if( CoverArtBox::set( job.fileHandle, item, _artFilter ))
        return herrf( "unable to add covr-box: %s\n", job.file.c_str() );

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////

bool
ArtUtility::extractSingle( JobContext& job, const CoverArtBox::Item& item, uint32_t index )
{
    // compute out filename
    string out_name = job.file;
    FileSystem::pathnameStripExtension( out_name );

    ostringstream oss;
    oss << out_name << ".art[" << index << ']';

    // if implicit we try to determine type by inspecting data
    BasicType bt = item.type;
    if( bt == BT_IMPLICIT )
        bt = computeBasicType( item.buffer, item.size );

    // add file extension appropriate for known covr-box types
    switch( bt ) {
        case BT_GIF:    oss << ".gif"; break;
        case BT_JPEG:   oss << ".jpg"; break;
        case BT_PNG:    oss << ".png"; break;
        case BT_BMP:    oss << ".bmp"; break;

        default:
            oss << ".dat";
            break;
    }

    out_name = oss.str();
    verbose1f( "extracting %s (index=%d) -> %s\n", job.file.c_str(), index, out_name.c_str() );
    if( dryrunAbort() )
        return SUCCESS;

    File out( out_name, File::MODE_CREATE );
    if( openFileForWriting( out ))
        return FAILURE;

    File::Size nout;
    if( out.write( item.buffer, item.size, nout ))
        return herrf( "write failed: %s\n", out_name.c_str() );

    out.close();
    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////

bool
ArtUtility::utility_job( JobContext& job )
{
    if( !_action )
        return herrf( "no action specified\n" );

    return (this->*_action)( job );
}

///////////////////////////////////////////////////////////////////////////////

bool
ArtUtility::utility_option( int code, bool& handled )
{
    handled = true;

    switch( code ) {
        case LC_ART_ANY:
            _artFilter = numeric_limits<uint32_t>::max();
            break;

        case LC_ART_INDEX:
        {
            istringstream iss( prog::optarg );
            iss >> _artFilter;
            if( iss.rdstate() != ios::eofbit )
                return herrf( "invalid cover-art index: %s\n", prog::optarg );
            break;
        }

        case LC_LIST:
            _action = &ArtUtility::actionList;
            break;

        case LC_ADD:
            _action = &ArtUtility::actionAdd;
            _artImageFile = prog::optarg;
            if( _artImageFile.empty() )
                return herrf( "invalid image file: empty-string\n" );
            break;

        case LC_REMOVE:
            _action = &ArtUtility::actionRemove;
            break;

        case LC_REPLACE:
            _action = &ArtUtility::actionReplace;
            _artImageFile = prog::optarg;
            if( _artImageFile.empty() )
                return herrf( "invalid image file: empty-string\n" );
            break;

        case LC_EXTRACT:
            _action = &ArtUtility::actionExtract;
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
    mp4v2::util::ArtUtility util( argc, argv );
    return util.process();
}
