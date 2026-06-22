#ifndef MP4V2_FILE_H
#define MP4V2_FILE_H

/**************************************************************************//**
 *
 *  @defgroup mp4_file MP4v2 File I/O
 *  @{
 *
 *****************************************************************************/

/** Bit: enable 64-bit data-atoms. */
#define MP4_CREATE_64BIT_DATA 0x01
/** Bit: enable 64-bit time-atoms. @note Incompatible with QuickTime. */
#define MP4_CREATE_64BIT_TIME 0x02
/** Bit: do not recompute avg/max bitrates on file close.  @note See http://code.google.com/p/mp4v2/issues/detail?id=66 */
#define MP4_CLOSE_DO_NOT_COMPUTE_BITRATE 0x01
#define MP4_CLOSE_DO_NOT_COMPUTE_BITRATE_V2 0x03

/** Enumeration of file modes for custom file provider. */
typedef enum MP4FileMode_e
{
    FILEMODE_UNDEFINED, /**< undefined */
    FILEMODE_READ,      /**< file may be read */
    FILEMODE_MODIFY,    /**< file may be read/written */
    FILEMODE_CREATE    /**< file will be created/truncated for read/write */
} MP4FileMode;

/** Structure of functions implementing custom file provider.
 *
 *  Except for <b>open</b>, all the functions must return a true value
 *  to indicate failure or false on success. The open function must return
 *  a pointer or handle which represents the open file, otherwise NULL.
 *
 *  maxChunkSize is a hint suggesting what the max size of data should be read
 *  as in underlying read/write operations. A value of 0 indicates there is no hint.
 */
typedef struct MP4FileProvider_s
{
    void* ( *open  )( const char* name, MP4FileMode mode );
    int   ( *seek  )( void* handle, int64_t pos );
    int   ( *read  )( void* handle, void* buffer, int64_t size, int64_t* nin, int64_t maxChunkSize );
    int   ( *write )( void* handle, const void* buffer, int64_t size, int64_t* nout, int64_t maxChunkSize );
    int   ( *close )( void* handle );
} MP4FileProvider;

/** Close an mp4 file.
 *  MP4Close closes a previously opened mp4 file. If the file was opened
 *  writable with MP4Create() or MP4Modify(), then MP4Close() will write
 *  out all pending information to disk.
 *
 *  @param hFile handle of file to close.
 *  @param flags bitmask that allows the user to set extra options for the
 *       close commands.  Valid options include:
 *          @li #MP4_CLOSE_DO_NOT_COMPUTE_BITRATE
 */
MP4V2_EXPORT
void MP4Close(
    MP4FileHandle hFile,
    uint32_t    flags DEFAULT(0) );

/** Create a new mp4 file.
 *
 *  MP4Create is the first call that should be used when you want to create
 *  a new, empty mp4 file. It is equivalent to opening a file for writing,
 *  but also involved with creation of necessary mp4 framework structures.
 *  ie. invoking MP4Create() followed by MP4Close() will result in a file
 *  with a non-zero size.
 *
 *  @param fileName pathname of the file to be created.
 *      On Windows, this should be a UTF-8 encoded string.
 *      On other platforms, it should be an 8-bit encoding that is
 *      appropriate for the platform, locale, file system, etc.
 *      (prefer to use UTF-8 when possible).
 *  @param flags bitmask that allows the user to set 64-bit values for
 *      data or time atoms. Valid bits may be any combination of:
 *          @li #MP4_CREATE_64BIT_DATA
 *          @li #MP4_CREATE_64BIT_TIME
 *
 *  @return On success a handle of the newly created file for use in
 *      subsequent calls to the library.
 *      On error, #MP4_INVALID_FILE_HANDLE.
 */
MP4V2_EXPORT
MP4FileHandle MP4Create(
    const char* fileName,
    uint32_t    flags DEFAULT(0) );

