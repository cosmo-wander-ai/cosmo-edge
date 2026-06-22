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

namespace mp4v2 {
namespace impl {

///////////////////////////////////////////////////////////////////////////////

MP4MdatAtom::MP4MdatAtom(MP4File &file)
        : MP4Atom(file, "mdat")
{
	m_ui64CurSize = 0;
}

void MP4MdatAtom::Read()
{
    Skip();
}

void MP4MdatAtom::Write()
{
    // should never get here
    ASSERT(false);
}

///////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////

void MP4MdatAtom::SetMulMdatCurSize( uint64_t mdatSize)
{
    m_ui64CurSize = mdatSize;
}

void MP4MdatAtom::BeginWrite(bool use64)
{
    if(m_File.IsMulMdatMode())
	{
#if 0
        //m_start = m_File.GetPosition();
        //use64 = m_File.Use64Bits();
        if (use64) 
		{
            m_File.WriteUInt32(m_ui64CurSize);
        } 
		else 
		{
            m_File.WriteUInt32(m_ui64CurSize);
        }
		
        m_File.WriteBytes((uint8_t*)&m_type[0], 4);
		
        if (use64) 
		{
            m_File.WriteUInt64(m_ui64CurSize);
        }
		
        if (ATOMID(m_type) == ATOMID("uuid")) 
		{
            m_File.WriteBytes(m_extendedType, sizeof(m_extendedType));
        }
#else
        //m_start = m_File.GetPosition();
        //use64 = m_File.Use64Bits();
        if (use64) 
		{
            m_File.WriteUInt32(1);
        }
		else 
        {
            m_File.WriteUInt32(m_File.GetMdatSize());
        }
		
        m_File.WriteBytes((uint8_t*)&m_type[0], 4);
		
        if (use64) 
		{
            m_File.WriteUInt64(m_File.GetMdatSize());
        }
        if (ATOMID(m_type) == ATOMID("uuid")) 
		{
            m_File.WriteBytes(m_extendedType, sizeof(m_extendedType));
        }
#endif
	}
	else
	{
        m_start = m_File.GetPosition();
        //use64 = m_File.Use64Bits();
        if (use64) 
		{
            m_File.WriteUInt32(1);
        } 
		else 
        {
            m_File.WriteUInt32(0);
        }
		
        m_File.WriteBytes((uint8_t*)&m_type[0], 4);
		
        if (use64) 
		{
            m_File.WriteUInt64(0);
        }

		if (ATOMID(m_type) == ATOMID("uuid")) 
		{
            m_File.WriteBytes(m_extendedType, sizeof(m_extendedType));
        }
    }
}

void MP4MdatAtom::FinishWrite(bool use64)
{
    if(m_File.IsMulMdatMode()){
        //m_end = m_File.GetPosition();
        //m_size = (m_end - m_start);
        uint64_t ui64AllSize = 0;
        uint64_t ui64FillSize = 0;
        uint64_t ui64Tmp = 0;
        const uint64_t ui64NewSize = 1024*1024;
        uint8_t *pcTmp = NULL;
		(void)pcTmp;

#if 1
		ui64AllSize = m_File.GetActualMdatSize();
		#else
		ui64AllSize = m_File.GetTailPositonOfBuf() -  m_File.GetLastAllBufNonius();
		#endif

        log.verbose1f("end: type %s %" PRIu64 " %" PRIu64 " size %" PRIu64,
						   m_type,m_start, m_end, m_size);

#if 0
        ui64FillSize = m_ui64CurSize - ui64AllSize;

		if(m_ui64CurSize < ui64AllSize){
			char cBuf[512] = {0};
			sprintf(cBuf, "error: Actual mdat size is error.-> ui64FillSize[%llu], m_ui64CurSize[%llu], ui64AllSize[%llu].\n", ui64FillSize, m_ui64CurSize, ui64AllSize);
            throw new Exception(cBuf, __FILE__, __LINE__, __FUNCTION__);
		}
#else
        ui64FillSize = m_File.GetMdatSize() - ui64AllSize;

		if(m_File.GetMdatSize() < ui64AllSize)
		{
			char cBuf[512] = {0};
			sprintf(cBuf, "error: Actual mdat size is error.-> ui64FillSize[%" PRIu64 "], m_ui64CurSize[%" PRIu64 "], ui64AllSize[%" PRIu64 "].\n",
					ui64FillSize, m_File.GetMdatSize(), ui64AllSize);
			throw new Exception(cBuf, __FILE__, __LINE__, __FUNCTION__);
		}
#endif

		if(m_File.m_bAddFlag)
		{
			m_File.m_bAddFlag = false;
		}
		else
		{		
		    ui64Tmp = ui64FillSize;
			#if 0
		    pcTmp = new uint8_t[ui64NewSize];
		    memset(pcTmp, 0, ui64NewSize);
			#endif

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

		    do
			{
		        if(ui64Tmp >= ui64NewSize)
				{
		            m_File.WriteBytes(m_File.m_pFillData, ui64NewSize);
		            ui64Tmp -= ui64NewSize;
		        }
				else
		        {
		            m_File.WriteBytes(m_File.m_pFillData, ui64Tmp);
		            break;
		        }
				
				if( (m_File.m_IsCloseFlag) && (NULL != m_File.m_RealtimeStreamFun) )
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
		    }while(1);
			#if 0
		    if(NULL != pcTmp){
		        delete []pcTmp;
		        pcTmp = NULL;
		    }
			#endif
		}
		
		//log.infof("MP4MdatAtom::FinishWrite m_bAddFlag=%d, ui64FillSize=%llu,ui64AllSize=%llu,mdatsize=%llu.\n"
		//	,m_File.m_bAddFlag, ui64FillSize,ui64AllSize,m_File.GetMdatSize());
		
        //use64 = m_File.Use64Bits();
        //if (use64) {
        //    m_File.SetPosition(m_start + 8);
        //    m_File.WriteUInt64(m_size);
        //} else {
        //    ASSERT(m_size <= (uint64_t)0xFFFFFFFF);
        //    m_File.SetPosition(m_start);
        //    m_File.WriteUInt32(m_size);
        //}
        //m_File.SetPosition(m_end);

		//m_size = m_ui64CurSize;
		
        // adjust size to just reflect data portion of atom
        m_size -= (use64 ? 16 : 8);
        if (ATOMID(m_type) == ATOMID("uuid")) 
		{
            m_size -= sizeof(m_extendedType);
        }
    }
	else
	{
        m_end = m_File.GetPosition();
        m_size = (m_end - m_start);

        log.verbose1f("end: type %s %" PRIu64 " %" PRIu64 " size %" PRIu64,
						   m_type,m_start, m_end, m_size);
        //use64 = m_File.Use64Bits();
        if (use64) 
		{
            m_File.SetPosition(m_start + 8);
            m_File.WriteUInt64(m_size);
        } 
		else 
		{
            ASSERT(m_size <= (uint64_t)0xFFFFFFFF);
            m_File.SetPosition(m_start);
            m_File.WriteUInt32(m_size);
        }
        m_File.SetPosition(m_end);
		
        // adjust size to just reflect data portion of atom
        m_size -= (use64 ? 16 : 8);
        if (ATOMID(m_type) == ATOMID("uuid")) 
		{
            m_size -= sizeof(m_extendedType);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////

}
} // namespace mp4v2::impl
