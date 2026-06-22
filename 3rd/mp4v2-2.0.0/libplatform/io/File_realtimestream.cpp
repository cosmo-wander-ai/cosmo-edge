#include "libplatform/impl.h"

namespace mp4v2 { namespace platform { namespace io {
	
//////////////////////////////////////////////////////////////////////////

class CRTBuffer
{
public:
	CRTBuffer();
	CRTBuffer(char*, int);
	CRTBuffer(CRTBuffer &);
	~CRTBuffer();
	CRTBuffer& operator=(CRTBuffer &);
	CRTBuffer& operator+=(CRTBuffer &);
	CRTBuffer& operator^(CRTBuffer &);
	bool UpdateInsert(CRTBuffer &,uint64_t);
	void Expand(uint64_t);
	void SetUseSize(uint64_t);

public:
	char* m_pcBuf;
	uint64_t m_ui64Size;
	uint64_t m_ui64UseSize;
protected:
private:
};

CRTBuffer::CRTBuffer()
{
#if 0
	m_pcBuf = NULL;
	m_ui64Size = 0;
	m_ui64UseSize = 0;
#else
	uint64_t uiExpandSize = 1024*1024;
	m_pcBuf = NULL;
	m_ui64Size = 0;
	m_ui64UseSize = 0;

	m_pcBuf = new char[uiExpandSize];
	m_ui64UseSize = uiExpandSize;
#endif
}

CRTBuffer::CRTBuffer(char* _pcBuf, int _iBufLen)
{
	m_pcBuf = NULL;
	m_ui64Size = 0;
	m_ui64UseSize = 0;

	m_pcBuf = new char[_iBufLen];
	memcpy(m_pcBuf, _pcBuf, _iBufLen);
	m_ui64Size = _iBufLen;
	m_ui64UseSize = _iBufLen;
}

CRTBuffer::CRTBuffer(CRTBuffer &Obj)
{
	m_pcBuf = NULL;
	m_ui64Size = 0;
	m_ui64UseSize = 0;
	
	m_pcBuf = new char[m_ui64Size];
	memcpy(m_pcBuf, Obj.m_pcBuf, Obj.m_ui64Size);
	m_ui64Size = Obj.m_ui64Size;
	m_ui64UseSize = Obj.m_ui64UseSize;
}

CRTBuffer::~CRTBuffer()
{
	if (NULL != m_pcBuf)
	{
		delete[]m_pcBuf;
	}
	
	m_pcBuf = NULL;
	m_ui64Size = 0;
	m_ui64UseSize = 0;
}

CRTBuffer& CRTBuffer::operator=(CRTBuffer &Obj)
{
	m_pcBuf = NULL;
	m_ui64Size = 0;
	m_ui64UseSize = 0;

	delete []m_pcBuf;
	m_pcBuf = new char[m_ui64Size];
	memcpy(m_pcBuf, Obj.m_pcBuf, Obj.m_ui64Size);
	m_ui64Size = Obj.m_ui64Size;
	m_ui64UseSize = Obj.m_ui64UseSize;
	return *this;
}

CRTBuffer& CRTBuffer::operator+=(CRTBuffer &Obj)
{
	if ( (m_ui64Size - m_ui64UseSize) < Obj.m_ui64UseSize )
	{
		char* pcTmpBuf = NULL;
		// uint64_t uiSize = 0;
		uint64_t uiUseSize = 0;

		// uiSize = m_ui64Size +  Obj.m_ui64Size;
		uiUseSize = m_ui64UseSize + Obj.m_ui64UseSize;

		pcTmpBuf = new char[uiUseSize];
		memcpy(pcTmpBuf, m_pcBuf, m_ui64UseSize);
		memcpy(pcTmpBuf+m_ui64UseSize, Obj.m_pcBuf, Obj.m_ui64UseSize);
		delete []m_pcBuf;
		m_pcBuf = NULL;

		m_pcBuf = pcTmpBuf;
		m_ui64Size = uiUseSize;
		m_ui64UseSize = uiUseSize;
	}
	else
	{
		memcpy(m_pcBuf+m_ui64UseSize, Obj.m_pcBuf, Obj.m_ui64UseSize);
		m_ui64Size += Obj.m_ui64UseSize;
		m_ui64UseSize += Obj.m_ui64UseSize;
	}
	
	return *this;
}

bool CRTBuffer::UpdateInsert(CRTBuffer &Obj, uint64_t _ui64InsertPos)
{
	if (0 > _ui64InsertPos)
	{
		return true;
	}
	memcpy((void*)(m_pcBuf+_ui64InsertPos), Obj.m_pcBuf, (size_t)Obj.m_ui64UseSize);

	return false;
}

void CRTBuffer::Expand(uint64_t size)
{
#if 0
	char* pcBuf = NULL;
	uint64_t uiTotalSize = 0;

	uiTotalSize = m_ui64Size + size;
	
	pcBuf = new char[uiTotalSize];
	if(NULL != m_pcBuf){
		memcpy((void*)pcBuf, m_pcBuf, m_ui64UseSize);
		delete []m_pcBuf;
		m_pcBuf = NULL;
	}

	m_pcBuf = pcBuf;
	m_ui64Size += size;
#else

	char* pcBuf = NULL;
	uint64_t uiExpandSize = 1024*1024;

	if(size > uiExpandSize)
	{
		uiExpandSize = size;
	}
	
	pcBuf = new char[uiExpandSize + m_ui64Size];
	if(NULL != m_pcBuf)
	{
		memcpy((void*)pcBuf, m_pcBuf, m_ui64UseSize);
		delete []m_pcBuf;
		m_pcBuf = NULL;
	}

	m_pcBuf = pcBuf;
	m_ui64Size += uiExpandSize;

#endif
}

void CRTBuffer::SetUseSize(uint64_t size)
{
	m_ui64UseSize = size;
}
///////////////////////////////////////////////////////////////////////////////

class StandardRealtimestreamProvider : public FileProvider
{
public:
	StandardRealtimestreamProvider();
	~StandardRealtimestreamProvider();

