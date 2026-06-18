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
 * Copyright (C) Cisco Systems Inc. 2004.  All Rights Reserved.
 *
 * Contributor(s):
 *      Bill May wmay@cisco.com
 */

#include "src/impl.h"
#include "libavcodec/hevc.h"
#include "libavutil/log.h"

namespace mp4v2 {
namespace impl {

///////////////////////////////////////////////////////////////////////////////

/*
 * SizeTableProperty is a special version of the MP4TableProperty -
 * the BytesProperty will need to set the value before it can read
 * from the file
 */
class SizeTableProperty : public MP4TableProperty
{
public:
    SizeTableProperty(MP4Atom& parentAtom, const char *name, MP4IntegerProperty *pCountProperty) :
            MP4TableProperty(parentAtom, name, pCountProperty) {};
protected:
    void ReadEntry(MP4File& file, uint32_t index) {
        // Each table has a size, followed by the length field
        // first, read the length
        m_pProperties[0]->Read(file, index);
        MP4IntegerProperty *pIntProp = (MP4IntegerProperty *)m_pProperties[0];
        // set the size in the bytes property
        MP4BytesProperty *pBytesProp = (MP4BytesProperty *)m_pProperties[1];
        pBytesProp->SetValueSize(pIntProp->GetValue(index), index);
        // And read the bytes
        m_pProperties[1]->Read(file, index);
    };
private:
    SizeTableProperty();
    SizeTableProperty ( const SizeTableProperty &src );
    SizeTableProperty &operator= ( const SizeTableProperty &src );
};

MP4AvcCAtom::MP4AvcCAtom(MP4File &file)
        : MP4Atom(file, "avcC")
{
    MP4BitfieldProperty *pCount;
    MP4TableProperty *pTable;

    AddProperty( new MP4Integer8Property(*this,"configurationVersion")); /* 0 */

    AddProperty( new MP4Integer8Property(*this,"AVCProfileIndication")); /* 1 */

    AddProperty( new MP4Integer8Property(*this,"profile_compatibility")); /* 2 */

    AddProperty( new MP4Integer8Property(*this,"AVCLevelIndication")); /* 3 */

    AddProperty( new MP4BitfieldProperty(*this,"reserved", 6)); /* 4 */
    AddProperty( new MP4BitfieldProperty(*this,"lengthSizeMinusOne", 2)); /* 5 */
    AddProperty( new MP4BitfieldProperty(*this,"reserved1", 3)); /* 6 */
    pCount = new MP4BitfieldProperty(*this,"numOfSequenceParameterSets", 5);
    AddProperty(pCount); /* 7 */

    pTable = new SizeTableProperty(*this,"sequenceEntries", pCount);
    AddProperty(pTable); /* 8 */
    pTable->AddProperty(new MP4Integer16Property(pTable->GetParentAtom(),"sequenceParameterSetLength"));
    pTable->AddProperty(new MP4BytesProperty(pTable->GetParentAtom(),"sequenceParameterSetNALUnit"));

    MP4Integer8Property *pCount2 = new MP4Integer8Property(*this,"numOfPictureParameterSets");
    AddProperty(pCount2); /* 9 */

    pTable = new SizeTableProperty(*this,"pictureEntries", pCount2);
    AddProperty(pTable); /* 10 */
    pTable->AddProperty(new MP4Integer16Property(pTable->GetParentAtom(),"pictureParameterSetLength"));
    pTable->AddProperty(new MP4BytesProperty(pTable->GetParentAtom(),"pictureParameterSetNALUnit"));
}

void MP4AvcCAtom::Generate()
{
    MP4Atom::Generate();

    ((MP4Integer8Property*)m_pProperties[0])->SetValue(1);

    m_pProperties[4]->SetReadOnly(false);
    ((MP4BitfieldProperty*)m_pProperties[4])->SetValue(0x3f);
    m_pProperties[4]->SetReadOnly(true);

    m_pProperties[6]->SetReadOnly(false);
    ((MP4BitfieldProperty*)m_pProperties[6])->SetValue(0x7);
    m_pProperties[6]->SetReadOnly(true);
#if 0
    // property reserved4 has non-zero fixed values
    static uint8_t reserved4[4] = {
        0x00, 0x18, 0xFF, 0xFF,
    };
    m_pProperties[7]->SetReadOnly(false);
    ((MP4BytesProperty*)m_pProperties[7])->
    SetValue(reserved4, sizeof(reserved4));
    m_pProperties[7]->SetReadOnly(true);
#endif
}

//
// Clone - clone my properties to destination atom
//
// this method simplifies duplicating avcC atom properties from
// source to destination file using a single API rather than
// having to copy each property.  This API encapsulates the object
// so the application layer need not concern with each property
// thereby isolating any future changes to atom properties.
//
// ----------------------------------------
// property   description
// ----------------------------------------
//
// 0    configurationVersion
// 1    AVCProfileIndication
// 2    profile_compatibility
// 3    AVCLevelIndication
// 4    reserved
// 5    lengthSizeMinusOne
// 6    reserved
// 7    number of SPS
// 8    SPS entries
// 9    number of PPS
// 10   PPS entries
//
//
void MP4AvcCAtom::Clone(MP4AvcCAtom *dstAtom)
{

    MP4Property *dstProperty;
    MP4TableProperty *pTable;
    uint16_t i16;
    uint64_t i32;
    uint64_t i64;
    uint8_t *tmp;

    // source pointer Property I16
    MP4Integer16Property *spPI16;
    // source pointer Property Bytes
    MP4BytesProperty *spPB;

    // dest pointer Property I16
    MP4Integer16Property *dpPI16;
    // dest pointer Property Bytes
    MP4BytesProperty *dpPB;


    // start with defaults and reserved fields
    dstAtom->Generate();

    // 0, 4, 6 are now generated from defaults
    // leaving 1, 2, 3, 5, 7, 8, 9, 10 to export

    dstProperty=dstAtom->GetProperty(1);
    ((MP4Integer8Property *)dstProperty)->SetValue(
        ((MP4Integer8Property *)m_pProperties[1])->GetValue());

    dstProperty=dstAtom->GetProperty(2);
    ((MP4Integer8Property *)dstProperty)->SetValue(
        ((MP4Integer8Property *)m_pProperties[2])->GetValue());

    dstProperty=dstAtom->GetProperty(3);
    ((MP4Integer8Property *)dstProperty)->SetValue(
        ((MP4Integer8Property *)m_pProperties[3])->GetValue());

    dstProperty=dstAtom->GetProperty(5);
    ((MP4BitfieldProperty *)dstProperty)->SetValue(
        ((MP4BitfieldProperty *)m_pProperties[5])->GetValue());

    //
    // 7 and 8 are related SPS (one set of sequence parameters)
    //
    // first the count bitfield
    //
    dstProperty=dstAtom->GetProperty(7);
    dstProperty->SetReadOnly(false);
    ((MP4BitfieldProperty *)dstProperty)->SetValue(
        ((MP4BitfieldProperty *)m_pProperties[7])->GetValue());
    dstProperty->SetReadOnly(true);

    // next export SPS Length and NAL bytes */

    // first source pointers
    pTable = (MP4TableProperty *) m_pProperties[8];
    spPI16 = (MP4Integer16Property *)pTable->GetProperty(0);
    spPB = (MP4BytesProperty *)pTable->GetProperty(1);

    // now dest pointers
    dstProperty=dstAtom->GetProperty(8);
    pTable = (MP4TableProperty *) dstProperty;
    dpPI16 = (MP4Integer16Property *)pTable->GetProperty(0);
    dpPB = (MP4BytesProperty *)pTable->GetProperty(1);

    // sps length
    i16 = spPI16->GetValue();
    i64 = i16;
    // FIXME - this leaves m_maxNumElements =2
    // but src atom m_maxNumElements is 1
    dpPI16->InsertValue(i64, 0);

    // export byte array
    i32 = i16;
    // copy bytes to local buffer
    tmp = (uint8_t *)MP4Malloc(i32);
    ASSERT(tmp != NULL);
    spPB->CopyValue(tmp, 0);
    // set element count
    dpPB->SetCount(1);
    // copy bytes
    dpPB->SetValue(tmp, i32, 0);
    MP4Free((void *)tmp);

    //
    // 9 and 10 are related PPS (one set of picture parameters)
    //
    // first the integer8 count
    //
    dstProperty=dstAtom->GetProperty(9);
    dstProperty->SetReadOnly(false);
    ((MP4Integer8Property *)dstProperty)->SetValue(
        ((MP4Integer8Property *)m_pProperties[9])->GetValue());
    dstProperty->SetReadOnly(true);

    // next export PPS Length and NAL bytes */

    // first source pointers
    pTable = (MP4TableProperty *) m_pProperties[10];
    spPI16 = (MP4Integer16Property *)pTable->GetProperty(0);
    spPB = (MP4BytesProperty *)pTable->GetProperty(1);

    // now dest pointers
    dstProperty=dstAtom->GetProperty(10);
    pTable = (MP4TableProperty *) dstProperty;
    dpPI16 = (MP4Integer16Property *)pTable->GetProperty(0);
    dpPB = (MP4BytesProperty *)pTable->GetProperty(1);

    // pps length
    i16 = spPI16->GetValue();
    i64 = i16;
    dpPI16->InsertValue(i64, 0);

    // export byte array
    i32 = i16;
    // copy bytes to local buffer
    tmp = (uint8_t *)MP4Malloc(i32);
    ASSERT(tmp != NULL);
    spPB->CopyValue(tmp, 0);
    // set element count
    dpPB->SetCount(1);
    // copy bytes
    dpPB->SetValue(tmp, i32, 0);
    MP4Free((void *)tmp);
}

///////////////////////////////////////////////////////////////////////////////

//===========================================cwm

MP4HvcCAtom::MP4HvcCAtom(MP4File &file)
        : MP4Atom(file, "hvcC")
{
#if 1 //cwm 278

    MP4TableProperty *pTable;
	
	AddProperty( new MP4Integer8Property(*this,"configurationVersion")); 
	AddProperty( new MP4Integer8Property(*this,"AVCProfileIndication")); 
	AddProperty( new MP4Integer32Property(*this,"profile_compatibility")); 
	AddProperty( new MP4Integer32Property(*this,"AVCLevelIndication")); 
	AddProperty( new MP4Integer16Property(*this,"m_constraint_indicator_flags_2B")); 
	AddProperty( new MP4Integer8Property(*this,"m_hvcC_level_idc_1B")); 
	AddProperty( new MP4Integer16Property(*this,"m_hvcC_min_spatial_segmentation_idc_2B")); 
	AddProperty( new MP4Integer8Property(*this,"m_hvcC_parallelismType_1B")); 
	AddProperty( new MP4Integer8Property(*this,"m_hvcC_chromaFormat_1B")); 
	AddProperty( new MP4Integer8Property(*this,"m_hvcC_bitDepthLumaMinus8_1B")); 
	AddProperty( new MP4Integer8Property(*this,"m_hvcC_bitDepthChromaMinus8_1B")); 
	AddProperty( new MP4Integer16Property(*this,"m_hvcC_avgFrameRate_2B")); 
	AddProperty( new MP4Integer8Property(*this,"lengthSizeMinusOne")); 
	AddProperty( new MP4Integer8Property(*this,"m_hvcC_numOfArrays_1B")); 

	//cwm vps	
	AddProperty( new MP4Integer8Property(*this,"typeOfVideoParameterSets")); 
	MP4BitfieldProperty *pVps = new MP4BitfieldProperty(*this,"numOfVideoParameterSets", 16);
	AddProperty(pVps); /* 7 */	
	pTable = new SizeTableProperty(*this,"videoEntries", pVps);
	AddProperty(pTable); /* 8 */
	pTable->AddProperty(new MP4Integer16Property(pTable->GetParentAtom(),"videoParameterSetLength"));
	pTable->AddProperty(new MP4BytesProperty(pTable->GetParentAtom(),"videoParameterSetNALUnit"));

	//cwm sps
	AddProperty( new MP4Integer8Property(*this,"typeOfSequenceParameterSets")); 
	MP4BitfieldProperty *pSps = new MP4BitfieldProperty(*this,"numOfSequenceParameterSets", 16);
    AddProperty(pSps); /* 7 */	
    pTable = new SizeTableProperty(*this,"sequenceEntries", pSps);
    AddProperty(pTable); /* 8 */
    pTable->AddProperty(new MP4Integer16Property(pTable->GetParentAtom(),"sequenceParameterSetLength"));
    pTable->AddProperty(new MP4BytesProperty(pTable->GetParentAtom(),"sequenceParameterSetNALUnit"));

	//cwm pps
	AddProperty( new MP4Integer8Property(*this,"typeOfPictureParameterSets")); 
    //MP4Integer8Property *pCount2 = new MP4Integer16Property(*this,"numOfPictureParameterSets");
    MP4BitfieldProperty *pPps = new MP4BitfieldProperty(*this,"numOfPictureParameterSets", 16);
    AddProperty(pPps); /* 9 */
    pTable = new SizeTableProperty(*this,"pictureEntries", pPps);
    AddProperty(pTable); /* 10 */
    pTable->AddProperty(new MP4Integer16Property(pTable->GetParentAtom(),"pictureParameterSetLength"));
    pTable->AddProperty(new MP4BytesProperty(pTable->GetParentAtom(),"pictureParameterSetNALUnit"));
	
#else //cwm 278

    MP4BitfieldProperty *pCount;
    MP4TableProperty *pTable;

    AddProperty( new MP4Integer8Property(*this,"configurationVersion")); /* 0 */ // 1B m_hvcC_version_1B

    AddProperty( new MP4Integer8Property(*this,"AVCProfileIndication")); /* 1 */ // 1B m_hvcC_profile_space_tier_flag_profile_idc_1B

    AddProperty( new MP4Integer8Property(*this,"profile_compatibility")); /* 2 */// 1B -> 4B m_hvcC_profile_compatibility_flags_4B

    AddProperty( new MP4Integer8Property(*this,"AVCLevelIndication")); /* 3 */ // 1B -> 4B m_hvcC_constraint_indicator_flags_4B

    AddProperty( new MP4BitfieldProperty(*this,"reserved", 6)); /* 4 */
    AddProperty( new MP4BitfieldProperty(*this,"lengthSizeMinusOne", 2)); /* 5 */
    AddProperty( new MP4BitfieldProperty(*this,"reserved1", 3)); /* 6 */
    pCount = new MP4BitfieldProperty(*this,"numOfSequenceParameterSets", 5);
    AddProperty(pCount); /* 7 */
	
	#if 1 //cwm 301
    pTable = new SizeTableProperty(*this,"sequenceEntries", pCount);
    AddProperty(pTable); /* 8 */
    pTable->AddProperty(new MP4Integer16Property(pTable->GetParentAtom(),"sequenceParameterSetLength"));
    pTable->AddProperty(new MP4BytesProperty(pTable->GetParentAtom(),"sequenceParameterSetNALUnit"));

    MP4Integer8Property *pCount2 = new MP4Integer8Property(*this,"numOfPictureParameterSets");
    AddProperty(pCount2); /* 9 */

    pTable = new SizeTableProperty(*this,"pictureEntries", pCount2);
    AddProperty(pTable); /* 10 */
    pTable->AddProperty(new MP4Integer16Property(pTable->GetParentAtom(),"pictureParameterSetLength"));
    pTable->AddProperty(new MP4BytesProperty(pTable->GetParentAtom(),"pictureParameterSetNALUnit"));
	#endif //cwm 301
#endif //cwm 278

}

void MP4HvcCAtom::Generate()
{
    MP4Atom::Generate();
	
#if 0 //cwm 322
	uint8_t *out_buf = NULL;
	uint32_t out_size = 0;
	if(NULL != m_File.m_pPsData)
	{
		mov_write_hvcc_tag(m_File.m_pPsData, m_File.m_ui32PsDataSize, &out_buf, &out_size);
	    m_pProperties[0]->SetReadOnly(false);
	    ((MP4BytesProperty*)m_pProperties[0])->
	    SetValue(out_buf, out_size);
	    m_pProperties[0]->SetReadOnly(true);
		m_File.m_ui32PsDataSize = 0;
		free(m_File.m_pPsData);
	}
#else //cwm 322

	((MP4Integer8Property*)m_pProperties[0])->SetValue(1);

	#if 0 //cwm 351
	((MP4Integer8Property*)m_pProperties[0])->SetValue(1);

    m_pProperties[4]->SetReadOnly(false);
    ((MP4BitfieldProperty*)m_pProperties[4])->SetValue(0x3f);
    m_pProperties[4]->SetReadOnly(true);

    m_pProperties[6]->SetReadOnly(false);
    ((MP4BitfieldProperty*)m_pProperties[6])->SetValue(0x7);
    m_pProperties[6]->SetReadOnly(true);
	#endif //cwm 351
	#if 0 //cwm 389
	const uint8_t buffer[4] = {0x12, 0x34, 0x56, 0x78};
	
    MP4BytesProperty *pUnit;
	if(this->FindProperty("hvcC.sequenceEntries.sequenceParameterSetNALUnit",
                                    (MP4Property **)&pUnit) == false)
	{
		PrintInfo("hvcC.sequenceEntries.sequenceParameterSetNALUnit failed");
	}
    pUnit->AddValue(buffer, sizeof(buffer));
	#endif //cwm 389
	#if 0 //cwm_add 336
	uint8_t *out_buf = NULL;
	uint32_t out_size = 0;
	if(NULL != m_File.m_pPsData)
	{
		mov_write_hvcc_tag(m_File.m_pPsData, m_File.m_ui32PsDataSize, &out_buf, &out_size);
	    m_pProperties[0]->SetReadOnly(false);
	    ((MP4BytesProperty*)m_pProperties[0])->
	    SetValue(out_buf, out_size);
	    m_pProperties[0]->SetReadOnly(true);
		m_File.m_ui32PsDataSize = 0;
		free(m_File.m_pPsData);
	}

	#endif //cwm_add 336
	
#endif //cwm 322

}

//
// Clone - clone my properties to destination atom
//
// this method simplifies duplicating avcC atom properties from
// source to destination file using a single API rather than
// having to copy each property.  This API encapsulates the object
// so the application layer need not concern with each property
// thereby isolating any future changes to atom properties.
//
// ----------------------------------------
// property   description
// ----------------------------------------
//
// 0    configurationVersion
// 1    AVCProfileIndication
// 2    profile_compatibility
// 3    AVCLevelIndication
// 4    reserved
// 5    lengthSizeMinusOne
// 6    reserved
// 7    number of SPS
// 8    SPS entries
// 9    number of PPS
// 10   PPS entries
//
//
void MP4HvcCAtom::Clone(MP4HvcCAtom *dstAtom)
{
#if 1 //cwm
    MP4Property *dstProperty;
    MP4TableProperty *pTable;
    uint16_t i16;
    uint64_t i32;
    uint64_t i64;
    uint8_t *tmp;

    // source pointer Property I16
    MP4Integer16Property *spPI16;
    // source pointer Property Bytes
    MP4BytesProperty *spPB;

    // dest pointer Property I16
    MP4Integer16Property *dpPI16;
    // dest pointer Property Bytes
    MP4BytesProperty *dpPB;


    // start with defaults and reserved fields
    dstAtom->Generate();

    // 0, 4, 6 are now generated from defaults
    // leaving 1, 2, 3, 5, 7, 8, 9, 10 to export

    dstProperty=dstAtom->GetProperty(1);
    ((MP4Integer8Property *)dstProperty)->SetValue(
        0x12);//((MP4Integer8Property *)m_pProperties[1])->GetValue()

    dstProperty=dstAtom->GetProperty(2);
    ((MP4Integer8Property *)dstProperty)->SetValue(
        0x34);//((MP4Integer8Property *)m_pProperties[2])->GetValue()

    dstProperty=dstAtom->GetProperty(3);
    ((MP4Integer8Property *)dstProperty)->SetValue(
        ((MP4Integer8Property *)m_pProperties[3])->GetValue());

    dstProperty=dstAtom->GetProperty(5);
    ((MP4BitfieldProperty *)dstProperty)->SetValue(
        ((MP4BitfieldProperty *)m_pProperties[5])->GetValue());

    //
    // 7 and 8 are related SPS (one set of sequence parameters)
    //
    // first the count bitfield
    //
    dstProperty=dstAtom->GetProperty(7);
    dstProperty->SetReadOnly(false);
    ((MP4BitfieldProperty *)dstProperty)->SetValue(
        ((MP4BitfieldProperty *)m_pProperties[7])->GetValue());
    dstProperty->SetReadOnly(true);

    // next export SPS Length and NAL bytes */

    // first source pointers
    pTable = (MP4TableProperty *) m_pProperties[8];
    spPI16 = (MP4Integer16Property *)pTable->GetProperty(0);
    spPB = (MP4BytesProperty *)pTable->GetProperty(1);

    // now dest pointers
    dstProperty=dstAtom->GetProperty(8);
    pTable = (MP4TableProperty *) dstProperty;
    dpPI16 = (MP4Integer16Property *)pTable->GetProperty(0);
    dpPB = (MP4BytesProperty *)pTable->GetProperty(1);

    // sps length
    i16 = spPI16->GetValue();
    i64 = i16;
    // FIXME - this leaves m_maxNumElements =2
    // but src atom m_maxNumElements is 1
    dpPI16->InsertValue(i64, 0);

    // export byte array
    i32 = i16;
    // copy bytes to local buffer
    tmp = (uint8_t *)MP4Malloc(i32);
    ASSERT(tmp != NULL);
    spPB->CopyValue(tmp, 0);
    // set element count
    dpPB->SetCount(1);
    // copy bytes
    dpPB->SetValue(tmp, i32, 0);
    MP4Free((void *)tmp);

    //
    // 9 and 10 are related PPS (one set of picture parameters)
    //
    // first the integer8 count
    //
    dstProperty=dstAtom->GetProperty(9);
    dstProperty->SetReadOnly(false);
    ((MP4Integer8Property *)dstProperty)->SetValue(
        ((MP4Integer8Property *)m_pProperties[9])->GetValue());
    dstProperty->SetReadOnly(true);

    // next export PPS Length and NAL bytes */

    // first source pointers
    pTable = (MP4TableProperty *) m_pProperties[10];
    spPI16 = (MP4Integer16Property *)pTable->GetProperty(0);
    spPB = (MP4BytesProperty *)pTable->GetProperty(1);

    // now dest pointers
    dstProperty=dstAtom->GetProperty(10);
    pTable = (MP4TableProperty *) dstProperty;
    dpPI16 = (MP4Integer16Property *)pTable->GetProperty(0);
    dpPB = (MP4BytesProperty *)pTable->GetProperty(1);
#if 1//cwm 500
    // pps length
    i16 = spPI16->GetValue();
    i64 = i16;
    dpPI16->InsertValue(i64, 0);

    // export byte array
    i32 = i16;
    // copy bytes to local buffer
    tmp = (uint8_t *)MP4Malloc(i32);
    ASSERT(tmp != NULL);
    spPB->CopyValue(tmp, 0);
    // set element count
    dpPB->SetCount(1);
    // copy bytes
    dpPB->SetValue(tmp, i32, 0);
    MP4Free((void *)tmp);
#endif //cwm 500
#endif //cwm
}

//====================================================cwm

}
} // namespace mp4v2::impl