/** Create a new mp4 file with extended options.
 *
 *  MP4CreateEx is an extended version of MP4Create().
 *
 *  @param fileName pathname of the file to be created.
 *      On Windows, this should be a UTF-8 encoded string.
 *      On other platforms, it should be an 8-bit encoding that is
 *      appropriate for the platform, locale, file system, etc.
 *      (prefer to use UTF-8 when possible).
 *  @param flags bitmask that allows the user to set 64-bit values for
 *      data or time atoms. Valid bits may be any combination of:
 *          @li #MP4_CREATE_64BIT_DATA
 *          @li #MP4_CREATE_64BIT_TIME
 *  @param add_ftyp if true an <b>ftyp</b> atom is automatically created.
 *  @param add_iods if true an <b>iods</b> atom is automatically created.
 *  @param majorBrand <b>ftyp</b> brand identifier.
 *  @param minorVersion <b>ftyp</b> informative integer for the minor version
 *      of the major brand.
 *  @param compatibleBrands <b>ftyp</b> list of compatible brands.
 *  @param compatibleBrandsCount is the count of items specified in
 *      compatibleBrands.
 *
 *  @return On success a handle of the newly created file for use in
 *      subsequent calls to the library.
 *      On error, #MP4_INVALID_FILE_HANDLE.
 */
MP4V2_EXPORT
MP4FileHandle MP4CreateEx(
    const char* fileName,
    uint32_t    flags DEFAULT(0),
    int         add_ftyp DEFAULT(1),
    int         add_iods DEFAULT(1),
    char*       majorBrand DEFAULT(0),
    uint32_t    minorVersion DEFAULT(0),
    char**      compatibleBrands DEFAULT(0),
    uint32_t    compatibleBrandsCount DEFAULT(0) );

/** Dump mp4 file contents as ASCII either to stdout or the
 *  log callback (@p see MP4SetLogCallback)
 *
 *  Dump is an invaluable debugging tool in that in can reveal all the details
 *  of the mp4 control structures. However, the output will not make much sense
 *  until you familiarize yourself with the mp4 specification (or the Quicktime
 *  File Format specification).
 *

 *  Note that MP4Dump() will not print the individual values of control tables,
 *  such as the size of each sample, unless the current log level is at least
 *  #MP4_LOG_VERBOSE2.  @p see MP4LogSetLevel() for how to set this.
 *
 *  @param hFile handle of file to dump.
 *  @param dumpImplicits prints properties which would not actually be
 *      written to the mp4 file, but still exist in mp4 control structures.
 *      ie. they are implicit given the current values of other controlling
 *      properties.
 *
 *  @return <b>true</b> on success, <b>false</b> on failure.
 */
MP4V2_EXPORT
bool MP4Dump(
    MP4FileHandle hFile,
    bool          dumpImplicits DEFAULT(0) );

/** Return a textual summary of an mp4 file.
 *
 *  MP4FileInfo provides a string that contains a textual summary of the
 *  contents of an mp4 file. This includes the track id's, the track type,
 *  and track specific information. For example, for a video track, media
 *  encoding, image size, frame rate, and bitrate are summarized.
 *
 *  Note that the returned string is malloc'ed, so it is the caller's
 *  responsibility to free() the string. Also note that the returned string
 *  contains newlines and tabs which may or may not be desirable. 
 *
 *  The following is an example of the output of MP4Info():
@verbatim
Track  Type   Info
1      video  MPEG-4 Simple @ L3, 119.625 secs, 1008 kbps, 352x288 @ 24.00 fps
2      audio  MPEG-4, 119.327 secs, 128 kbps, 44100 Hz
3      hint   Payload MP4V-ES for track 1
4      hint   Payload mpeg4-generic for track 2
5      od     Object Descriptors
6      scene  BIFS
@endverbatim
 *
 *  @param fileName pathname to mp4 file to summarize.
 *      On Windows, this should be a UTF-8 encoded string.
 *      On other platforms, it should be an 8-bit encoding that is
 *      appropriate for the platform, locale, file system, etc.
 *      (prefer to use UTF-8 when possible).
 *  @param trackId specifies track to summarize. If the value is
 *      #MP4_INVALID_TRACK_ID, the summary info is created for all
 *      tracks in the file.
 *
 *  @return On success a malloc'd string containing summary information.
 *      On failure, <b>NULL</b>.
 *
 *  @see MP4Info().
 */