	bool open( std::string name, Mode mode );
	bool seek( Size pos );
	bool read( void* buffer, Size size, Size& nin, Size maxChunkSize );
	bool write( const void* buffer, Size size, Size& nout, Size maxChunkSize );
	bool close();
	
private:
	bool		 _seekg;
	bool		 _seekp;
	std::fstream _fstream;

//////////
public:
	void SetRealTimeMode(int32_t _iMode);
	bool GetRealTimeData(uint8_t** pui8Data, uint64_t* _pui64DataSize);
	int32_t GetRealTimeMode( void );
	uint64_t GetPositonOfBuf( void );
	uint64_t GetTailPositonOfBuf( void );
	uint64_t GetLastAllBufNonius( void );
	void RecordAllBufNonius( void );
	
private:
	int32_t m_iMode;
	bool m_EndFinishFlg;
	bool m_bSeekFlg;
	int64_t m_i64WriteSeekPos;
	CRTBuffer* m_pRTData;
	uint64_t m_ui64Total;
	uint64_t m_uiNonius;
	uint64_t m_ui64AllBufSize;
	uint64_t m_ui64AllBufNonius;
};

///////////////////////////////////////////////////////////////////////////////

#define _A_	1

StandardRealtimestreamProvider::StandardRealtimestreamProvider()
	: _seekg ( false )
	, _seekp ( false )
{
	m_iMode = 0;
	m_bSeekFlg = false;
	m_i64WriteSeekPos = -1;
	
#if (1 == _A_)
	m_pRTData = new CRTBuffer;
#else
	m_pRTData = NULL;
#endif

	m_EndFinishFlg = false;
	m_ui64Total = 0;
	m_uiNonius = 0;
	m_ui64AllBufSize = 0;
	m_ui64AllBufNonius = 0;
}

StandardRealtimestreamProvider::~StandardRealtimestreamProvider()
{
	if(NULL != m_pRTData)
	{
		delete m_pRTData;
		m_pRTData = NULL;
	}
}

bool StandardRealtimestreamProvider::GetRealTimeData( uint8_t** pui8Data, uint64_t* _pui64DataSize)
{
	if(MP4_RT_MOOV <= m_iMode)
	{		
		if ((NULL == pui8Data) || (NULL == _pui64DataSize))
		{
			return true;
		}
		
		m_ui64AllBufSize += m_ui64Total;
#if (1 == _A_)	
		m_pRTData->m_ui64UseSize = 0;	
#else
		delete m_pRTData;
		m_pRTData = NULL;
#endif
		m_ui64Total = 0;
		m_i64WriteSeekPos = -1;
		m_bSeekFlg = false;
		m_uiNonius = 0;
		
		return false;
		
	}
	else if(MP4_RT <= m_iMode)
	{
		if ((NULL == pui8Data) || (NULL == _pui64DataSize))
		{
			return true;
		}
		
#if (1 == _A_)
		if (0 == m_pRTData->m_ui64UseSize)
		{
			return true;
		}
		*pui8Data = (uint8_t*)m_pRTData->m_pcBuf;
		*_pui64DataSize = m_pRTData->m_ui64UseSize;
#else
		if (NULL == m_pRTData)
		{
			return true;
		}
		*pui8Data = (uint8_t*)malloc(m_pRTData->m_ui64UseSize);
		memcpy(*pui8Data, m_pRTData->m_pcBuf, m_pRTData->m_ui64UseSize);
		*_pui64DataSize = m_pRTData->m_ui64UseSize; 
#endif
		
		m_ui64AllBufSize += m_ui64Total;
		
#if (1 == _A_)
		m_pRTData->m_ui64UseSize = 0;
#else
		delete m_pRTData;
		m_pRTData = NULL;
#endif
		m_ui64Total = 0;
		m_i64WriteSeekPos = -1;
		m_bSeekFlg = false;
		m_uiNonius = 0;
		
		return false;
	}
	else
	{
		return true;
	}
}


void StandardRealtimestreamProvider::SetRealTimeMode(int32_t _iMode)
{
	m_iMode = _iMode;
}

int32_t StandardRealtimestreamProvider::GetRealTimeMode()
{
	return m_iMode;
}

uint64_t StandardRealtimestreamProvider::GetPositonOfBuf( void )
{
	return m_ui64Total;
}

uint64_t StandardRealtimestreamProvider::GetTailPositonOfBuf( void )
{
	return m_ui64AllBufSize + m_ui64Total;
}

uint64_t 
StandardRealtimestreamProvider::GetLastAllBufNonius( void )
{
	return m_ui64AllBufNonius;
}

void 
StandardRealtimestreamProvider::RecordAllBufNonius( void )
{
	m_ui64AllBufNonius = m_ui64AllBufSize + m_ui64Total;
	m_ui64AllBufNonius -= 8;
}

bool
StandardRealtimestreamProvider::open( std::string name, Mode mode )
{
	return false;
}

bool
StandardRealtimestreamProvider::seek( Size pos )
{
	m_bSeekFlg = true;
	m_i64WriteSeekPos = pos;
	m_uiNonius = pos;
	return false;
}

bool
StandardRealtimestreamProvider::read( void* buffer, Size size, Size& nin, Size maxChunkSize )
{
	return false;
}

bool
StandardRealtimestreamProvider::write( const void* buffer, Size size, Size& nout, Size maxChunkSize )
{
	if(MP4_RT_MOOV <= m_iMode)
	{
		m_uiNonius += size;
		if(m_uiNonius>m_ui64Total)
		{
			m_ui64Total = m_uiNonius;
		}
		nout = size;
		return false;
	}
	else if(MP4_RT <= m_iMode)
	{
		CRTBuffer tmpObj((char*)buffer, size);
#if (1 == _A_)
#else
		if (NULL == m_pRTData)
		{
			m_pRTData = new CRTBuffer;
		}
#endif
		//if(m_uiNonius+size > m_ui64Total)
		if(m_uiNonius+size > m_pRTData->m_ui64Size)
		{
			m_pRTData->Expand(m_uiNonius+size-m_ui64Total);
		}
		
		m_pRTData->UpdateInsert(tmpObj, m_uiNonius);
		m_uiNonius += size;
		if(m_uiNonius>m_ui64Total)
		{
			m_ui64Total = m_uiNonius;
		}
		m_pRTData->SetUseSize(m_ui64Total);
		nout = size;
		return false;
	}
	return false;
}

bool
StandardRealtimestreamProvider::close()
{
	return false;
}

///////////////////////////////////////////////////////////////////////////////

FileProvider&
FileProvider::standardRealtimeStream()
{
	return *new StandardRealtimestreamProvider();
}

///////////////////////////////////////////////////////////////////////////////

}}} // namespace mp4v2::platform::io

