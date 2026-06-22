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

#include "src/impl.h"

namespace mp4v2 { namespace impl {

///////////////////////////////////////////////////////////////////////////////

MP4RootAtom::MP4RootAtom(MP4File &file)
    : MP4Atom( file, NULL )
    , m_rewrite_ftyp         ( NULL )
    , m_rewrite_ftypPosition ( 0 )
    , m_rewrite_free         ( NULL )
    , m_rewrite_freePosition ( 0 )
    , m_ui64PacketSize ( 0 )
    , m_ui32MulMdatIndex ( 0 )
{
    ExpectChildAtom( "moov", Required, OnlyOne );
    ExpectChildAtom( "ftyp", Optional, OnlyOne );
    ExpectChildAtom( "mdat", Optional, Many );
    ExpectChildAtom( "free", Optional, Many );
    ExpectChildAtom( "skip", Optional, Many );
    ExpectChildAtom( "udta", Optional, Many );
    ExpectChildAtom( "moof", Optional, Many );
}

void MP4RootAtom::BeginWrite(bool use64)
{
    m_rewrite_ftyp = (MP4FtypAtom*)FindChildAtom( "ftyp" );
    if( m_rewrite_ftyp ) {
        m_rewrite_free = (MP4FreeAtom*)MP4Atom::CreateAtom( m_File, NULL, "free" );
        m_rewrite_free->SetSize( 32*4 ); // room for 32 additional brands
        AddChildAtom( m_rewrite_free );

        m_rewrite_ftypPosition = m_File.GetPosition();
        m_rewrite_ftyp->Write();

        m_rewrite_freePosition = m_File.GetPosition();
        m_rewrite_free->Write();
    }
	
    if(m_File.IsMulMdatMode())
	{//do nothing
    }
	else
	{
        m_pChildAtoms[GetLastMdatIndex()]->BeginWrite( m_File.Use64Bits( "mdat" ));
    }
}

void MP4RootAtom::Write()
{
    // no-op
}

void MP4RootAtom::FinishWrite(bool use64)
{
	log.infof("MP4RootAtom::FinishWrite start.\n");
	if(m_File.GetRealTimeMode() > MP4_NORMAL)
	{
		// finish writing last mdat atom
	    const uint32_t mdatIndex = GetLastMdatIndex();

		if(m_File.GetRealTimeMode() >= MP4_ADD_INFO)
		{//do nothing
		}
		else
		{
	    	m_pChildAtoms[mdatIndex]->FinishWrite( m_File.Use64Bits( "mdat" ));
		}

#if 1

		if( NULL != m_File.m_RealtimeStreamFun )
		{
			uint8_t* pui8Data = NULL;
			uint64_t pui64DataSize= 0;
			m_File.GetRealTimeData(&pui8Data, &pui64DataSize);
			if( MP4_RT_MOOV != m_File.GetRealTimeMode() )
			{
				if( (NULL != pui8Data) && (pui64DataSize > 0) )
				{
					m_File.m_RealtimeStreamFun((void*)(&m_File), 0, pui8Data, pui64DataSize);
				}
			}
		}
		else
		{
			uint8_t* pTmp = NULL;
			m_File.GetRealTimeData(&(m_File.m_mdatBuf), &(m_File.m_mdatBufSize));
			pTmp = (uint8_t*)malloc(m_File.m_mdatBufSize);
			if(NULL == pTmp)
			{
	        	throw new Exception("malloc memery for pTmp failed!\n", __FILE__, __LINE__, __FUNCTION__ );
			}
			MP4File::m_ui32MallocCount++;

			memcpy(pTmp, m_File.m_mdatBuf, m_File.m_mdatBufSize);
			m_File.m_mdatBuf = pTmp;
		}
		
		if(m_File.m_VirtualFramePos > 0)
		{//do nothing
		}
		else
		{
			m_File.m_MoovPos = m_File.GetTailPositonOfBuf();
		}
#else
		m_File.GetRealTimeData(&(m_File.m_mdatBuf), &(m_File.m_mdatBufSize));
#endif

		log.infof("MP4RootAtom::FinishWrite Last data size is %" PRIu64 ".\n", m_File.m_mdatBufSize);

		if(m_File.GetRealTimeMode() == MP4_RT_MOOV)
		{//切换状态
			m_File.SetRealTimeMode(MP4_RT);
		}

	    // write all atoms after last mdat
	    const uint32_t size = m_pChildAtoms.Size();
	    for ( uint32_t i = mdatIndex + 1; i < size; i++ )
		{
	        m_pChildAtoms[i]->Write();
	    }
	}
	else
	{
	    if( m_rewrite_ftyp ) 
		{
	        const uint64_t savepos = m_File.GetPosition();
	        m_File.SetPosition( m_rewrite_ftypPosition );
	        m_rewrite_ftyp->Write();

	        const uint64_t newpos = m_File.GetPosition();
	        if( newpos > m_rewrite_freePosition )
			{
	            m_rewrite_free->SetSize( m_rewrite_free->GetSize() - (newpos - m_rewrite_freePosition) ); // shrink
	        }
	        else if( newpos < m_rewrite_freePosition )
	        {
	            m_rewrite_free->SetSize( m_rewrite_free->GetSize() + (m_rewrite_freePosition - newpos) ); // grow
	        }

	        m_rewrite_free->Write();
	        m_File.SetPosition( savepos );
	    }

	    // finish writing last mdat atom
	    const uint32_t mdatIndex = GetLastMdatIndex();
	    m_pChildAtoms[mdatIndex]->FinishWrite( m_File.Use64Bits( "mdat" ));

	    // write all atoms after last mdat
	    const uint32_t size = m_pChildAtoms.Size();
	    for ( uint32_t i = mdatIndex + 1; i < size; i++ )
	    {
	        m_pChildAtoms[i]->Write();
	    }
	}
	log.infof("MP4RootAtom::FinishWrite end.\n");
}

void MP4RootAtom::BeginOptimalWrite()
{
    WriteAtomType("ftyp", OnlyOne);
    WriteAtomType("moov", OnlyOne);
    WriteAtomType("udta", Many);

    m_pChildAtoms[GetLastMdatIndex()]->BeginWrite(m_File.Use64Bits("mdat"));
}

void MP4RootAtom::FinishOptimalWrite()
{
    // finish writing mdat
    m_pChildAtoms[GetLastMdatIndex()]->FinishWrite(m_File.Use64Bits("mdat"));

    // find moov atom
    uint32_t size = m_pChildAtoms.Size();
    MP4Atom* pMoovAtom = NULL;

    uint32_t i;
    for (i = 0; i < size; i++) {
        if (!strcmp("moov", m_pChildAtoms[i]->GetType())) {
            pMoovAtom = m_pChildAtoms[i];
            break;
        }
    }
    ASSERT(i < size);
    ASSERT(pMoovAtom != NULL);

    // rewrite moov so that updated chunkOffsets are written to disk
    m_File.SetPosition(pMoovAtom->GetStart());
    uint64_t oldSize = pMoovAtom->GetSize();

    pMoovAtom->Write();

    // sanity check
    uint64_t newSize = pMoovAtom->GetSize();
    ASSERT(oldSize == newSize);
}

uint32_t MP4RootAtom::GetLastMdatIndex()
{
    for (int32_t i = m_pChildAtoms.Size() - 1; i >= 0; i--) {
        if (!strcmp("mdat", m_pChildAtoms[i]->GetType())) {
            return i;
        }
    }
    ASSERT(false);
    return (uint32_t)-1;
}

void MP4RootAtom::WriteAtomType(const char* type, bool onlyOne)
{
    uint32_t size = m_pChildAtoms.Size();

    for (uint32_t i = 0; i < size; i++) {
        if (!strcmp(type, m_pChildAtoms[i]->GetType())) {
            m_pChildAtoms[i]->Write();
            if (onlyOne) {
                break;
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////

uint32_t MP4RootAtom::GetMulMdatIndex( void )
{
	return m_ui32MulMdatIndex;
}

bool MP4RootAtom::StartNewPacket( uint64_t mdatSize )
{//开始mdat
    MP4Atom* pChildAtom = NULL;
	uint32_t ui32Index = 0;

#if 0
	m_ui64PacketSize = mdatSize;
    pChildAtom = CreateAtom(m_File, this, "mdat");
	pChildAtom->SetMulMdatCurSize(m_ui64PacketSize);
#else
    pChildAtom = CreateAtom(m_File, this, "mdat");
#endif

	//ui32Index = GetMulMdatIndex();
	ui32Index = GetLastMdatIndex();

    ASSERT(pChildAtom);	
    this->InsertChildAtom(pChildAtom, (ui32Index+1));
    m_pChildAtoms[ui32Index]->BeginWrite( m_File.Use64Bits( "mdat" ));
    return true;
}

bool MP4RootAtom::EndOldPacket( void )
{//结束mdat
    MP4MdatAtom *pMdatAtom = NULL;
	uint32_t ui32Index = 0;
	
    ui32Index = GetLastMdatIndex();
    pMdatAtom =  (MP4MdatAtom *)m_pChildAtoms[ui32Index];	
    pMdatAtom->FinishWrite( m_File.Use64Bits( "mdat" ));

    m_ui32MulMdatIndex = ui32Index;
	return true;
}


void MP4RootAtom::FirstWriteMdat(bool use64)
{
    MP4MdatAtom *pMdatAtom = NULL;
	
	pMdatAtom = (MP4MdatAtom *)m_pChildAtoms[GetLastMdatIndex()];	
	pMdatAtom->SetMulMdatCurSize(m_File.GetMdatSize());
    m_pChildAtoms[GetLastMdatIndex()]->BeginWrite( m_File.Use64Bits( "mdat" ));
}

///////////////////////////////////////////////////////////////////////////////

}} // namespace mp4v2::impl