MP4V2_EXPORT
char* MP4FileInfo(
    const char* fileName,
    MP4TrackId  trackId DEFAULT(MP4_INVALID_TRACK_ID) );

/** Accessor for the filename associated with a file handle
 *
 * @param hFile a file handle
 *
 * @return the NUL-terminated, UTF-8 encoded filename
 * associated with @p hFile
 */
MP4V2_EXPORT
const char* MP4GetFilename(
    MP4FileHandle hFile );

/** Return a textual summary of an mp4 file.
 *
 *  MP4FileInfo provides a string that contains a textual summary of the
 *  contents of an mp4 file. This includes the track id's, the track type,
 *  and track specific information. For example, for a video track, media
 *  encoding, image size, frame rate, and bitrate are summarized.
 *
 *  Note that the returned string is malloc'ed, so it is the caller's
 *  responsibility to free() the string. Also note that the returned string
 *  contains newlines and tabs which may or may not be desirable. 
 *
 *  The following is an example of the output of MP4Info():
@verbatim
Track  Type   Info
1      video  MPEG-4 Simple @ L3, 119.625 secs, 1008 kbps, 352x288 @ 24.00 fps
2      audio  MPEG-4, 119.327 secs, 128 kbps, 44100 Hz
3      hint   Payload MP4V-ES for track 1
4      hint   Payload mpeg4-generic for track 2
5      od     Object Descriptors
6      scene  BIFS
@endverbatim
 *
 *  @param hFile handle of file to summarize.
 *  @param trackId specifies track to summarize. If the value is
 *      #MP4_INVALID_TRACK_ID, the summary info is created for all
 *      tracks in the file.
 *
 *  @return On success a malloc'd string containing summary information.
 *      On failure, <b>NULL</b>.
 *
 *  @see MP4FileInfo().
 */
MP4V2_EXPORT
char* MP4Info(
    MP4FileHandle hFile,
    MP4TrackId    trackId DEFAULT(MP4_INVALID_TRACK_ID) );

/** Modify an existing mp4 file.
 *
 *  MP4Modify is the first call that should be used when you want to modify
 *  an existing mp4 file. It is roughly equivalent to opening a file in
 *  read/write mode.
 *
 *  Since modifications to an existing mp4 file can result in a sub-optimal
 *  file layout, you may want to use MP4Optimize() after you have  modified
 *  and closed the mp4 file.
 *
 *  @param fileName pathname of the file to be modified.
 *      On Windows, this should be a UTF-8 encoded string.
 *      On other platforms, it should be an 8-bit encoding that is
 *      appropriate for the platform, locale, file system, etc.
 *      (prefer to use UTF-8 when possible).
 *  @param flags currently ignored.
 *
 *  @return On success a handle of the target file for use in subsequent calls
 *      to the library.
 *      On error, #MP4_INVALID_FILE_HANDLE.
 */
MP4V2_EXPORT
MP4FileHandle MP4Modify(
    const char* fileName,
    uint32_t    flags DEFAULT(0) );

