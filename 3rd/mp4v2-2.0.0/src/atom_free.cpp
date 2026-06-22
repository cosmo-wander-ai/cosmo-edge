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

MP4FreeAtom::MP4FreeAtom( MP4File &file, const char* type )
    : MP4Atom( file, type ? type : "free" )
{
}

void MP4FreeAtom::Read()
{
    Skip();
}

void MP4FreeAtom::Write()
{
	if(m_File.GetRealTimeMode() > MP4_NORMAL)
	{
		bool use64 = (GetSize() > (0xFFFFFFFF - 8));
		BeginWrite(use64);
#if 1
		bool bIsInFlag = true;
		bool bIsInFlag2 = true;
		bool bIsInFlag3 = true;
		bool bIsInFlag4 = true;
		bool bIsInFlag5 = true;

		if(m_File.m_ExeFreeTimes%2 == 0)
		{
			for (uint64_t ix = 0; ix < GetSize(); ix++)
			{
				if(3 == ix)
				{//版本
					m_File.WriteUInt8(1);
				}
				else if(7 == ix)
				{//加密标识
					m_File.WriteUInt8(m_File.m_encryptionFlag);
				}
				else if((8 <= ix) && (ix <= 15))
				{//mdat大小
					if(bIsInFlag)
					{
						m_File.WriteUInt64(m_File.GetMdatSize());
						bIsInFlag = false;
					}
				}
				else
				{
					m_File.WriteUInt8(0);
				}
			}
		}
		else
		{
			for (uint64_t ix = 0; ix < GetSize(); ix++)
			{
				if(3 == ix)
				{//版本
					m_File.WriteUInt8(1);
				}
				else if((4 <= ix) && (ix <= 11))
				{//休庭信息
					if(bIsInFlag)
					{
						m_File.WriteUInt64(m_File.m_AdjournPos);
						bIsInFlag = false;
					}
				}
				else if((12 <= ix) && (ix <= 15))
				{//加密标识
					if(bIsInFlag3)
					{
						m_File.WriteBytes((uint8_t*)(&m_File.m_Encryption), 4);
						bIsInFlag3 = false;
					}
				}
				else if((16 <= ix) && (ix <= 19))
				{//音频类型
					if(bIsInFlag4)
					{
						m_File.WriteBytes((uint8_t*)(&m_File.m_AudioEncode), 4);
						bIsInFlag4 = false;
					}
				}
				else if((20 <= ix) && (ix <= 27))
				{//优化使用
					if(bIsInFlag5)
					{
						m_File.WriteUInt64(m_File.m_NormalVirtualFramePos);
						bIsInFlag5 = false;
					}
				}
				else if((120 <= ix) && (ix <= 127))
				{//优化使用
					if(bIsInFlag2)
					{
						m_File.WriteUInt64(m_File.m_MoovPos);
						bIsInFlag2 = false;
					}
				}
				else{
					m_File.WriteUInt8(0);
				}
			}
		}
		
		m_File.m_ExeFreeTimes++;
		if(10 == m_File.m_ExeFreeTimes)
		{
			m_File.m_ExeFreeTimes = 0;
		}
#else
		m_File.SetPosition(m_File.GetPosition() + GetSize());
#endif
		FinishWrite(use64);
	}
	else
	{
	    bool use64 = (GetSize() > (0xFFFFFFFF - 8));
	    BeginWrite(use64);
#if 1
	    for (uint64_t ix = 0; ix < GetSize(); ix++) 
		{
	        m_File.WriteUInt8(0);
	    }
#else
	    m_File.SetPosition(m_File.GetPosition() + GetSize());
#endif
	    FinishWrite(use64);
	}
}

///////////////////////////////////////////////////////////////////////////////

}
} // namespace mp4v2::impl
