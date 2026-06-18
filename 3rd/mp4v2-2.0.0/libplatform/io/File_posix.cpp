#include "libplatform/impl.h"

namespace mp4v2 { namespace platform { namespace io {

///////////////////////////////////////////////////////////////////////////////

class StandardFileProvider : public FileProvider
{
public:
    StandardFileProvider();

    bool open( std::string name, Mode mode );
    bool seek( Size pos );
    bool read( void* buffer, Size size, Size& nin, Size maxChunkSize );
    bool write( const void* buffer, Size size, Size& nout, Size maxChunkSize );
    bool close();

private:
    bool         _seekg;
    bool         _seekp;
    std::fstream _fstream;
	
	//////////
public:
	void SetRealTimeMode(int32_t _iMode);
	int32_t GetRealTimeMode( void );
	bool GetRealTimeData( uint8_t** pui8Data, uint64_t* _pui64DataSize);
	uint64_t GetPositonOfBuf( void );
	uint64_t GetTailPositonOfBuf( void );
	void RecordAllBufNonius( void );
	uint64_t GetLastAllBufNonius( void );
};

///////////////////////////////////////////////////////////////////////////////

StandardFileProvider::StandardFileProvider()
    : _seekg ( false )
    , _seekp ( false )
{
}

bool
StandardFileProvider::open( std::string name, Mode mode )
{
    ios::openmode om = ios::binary;
    switch( mode ) {
        case MODE_UNDEFINED:
        case MODE_READ:
        default:
            om |= ios::in;
            _seekg = true;
            _seekp = false;
            break;

        case MODE_MODIFY:
            om |= ios::in | ios::out;
            _seekg = true;
            _seekp = true;
            break;

        case MODE_CREATE:
            om |= ios::in | ios::out | ios::trunc;
            _seekg = true;
            _seekp = true;
            printf("=========> delete file \n");
            unlink(name.c_str());
            break;
    }

    _fstream.open( name.c_str(), om );
    return _fstream.fail();
}

bool
StandardFileProvider::seek( Size pos )
{
    if( _seekg )
        _fstream.seekg( pos, ios::beg );
    if( _seekp )
        _fstream.seekp( pos, ios::beg );
    return _fstream.fail();
}

bool
StandardFileProvider::read( void* buffer, Size size, Size& nin, Size maxChunkSize )
{
    _fstream.read( (char*)buffer, size );
    if( _fstream.fail() )
        return true;
    nin = _fstream.gcount();
    return false;
}

bool
StandardFileProvider::write( const void* buffer, Size size, Size& nout, Size maxChunkSize )
{
    _fstream.write( (const char*)buffer, size );
    if( _fstream.fail() )
        return true;
    nout = size;
    return false;
}

bool
StandardFileProvider::close()
{
    _fstream.close();
    return _fstream.fail();
}

///////////////////////////////////////////////////////////////////////////////

FileProvider&
FileProvider::standard()
{
    return *new StandardFileProvider();
}

///////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////

void 
StandardFileProvider::SetRealTimeMode(int32_t _iMode)
{
}

int32_t 
StandardFileProvider::GetRealTimeMode( void )
{
	return MP4_NORMAL;
}

bool 
StandardFileProvider::GetRealTimeData( uint8_t** pui8Data, uint64_t* _pui64DataSize)
{
	return false;
}

uint64_t 
StandardFileProvider::GetPositonOfBuf( void )
{
	return 0;
}

uint64_t 
StandardFileProvider::GetTailPositonOfBuf( void )
{
	return 0;
}

uint64_t 
StandardFileProvider::GetLastAllBufNonius( void )
{
	return 0;
}

void 
StandardFileProvider::RecordAllBufNonius( void )
{
}

///////////////////////////////////////////////////////////////////////////////


}}} // namespace mp4v2::platform::io