/** Optimize the layout of an mp4 file.
 *
 *  MP4Optimize reads an existing mp4 file and writes a new version of the
 *  file with the two important changes:
 *
 *  First, the mp4 control information is moved to the beginning of the file.
 *  (Frequenty it is at the end of the file due to it being constantly
 *  modified as track samples are added to an mp4 file). This optimization
 *  is useful in that in allows the mp4 file to be HTTP streamed.
 *
 *  Second, the track samples are interleaved so that the samples for a
 *  particular instant in time are colocated within the file. This
 *  eliminates disk seeks during playback of the file which results in
 *  better performance.
 *
 *  There are also two important side effects of MP4Optimize():
 *
 *  First, any free blocks within the mp4 file are eliminated.
 *
 *  Second, as a side effect of the sample interleaving process any media
 *  data chunks that are not actually referenced by the mp4 control
 *  structures are deleted. This is useful if you have called MP4DeleteTrack()
 *  which only deletes the control information for a track, and not the
 *  actual media data.
 *
 *  @param fileName pathname of (existing) file to be optimized.
 *      On Windows, this should be a UTF-8 encoded string.
 *      On other platforms, it should be an 8-bit encoding that is
 *      appropriate for the platform, locale, file system, etc.
 *      (prefer to use UTF-8 when possible).
 *  @param newFileName pathname of the new optimized file.
 *      On Windows, this should be a UTF-8 encoded string.
 *      On other platforms, it should be an 8-bit encoding that is
 *      appropriate for the platform, locale, file system, etc.
 *      (prefer to use UTF-8 when possible).
 *      If NULL a temporary file in the same directory as the
 *      <b>fileName</b> will be used and <b>fileName</b>
 *      will be over-written upon successful completion.
 *
 *  @return <b>true</b> on success, <b>false</b> on failure.
 */
MP4V2_EXPORT
bool MP4Optimize(
    const char* fileName,
    const char* newFileName DEFAULT(NULL) );


/** Read an existing mp4 file.
 *
 *  MP4Read is the first call that should be used when you want to just
 *  read an existing mp4 file. It is equivalent to opening a file for
 *  reading, but in addition the mp4 file is parsed and the control
 *  information is loaded into memory. Note that actual track samples are not
 *  read into memory until MP4ReadSample() is called.
 *
 *  @param fileName pathname of the file to be read.
 *      On Windows, this should be a UTF-8 encoded string.
 *      On other platforms, it should be an 8-bit encoding that is
 *      appropriate for the platform, locale, file system, etc.
 *      (prefer to use UTF-8 when possible).
(
 *  @return On success a handle of the file for use in subsequent calls to
 *      the library.
 *      On error, #MP4_INVALID_FILE_HANDLE.
 */
MP4V2_EXPORT
MP4FileHandle MP4Read(
    const char* fileName );

/** Read an existing mp4 file.
 *
 *  MP4ReadProvider is the first call that should be used when you want to just
 *  read an existing mp4 file. It is equivalent to opening a file for
 *  reading, but in addition the mp4 file is parsed and the control
 *  information is loaded into memory. Note that actual track samples are not
 *  read into memory until MP4ReadSample() is called.
 *
 *  @param fileName pathname of the file to be read.
 *      On Windows, this should be a UTF-8 encoded string.
 *      On other platforms, it should be an 8-bit encoding that is
 *      appropriate for the platform, locale, file system, etc.
 *      (prefer to use UTF-8 when possible).
 *  @param fileProvider custom implementation of file I/O operations.
 *      All functions in structure must be implemented.
 *      The structure is immediately copied internally.
 *
 *  @return On success a handle of the file for use in subsequent calls to
 *      the library.
 *      On error, #MP4_INVALID_FILE_HANDLE.
 */
MP4V2_EXPORT
MP4FileHandle MP4ReadProvider(
    const char*            fileName,
    const MP4FileProvider* fileProvider DEFAULT(NULL) );

/** @} ***********************************************************************/

//////////////////////////////////////////////////

/** Create a new mp4 realtime stream handle with extended options.
 *
 *  MP4CreateMM is an extended version of MP4CreateEx().
 *
 *  @param fileName reserved. Set to be NULL.
 *  @param flags bitmask that allows the user to set 64-bit values for
 *      data or time atoms. Valid bits may be any combination of:
 *          @li #MP4_CREATE_64BIT_DATA
 *          @li #MP4_CREATE_64BIT_TIME
 *  @param add_ftyp if true an <b>ftyp</b> atom is automatically created.
 *  @param add_iods if true an <b>iods</b> atom is automatically created.
 *  @param majorBrand <b>ftyp</b> brand identifier.
 *  @param minorVersion <b>ftyp</b> informative integer for the minor version
 *      of the major brand.
 *  @param compatibleBrands <b>ftyp</b> list of compatible brands.
 *  @param compatibleBrandsCount is the count of items specified in
 *      compatibleBrands.
 *  @param mulMdat it is multy mdat status if it was set to be true.
 *  @param mdatSize the size of mdat.
 *
 *  @return On success a handle of the newly created file for use in
 *      subsequent calls to the library.
 *      On error, #MP4_INVALID_FILE_HANDLE.
 */
MP4V2_EXPORT
MP4FileHandle MP4CreateMM(
	const char* fileName,
	uint32_t	flags DEFAULT(0),
	int 		add_ftyp DEFAULT(1),
	int 		add_iods DEFAULT(1),
	char*		majorBrand DEFAULT(0),
	uint32_t	minorVersion DEFAULT(0),
	char**		compatibleBrands DEFAULT(0),
	uint32_t	compatibleBrandsCount DEFAULT(0),
	bool		mulMdat DEFAULT(true),
	uint64_t	mdatSize DEFAULT(MP4_MDAT_SIZE));

 /** Create a new mp4 realtime stream handle with extended options.
 *
 *  MP4CreateRT is an extended version of MP4CreateEx().
 *
 *  @param fileName reserved. Set to be NULL.
 *  @param flags bitmask that allows the user to set 64-bit values for
 *      data or time atoms. Valid bits may be any combination of:
 *          @li #MP4_CREATE_64BIT_DATA
 *          @li #MP4_CREATE_64BIT_TIME
 *  @param add_ftyp if true an <b>ftyp</b> atom is automatically created.
 *  @param add_iods if true an <b>iods</b> atom is automatically created.
 *  @param majorBrand <b>ftyp</b> brand identifier.
 *  @param minorVersion <b>ftyp</b> informative integer for the minor version
 *      of the major brand.
 *  @param compatibleBrands <b>ftyp</b> list of compatible brands.
 *  @param compatibleBrandsCount is the count of items specified in
 *      compatibleBrands.
 *  @param realimeMode create mode.
 *      MP4_RT it is basic realttime mode
 *      MP4_ADD_INFO in this mode, will fill user-define data.
 *      MP4_RT_MOOV in this mode, will creat moov.
 *  @param mdatSize the size of mdat.
 *  @param encryptionFlag it was encrypted if set to be ture.
 *  @realimeData out parameter, the buffer of contain realtime stream
 *  @realimeDataSize out parameter, realtime stream buffer size
 *
 *  @return On success a handle of the newly created file for use in
 *      subsequent calls to the library.
 *      On error, #MP4_INVALID_FILE_HANDLE.
 */
MP4V2_EXPORT
MP4FileHandle MP4CreateRT(
	const char* fileName,
	uint32_t	flags DEFAULT(0),
	int 		add_ftyp DEFAULT(1),
	int 		add_iods DEFAULT(1),
	char*		majorBrand DEFAULT(0),
	uint32_t	minorVersion DEFAULT(0),
	char**		compatibleBrands DEFAULT(0),
	uint32_t	compatibleBrandsCount DEFAULT(0),
    uint32_t	realimeMode DEFAULT(1),
	uint64_t	mdatSize DEFAULT(MP4_MDAT_SIZE),
    bool        encryptionFlag DEFAULT(false),
    uint8_t**	realimeData DEFAULT(NULL),
    uint64_t*	realimeDataSize DEFAULT(NULL) );

MP4V2_EXPORT
MP4FileHandle MP4CreateRTV2(
	const char* fileName,
	uint32_t	flags DEFAULT(0),
	int 		add_ftyp DEFAULT(1),
	int 		add_iods DEFAULT(1),
	char*		majorBrand DEFAULT(0),
	uint32_t	minorVersion DEFAULT(0),
	char**		compatibleBrands DEFAULT(0),
	uint32_t	compatibleBrandsCount DEFAULT(0),
    uint32_t	realimeMode DEFAULT(1),
	uint64_t	mdatSize DEFAULT(MP4_MDAT_SIZE),
    bool        encryptionFlag DEFAULT(false),
    uint8_t*    callbackFun DEFAULT(NULL),
    uint8_t**	realimeData DEFAULT(NULL),
    uint64_t*	realimeDataSize DEFAULT(NULL)  );

/** Close an mp4 realtime stream handle.
 *  MP4Close closes a previously opened mp4 realtime stream handle. If the realtime stream handle was opened
 *  writable with MP4CreateRT(), then MP4CloseRT() will write
 *  out all pending information to buffer.
 *
 *  @param hFile handle of realtime stream handle to close.
 *  @param flags bitmask that allows the user to set extra options for the
 *       close commands.  Valid options include:
 *          @li #MP4_CLOSE_DO_NOT_COMPUTE_BITRATE
 *  @realimeData out parameter, the buffer of contain realtime stream
 *  @realimeDataSize out parameter, realtime stream buffer size
 */
MP4V2_EXPORT
void MP4CloseRT(
    MP4FileHandle hFile,
    uint32_t    flags DEFAULT(0),
    uint8_t**	realimeData DEFAULT(NULL),
    uint64_t*	realimeDataSize DEFAULT(NULL) );

/** Close an mp4 realtime stream handle.
 *  MP4Close closes a previously opened mp4 realtime stream handle. 
 *
 *  @param hFile handle of file for operation.
 *  @param selfType data type.
 *       VMFT, virtual frame
 *       AVST, audio and vedio infomations,contain one sameple size and during
 *       VDTT, vedio track infomations,contain create infomations
 *       VDTT, audio track infomations,contain create infomations
 *  @memSize member size of user-define data
 *  @unitBuf user-define data
 *  @uinitBufSize user-define data size
 *
 *  @return <b>true</b> on success, <b>false</b> on failure.
 */
MP4V2_EXPORT
bool MP4WriteBaseUnit(MP4FileHandle hFile, MP4SelfType selfType, uint32_t memSize, uint8_t* unitBuf, uint32_t uinitBufSize);

/** Set inner create time and modified time.
 *  keep same md5 value for the same source file. 
 *
 *  @param createTime second time(base on 2082844800) 
 *
 */
MP4V2_EXPORT
void MP4SetAllCreateTime(MP4Timestamp createTime);

/** Get mp4 mp4 file tail size.
 *  . 
 *
 *  @param hFile handle of file for operation.
 *
 *  @return file tail size
 */
MP4V2_EXPORT
int64_t MP4GetFileTailSize(MP4FileHandle hFile);


/** Used for repair file to fill lost data.
 *  . 
 *
 *  @param hFile handle of file for operation.
 *  @unitBuf not used, set to NULL
 *  @uinitBufSize aligned data size
 *
 *  @return file tail size
 */
MP4V2_EXPORT
bool MP4WriteAlignData(MP4FileHandle hFile, uint8_t* unitBuf, uint64_t uinitBufSize, uint32_t uiVfSize);


/** Set realtime stream callback function.
 *  . 
 *
 *  @param hFile handle of file for operation.
 *  @callbackFun callback function :
 *   typedef void (*RealTimeCallbackFun)(MP4FileHandle hMP4Hadle, int32_t iErrorCode, uint8_t* pRTStream, uint64_t ui64RTStreamSize)
 *
 *  @return true is successfully.
 */
MP4V2_EXPORT
bool MP4SetRealtimeCallbackFun(MP4FileHandle hFile, void* callbackFun);

/** Set seldf-defined data mode.
 *  . 
 *
 *  @param hFile handle of file for operation.
 *  @uiMode set to MP4_COMPACT_MODE if use compact mode.  
 *
 *  @return true is successfully.
 */
MP4V2_EXPORT
bool MP4SetSelfDataMode(MP4FileHandle hFile, uint32_t uiMode);

/** Align the damage tail.
 *  . 
 *
 *  @param hFile handle of file for operation.
 *  @eType set to MP4_MOOV or MP4_FREE base on the  truth.   
 *  @uiLength the  truth length.  
 *
 *  @return true is successfully.
 */
MP4V2_EXPORT
bool MP4AlignTail(MP4FileHandle hFile, MP4BoxType eType, uint32_t uiLength);

/** @} ***********************************************************************/

#endif /* MP4V2_FILE_H */
