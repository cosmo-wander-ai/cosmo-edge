#ifndef BMCV_API_EXT_H
#define BMCV_API_EXT_H
#define BMCV_VERSION_MAJOR 2
#define BMCV_VERSION_MINOR 1
#include "bmlib_runtime.h"
#ifdef _WIN32
#ifndef NULL
  #ifdef __cplusplus
    #define NULL 0
  #else
    #define NULL ((void *)0)
  #endif
#endif
#define DECL_EXPORT __declspec(dllexport)
#define DECL_IMPORT __declspec(dllimport)
#else
#define DECL_EXPORT
#define DECL_IMPORT
#endif

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * bmcv api with the new interface.
 */

#ifndef WIN32
/*
 * Face detection frame information
 * x1 y1 x2 y2 is top left and bottom right coordinates
 * score is confidence or score face detection
 */
typedef struct {
    float x1;
    float y1;
    float x2;
    float y2;
    float score;
} __attribute__((packed)) face_rect_t;
#else
#pragma pack(push, 1)
/*
 * Face detection frame information
 * x1 y1 x2 y2 is top left and bottom right coordinates
 * score is confidence or score face detection
 */
typedef struct {
    float x1;
    float y1;
    float x2;
    float y2;
    float score;
} face_rect_t;
#pragma pack(pop)
#endif

#define MIN_PROPOSAL_NUM (1)
#define MAX_PROPOSAL_NUM (50000) //(65535)

#ifndef WIN32
/*
 * The nms_proposal_t structure is used to store the results of the face detection algorithm
 * size is number used of fact_rect array
 * face_rect is detected facial rectangle information
 * capacity is maxinum of array element
 * begin and end are array first used position with last used position
 */
typedef struct nms_proposal {
    int          size;
    face_rect_t  face_rect[MAX_PROPOSAL_NUM];
    int          capacity;
    face_rect_t *begin;
    face_rect_t *end;
} __attribute__((packed)) nms_proposal_t;
#else
#pragma pack(push, 1)
/*
 * The nms_proposal_t structure is used to store the results of the face detection algorithm
 * size is number used of fact_rect array
 * face_rect is detected facial rectangle information
 * capacity is maxinum of array element
 * begin and end are array first used position with last used position
 */
typedef struct nms_proposal {
    int          size;
    face_rect_t  face_rect[MAX_PROPOSAL_NUM];
    int          capacity;
    face_rect_t *begin;
    face_rect_t *end;
} nms_proposal_t;
#pragma pack(pop)
#endif

#define MAX_RECT_NUM (8 * 1024)
#ifndef WIN32
/*
 * fact_rect is array of detected facial rectangle
 * size is number used of fact_rect array
 * capacity is maxinum of array element
 * begin and end are array first used position with last used position
 */
typedef struct {
    face_rect_t  face_rect[MAX_RECT_NUM];
    int          size;
    int          capacity;
    face_rect_t *begin;
    face_rect_t *end;
} __attribute__((packed)) m_proposal_t;
#else
#pragma pack(push, 1)
/*
 * fact_rect is array of detected facial rectangle
 * size is number used of fact_rect array
 * capacity is maxinum of array element
 * begin and end are array first used position with last used position
 */
typedef struct {
    face_rect_t  face_rect[MAX_RECT_NUM];
    int          size;
    int          capacity;
    face_rect_t *begin;
    face_rect_t *end;
} m_proposal_t;
#pragma pack(pop)
#endif

// common struct

// BMCV_IMAGE_FOR_IN and BMCV_IMAGE_FOR_OUT may be deprecated in future version.
// We recommend not use this.
#define BMCV_IMAGE_FOR_IN BMCV_HEAP1_ID
#define BMCV_IMAGE_FOR_OUT BMCV_HEAP0_ID

#define BM_STITCH_MAX_SRC_NUM 4

#define AFFINE_MAX_REGION_NUM 32
#define FISHEYE_MAX_REGION_NUM 4

#define GDC_MAX_REGION_NUM         4

#define MAP_TABLE_SIZE 256
/*
 * Apply ip of the memory heap
 * BMCV_HEAP_ANY is any ip
 */
typedef enum bmcv_heap_id_ {
    BMCV_HEAP0_ID = 0,
    BMCV_HEAP1_ID = 1,
    BMCV_HEAP_ANY
} bmcv_heap_id;

/*
* All data types are listed, pixel data is how stored，in enumeration order
* 1.float 32bit；2.unsigned int 8bit；3.int 8bit；4.Semi-precision float，5bit is exponent and 10bit is a decimal
* 5.16bit float,8 bits for exponents and 7 bits for decimals；6.unsigned 16bit；7.signed 16bit；8.unsigned 32bit
*/
typedef enum bm_image_data_format_ext_ {
    DATA_TYPE_EXT_FLOAT32,
    DATA_TYPE_EXT_1N_BYTE,
    DATA_TYPE_EXT_1N_BYTE_SIGNED,
    DATA_TYPE_EXT_FP16,
    DATA_TYPE_EXT_BF16,
    DATA_TYPE_EXT_U16,
    DATA_TYPE_EXT_S16,
    DATA_TYPE_EXT_U32,
    DATA_TYPE_EXT_4N_BYTE = 254,
    DATA_TYPE_EXT_4N_BYTE_SIGNED,
} bm_image_data_format_ext;

/*
* The image format represents the type of storage of the data,in enumeration order
* 1.YUV420 with 3 planes; 2.YUV422 with 3 planes; 3.YUV444 with 3 planes; 4.YUV420 Y plane and UV interleaving storage
* 5.YUV420 Y plane and VU interleaving storage; 6.YUV422 Y plane and UV interleaving storage; 7.YUV422 Y plane and VU interleaving storage
* 8.YUV444 Y plane UV interleaving storage; 9.RGB three planes; 10.BGR three planes; 11.RGB interleaved with a plane; 12.BGR interleaved with a plane
* 13.RGB 3 planes; 14.BGR 3 planes; 15.only Y plane; 16.compression format four planes with Y table Y data, cbcr table cbcr data
* 17.HSV 3 planes,H~[0,180]; 18.ARGB interleaved with a plane; 19.ARGB interleaved with a plane; 20.YUV staggered with a plane
* 21.YVU staggered with a plane; 22. YUYV staggered with a plane; 23.YVYU staggered with a plane; 24.UYVY staggered with a plane
* 25.VYUY interleaved with a plane; 26.RGBY with 4 planes; 27.HSV with a plane, H~[0,180]; 28.HSV with a plane, H~[0,255]
* 29.sensor data format,Each pixel about one color channel; 30.red-green mode with 8-bit depth; 31.alpha, red, green, and blue channels, each with 4 bits
* 32.alpha, blue, green, and red channels, each with 4 bits; 33.alpha, red, green, and blue channels,1bit 5bit 5bit 5bit
* 34.alpha, blue, green, and red channels,1bit 5bit 5bit 5bit
*/
typedef enum bm_image_format_ext_ {
    FORMAT_YUV420P,
    FORMAT_YUV422P,
    FORMAT_YUV444P,
    FORMAT_NV12,
    FORMAT_NV21,
    FORMAT_NV16,
    FORMAT_NV61,
    FORMAT_NV24,
    FORMAT_RGB_PLANAR,
    FORMAT_BGR_PLANAR,
    FORMAT_RGB_PACKED,
    FORMAT_BGR_PACKED,
    FORMAT_RGBP_SEPARATE,
    FORMAT_BGRP_SEPARATE,
    FORMAT_GRAY,
    FORMAT_COMPRESSED,
    FORMAT_HSV_PLANAR,
    FORMAT_ARGB_PACKED,
    FORMAT_ABGR_PACKED,
    FORMAT_YUV444_PACKED,
    FORMAT_YVU444_PACKED,
    FORMAT_YUV422_YUYV,
    FORMAT_YUV422_YVYU,
    FORMAT_YUV422_UYVY,
    FORMAT_YUV422_VYUY,
    FORMAT_RGBYP_PLANAR,
    FORMAT_HSV180_PACKED,
    FORMAT_HSV256_PACKED,
    FORMAT_BAYER,
    FORMAT_BAYER_RG8,
    FORMAT_ARGB4444_PACKED,
    FORMAT_ABGR4444_PACKED,
    FORMAT_ARGB1555_PACKED,
    FORMAT_ABGR1555_PACKED,
} bm_image_format_ext;

/*
* Define the color space conversion control mode. in enum order
* 1.YUV2RGB image used BT601; 2.YUV2RGB video used BT601; 3.RGB2YUV image used BT601; 4.YUV2RGB image used BT709
* 5.RGB2YUV image used BT709; 6.RGB2YUV video used BT601; 7.YUV2RGB video used BT709; 8.RGB2YUV video used BT709
* 9.10.Specific color conversion modes 11.custom color space conversion matrix can be used;
* 12.determine if the input is in range
*/
typedef enum csc_type {
    CSC_YCbCr2RGB_BT601 = 0,
    CSC_YPbPr2RGB_BT601,
    CSC_RGB2YCbCr_BT601,
    CSC_YCbCr2RGB_BT709,
    CSC_RGB2YCbCr_BT709,
    CSC_RGB2YPbPr_BT601,
    CSC_YPbPr2RGB_BT709,
    CSC_RGB2YPbPr_BT709,
    CSC_FANCY_PbPr_BT601 = 100,
    CSC_FANCY_PbPr_BT709,
    CSC_USER_DEFINED_MATRIX = 1000,
    CSC_MAX_ENUM
} csc_type_t;

struct bm_image_private;

/*
* Create a struct type corresponding to bm_image,Members are as follows:
* 1.image width; 2.image height; 3.Color format of the image; 4.Data format of the image
* 5.Private data of the image
*/
typedef struct bm_image{
    int                       width;
    int                       height;
    bm_image_format_ext       image_format;
    bm_image_data_format_ext  data_type;
    struct bm_image_private  *image_private = NULL;
} bm_image;

/*
* bm_image with tensor info,Members are as follows:
* 1.bm_image struct with image info; 2.channel of image; 3.number of image
*/
typedef struct bm_image_tensor{
    bm_image image;
    int      image_c;
    int      image_n;
} bm_image_tensor;

/*
* A data structure that holds the required information,Members are as follows:
* 1.number plane of image; 2.per plane device memory; 3.per plane stride; 4.image width; 5.image height
* 6.image format; 7.data type of the image;
*/
typedef struct bm_image_format_info {
    int                      plane_nb;
    bm_device_mem_t          plane_data[8];
    int                      stride[8];
    int                      width;
    int                      height;
    bm_image_format_ext      image_format;
    bm_image_data_format_ext data_type;
    bool                     default_stride;
} bm_image_format_info_t;


// vpp/tpu struct

/*
* each image config param
* 1.channel 0 linear transformation coefficient. 2.channel 0 linear transformation offset
* 3.channel 1 linear transformation coefficient. 4.channel 1 linear transformation offset
* 5.channel 2 linear transformation coefficient. 6.channel 2 linear transformation offset
*/
typedef struct bmcv_convert_to_attr_s {
    float alpha_0;
    float beta_0;
    float alpha_1;
    float beta_1;
    float alpha_2;
    float beta_2;
} bmcv_convert_to_attr;

/*
* each crop output image information
* 1.top left horizontal coordinate; 2.top left ordinate
* 3.width of the crop area; 4.height of the crop area
*/
typedef struct bmcv_rect {
    unsigned int start_x;
    unsigned int start_y;
    unsigned int crop_w;
    unsigned int crop_h;
} bmcv_rect_t;

/*
* resize algorithm
* 1.nearest neighbor interpolation model; 2.bilinear interpolation scaling mode
* 3.bicubic interpolation mode; 4.area interpolation scaling mode
*/
typedef enum bmcv_resize_algorithm_ {
    BMCV_INTER_NEAREST = 0,
    BMCV_INTER_LINEAR  = 1,
    BMCV_INTER_BICUBIC = 2,
    BMCV_INTER_AREA = 3,
} bmcv_resize_algorithm;

/*
* all crop image information in dst image and padding information, if need not padding with NULL
* 1.dst_crop_stx dst_crop_sty is Upper-left coordinate information relative to target image
* 2.dst_crop_w dst_crop_h is the crop image to resize with width and height
* 3.if image is RGB format, padding_r padding_g padding_b is each channel padding infor ,if need not with if_memeset=0
*/
typedef struct bmcv_padding_atrr_s {
    unsigned int  dst_crop_stx;
    unsigned int  dst_crop_sty;
    unsigned int  dst_crop_w;
    unsigned int  dst_crop_h;
    unsigned char padding_r;
    unsigned char padding_g;
    unsigned char padding_b;
    int           if_memset;
} bmcv_padding_attr_t;

typedef struct {
    short csc_coe00;
    short csc_coe01;
    short csc_coe02;
    unsigned char csc_add0;
    unsigned char csc_sub0;
    short csc_coe10;
    short csc_coe11;
    short csc_coe12;
    unsigned char csc_add1;
    unsigned char csc_sub1;
    short csc_coe20;
    short csc_coe21;
    short csc_coe22;
    unsigned char csc_add2;
    unsigned char csc_sub2;
} csc_matrix_t;

/*
* Copy the information of the image
* 1.start_x and start_y are copy the starting coordinate information to the output image
* 2.padding_r padding_g and padding_b are the channel filled value When the input image is smaller than the output image.
* 3.if need not paddding, setting if_padding is 0
*/
typedef struct bmcv_copy_to_atrr_s {
    int           start_x;
    int           start_y;
    unsigned char padding_r;
    unsigned char padding_g;
    unsigned char padding_b;
    int           if_padding;
} bmcv_copy_to_atrr_t;

/*
* the value of drawing line with each channel value, with r g b channels
*/
typedef struct {
    unsigned char r;
    unsigned char g;
    unsigned char b;
} bmcv_color_t;

/*
* Specific properties and parameters required to perform an image scaling operation
* 1.start_x start_y is starting coordinates of the scaling operation
* 2.in_width in_height is src or roi image width and height
* 3.out_width out_height id output image width and height
*/
typedef struct bmcv_resize_s {
    int start_x;
    int start_y;
    int in_width;
    int in_height;
    int out_width;
    int out_height;
} bmcv_resize_t;

/*
* resize image need the attr infor
* 1.resize_img_attr is pointer to the struct of type bmcv_resize_t
* 2.roi_num is num of interesting areas; 3.stretch_fit is if the image should be stretched to fit the target size
* 3.padding_b padding_g padding_r are each channel padding with the value
* 4.interpolation is witch interpolation method will be used
*/
typedef struct bmcv_resize_image_s {
    bmcv_resize_t *resize_img_attr;
    int            roi_num;
    unsigned char  stretch_fit;
    unsigned char  padding_b;
    unsigned char  padding_g;
    unsigned char  padding_r;
    unsigned int   interpolation;
} bmcv_resize_image;

/*
* Description flip Mode
* NO_FLIP means do nothing, HORIZONTAL_FLIP means flip horizontally,
* VERTICAL_FLIP means flip vertically, and ROTATE_180 means rotate 180 degrees.
*/
typedef enum {
    NO_FLIP = 0,
    HORIZONTAL_FLIP = 1,
    VERTICAL_FLIP = 2,
    ROTATE_180 = 3,
} bmcv_flip_mode;

/*
* The starting coordinate information corresponding to the line point
*/
typedef struct {
    int x;
    int y;
} bmcv_point_t;

/*
* Affine transformation matrix data
*/
typedef struct bmcv_affine_matrix_s {
    float m[6];
} bmcv_affine_matrix;

/*
* 1.matrix is the affine transformation matrix struct pointer
* 2.matrix_num is affine transformation matrix numbers
*/
typedef struct bmcv_affine_image_matrix_s {
    bmcv_affine_matrix *matrix;
    int                 matrix_num;
} bmcv_affine_image_matrix;

/*
* Perspective transformation matrix data
*/
typedef struct bmcv_perspective_matrix_s {
    float m[9];
} bmcv_perspective_matrix;

/*
* 1.matrix is perspective transformation matrix pointers
* 2.matrix_num is perspective transformation matrix numbers
*/
typedef struct bmcv_perspective_image_matrix_s {
    bmcv_perspective_matrix *matrix;
    int                      matrix_num;
} bmcv_perspective_image_matrix;

/*
* x and y array stores the coordinate information of the points
*/
typedef struct bmcv_perspective_coordinate_s {
    int x[4];
    int y[4];
} bmcv_perspective_coordinate;

/*
* 1.coordinate is the coordinate point after perspective transformation
* 2.coordinate_num is the number of coordinate points in a coordinate point group
*/
typedef struct bmcv_perspective_image_coordinate_s {
    bmcv_perspective_coordinate *coordinate;
    int                         coordinate_num;
} bmcv_perspective_image_coordinate;

typedef bmcv_affine_image_matrix bmcv_warp_image_matrix;
typedef bmcv_affine_matrix bmcv_warp_matrix;

/*
* Threshold type
* 1.BM_THRESH_BINARY is if pixel > threshold, pixel=maximum; otherwise set to 0;
* 2.BM_THRESH_BINARY_INV if pixel > threshold, pixel=0; otherwise set to maximum;
* 3.BM_THRESH_TRUNC if pixel > threshold, pixel=threshold, otherwise keep
* 4.BM_THRESH_TOZERO_INV if pixel > threshold, pixel=0, otherwise keep
* 5.BM_THRESH_TYPE_MAX is the number of valid threshold types in the enumeration
*/
typedef enum {
    BM_THRESH_BINARY = 0,
    BM_THRESH_BINARY_INV,
    BM_THRESH_TRUNC,
    BM_THRESH_TOZERO,
    BM_THRESH_TOZERO_INV,
    BM_THRESH_TYPE_MAX
} bm_thresh_type_t;

/*
* non-maximum suppression type
* 1.HARD_NMS is hard non-maximum suppression; 2.SOFT_NMS is soft non-maximum suppression
* 3.ADAPTIVE_NMS is adaptive non-maximum suppression; 4.SSD_NMS is Single Shot Multibox Detector with NMS algorithm
* 5.NMS types are used as loop boundaries
*/
typedef enum bm_cv_nms_alg_ {
    HARD_NMS = 0,
    SOFT_NMS,
    MAX_NMS_TYPE
} bm_cv_nms_alg_e;


// dpu struct

/*
* DPU_SGBM_MUX0 is only sgbm,u8 disp out(no post process),16 align
* DPU_SGBM_MUX1 is only sgbm,u16 disp out(post process),16 align
* DPU_SGBM_MUX2 is only sgbm,u8 disp out(post process),16 align
*/
typedef enum bmcv_dpu_sgbm_mode_{
    DPU_SGBM_MUX0 = 1,
    DPU_SGBM_MUX1 = 2,
    DPU_SGBM_MUX2 = 3,
} bmcv_dpu_sgbm_mode;

/*
* DPU_ONLINE_MUX0 is sgbm 2 fgs online, fgs u8 disp out,16 align
* DPU_ONLINE_MUX1 is sgbm 2 fgs online, fgs u16 depth out,32 align
* DPU_ONLINE_MUX2 is sgbm 2 fgs online, sgbm u16 depth out,32 align
*/
typedef enum bmcv_dpu_online_mode_{
    DPU_ONLINE_MUX0 = 4,
    DPU_ONLINE_MUX1 = 5,
    DPU_ONLINE_MUX2 = 6,
} bmcv_dpu_online_mode;

/*
* DPU_FGS_MUX0 is only fgs, u8 disp out,16 align
* DPU_FGS_MUX1 is only fgs, u16 depth out,32 align
*/
typedef enum bmcv_dpu_fgs_mode_{
    DPU_FGS_MUX0 = 7,
    DPU_FGS_MUX1 = 8,
} bmcv_dpu_fgs_mode;

/*
* BMCV_DPU_DISP_RANGE_DEFAULT is default search range with 16 pixels
* BMCV_DPU_DISP_RANGE_16 is search range with 16 pixels; BMCV_DPU_DISP_RANGE_32 is search range with 32 pixels
* BMCV_DPU_DISP_RANGE_48 is search range with 48 pixels; BMCV_DPU_DISP_RANGE_64 is search range with 64 pixels
* BMCV_DPU_DISP_RANGE_80 is search range with 80 pixels; BMCV_DPU_DISP_RANGE_96 is search range with 96 pixels
* BMCV_DPU_DISP_RANGE_112 is search range with 112 pixels; BMCV_DPU_DISP_RANGE_128 is search range with 128 pixels
* BMCV_DPU_DISP_RANGE_BUTT is range limitation
*/
typedef enum bmcv_dpu_disp_range_{
    BMCV_DPU_DISP_RANGE_DEFAULT,
    BMCV_DPU_DISP_RANGE_16,
    BMCV_DPU_DISP_RANGE_32,
    BMCV_DPU_DISP_RANGE_48,
    BMCV_DPU_DISP_RANGE_64,
    BMCV_DPU_DISP_RANGE_80,
    BMCV_DPU_DISP_RANGE_96,
    BMCV_DPU_DISP_RANGE_112,
    BMCV_DPU_DISP_RANGE_128,
    BMCV_DPU_DISP_RANGE_BUTT
} bmcv_dpu_disp_range;

/*
* DPU_BFW_MODE_DEFAULT is default BoxFilter window size with 7x7
* DPU_BFW_MODE_1x1 is BoxFilter window size 1x1; DPU_BFW_MODE_3x3 is BoxFilter window size 3x3;
* DPU_BFW_MODE_5x5 is BoxFilter window size 5x5; DPU_BFW_MODE_7x7 is BoxFilter window size 7x7;
* DPU_BFW_MODE_BUTT is range limitation
*/
typedef enum bmcv_dpu_bfw_mode_{
    DPU_BFW_MODE_DEFAULT,
    DPU_BFW_MODE_1x1,
    DPU_BFW_MODE_3x3,
    DPU_BFW_MODE_5x5,
    DPU_BFW_MODE_7x7,
    DPU_BFW_MODE_BUTT
} bmcv_dpu_bfw_mode;

/*
* BMCV_DPU_DEPTH_UNIT_DEFAULT is default depth unit, mm
* BMCV_DPU_DEPTH_UNIT_MM is unit of depth with mm; BMCV_DPU_DEPTH_UNIT_CM is unit of depth with cm
* BMCV_DPU_DEPTH_UNIT_DM is unit of depth with dm; BMCV_DPU_DEPTH_UNIT_M is unit of depth with m
* BMCV_DPU_DEPTH_UNIT_BUTT is range limitation
*/
typedef enum bmcv_dpu_depth_unit_{
    BMCV_DPU_DEPTH_UNIT_DEFAULT,
    BMCV_DPU_DEPTH_UNIT_MM,
    BMCV_DPU_DEPTH_UNIT_CM,
    BMCV_DPU_DEPTH_UNIT_DM,
    BMCV_DPU_DEPTH_UNIT_M,
    BMCV_DPU_DEPTH_UNIT_BUTT
} bmcv_dpu_depth_unit;

/*
* BMCV_DPU_DCC_DIR_DEFAULT is DCC default cost aggregation direction: A1+A2
* BMCV_DPU_DCC_DIR_A12 is DCC cost aggregation direction: A1+A2
* BMCV_DPU_DCC_DIR_A13 is DCC cost aggregation direction: A1+A3
* BMCV_DPU_DCC_DIR_A14 is DCC cost aggregation direction: A1+A4
* BMCV_DPU_DCC_DIR_BUTT is range limitation
*/
typedef enum bmcv_dpu_dcc_dir_{
    BMCV_DPU_DCC_DIR_DEFAULT,
    BMCV_DPU_DCC_DIR_A12,
    BMCV_DPU_DCC_DIR_A13,
    BMCV_DPU_DCC_DIR_A14,
    BMCV_DPU_DCC_DIR_BUTT
} bmcv_dpu_dcc_dir;

/*
* bfw_mode_en is DPU SGBM BoxFilter window size; disp_range_en is search scope on the right
* disp_start_pos is starting position of the search on the right
* dpu_census_shift is Census Transform offset; dpu_rshift1 is weight of the original BTcost map
* dpu_rshift2 is weight of the BTcost map of the Census Transform
* dcc_dir_en is DCC cost aggregation direction; dpu_ca_p1 is DCC cost aggregation P1 penalty factor
* dpu_ca_p2 is DCC cost aggregation P2 penalty factor
* dpu_uniq_ratio is Uniqueness check factor, value range :[0, 100];dpu_disp_shift is parallax offset
*/
typedef struct bmcv_dpu_sgbm_attrs_{
    bmcv_dpu_bfw_mode    bfw_mode_en;
    bmcv_dpu_disp_range  disp_range_en;
    unsigned short       disp_start_pos;
    unsigned int         dpu_census_shift;
    unsigned int         dpu_rshift1;
    unsigned int         dpu_rshift2;
    bmcv_dpu_dcc_dir     dcc_dir_en;
    unsigned int         dpu_ca_p1;
    unsigned int         dpu_ca_p2;
    unsigned int         dpu_uniq_ratio;
    unsigned int         dpu_disp_shift;
} bmcv_dpu_sgbm_attrs;

/*
* Configure parameters for a specific algorithm or function of a deep processing unit (DPU)
* fgs_max_count is fgs indicates the maximum number of times that it is converted to 0
* fgs_max_t is maximum number of iterations of Fgs; fxbase_line is baseline values of the left and right cameras
* depth_unit_en is measure of depth
*/
typedef struct bmcv_dpu_fgs_attrs_{
    unsigned int         fgs_max_count;
    unsigned int         fgs_max_t;
    unsigned int         fxbase_line;
    bmcv_dpu_depth_unit  depth_unit_en;
} bmcv_dpu_fgs_attrs;


// ldc/dwa/blend struct
/*
* BMCV_ROTATION_0 is a rotation of zero degrees;BMCV_ROTATION_90 is rotated 180 degrees clockwise
* BMCV_ROTATION_270 is rotated 270 degrees clockwise;BMCV_ROTATION_XY_FLIP is flipped on both the X and Y axes
* BMCV_ROTATION_MAX is range limitation
*/
typedef enum bmcv_rot_mode_ {
    BMCV_ROTATION_0 = 0,
    BMCV_ROTATION_90,
    BMCV_ROTATION_180,
    BMCV_ROTATION_270,
    BMCV_ROTATION_XY_FLIP,
    BMCV_ROTATION_MAX
} bmcv_rot_mode;

/*
* Parameter configuration list of the GDC function
* 1.bAspect is RW;Whether aspect ration  is keep
*/
typedef struct _bmcv_gdc_attr {
    bool bAspect; /* RW;Whether aspect ration  is keep */
    int s32XRatio; /* RW; Range: [0, 100], field angle ration of  horizontal,valid when bAspect=0.*/
    int s32YRatio; /* RW; Range: [0, 100], field angle ration of  vertical,valid when bAspect=0.*/
    int s32XYRatio; /* RW; Range: [0, 100], field angle ration of  all,valid when bAspect=1.*/
    int s32CenterXOffset;
    int s32CenterYOffset;
    int s32DistortionRatio;
    bm_device_mem_t grid_info;
    // char *grid_info;
} bmcv_gdc_attr;

/*
* point coordinate
*/
typedef struct _bmcv_point2f_s {
    float x;
    float y;
} bmcv_point2f_s;

/*
* u32Width is width of the target area size
* u32Height is height of the target area size
*/
typedef struct _bmcv_size_s {
    unsigned int u32Width;
    unsigned int u32Height;
} bmcv_size_s;

/*
* u32RegionNum is number of affine areas,range :[1, 32];
* astRegionAttr is array with four vertex coordinates of the affine region
* stDestSize is Size of the target area after affine
*/
typedef struct _bmcv_affine_attr_s {
    unsigned int u32RegionNum;
    bmcv_point2f_s astRegionAttr[AFFINE_MAX_REGION_NUM][4];
    bmcv_size_s stDestSize;
} bmcv_affine_attr_s;

/*
* Fisheye camera mounting mode
* BMCV_FISHEYE_DESKTOP_MOUNT is mounted on desktop, level
* BMCV_FISHEYE_CEILING_MOUNT is  mounted on the ceiling, top mounted
* BMCV_FISHEYE_WALL_MOUNT is mounted on wall, wall mount
* BMCV_FISHEYE_MOUNT_MODE_BUTT is range limitation
*/
typedef enum _bmcv_fisyeye_mount_mode_e {
    BMCV_FISHEYE_DESKTOP_MOUNT = 0,
    BMCV_FISHEYE_CEILING_MOUNT = 1,
    BMCV_FISHEYE_WALL_MOUNT = 2,
    BMCV_FISHEYE_MOUNT_MODE_BUTT
} bmcv_fisyeye_mount_mode_e;

/*
* usage mode of Fisheye function
* 1.BMCV_MODE_PANORAMA_360 is 360 degree panoramic mode; 2.BMCV_MODE_PANORAMA_180 is 180 degree panoramic mode
* 3.BMCV_MODE_01_1O and BMCV_MODE_02_1O4R is custom view mode specific to the app or camera
* 4.BMCV_MODE_03_4R is four Region views; 5.BMCV_MODE_04_1P2R is One Panorama and two Region views
* 6.BMCV_MODE_05_1P2R is One Panorama and two Region views; 7.BMCV_MODE_06_1P is Single panoramic view
* 8.BMCV_MODE_07_2P is two panoramic view; 9.BMCV_MODE_STEREO_FIT is Stereoscopic fitting model
* 10.BMCV_MODE_MAX is range limitation
*/
typedef enum _bmcv_usage_mode {
    BMCV_MODE_PANORAMA_360 = 1,
    BMCV_MODE_PANORAMA_180 = 2,
    BMCV_MODE_01_1O = 3,
    BMCV_MODE_02_1O4R = 4,
    BMCV_MODE_03_4R = 5,
    BMCV_MODE_04_1P2R = 6,
    BMCV_MODE_05_1P2R = 7,
    BMCV_MODE_06_1P = 8,
    BMCV_MODE_07_2P = 9,
    BMCV_MODE_STEREO_FIT = 10,
    BMCV_MODE_MAX
} bmcv_usage_mode;

/* View mode of client
* 1.BMCV_FISHEYE_VIEW_360_PANORAMA is 360 degree panoramic mode
* 2.BMCV_FISHEYE_VIEW_180_PANORAMA is 180 degree panoramic mode
* 3.BMCV_FISHEYE_VIEW_NORMAL is normal view mode
* 4.BMCV_FISHEYE_NO_TRANSFORMATION is untransformed mode
* 5.BMCV_FISHEYE_VIEW_MODE_BUTT is range limitation
*/
typedef enum _bmcv_fisheye_view_mode_e {
    BMCV_FISHEYE_VIEW_360_PANORAMA = 0,
    BMCV_FISHEYE_VIEW_180_PANORAMA = 1,
    BMCV_FISHEYE_VIEW_NORMAL = 2,
    BMCV_FISHEYE_NO_TRANSFORMATION = 3,
    BMCV_FISHEYE_VIEW_MODE_BUTT
} bmcv_fisheye_view_mode_e;

/*
* Parameter configuration list of Fisheye function
* 1.bEnable is if use fisheye correction function; 2.bBgColor is if use background color function
* 3.u32BgColor is range: [0, 0xffffff],if eq=1,background color is RGB888
* 4.s32HorOffset is horizontal shift of the image center from the physical center
* 5.s32VerOffset is vertical shift of the image center from the physical center
* 6.u32TrapezoidCoef is range :[0, 32]; 7.s32FanStrength is range :[-760, 760]
* 8.enMountMode is range :[0, 2]; 9.enUseMode is range:[1, 9];
* 10.u32RegionNum is range :[1, 4]number of areas for fisheye correction
* 11.enViewMode is range :[0, 3],fisheye view mode; 12.grid_info is store infor about grid_info
*/
typedef struct _bmcv_fisheye_attr_s {
    bool bEnable;
    bool bBgColor;
    unsigned int u32BgColor;

    int s32HorOffset;
    int s32VerOffset;

    unsigned int u32TrapezoidCoef;
    int s32FanStrength;

    bmcv_fisyeye_mount_mode_e enMountMode;

    bmcv_usage_mode enUseMode;
    unsigned int u32RegionNum;
    bmcv_fisheye_view_mode_e enViewMode;
    bm_device_mem_t grid_info;
} bmcv_fisheye_attr_s;

/*
* BM_STITCH_WGT_YUV_SHARE is YUV share alpha and beta (1 alpha + 1 beta)
* BM_STITCH_WGT_UV_SHARE is UV share alpha and beta (2 alpha + 2 beta), not recommended
* BM_STITCH_WGT_SEP is separate weighting modes
*/
enum bm_stitch_wgt_mode {
    BM_STITCH_WGT_YUV_SHARE = 0,
    BM_STITCH_WGT_UV_SHARE,
    BM_STITCH_WGT_SEP,
};

/*
* ovlp_lx store overlapping region left boundary point x coordinates
* ovlp_rx store overlap area right boundary point x coordinates
*/
struct bm_stitch_src_ovlp_attr {
    short ovlp_lx[BM_STITCH_MAX_SRC_NUM -1];
    short ovlp_rx[BM_STITCH_MAX_SRC_NUM -1];
};

/*
* bd_lx store width of the black edge of the image on the left
* bd_rx store width of the black edge of the image on the right
*/
struct bm_stitch_src_bd_attr {
    short bd_lx[BM_STITCH_MAX_SRC_NUM];
    short bd_rx[BM_STITCH_MAX_SRC_NUM];
};

/*
* ovlap_attr is enter source overlap area properties
* bd_attr is left and right area of the field. param is not open, set to 0
* wgt_phy_mem is phy address and size of the weight graph
* wgt_mode is pattern of the weight graph
*/
struct stitch_param {
    struct bm_stitch_src_ovlp_attr ovlap_attr;
    struct bm_stitch_src_bd_attr bd_attr;
    bm_device_mem_t wgt_phy_mem[BM_STITCH_MAX_SRC_NUM -1][2];
    enum bm_stitch_wgt_mode wgt_mode;
};


// ive struct

#define BM_IVE_MAX_REGION_NUM       254

/*
* add attr in "xA + yB",param_x is x,param_y is y
*/
typedef struct bmcv_ive_add_attr_s {
    unsigned short param_x;
    unsigned short param_y;
} bmcv_ive_add_attr;

/*
* sub mode
* IVE_SUB_ABS is absolute value of the difference;
* IVE_SUB_SHIFT is output result is obtained by s_fting the result one digit right to reserve the signed bit
*/
typedef enum bmcv_ive_sub_mode_e{
    IVE_SUB_ABS = 0x0,
    IVE_SUB_SHIFT = 0x1,
    IVE_SUB_BUTT
} bmcv_ive_sub_mode;

/* sub attr struct */
typedef struct bmcv_ive_sub_attr_s {
    bmcv_ive_sub_mode en_mode;
} bmcv_ive_sub_attr;

/*
  IVE_THRESH_BINARY:       srcVal <= lowThr, dstVal = minVal; srcVal > lowThr, dstVal = maxVal.
  IVE_THRESH_TRUNC:        srcVal <= lowThr, dstVal = srcVal; srcVal > lowThr, dstVal = maxVal.
  IVE_THRESH_TO_MINAL:     srcVal <= lowThr, dstVal = minVal; srcVal > lowThr, dstVal = srcVal.
  IVE_THRESH_MIN_MID_MAX: srcVal <= lowThr, dstVal = minVal;  lowThr < srcVal <= _ghThr,
                          dstVal = midVal; srcVal > _ghThr, dstVal = maxVal.
  IVE_THRESH_ORI_MID_MAX: srcVal <= lowThr, dstVal = srcVal;  lowThr < srcVal <= _ghThr,
                          dstVal = midVal; srcVal > _ghThr, dstVal = maxVal.
  IVE_THRESH_MIN_MID_ORI: srcVal <= lowThr, dstVal = minVal;  lowThr < srcVal <= _ghThr,
                          dstVal = midVal; srcVal > _ghThr, dstVal = srcVal.
  IVE_THRESH_MIN_ORI_MAX: srcVal <= lowThr, dstVal = minVal;  lowThr < srcVal <= _ghThr,
                          dstVal = srcVal; srcVal > _ghThr, dstVal = maxVal.
  IVE_THRESH_ORI_MID_ORI: srcVal <= lowThr, dstVal = srcVal;  lowThr < srcVal <= _ghThr,
                          dstVal = midVal; srcVal > _ghThr, dstVal = srcVal.
*/

typedef enum bmcv_ive_thresh_mode_e{
    // u8
    IVE_THRESH_BINARY = 0x0,
    IVE_THRESH_TRUNC = 0x1,
    IVE_THRESH_TO_MINAL = 0x2,
    IVE_THRESH_MIN_MID_MAX = 0x3,
    IVE_THRESH_ORI_MID_MAX = 0x4,
    IVE_THRESH_MIN_MID_ORI = 0x5,
    IVE_THRESH_MIN_ORI_MAX = 0x6,
    IVE_THRESH_ORI_MID_ORI = 0x7,
    // s16
    IVE_THRESH_S16_TO_S8_MIN_MID_MAX = 0x8,
    IVE_THRESH_S16_TO_S8_MIN_ORI_MAX = 0x9,
    IVE_THRESH_S16_TO_U8_MIN_MID_MAX = 0x10,
    IVE_THRESH_S16_TO_U8_MIN_ORI_MAX = 0x11,
    // u16
    IVE_THRESH_U16_TO_U8_MIN_MID_MAX = 0x12,
    IVE_THRESH_U16_TO_U8_MIN_ORI_MAX = 0x13,

} bmcv_ive_thresh_mode;

/*
* thresh mode type:MOD_U8 is u8 to u8;MOD_U16 is u16 to u8;MOD_S16 is s16 to 8bit(u8 or s8)
*/
typedef enum bm_ive_thresh_op_mode_e {
    MOD_U8 = 0,
    MOD_U16,
    MOD_S16,
} bm_ive_thresh_op_mode;

/*
* thresh attr struct
* 1.low_thr is low threshold; 2.high_thr is high threshold
* 3.min_val is minimum value; 4.mid_val is median value
* 5.max_val is maximum value
*/
typedef struct bmcv_ive_thresh_attr_s {
    int low_thr;
    int high_thr;
    int min_val;
    int mid_val;
    int max_val;
} bmcv_ive_thresh_attr;

/*
* IVE_MAP_U8 is unsigned 8 bits store one piece of map data
* IVE_MAP_S16 is signed 16 bits store one piece of map data
* IVE_MAP_U16 is unsigned 16 bits store one piece of map data
* IVE_MAP_BUTT is rangle limitation
*/
typedef enum bmcv_ive_map_mode_s{
    IVE_MAP_U8 = 0x0,
    IVE_MAP_S16 = 0x1,
    IVE_MAP_U16 = 0x2,
    IVE_MAP_BUTT
} bmcv_ive_map_mode;

/*
* DMA mode
* IVE_DMA_DIRECT_COPY is copy directly from one memory space to another
* IVE_DMA_INTERVAL_COPY is copy an interval from one memory space to another
*/
typedef enum bmcv_ive_dma_mode_e {
    IVE_DMA_DIRECT_COPY = 0x0,
    IVE_DMA_INTERVAL_COPY = 0x1
} bmcv_ive_dma_mode;

/*
* IVE_DMA_SET_3BYTE is amplitude form is 3 bytes in a DMA operation
* IVE_DMA_SET_8BYTE is amplitude form is 8 bytes in a DMA operation
*/
typedef enum bmcv_ive_dma_set_mode_e {
    IVE_DMA_SET_3BYTE = 0x0,
    IVE_DMA_SET_8BYTE = 0x1
} bmcv_ive_dma_set_mode;

/*
* hor_seg_size is interval copy, src image horizon segment size
* elem_size is valid field size in each paragraph
* ver_seg_rows is vertical partition standard size
*/
typedef struct bmcv_ive_interval_dma_attr_s {
    unsigned char hor_seg_size;
    unsigned char elem_size;
    unsigned char ver_seg_rows;
} bmcv_ive_interval_dma_attr;

/*
* integ ouput mode
* IVE_INTEG_MODE_COMBINE is integral graph and sum of squares integral graph combined output
* IVE_INTEG_MODE_SUM is integral graph output
* IVE_INTEG_MODE_SQSUM is sum of squares integral graph; IVE_INTEG_MODE_BUTT is rangle limitation
*/
typedef enum _ive_integ_out_ctrl_e {
    IVE_INTEG_MODE_COMBINE = 0x0,
    IVE_INTEG_MODE_SUM = 0x1,
    IVE_INTEG_MODE_SQSUM = 0x2,
    IVE_INTEG_MODE_BUTT
} ive_integ_out_ctrl_e;

/*
* en_out_ctrl is integ output mode
*/
typedef struct bmcv_integ_ctrl_t{
    ive_integ_out_ctrl_e en_out_ctrl;
} bmcv_ive_integ_ctrl_s;

/*
* Cross-correlation parameter
* u64_numerator is sum of the product of corresponding pixels in the region
* u64_quad_sum1 is area corresponding to the sum of pixel squares
* u64_quad_sum2 is area corresponding to the sum of pixel squares
* u8_reserved is reserved field
*/
typedef struct _bmcv_ive_ncc_dst_mem_s{
    unsigned long long u64_numerator;
    unsigned long long u64_quad_sum1;
    unsigned long long u64_quad_sum2;
    unsigned char u8_reserved[8];
}bmcv_ive_ncc_dst_mem_t;

/*
* Filter mode
* BM_IVE_ORD_STAT_FILTER_MEDIAN is median filtering
* BM_IVE_ORD_STAT_FILTER_MAX is maximum filtering
* BM_IVE_ORD_STAT_FILTER_MIN is minimum filtering
*/
typedef enum _bmcv_ord_stat_filter_mode_e{
    BM_IVE_ORD_STAT_FILTER_MEDIAN = 0x0,
    BM_IVE_ORD_STAT_FILTER_MAX    = 0x1,
    BM_IVE_ORD_STAT_FILTER_MIN    = 0x2,
} bmcv_ive_ord_stat_filter_mode;

/*
* Image local feature comparison model
* BM_IVE_LBP_CMP_MODE_NORMAL is P(x)-P(center)>= un8BitThr.s8Val, s(x)=1; else s(x)=0;
* BM_IVE_LBP_CMP_MODE_ABS is Abs(P(x)-P(center))>=un8BitThr.u8Val, s(x)=1; else s(x)=0;
*/
typedef enum bmcv_lbp_cmp_mode_e{
    BM_IVE_LBP_CMP_MODE_NORMAL = 0x0,
    BM_IVE_LBP_CMP_MODE_ABS    = 0x1,
} bmcv_lbp_cmp_mode;

/*
* s8_val and u8_val are the value required by the dma amplitude mode
*/
typedef union bmcv_ive_8bit_u{
    signed char  s8_val;
    unsigned char u8_val;
} bmcv_ive_8bit;

/*
* Image local feature control parameter
* en_mode is compare mode; un8_bit_thr is LBP comparison threshold
*/
typedef struct bmcv_lbp_ctrl_attr_s{
    bmcv_lbp_cmp_mode en_mode;
    bmcv_ive_8bit     un8_bit_thr;
} bmcv_ive_lbp_ctrl_attr;

/*
* BM_IVE_MAG_AND_ANG_OUT_MAG is only magnitude output
* BM_IVE_MAG_AND_ANG_OUT_ALL magnitude and angle are output
*/
typedef enum bmcv_ive_mag_and_ang_outctrl{
    BM_IVE_MAG_AND_ANG_OUT_MAG = 0x0,
    BM_IVE_MAG_AND_ANG_OUT_ALL = 0X1,
} bmcv_ive_mag_and_ang_outctrl;

/*
* Control information for gradient amplitude and Angle calculation
* en_out_ctrl is output mode; u16_thr is amplitude thresholding threshold
* as8_mask store template paramter
*/
typedef struct bmcv_ive_mag_and_ang_ctrl_s{
    bmcv_ive_mag_and_ang_outctrl     en_out_ctrl;
    unsigned short               u16_thr;
    signed char                  as8_mask[25];
} bmcv_ive_mag_and_ang_ctrl;

/*
* sobel mode enum
* BM_IVE_SOBEL_OUT_MODE_BOTH is output horizontal and vertical
* BM_IVE_SOBEL_OUT_MODE_HOR is output horizontal
* BM_IVE_SOBEL_OUT_MODE_VER is output vertical
*/
typedef enum bmcv_ive_sobel_out_mode_e{
    BM_IVE_SOBEL_OUT_MODE_BOTH = 0x0,
    BM_IVE_SOBEL_OUT_MODE_HOR  = 0x1,
    BM_IVE_SOBEL_OUT_MODE_VER  = 0X2,
} bmcv_ive_sobel_out_mode;

/*
* sobel control parameter
* sobel_mode is output mode; as8_mask is template parameter
*/
typedef struct bmcv_ive_sobel_ctrl_s{
    bmcv_ive_sobel_out_mode sobel_mode;
    signed char as8_mask[25];
} bmcv_ive_sobel_ctrl;

/*
* gradient information output control mode
* BM_IVE_NORM_GRAD_OUT_HOR_AND_VER is H and V component gradient output
* BM_IVE_NORM_GRAD_OUT_HOR is H graphs of gradient output
* BM_IVE_NORM_GRAD_OUT_VER is V graphs of gradient output
* BM_IVE_NORM_GRAD_OUT_COMBINE is output gradient information stored in package HV plot
*/
typedef enum bmcv_ive_normgrad_outmode_e{
    BM_IVE_NORM_GRAD_OUT_HOR_AND_VER = 0x0,
    BM_IVE_NORM_GRAD_OUT_HOR         = 0x1,
    BM_IVE_NORM_GRAD_OUT_VER         = 0x2,
    BM_IVE_NORM_GRAD_OUT_COMBINE     = 0x3,
} bmcv_ive_normgrad_outmode;

/*
* Normalized gradient information calculation parameters
* en_mode is gradient information output control mode
* as8_mask is gradient computing template
* u8_norm is normalized parameters
*/
typedef struct bmcv_ive_normgrad_ctrl_s{
    bmcv_ive_normgrad_outmode en_mode;
    signed char as8_mask[25];
    unsigned char u8_norm;
} bmcv_ive_normgrad_ctrl;

/*
* u22q10_noise_var is initial noise Variance; u22q10_max_var is max Variance
* u22q10_min_var is min Variance; u0q16_learn_rate is learning rate
* u0q16_bg_ratio is background ratio; u8q8_var_thr is variance Threshold
* u0q16_init_weight is initial Weight; u8_model_num is model number: 3 or 5
*/
typedef struct bmcv_ive_gmm_ctrl_s{
    unsigned int u22q10_noise_var;
    unsigned int u22q10_max_var;
    unsigned int u22q10_min_var;
    unsigned short u0q16_learn_rate;
    unsigned short u0q16_bg_ratio;
    unsigned short u8q8_var_thr;
    unsigned short u0q16_init_weight;
    unsigned char u8_model_num;
} bmcv_ive_gmm_ctrl;

/*
* SNS_FACTOR_MODE_GLB is global sensitivity factor mode
* SNS_FACTOR_MODE_PIX is pixel sensitivity factor mode
*/
typedef enum gmm2_sns_factor_mode_e{
    SNS_FACTOR_MODE_GLB = 0x0,
    SNS_FACTOR_MODE_PIX = 0x1,
} gmm2_sns_factor_mode;

/*
* LIFE_UPDATE_FACTOR_MODE_GLB is global life update factor mode
* LIFE_UPDATE_FACTOR_MODE_PIX is pixel life update factor mode
*/
typedef enum gmm2_life_update_factor_mode_e{
    LIFE_UPDATE_FACTOR_MODE_GLB = 0x0,
    LIFE_UPDATE_FACTOR_MODE_PIX = 0x1,
} gmm2_life_update_factor_mode;

/*
* gmm2 control information structure
* en_sns_factor_mode is sensitivity factor mode;en_life_update_factor_mode is life update factor mode
* u16_glb_life_update_factor is global life update factor (default: 4)
* u16_life_thr is life threshold (default: 5000);u16_freq_init_val is initial frequency (default: 20000)
* u16_freq_redu_factor is frequency reduction factor (default: 0xFF00)
* u16_freq_add_factor is frequency adding factor (default: 0xEF)
* u16_freq_thr is frequency threshold (default: 12000); u16_var_rate is variation update rate (default: 1)
* u9q7_max_var is max variation (default: (16 * 16)<<7); u9q7_min_var is min variation (default: ( 8 *  8)<<7)
* u8_glb_sns_factor is global sensitivity factor (default: 8); u8_model_num is model number (range: 1~5, default: 3)
*/
typedef struct bmcv_ive_gmm2_ctrl_s{
    gmm2_sns_factor_mode en_sns_factor_mode;
    gmm2_life_update_factor_mode en_life_update_factor_mode;
    unsigned short u16_glb_life_update_factor;
    unsigned short u16_life_thr;
    unsigned short u16_freq_init_val;
    unsigned short u16_freq_redu_factor;
    unsigned short u16_freq_add_factor;
    unsigned short u16_freq_thr;
    unsigned short u16_var_rate;
    unsigned short u9q7_max_var;
    unsigned short u9q7_min_var;
    unsigned char u8_glb_sns_factor;
    unsigned char u8_model_num;
} bmcv_ive_gmm2_ctrl;

/*
* u16_x is horizontal coordinate of point in image
* u16_y is ordinate of point in image
*/
typedef struct bm_ive_point_u16_s{
    unsigned short u16X;
    unsigned short u16Y;
} bm_ive_point_u16;

/*
* u32_stack_size is size of the stack for edge tracking in the Canny algorithm
* u8_reserved is 12-byte reserved field
*/
typedef struct bm_ive_canny_stack_size_s{
    unsigned int u32StackSize;
    unsigned char u8Reserved[12];
} bm_ive_canny_stack_size;

/*
* The first half of the Canny edge calculates the control parameters of the task
* st_mem is auxiliary memory; u16_low_thr is low threshold, range: [0, 255]
* u16_high_thr is high threshold, range: [u16LowThr, 255]
* as8_mask is parameter template for calculating gradient
*/
typedef struct bmcv_ive_canny_hys_edge_ctrl_s{
    bm_device_mem_t  st_mem;
    unsigned short u16_low_thr;
    unsigned short u16_high_thr;
    signed char as8_mask[25];
} bmcv_ive_canny_hys_edge_ctrl;

/*
* Template filtering control information
* as8_mask is template parameter filter coefficient
* u8_norm is normalization parameter, by right s_ft
*/
typedef struct bmcv_ive_filter_ctrl_s{
    signed char as8_mask[25];
    unsigned char u8_norm;
} bmcv_ive_filter_ctrl;

/*
* Shi-Tomas-like candidate corner point calculation control parameters
* st_mem is memory allocation size of struct bm_ive_st_max_eig
* u0q8_quality_level is corner quality control parameters, range: [1, 255]
*/
typedef struct bmcv_ive_stcandicorner_attr_s{
    bm_device_mem_t st_mem;
    unsigned char   u0q8_quality_level;
} bmcv_ive_stcandicorner_attr;

/*
* Maximum corner response structure
* u16_max_eig is Maximum corner response value; u8_reserved need for 16 byte align
*/
typedef struct bmcv_ive_st_max_eig_s{
    unsigned short u16_max_eig;
    unsigned char  u8_reserved[14];
} bmcv_ive_st_max_eig;

/*
* GRAD_FG_MODE_USE_CUR_GRAD is current position gradient calculation mode
* GRAD_FG_MODE_FIND_MIN_GRAD is peripheral minimum gradient calculation model
*/
typedef enum bmcv_ive_gradfg_mode_e{
    GRAD_FG_MODE_USE_CUR_GRAD = 0x0,
    GRAD_FG_MODE_FIND_MIN_GRAD = 0x1,
} bmcv_ive_gradfg_mode;

/*
* Gradient foreground control parameters
* en_mode is calculation mode
* u16_edw_factor is edge width adjustment factor (range: 500 to 2000; default: 1000)
* u8_crl_coef_thr is gradient vector correlation coefficient threshold (ranges: 50 to 100; default: 80)
* u8_mag_crl_thr is gradient amplitude threshold (range: 0 to 20; default: 4
* u8_min_mag_diff is gradient magnitude difference threshold (range: 2 to 8; default: 2)
* u8_noise_val is gradient amplitude noise threshold (range: 1 to 8; default: 1)
* u8_edw_dark is black pixels enable flag (range: 0 (no), 1 (yes); default: 1)
*/
typedef struct bmcv_ive_gradfg_attr_s{
    bmcv_ive_gradfg_mode en_mode;
    unsigned short u16_edw_factor;
    unsigned char u8_crl_coef_thr;
    unsigned char u8_mag_crl_thr;
    unsigned char u8_min_mag_diff;
    unsigned char u8_noise_val;
    unsigned char u8_edw_dark;
} bmcv_ive_gradfg_attr;

/*
* SAD calculation mode
* BM_IVE_SAD_MODE_MB_4X4 is 4x4 pixel block calculates SAD
* BM_IVE_SAD_MODE_MB_8X8 is 8x8 pixel blocks calculate SAD
* BM_IVE_SAD_MODE_MB_16X16 is 16x16 pixel blocks calculate SAD
*/
typedef enum bmcv_ive_sad_mode_e{
    BM_IVE_SAD_MODE_MB_4X4 = 0x0,
    BM_IVE_SAD_MODE_MB_8X8 = 0x1,
    BM_IVE_SAD_MODE_MB_16X16 = 0x2,
} bmcv_ive_sad_mode;

/*
* BM_IVE_SAD_OUT_BOTH is output sad and thresh 16/8
* BM_IVE_SAD_OUT_SAD is output sad 16/8
* BM_IVE_SAD_OUT_THRESH is output 16 thresh
*/
typedef enum bmcv_ive_sad_out_ctrl_s{
    BM_IVE_SAD_OUT_BOTH = 0x0,
    BM_IVE_SAD_OUT_SAD  = 0x1,
    BM_IVE_SAD_OUT_THRESH  = 0x2,
} bmcv_ive_sad_out_ctrl;

/*
* SAD control parameter
* en_mode is SAD computing mode
* en_out_ctrl is SAD output control mode
*/
typedef struct bmcv_ive_sad_attr_s{
    bmcv_ive_sad_mode en_mode;
    bmcv_ive_sad_out_ctrl en_out_ctrl;
} bmcv_ive_sad_attr;

/*
* u16_thr is calculate threshold of the SAD graph
* srcVal <= u16Thr, dstVal = minVal; srcVal > u16Thr, dstVal = maxVal
* u8_min_val is minVal; u8_max_val is maxVal
*/
typedef struct bmcv_ive_sad_thresh_attr_s{
    unsigned short u16_thr;
    unsigned char  u8_min_val;
    unsigned char  u8_max_val;
}bmcv_ive_sad_thresh_attr;

/*
* u8q4f4_mean is 0# background grays value; u16_acc_time is ackground cumulative access time
* u8_pre_gray is gray value of last pixel; u5q3_diff_thr is differential threshold
* u8_acc_flag is background access flag; u8_bg_gray is 1# ~ 3# background grays value
*/
typedef struct bmcv_ive_work_bg_pix_s{
    unsigned short u8q4f4_mean;
    unsigned short u16_acc_time;
    unsigned char u8_pre_gray;
    unsigned char u5q3_diff_thr;
    unsigned char u8_acc_flag;
    unsigned char u8_bg_gray[3];
} bmcv_ive_work_bg_pix;

/*
* u8q4f4_mean is candidate background grays value
* u16_start_time is candidate Background start time
* u16_sum_access_time is candidate Background cumulative access time
* u16_short_keep_time is candidate background short hold time
* u8_chg_cond is time condition for candidate background into the changing state
* u8_poten_bg_life is potential background cumulative access time
*/
typedef struct bmcv_ive_candi_bg_pix_s{
    unsigned short u8q4f4_mean;
    unsigned short u16_start_time;
    unsigned short u16_sum_access_time;
    unsigned short u16_short_keep_time;
    unsigned char  u8_chg_cond;
    unsigned char  u8_poten_bg_life;
} bmcv_ive_candi_bg_pix;

/*
* u8_work_bg_life is 1# ~ 3# background vitality
* u8_candi_bg_life is candidate background vitality
*/
typedef struct bmcv_ive_bg_life_s{
    unsigned char u8_work_bg_life[3];
    unsigned char u8_candi_bg_life;
} bmcv_ive_bg_life;

/*
* st_work_bg_pixel is working background
* st_candi_pixelis is candidate background
* st_bg_life is background vitality
*/
typedef struct bmcv_ive_bg_model_pix_s{
    bmcv_ive_work_bg_pix st_work_bg_pixel;
    bmcv_ive_candi_bg_pix st_candi_pixel;
    bmcv_ive_bg_life st_bg_life;
} bmcv_ive_bg_model_pix;

/*
* Define background status data
* u32_pix_num is number of foreground pixels; u32_sum_lum is sum of all pixel
* u8_reserved is reserve the field
*/
typedef struct bmcv_ive_bg_stat_data_s{
    unsigned int u32_pix_num;
    unsigned int u32_sum_lum;
    unsigned char u8_reserved[8];
} bmcv_ive_bg_stat_data;

typedef struct bmcv_ive_match_bgmodel_attr_s{
    unsigned int u32_cur_frm_num;  // Current frame timestamp, in frame units
    unsigned int u32_pre_frm_num;  // Previous frame timestamp, in frame units
    unsigned short u16_time_thr;  // Potential background replacement time threshold (range: 2 to 100 frames; default: 20)

    /*
     * Correlation coefficients between differential threshold and gray value
     * (range: 0 to 5; default: 0)
     */
    unsigned char u8_diff_thr_crl_coef;
    unsigned char u8_diff_max_thr; // Maximum of background differential threshold (range: 3 to 15; default: 6)
    unsigned char u8_diff_min_thr; // Minimum of background differential threshold (range: 3 to 15; default: 4)
    unsigned char u8_diff_thr_inc; // Dynamic Background differential threshold increment (range: 0 to 6; default: 0)
    unsigned char u8_fast_learn_rate; // Quick background learning rate (range: 0 to 4; default: 2)
    unsigned char u8_det_chg_region;   // Whether to detect change region (range: 0 (no), 1 (yes); default: 0)
} bmcv_ive_match_bgmodel_attr;

typedef struct bmcv_ive_update_bgmodel_attr_s{
    unsigned int u32_cur_frm_num;    // Current frame timestamp, in frame units
    unsigned int u32_pre_chk_time;   // The last time when background status is checked
    unsigned int u32_frm_chk_period; // Background status checking period (range: 0 to 2000 frames; default: 50)
    unsigned int u32_init_min_time;  // Background initialization shortest time (range: 20 to 6000 frames; default: 100)
    /*
     * Steady background integration shortest time
     * (range: 20 to 6000 frames; default: 200)
     */
    unsigned int u32_sty_bg_min_blend_time;
    /*
     * Steady background integration longest time
     * (range: 20 to 40000 frames; default: 1500)
     */
    unsigned int u32_sty_bg_max_blend_time;
    /*
     * Dynamic background integration shortest time
     * (range: 0 to 6000 frames; default: 0)
     */

    unsigned int u32_dyn_bg_min_blend_time;
    unsigned int u32_static_det_min_time; // Still detection shortest time (range: 20 to 6000 frames; default: 80)
    unsigned short u16_fg_max_fade_time;  // Foreground disappearing longest time (range: 1 to 255 seconds; default: 15)
    unsigned short u16_bg_max_fade_time;  // Background disappearing longest time (range: 1 to 255  seconds ; default: 60)

    unsigned char u8_sty_bg_acc_time_rate_thr; // Steady background access time ratio threshold (range: 10 to 100; default: 80)
    unsigned char u8_chg_bg_acc_time_rate_thr; // Change background access time ratio threshold (range: 10 to 100; default: 60)
    unsigned char u8_dyn_bg_acc_time_thr;     // Dynamic background access time ratio threshold (range: 0 to 50; default: 0)
    unsigned char u8_dyn_bg_depth;          // Dynamic background depth (range: 0 to 3; default: 3)

    /*
     * Background state time ratio threshold when initializing
     * (range: 90 to 100; default: 90)
     */
    unsigned char u8_bg_eff_sta_rate_thr;
    unsigned char u8_acce_bg_learn;  // Whether to accelerate background learning (range: 0 (no), 1 (yes); default: 0)
    unsigned char u8_det_chg_region; // Whether to detect change region (range: 0 (no), 1 (yes); default: 0)
} bmcv_ive_update_bgmodel_attr;

/*
* The size of the connected area,BM_IVE_CCL_MODE_4C is 4-connected;BM_IVE_CCL_MODE_8C is 8-connected
*/
typedef enum bmcv_ive_ccl_mode_e{
    BM_IVE_CCL_MODE_4C = 0x0,
    BM_IVE_CCL_MODE_8C = 0x1,
} bmcv_ive_ccl_mode;

/*
* en_mode is ccl mode; u16_init_area_thr is init threshold of region area;
* u16_step is increase area step for once
*/
typedef struct bmcv_ive_ccl_attr_s{
    bmcv_ive_ccl_mode en_mode;
    unsigned short  u16_init_area_thr;
    unsigned short  u16_step;
} bmcv_ive_ccl_attr;

/*
* u32_area is represented by the pixel number;
* u16_left is circumscribed rectangle left border;u16_right is circumscribed rectangle right border
* u16_top is circumscribed rectangle top border;u16_bottom is circumscribed rectangle bottom border
*/
typedef struct bmcv_ive_region_s{
    int u32_area;
    unsigned short u16_left;
    unsigned short u16_right;
    unsigned short u16_top;
    unsigned short u16_bottom;
} bmcv_ive_region;

/*
* u16_cur_aera_thr is threshold of the result regions area
* s8_label_status is -1: Labeled failed ; 0: Labeled successfully
* u8_region_num is Number of valid region, non-continuous stored
* ast_region is valid regions with 'u32Area>0' and 'label = ArrayIndex+1
*/
typedef struct bmcv_ive_ccblob_s{
    unsigned short    u16_cur_aera_thr;
    signed char       s8_label_status;
    unsigned char     u8_region_num;
    bmcv_ive_region   ast_region[BM_IVE_MAX_REGION_NUM];
} bmcv_ive_ccblob;

/*
* BM_IVE_BERNSEN_NORMAL is simple Bernsen thresh;
* BM_IVE_BERNSEN_THRESH is thresh based on the global threshold and local Bernsen threshold
* BM_IVE_BERNSEN_PAPER is same with original paper method
*/
typedef enum bmcv_ive_bernsen_mode_e{
    BM_IVE_BERNSEN_NORMAL = 0x0,
    BM_IVE_BERNSEN_THRESH = 0x1,
    BM_IVE_BERNSEN_PAPER = 0x2,
} bmcv_ive_bernsen_mode;

/*
* en_mode is Bernsen algorithm for image binarization;
* u8_win_size is 3x3 or 5x5 window;u8_thr is threshold value; u8_contrast_threshold is compare with midgray
*/
typedef struct bmcv_ive_bernsen_attr_s{
    bmcv_ive_bernsen_mode en_mode;
    unsigned char u8_win_size;
    unsigned char u8_thr;
    unsigned char u8_contrast_threshold;
} bmcv_ive_bernsen_attr;

/*
* BM_IVE_S16_TO_S8 is S16 data to S8 data;
* BM_IVE_S16_TO_U8_ABS is absolute value of the linear change from S16 data to S8 data
* BM_IVE_S16_TO_U8_BIAS is S16 data is linearly changed to S8 data and truncated
* BM_IVE_U16_TO_U8 is U16 data changes linearly to U8 data
*/
typedef enum bmcv_ive_16bit_to_8bit_mode_e{
    BM_IVE_S16_TO_S8 = 0x0,
    BM_IVE_S16_TO_U8_ABS = 0x1,
    BM_IVE_S16_TO_U8_BIAS = 0x2,
    BM_IVE_U16_TO_U8 = 0x3,
} bmcv_ive_16bit_to_8bit_mode;

/*
* 16bit image data to 8bit image data conversion control parameters
* mode is conversion mode; u16_denominator is denominator, value range: {max{1, u8Numerator}, 65535}
* u8_numerator is molecules, value range: [0, 255]; s8_bias is translation term, range: [-128, 127]
*/
typedef struct bmcv_ive_16bit_to_8bit_attr_s{
    bmcv_ive_16bit_to_8bit_mode mode;
    unsigned short u16_denominator;
    unsigned char u8_numerator;
    signed char   s8_bias;
} bmcv_ive_16bit_to_8bit_attr;

/*
* Control information:
* sub_mode is sub mode; thr_mode support only MOD_U8 mode;
* u8_thr_low is low threshold, range: [0, 255];u8_thr_high is high threshold,0 ≤ u8ThrLow ≤ u8ThrHigh ≤ 255
* u8_thr_min_val is minimum value,range: [0, 255]; u8_thr_mid_val is middle value,range: [0, 255]
* u8_thr_max_val is range: [0, 255];au8_erode_mask is 5x5 template coefficient of erode.range: 0 or 255
* au8_dilate_mask is 5x5 template coefficient of dilate.range: 0 or 255
*/
typedef struct bmcv_ive_frame_diff_motion_attr_s{
    bmcv_ive_sub_mode sub_mode;
    bmcv_ive_thresh_mode thr_mode;
    unsigned char u8_thr_low;
    unsigned char u8_thr_high;
    unsigned char u8_thr_min_val;
    unsigned char u8_thr_mid_val;
    unsigned char u8_thr_max_val;
    unsigned char au8_erode_mask[25];
    unsigned char au8_dilate_mask[25];
} bmcv_ive_frame_diff_motion_attr;



// common api

/**
 * @brief Update the bm_image structure with image information.
 *
 * This function updates the bm_image structure with the provided image information,
 * including width, height, image format, data type, memory_layout and handle. It performs error
 * checks on the image format and size, as well as the stride, before updating the
 * image_private handle with the given handle.
 *
 * @param handle The handle associated with the image.
 * @param img_h The height of the image.
 * @param img_w The width of the image.
 * @param image_format The image format.
 * @param data_type The data type of the image.
 * @param res Pointer to the bm_image structure to be updated.
 * @param stride Pointer to the stride value.
 * @return BM_SUCCESS if the update is successful, otherwise an error code.
 */
bm_status_t bm_image_update(bm_handle_t handle,
                            int img_h,
                            int img_w,
                            bm_image_format_ext image_format,
                            bm_image_data_format_ext data_type,
                            bm_image *res,
                            int *stride);

/**
 * @brief Get the size of struct bm_image_private in bytes.
 *
 * This function calculates and returns the size in bytes of the struct bm_image_private.
 *
 * @return The size of struct bm_image_private in bytes.
 */
size_t bmcv_get_private_size(void);

/** bm_image_create_private
 * @brief Create and fill bm_image structure
 * @param [in] handle   The bm handle which return by bm_dev_request
 * @param [in] img_h    The height or rows of the creating image
 * @param [in] img_w    The width or cols of the creating image.
 * @param [in] image_format  The image_format of the creating image,
 *  please choose one from bm_image_format_ext enum.
 * @param [in] data_type The data_type of the creating image,
 *  be caution that not all combinations between image_format and data_type
 *  are supported.
 * @param [in] stride    the stride array for planes, each
 * number in array means corresponding plane pitch stride in bytes. The plane
 * size is determinated by image_format. If this array is null, we may use
 * default value.
 * @param [in] bm_private   A pointer to the bm_image_private structure to
 * be initialized.
 * @param [out] res       The filled bm_image structure.
 * For example, we need create a 480x480 NV12 format image, we know that NV12
 * format has 2 planes, we need pitch stride is 256 aligned(just for example) so
 * the pitch stride for the first plane is 512, so as the same for the second
 * plane.
 * The call may as following
 * bm_image res;
 *  int stride[] = {512, 512};
 *  bm_image_create_private(handle, 480, 480, FORMAT_NV12, DATA_TYPE_EXT_1N_BYTE, &res,
 * stride, bm_private); If bm_image_create return BM_SUCCESS, res is created successfully.
 */
bm_status_t bm_image_create_private(bm_handle_t              handle,
                                    int                      img_h,
                                    int                      img_w,
                                    bm_image_format_ext      image_format,
                                    bm_image_data_format_ext data_type,
                                    bm_image *               res,
                                    int *                    stride,
                                    void*                    bm_private);

/**
 * The data in the address requested by bm_image is cleared before use
*/
DECL_EXPORT bm_status_t bm_image_zeros(bm_image image);

DECL_EXPORT unsigned long long bmcv_calc_cbcr_addr(unsigned long long y_addr, unsigned int y_stride, unsigned int frame_height);

/**
 * If $BMCV_PRINT_VERSION == 1, this function prints the BMCV version information
 * and TPU firmware version information. Otherwise, the function does nothing.
 */
DECL_EXPORT void bmcv_print_version();

/** bm_image_create
 * @brief Create and fill bm_image structure
 * @param [in] handle                     The bm handle which return by
 * bm_dev_request.
 * @param [in] img_h                      The height or rows of the creating
 * image.
 * @param [in] img_w                     The width or cols of the creating
 * image.
 * @param [in] image_format      The image_format of the creating image,
 *  please choose one from bm_image_format_ext enum.
 * @param [in] data_type               The data_type of the creating image,
 *  be caution that not all combinations between image_format and data_type
 *  are supported.
 * @param [in] stride                        the stride array for planes, each
 * number in array means corresponding plane pitch stride in bytes. The plane
 * size is determinated by image_format. If this array is null, we may use
 * default value.
 *  @param [out] image                   The filled bm_image structure.
 *  For example, we need create a 480x480 NV12 format image, we know that NV12
 * format has 2 planes, we need pitch stride is 256 aligned(just for example) so
 * the pitch stride for the first plane is 512, so as the same for the second
 * plane.
 * The call may as following
 * bm_image res;
 *  int stride[] = {512, 512};
 *  bm_image_create(handle, 480, 480, FORMAT_NV12, DATA_TYPE_EXT_1N_BYTE, &res,
 * stride); If bm_image_create return BM_SUCCESS, res is created successfully.
 */
DECL_EXPORT bm_status_t bm_image_create(
    bm_handle_t              handle,
    int                      img_h,
    int                      img_w,
    bm_image_format_ext      image_format,
    bm_image_data_format_ext data_type,
    bm_image *               image,
    int *                    stride = NULL);

/** bm_image_destroy
 * @brief Destroy bm_image and free the corresponding system memory and device
 * memory.
 * @param [in] image                     The bm_image structure ready to
 * destroy. If bm_image_destroy return BM_SUCCESS, image is destroy successfully
 * and the corresponding system memory and device memory are freed.
 */
DECL_EXPORT bm_status_t bm_image_destroy(bm_image *image);
/*
 * Get the handle from bm_image
 */
DECL_EXPORT bm_handle_t bm_image_get_handle(bm_image *image);
/*
 * The API copies the host data to the device memory corresponding to the bm_image structure
 */
DECL_EXPORT bm_status_t bm_image_copy_host_to_device(bm_image image, void *buffers[]);
/*
 * The API copies the device data to the host memory corresponding to buffers
 */
DECL_EXPORT bm_status_t bm_image_copy_device_to_host(bm_image image, void *buffers[]);
/*
 * If the user wants to manage the device memory themselves
 * or if the device memory is generated by an external component (VPU/VPP, etc.)
 * the following API can be called to associate the device memory with bm_image
 */
DECL_EXPORT bm_status_t bm_image_attach(bm_image image, bm_device_mem_t *device_memory);
/*
 * This API is used to disassociate device memory from bm_image
 */
DECL_EXPORT bm_status_t bm_image_detach(bm_image image);
/*
 * This interface is used to determine whether the target has attached storage space
 * The memory requested by bm_image calling bm_image_alloc_dev_mem is automatically managed internally.
 * It is automatically released when bm_image_destroy, bm_image_detach,
 * or bm_image_attach other device memory is called. In contrast,
 * if bm_image_attach a device memory, that memory will be managed by the caller.
 * bm_image_destroy, bm_image_detach, or call bm_image_attach to other device memory will not be released,
 * and the caller needs to release it manually.
 */
DECL_EXPORT bool        bm_image_is_attached(bm_image);
/*
 * This interface is used to obtain the number of planes of the target bm_image
 */
DECL_EXPORT int         bm_image_get_plane_num(bm_image);
/*
 * This interface is used to obtain the stride information of the target bm_image
 */
DECL_EXPORT bm_status_t bm_image_get_stride(bm_image image, int *stride);

/* This interface is used to get some information about bm_image */
DECL_EXPORT bm_status_t bm_image_get_format_info(bm_image *image, bm_image_format_info_t *info);

/* The API applies for internal management memory for bm_image object.
  The requested device memory size is the sum of the device memory size required by each plane.
  The plane_byte_size calculation method is described in bm_image_copy_host_to_device,
  or confirmed by calling the bm_image_get_byte_size API.*/
DECL_EXPORT bm_status_t bm_image_alloc_dev_mem(bm_image image, int heap_id = BMCV_HEAP_ANY);
DECL_EXPORT bm_status_t bm_image_alloc_dev_mem_heap_mask(bm_image image, int heap_mask);

/* Gets the size of each plane byte of the bm_image object */
DECL_EXPORT bm_status_t bm_image_get_byte_size(bm_image image, int *size);

/* Gets the device memory of each plane of the bm_image object */
DECL_EXPORT bm_status_t bm_image_get_device_mem(bm_image image, bm_device_mem_t *mem);

/* Allocate contiguous memory for multiple images */
DECL_EXPORT bm_status_t bm_image_alloc_contiguous_mem(int image_num, bm_image *images, int heap_id = BMCV_HEAP_ANY);

/* Allocates contiguous memory on the specified heap for multiple images */
DECL_EXPORT bm_status_t bm_image_alloc_contiguous_mem_heap_mask(int image_num, bm_image *images, int heap_mask);

/* Frees contiguous memory in multiple images assigned by bm_image_alloc_contiguous_mem */
DECL_EXPORT bm_status_t bm_image_free_contiguous_mem(int image_num, bm_image *images);

/* attach a contiguous memory to multiple images */
DECL_EXPORT bm_status_t bm_image_attach_contiguous_mem(int image_num, bm_image *images, bm_device_mem_t dmem);

/* detach a contiguous block of memory from multiple images */
DECL_EXPORT bm_status_t bm_image_detach_contiguous_mem(int image_num, bm_image *images);

/* Obtain device memory information of consecutive memory from successive images of multiple memory */
DECL_EXPORT bm_status_t bm_image_get_contiguous_device_mem(int image_num, bm_image *images, bm_device_mem_t *mem);

DECL_EXPORT bm_status_t bm_image_write_to_bmp(bm_image image, const char *filename);

/* Read information from a file into the bm_image data structure */
DECL_EXPORT void bm_read_bin(bm_image src, const char *input_name);
/* Insert the data from the bm_image structure into the file */
DECL_EXPORT void bm_write_bin(bm_image dst, const char *output_name);
/* input image Width align for output image */
DECL_EXPORT bm_status_t bmcv_width_align(bm_handle_t handle, bm_image input, bm_image output);

bm_status_t bm_image_tensor_get_device_mem(bm_image_tensor  image_tensor,
                                           bm_device_mem_t *mem);
bool bm_image_tensor_is_attached(bm_image_tensor image_tensor);

bm_status_t bm_image_tensor_destroy(bm_image_tensor image_tensor);

bm_status_t bm_image_tensor_alloc_dev_mem(bm_image_tensor image_tensor,
                                          int             heap_id);

bm_status_t concat_images_to_tensor(bm_handle_t      handle,
                                    int              image_num,
                                    bm_image *       images,
                                    bm_image_tensor *image_tensor);

DECL_EXPORT void algorithm_to_str(bmcv_resize_algorithm algorithm, char* res);

DECL_EXPORT void format_to_str(bm_image_format_ext format, char* res);

DECL_EXPORT int is_csc_yuv_or_rgb(bm_image_format_ext format);


// vpp/tpu/jpu api
/*
* The interface realize the linear change of image pixels;Parameters are as follows
* handle; input_num is input image num,if input_num > 1，it needs continuous data storage
* convert_to_attr is config parameters for each image; input is input bm_image; output is output bm_image
*/
DECL_EXPORT bm_status_t bmcv_image_convert_to(
    bm_handle_t          handle,
    int                  input_num,
    bmcv_convert_to_attr convert_to_attr,
    bm_image *           input,
    bm_image *           output);

/*
* The interface realize Image format conversion,support crop + resize function,
* support from one input crop multiple output and resize to output picture size.Parameters are as follows
* handle; output_num is output num; input is input bm_image; output is output bm_image pointer;
* crop_rect is each output bm_image object corresponds to the crop parameter on the input image
* algorithm is resize algorithm selection
*/
DECL_EXPORT bm_status_t bmcv_image_vpp_convert(
    bm_handle_t           handle,
    int                   output_num,
    bm_image              input,
    bm_image *            output,
    bmcv_rect_t *         crop_rect = NULL,
    bmcv_resize_algorithm algorithm = BMCV_INTER_LINEAR);

/*
* The interface realize the combination of crop, color-space-convert, resize, padding and any number of functions for multiple pictures
* in_img_num is input bm_image num; input is input bm_image pointer; output is output image pointer;crop_num_vec is crop quantity per input picture
* crop_rect is output bm_image object information to the crop parameter on the input image
* padding_attr is all crop image infor and padding infor pointer; algorithm is resize algorithm; csc_type is color space convert Specifies the parameter type
* matrix is that CSC_USER_DEFINED_MATRIX is selected, the coefficient matrix needs to be passed.
*/
DECL_EXPORT bm_status_t bmcv_image_vpp_basic(
    bm_handle_t           handle,
    int                   in_img_num,
    bm_image*             input,
    bm_image*             output,
    int*                  crop_num_vec = NULL,
    bmcv_rect_t*          crop_rect = NULL,
    bmcv_padding_attr_t*  padding_attr = NULL,
    bmcv_resize_algorithm algorithm = BMCV_INTER_LINEAR,
    csc_type_t            csc_type = CSC_MAX_ENUM,
    csc_matrix_t*         matrix = NULL);

/*
* Interface parameter description:
* output_num is output num;input is input bm_image;output is output bm_image pointer;csc is gamut conversion enumeration type
* matrix is color gamut conversion custom matrix;algorithm is resize algorithm selection;
* crop_rect is crop infor in src bm_image
*/
DECL_EXPORT bm_status_t bmcv_image_vpp_csc_matrix_convert(
    bm_handle_t           handle,
    int                   output_num,
    bm_image              input,
    bm_image*             output,
    csc_type_t            csc,
    csc_matrix_t*         matrix = NULL,
    bmcv_resize_algorithm algorithm = BMCV_INTER_LINEAR,
    bmcv_rect_t*          crop_rect = NULL);

/*
* Interface parameter description:
* image_num is input or output image; input_ is input bm_image; output_ is output image pointer;
*/
DECL_EXPORT bm_status_t bmcv_image_storage_convert(
    bm_handle_t      handle,
    int              image_num,
    bm_image*        input_,
    bm_image*        output_);

/*
* Interface parameter description:
* image_num is image number; input_ is input image pointer; output_ is output image pointer;
* csc_type is csc type;
*/
DECL_EXPORT bm_status_t bmcv_image_storage_convert_with_csctype(
    bm_handle_t      handle,
    int              image_num,
    bm_image*        input_,
    bm_image*        output_,
    csc_type_t       csc_type);

/*
* Interface parameter description:
* output_num is output bm_image num, it is same num with crop num of src image
* input is input bm_image; output is output bm_image; padding_attr is bmcv_rect_t pointer
* algorithm is resize algorithm
*/
DECL_EXPORT bm_status_t bmcv_image_vpp_convert_padding(
    bm_handle_t             handle,
    int                     output_num,
    bm_image                input,
    bm_image*               output,
    bmcv_padding_attr_t*    padding_attr,
    bmcv_rect_t*            crop_rect = NULL,
    bmcv_resize_algorithm   algorithm = BMCV_INTER_LINEAR);

/**
 * Image size changes, such as zoom in, zoom out, matting and other functions.
 * input_num is src num;resize_attr is resize parameter for each image
 * input is src image;output is resize dst image
*/
DECL_EXPORT bm_status_t bmcv_image_resize(
    bm_handle_t          handle,
    int                  input_num,
    bmcv_resize_image    resize_attr[],
    bm_image *           input,
    bm_image *           output);

/*
* Interface parameter description:
* image is which you want to draw a rectangular box
* rect_num is number of rectangles;rects is rectangle box object pointer;
* line_width is rectangular frame line width; r,g, and b are the rectangular box colors
*/
DECL_EXPORT bm_status_t bmcv_image_draw_rectangle(
    bm_handle_t   handle,
    bm_image      image,
    int           rect_num,
    bmcv_rect_t * rects,
    int           line_width,
    unsigned char r,
    unsigned char g,
    unsigned char b);

/*
* This interface realize the combination of crop, color-space-convert, resize, padding, convert_to for multiple pictures
* img_num is bm_image num; input is input bm_image pointer;output is output bm_image pointer; crop_num_vec is each input image with crop num
* crop_rect is output image crop attr in input_image; padding_attr is crop infor in dst image and padding value;
* algorithm is resize algorithm selection; csc_type is color space convert type; matrix is CSC_USER_DEFINED_MATRIX with coefficient matrix
* convert_to_attr is linear transformation coefficient
*/
DECL_EXPORT bm_status_t bmcv_image_csc_convert_to(
    bm_handle_t             handle,
    int                     img_num,
    bm_image*               input,
    bm_image*               output,
    int*                    crop_num_vec,
    bmcv_rect_t*            crop_rect,
    bmcv_padding_attr_t*    padding_attr,
    bmcv_resize_algorithm   algorithm,
    csc_type_t              csc_type,
    csc_matrix_t*           matrix,
    bmcv_convert_to_attr*   convert_to_attr);

/*
* The interface implements a copy of an image to the corresponding memory area of the destination image
* copy_to_attr is attribute configuration; input is input bm_image; output is output bm_image
*/
DECL_EXPORT bm_status_t bmcv_image_copy_to(
    bm_handle_t         handle,
    bmcv_copy_to_atrr_t copy_to_attr,
    bm_image            input,
    bm_image            output);

/*
* Using the crop function with hardware resources to do image stitching,src crop + csc + resize + dst crop at one time can be compleled
* input_num is input image num;input is input image pointer;output is output image;dst_crop_rect is coordinates,width and height in dst image
* src_crop_rect is coordinates,width and height of dst in src image;algorithm is resize algorithm;
*/
DECL_EXPORT bm_status_t bmcv_image_vpp_stitch(
    bm_handle_t           handle,
    int                   input_num,
    bm_image*             input,
    bm_image              output,
    bmcv_rect_t*          dst_crop_rect,
    bmcv_rect_t*          src_crop_rect = NULL,
    bmcv_resize_algorithm algorithm = BMCV_INTER_LINEAR);

/*
* The interface image is printed with one or more mosaics
* mosaic_num is Mosaic num; input is need to Mosaic the bm_image
* mosaic_rect is contains Pointers to the starting point and width and height of each Mosaic
* is_expand is column expansion or not
*/
DECL_EXPORT bm_status_t bmcv_image_mosaic(
    bm_handle_t           handle,
    int                   mosaic_num,
    bm_image              input,
    bmcv_rect_t *         mosaic_rect,
    int                   is_expand);

/*
* The interface image is filled with one or more rectangles
* image needs to be drawn as a rectangular box;rect_num is number of rectangles
* rects is an output pointer containing a rectangular starting point and width and height
* Rectangle color of r,g,b
*/
DECL_EXPORT bm_status_t bmcv_image_fill_rectangle(
    bm_handle_t             handle,
    bm_image                image,
    int                     rect_num,
    bmcv_rect_t *           rects,
    unsigned char           r,
    unsigned char           g,
    unsigned char           b);

/**
 * Used to overlay one or more watermarks on an image
 * image is src;bitmap_mem is Watermark data pointer;bitmap_num is Watermark num
 * bitmap_type is Watermark type;pitch is Watermark width;rects is Watermark position pointer
 * color is Watermark color
*/
DECL_EXPORT bm_status_t bmcv_image_watermark_superpose(
    bm_handle_t           handle,
    bm_image *            image,
    bm_device_mem_t *     bitmap_mem,
    int                   bitmap_num,
    int                   bitmap_type,
    int                   pitch,
    bmcv_rect_t *         rects,
    bmcv_color_t          color);

/**
 * Used to overlay one or more watermarks on an image
 * image is src;bitmap_mem is Watermark data pointer;bitmap_num is Watermark num
 * bitmap_type is Watermark type;pitch is Watermark width;rects is Watermark position pointer
 * color is Watermark color
*/
DECL_EXPORT bm_status_t bmcv_image_watermark_repeat_superpose(
    bm_handle_t         handle,
    bm_image            image,
    bm_device_mem_t     bitmap_mem,
    int                 bitmap_num,
    int                 bitmap_type,
    int                 pitch,
    bmcv_rect_t *       rects,
    bmcv_color_t        color);

DECL_EXPORT bm_status_t bmcv_image_overlay(
    bm_handle_t         handle,
    bm_image            image,
    int                 overlay_num,
    bmcv_rect_t*        overlay_info,
    bm_image*           overlay_image);

DECL_EXPORT bm_status_t bmcv_gen_text_watermark(
    bm_handle_t         handle,
    const wchar_t*      hexcode,
    bmcv_color_t        color,
    float               fontscale,
    bm_image_format_ext format,
    bm_image*           output);

/*
* The input image is flipped horizontally, flipped vertically, or rotated 180 degrees
* input is input image; output is output image;flip_mode is flip mode;
*/
DECL_EXPORT bm_status_t bmcv_image_flip(
    bm_handle_t          handle,
    bm_image             input,
    bm_image             output,
    bmcv_flip_mode       flip_mode);

// quality_factor = 84
/*
* JPEG encoding process for multiple BM_images
* image_num is input image num;src is input image;p_jpeg_data is output image;
* out_size is size of the output picture (in bytes);quality_factor is quality factor of the encoded image
*/
DECL_EXPORT bm_status_t bmcv_image_jpeg_enc(
    bm_handle_t handle,
    int         image_num,
    bm_image *  src,
    void **     p_jpeg_data,
    size_t *    out_size,
    int         quality_factor = 85);

/*
* JPEG decoding of multiple images
* p_jpeg_data is pointer to picture data to be decoded;in_size is size (in byte) of each image to be decoded;
* image_num is input image num,num < 4;dst is pointer to bm_image
*/
DECL_EXPORT bm_status_t bmcv_image_jpeg_dec(
    bm_handle_t handle,
    void **     p_jpeg_data,
    size_t *    in_size,
    int         image_num,
    bm_image *  dst);

/*
* Two pictures of the same size correspond to the pixel values and take the absolute value
* input1 intput2 is the input image; output is output image;
*/
DECL_EXPORT bm_status_t bmcv_image_absdiff(
    bm_handle_t handle,
    bm_image input1,
    bm_image input2,
    bm_image output);

/*
* The interface does F = A * X + Y, A is a constant and size is n * c, and F, X, and Y are matrices of size n * c * h * w.
* tensor_A is Memory address of the device of A;tensor_X is Device memory address of matrix X;
* tensor_Y is Device memory address of matrix Y;tensor_F is Device memory address of matrix F;
* input_n is size of n; input_c is size of c; input_h is size of h; input is size of w;
*/
DECL_EXPORT bm_status_t bmcv_image_axpy(
    bm_handle_t handle,
    bm_device_mem_t tensor_A,
    bm_device_mem_t tensor_X,
    bm_device_mem_t tensor_Y,
    bm_device_mem_t tensor_F,
    int input_n,
    int input_c,
    int input_h,
    int input_w);

/*
* A weighted fusion of two images of the same size
* input1 is input image1;input2 is input image2
* alpha is weight of the 1st image;beta is weight of the 2nd image
* gamma is offset after fusion;output is output bm_image
*/
DECL_EXPORT bm_status_t bmcv_image_add_weighted(
    bm_handle_t handle,
    bm_image input1,
    float alpha,
    bm_image input2,
    float beta,
    float gamma,
    bm_image output);

/*
* Image pixel values are bitwise and manipulated
* input1 input2 is 1st and 2nd image;output is output image;
*/
DECL_EXPORT bm_status_t bmcv_image_bitwise_and(
    bm_handle_t handle,
    bm_image input1,
    bm_image input2,
    bm_image output);

/*
* Pixel values are bitwise or manipulated
* input1 input2 is 1st and 2nd image;output is output image;
*/
DECL_EXPORT bm_status_t bmcv_image_bitwise_or(
    bm_handle_t handle,
    bm_image input1,
    bm_image input2,
    bm_image output);

/*
* The pixel value is operated on a bit-by-bit basis
* input1 input2 is 1st and 2nd image;output is output image;
*/
DECL_EXPORT bm_status_t bmcv_image_bitwise_xor(
    bm_handle_t handle,
    bm_image input1,
    bm_image input2,
    bm_image output);

DECL_EXPORT bm_status_t bmcv_image_rotate_trans(
    bm_handle_t handle,
    bm_image input,
    bm_image output,
    int rotation_angle);

DECL_EXPORT bm_status_t bmcv_image_rotate(
    bm_handle_t handle,
    bm_image input,
    bm_image output,
    int rotation_angle);

/**
 * Edge detection Sobel operator
 * input is src image;output is dst image;
 * dx is difference order in the x direction;dy is difference order in the y direction
 * ksize is size of the Sobel nucleus;scale is resulting difference is multiplied by the coefficient
 * delta is offset
*/
DECL_EXPORT bm_status_t bmcv_image_sobel(
    bm_handle_t handle,
    bm_image input,
    bm_image output,
    int dx,
    int dy,
    int ksize = 3,
    float scale = 1,
    float delta = 0);

/*
* The image is operated by Gaussian filtering
* input is input image; output is output image;kw is kernel width size;
* kh is kernel height size;sigmaX is Gaussian kernel standard deviation in the X direction
* sigmaY is Gaussian kernel standard deviation in the Y direction
*/
DECL_EXPORT bm_status_t bmcv_image_gaussian_blur(
    bm_handle_t handle,
    bm_image input,
    bm_image output,
    int kw,
    int kh,
    float sigmaX,
    float sigmaY = 0);

/*
* Hamming distance of each element in two vectors
* input1 is Address information for vector 1 data; intput2 is Address information for vector 2 data
* output is output Indicates the address of the vector data;bits_len is length of each vector element
* input1_num is number of data in vector 1;input2_num is number of data in vector 2;
*/
DECL_EXPORT bm_status_t bmcv_hamming_distance(
    bm_handle_t handle,
    bm_device_mem_t input1,
    bm_device_mem_t input2,
    bm_device_mem_t output,
    int bits_len,
    int input1_num,
    int input2_num);

/*
* Sorting of floating-point data (ascending/descending)
* src_index_addr is enter the address of the index corresponding to the data
* src_data_addr is address of the input data to be sorted
* dst_index_addr is address of the sorted output data index
* dst_data_addr is address of the sorted output data
* data_cnt is amount of input data to be sorted;sort_cnt is number that needs to be sorted
* order is Ascending or descending;index_enable is Whether to enable index;
* auto_index isWhether to enable the automatic index generation function;
*/
DECL_EXPORT bm_status_t bmcv_sort(
    bm_handle_t     handle,
    bm_device_mem_t src_index_addr,
    bm_device_mem_t src_data_addr,
    int             data_cnt,
    bm_device_mem_t dst_index_addr,
    bm_device_mem_t dst_data_addr,
    int             sort_cnt,
    int             order,
    bool            index_enable,
    bool            auto_index);

/*
* Image pixel threshold operation
* input is input image; output is output image;thresh is Pixel threshold,range [0,255]
* max_value is maximum pixel value after thresholding;type is thresholding type,range[0,4]
*/
DECL_EXPORT bm_status_t bmcv_image_threshold(
    bm_handle_t handle,
    bm_image input,
    bm_image output,
    unsigned char thresh,
    unsigned char max_value,
    bm_thresh_type_t type);

/*
* Convert float data to int
* input is input image; output is output image
*/
DECL_EXPORT bm_status_t bmcv_image_quantify(
    bm_handle_t handle,
    bm_image input,
    bm_image output);

/**
 * @brief: calculate inner product distance between query vectors and database vectors, output the top K IP-values and the corresponding indices, return BM_SUCCESS if succeed.
 * @param handle                               [in]: the device handle.
 * @param input_data_global_addr               [in]: device addr information of the query matrix.
 * @param db_data_global_addr                  [in]: device addr information of the database matrix.
 * @param buffer_global_addr                   [in]: inner product values stored in the buffer.
 * @param output_sorted_similarity_global_addr [out]: the IP-values matrix.
 * @param output_sorted_index_global_addr      [out]: the result indices matrix.
 * @param vec_dims          [in]: vector dimension.
 * @param query_vecs_num    [in]: the num of query vectors.
 * @param database_vecs_num [in]: the num of database vectors.
 * @param sort_cnt          [in]: get top sort_cnt values.
 * @param is_transpose      [in]: db_matrix 0: NO_TRNAS; 1: TRANS.
 * @param input_dtype       [in]: DT_FP32 / DT_FP16 / DT_INT8.
 * @param output_dtype      [in]: DT_FP32 / DT_FP16 / DT_INT32.
 */
DECL_EXPORT bm_status_t bmcv_faiss_indexflatIP(
    bm_handle_t handle,
    bm_device_mem_t input_data_global_addr,
    bm_device_mem_t db_data_global_addr,
    bm_device_mem_t buffer_global_addr,
    bm_device_mem_t output_sorted_similarity_global_addr,
    bm_device_mem_t output_sorted_index_global_addr,
    int vec_dims,
    int query_vecs_num,
    int database_vecs_num,
    int sort_cnt,
    int is_transpose,
    int input_dtype,
    int output_dtype);

/**
 * @brief: calculate squared L2 distance between query vectors and database vectors, output the top K L2sqr-values and the corresponding indices, return BM_SUCCESS if succeed.
 * @param handle                               [in]: the device handle.
 * @param input_data_global_addr               [in]: device addr information of the query matrix.
 * @param db_data_global_addr                  [in]: device addr information of the database matrix.
 * @param query_L2norm_global_addr             [in]: device addr information of the query norm_L2sqr vector.
 * @param db_L2norm_global_addr                [in]: device addr information of the database norm_L2sqr vector.
 * @param buffer_global_addr                   [in]: squared L2 values stored in the buffer.
 * @param output_sorted_similarity_global_addr [out]: the L2sqr-values matrix.
 * @param output_sorted_index_global_addr      [out]: the result indices matrix.
 * @param vec_dims          [in]: vector dimension.
 * @param query_vecs_num    [in]: the num of query vectors.
 * @param database_vecs_num [in]: the num of database vectors.
 * @param sort_cnt          [in]: get top sort_cnt values.
 * @param is_transpose      [in]: db_matrix 0: NO_TRNAS; 1: TRANS.
 * @param input_dtype       [in]: DT_FP32 / DT_FP16.
 * @param output_dtype      [in]: DT_FP32 / DT_FP16.
 */
DECL_EXPORT bm_status_t bmcv_faiss_indexflatL2(
    bm_handle_t handle,
    bm_device_mem_t input_data_global_addr,
    bm_device_mem_t db_data_global_addr,
    bm_device_mem_t query_L2norm_global_addr,
    bm_device_mem_t db_L2norm_global_addr,
    bm_device_mem_t buffer_global_addr,
    bm_device_mem_t output_sorted_similarity_global_addr,
    bm_device_mem_t output_sorted_index_global_addr,
    int vec_dims,
    int query_vecs_num,
    int database_vecs_num,
    int sort_cnt,
    int is_transpose,
    int input_dtype,
    int output_dtype);

/*
 * @brief: This interface inputs vectors and centroids, calculates the distance table and sorts it, and outputs the quantized code of vectors.
 * @param handle                               [in]: the device handle.
 * @param vector_input_dev                     [in]: the device space for storing the vector to be encoded.
 * @param centroids_input_dev                  [in]: the deivce space for storing cluster center data.
 * @param buffer_table_dev                     [in]: cache space for storing calculated distance tables.
 * @param codes_output_dev                     [out]: the device space for storing vector encoding results.
 * @param encode_vec_num                       [in]: the number of vectors to be encoded.
 * @param vec_dims                             [in]: the dimensions of the original vector.
 * @param slice_num                            [in]: the number of original dimension splits.
 * @param centroids_num                        [in]: the number of cluster centers
 * @param IP_metric                            [in]: 0 means L2 distance calculation; 1 means IP distance calculation.
 * @param input_dtype       [in]: DT_FP32 / DT_FP16.
 * @param output_dtype      [in]: DT_FP32 / DT_FP16.
 */
DECL_EXPORT bm_status_t bmcv_faiss_indexPQ_encode_ext(
    bm_handle_t handle,
    bm_device_mem_t vector_input_dev,
    bm_device_mem_t centroids_input_dev,
    bm_device_mem_t buffer_table_dev,
    bm_device_mem_t nvcodes_output_dev,
    int encode_vec_num,
    int vec_dims,
    int slice_num,
    int centroids_num,
    int IP_metric,
    int input_dtype,
    int output_dtype);
DECL_EXPORT bm_status_t bmcv_faiss_indexPQ_encode(
    bm_handle_t handle,
    bm_device_mem_t vector_input_dev,
    bm_device_mem_t centroids_input_dev,
    bm_device_mem_t buffer_table_dev,
    bm_device_mem_t nvcodes_output_dev,
    int encode_vec_num,
    int vec_dims,
    int slice_num,
    int centroids_num,
    int IP_metric);

/*
 * @brief: calculates the distance table through query and centroids, looks up and sorts the base database encoding, and outputs the top K (sort_cnt) most matching vector indices and their corresponding distances.
 * @param handle                               [in]: the device handle.
 * @param centroids_input_dev                  [in]: the deivce space for storing cluster center data.
 * @param nxquery_input_dev                    [in]: the deivce space that stores the matrix consisting of query vectors.
 * @param nycodes_input_dev                    [in]: the device space for storing the matrix composed of the base vector.
 * @param distance_output_dev                  [out]: the device space for storing the output distance.
 * @param index_output_dev                     [out]: the device space for storing output sorting.
 * @param vec_dims                             [in]: the dimensions of the original vector.
 * @param slice_num                            [in]: the number of original dimension splits.
 * @param centroids_num                        [in]: the number of cluster centers
 * @param database_num                         [in]: the number of database bases.
 * @param query_num                            [in]: retrieve the number of vectors.
 * @param sort_cnt                             [in]: get top sort_cnt values.
 * @param IP_metric                            [in]: 0 means L2 distance calculation; 1 means IP distance calculation.
 * @param input_dtype       [in]: DT_FP32 / DT_FP16.
 * @param output_dtype      [in]: DT_FP32 / DT_FP16.
 */
DECL_EXPORT bm_status_t bmcv_faiss_indexPQ_ADC_ext(
    bm_handle_t handle,
    bm_device_mem_t centroids_input_dev,
    bm_device_mem_t nxquery_input_dev,
    bm_device_mem_t nycodes_input_dev,
    bm_device_mem_t distance_output_dev,
    bm_device_mem_t index_output_dev,
    int vec_dims,
    int slice_num,
    int centroids_num,
    int database_num,
    int query_num,
    int sort_cnt,
    int IP_metric,
    int input_dtype,
    int output_dtype);
DECL_EXPORT bm_status_t bmcv_faiss_indexPQ_ADC(
    bm_handle_t handle,
    bm_device_mem_t centroids_input_dev,
    bm_device_mem_t nxquery_input_dev,
    bm_device_mem_t nycodes_input_dev,
    bm_device_mem_t distance_output_dev,
    bm_device_mem_t index_output_dev,
    int vec_dims,
    int slice_num,
    int centroids_num,
    int database_num,
    int query_num,
    int sort_cnt,
    int IP_metric);

/*
 * @brief: calculates the distance table through query and centroids, looks up and sorts the base database encoding, and outputs the top K (sort_cnt) most matching vector indices and their corresponding distances.
 * @param handle                               [in]: the device handle.
 * @param sdc_table_input_dev                  [in]: the device space for storing the symmetric distance table.
 * @param nxcodes_input_dev                    [in]: the device space where the retrieval vector encoding is stored.
 * @param nycodes_input_dev                    [in]: the device space for storing the matrix composed of the base vector.
 * @param distance_output_dev                  [out]: the device space for storing the output distance.
 * @param index_output_dev                     [out]: the device space for storing output sorting.
 * @param slice_num                            [in]: the number of original dimension splits.
 * @param centroids_num                        [in]: the number of cluster centers
 * @param database_num                         [in]: the number of database bases.
 * @param query_num                            [in]: retrieve the number of vectors.
 * @param sort_cnt                             [in]: get top sort_cnt values.
 * @param IP_metric                            [in]: 0 means L2 distance calculation; 1 means IP distance calculation.
 * @param input_dtype       [in]: DT_FP32 / DT_FP16.
 * @param output_dtype      [in]: DT_FP32 / DT_FP16.
 */
DECL_EXPORT bm_status_t bmcv_faiss_indexPQ_SDC_ext(
    bm_handle_t handle,
    bm_device_mem_t sdc_table_input_dev,
    bm_device_mem_t nxcodes_input_dev,
    bm_device_mem_t nycodes_input_dev,
    bm_device_mem_t distance_output_dev,
    bm_device_mem_t index_output_dev,
    int slice_num,
    int centroids_num,
    int database_num,
    int query_num,
    int sort_cnt,
    int IP_metric,
    int input_dtype,
    int output_dtype);
DECL_EXPORT bm_status_t bmcv_faiss_indexPQ_SDC(
    bm_handle_t handle,
    bm_device_mem_t sdc_table_input_dev,
    bm_device_mem_t nxcodes_input_dev,
    bm_device_mem_t nycodes_input_dev,
    bm_device_mem_t distance_output_dev,
    bm_device_mem_t index_output_dev,
    int slice_num,
    int centroids_num,
    int database_num,
    int query_num,
    int sort_cnt,
    int IP_metric);

/*
* Gets the maximum and minimum values of data from a continuous space
* input is device addr of input data; minval is Minimum value;maxVal is maximum value;
* len is data length
*/
DECL_EXPORT bm_status_t bmcv_min_max(
    bm_handle_t handle,
    bm_device_mem_t input,
    float *minVal,
    float *maxVal,
    int len);

/*
* Complex multiplication operation
* inputReal is enter the data address of the real part;inputImag is enter the data address of the virtual part
* pointImag is Enter the data address of the virtual part of 2; pointReal is enter the data address of the real part of 2
* outputReal is Output the data address of the real part;outputImag is Output the data address of the virtual part
* batch is batch quantity;len is batch plural number
*/
DECL_EXPORT bm_status_t bmcv_cmulp(
    bm_handle_t handle,
    bm_device_mem_t inputReal,
    bm_device_mem_t inputImag,
    bm_device_mem_t pointReal,
    bm_device_mem_t pointImag,
    bm_device_mem_t outputReal,
    bm_device_mem_t outputImag,
    int batch,
    int len);

/**
 * Image affine transformation can realize rotation, translation, scaling and other operations
 * image_num is image num;matrix is transformation matrix data structure corresponding to each image
 * input is src image;output is dst image;
*/
DECL_EXPORT bm_status_t bmcv_image_warp(
    bm_handle_t              handle,
    int                      image_num,
    bmcv_affine_image_matrix matrix[4],
    bm_image *               input,
    bm_image *               output);

/**
 * Image affine transformation can realize rotation, translation, scaling and other operations
 * image_num is image num;matrix is transformation matrix data structure corresponding to each image
 * input is src image;output is dst image;use_bilinear is if to use bilinear for interpolation
*/
DECL_EXPORT bm_status_t bmcv_image_warp_affine(
    bm_handle_t              handle,
    int                      image_num,
    bmcv_affine_image_matrix matrix[4],
    bm_image *               input,
    bm_image *               output,
    int                      use_bilinear = 0);

DECL_EXPORT bm_status_t bmcv_image_warp_affine_padding(
    bm_handle_t              handle,
    int                      image_num,
    bmcv_affine_image_matrix matrix[4],
    bm_image *               input,
    bm_image *               output,
    int                      use_bilinear = 0);

/**
 * Image affine transformation can realize rotation, translation, scaling and other operations
 * image_num is image num;matrix is transformation matrix data structure corresponding to each image
 * input is src image;output is dst image;use_bilinear is if to use bilinear for interpolation
*/
DECL_EXPORT bm_status_t bmcv_image_warp_affine_similar_to_opencv(
    bm_handle_t              handle,
    int                      image_num,
    bmcv_affine_image_matrix matrix[4],
    bm_image *               input,
    bm_image *               output,
    int                      use_bilinear);

DECL_EXPORT bm_status_t bmcv_image_warp_affine_similar_to_opencv_padding(
    bm_handle_t              handle,
    int                      image_num,
    bmcv_affine_image_matrix matrix[4],
    bm_image *               input,
    bm_image *               output,
    int                      use_bilinear);

/**
 * The interface realizes transmission transformation of image
 * image_num is image num;matrix is transformation matrix data structure corresponding to each image
 * input is src image;output is dst image;use_bilinear is if to use bilinear for interpolation
*/
DECL_EXPORT bm_status_t bmcv_image_warp_perspective(
    bm_handle_t                   handle,
    int                           image_num,
    bmcv_perspective_image_matrix matrix[4],
    bm_image *                    input,
    bm_image *                    output,
    int                           use_bilinear = 0);

/**
 * The interface realizes transmission transformation of image
 * image_num is image num;matrix is transformation matrix data structure corresponding to each image
 * input is src image;output is dst image;use_bilinear is if to use bilinear for interpolation
*/
DECL_EXPORT bm_status_t bmcv_image_warp_perspective_with_coordinate(
    bm_handle_t                       handle,
    int                               image_num,
    bmcv_perspective_image_coordinate coordinate[4],
    bm_image *                        input,
    bm_image *                        output,
    int                               use_bilinear = 0);

/**
 * The interface realizes transmission transformation of image
 * image_num is image num;matrix is transformation matrix data structure corresponding to each image
 * input is src image;output is dst image;use_bilinear is if to use bilinear for interpolation
*/
DECL_EXPORT bm_status_t bmcv_image_warp_perspective_similar_to_opencv(
    bm_handle_t                       handle,
    int                               image_num,
    bmcv_perspective_image_matrix     matrix[4],
    bm_image *                        input,
    bm_image *                        output,
    int                               use_bilinear);

/**
 * Implement base64 encoding task
 * src is input data;dst is output data;len is lenth of input and output
*/
DECL_EXPORT bm_status_t bmcv_base64_enc(
    bm_handle_t     handle,
    bm_device_mem_t src,
    bm_device_mem_t dst,
    unsigned long   len[2]);

/**
 * Implement base64 decoding task
 * src is input data;dst is output data;len is lenth of input and output
*/
DECL_EXPORT bm_status_t bmcv_base64_dec(
    bm_handle_t     handle,
    bm_device_mem_t src,
    bm_device_mem_t dst,
    unsigned long   len[2]);

/*
* The function of writing on an image
* image is input image;text is Text to be written, in English;
* org is coordinates at the bottom left of the first character;
* color is draw the color of the line
* fontScale is font size; thickness is draw the width of the line
*/
DECL_EXPORT bm_status_t bmcv_image_put_text(
    bm_handle_t handle,
    bm_image image,
    const char* text,
    bmcv_point_t org,
    bmcv_color_t color,
    float fontScale,
    int thickness);

/**
 * Eliminate network calculations to get too many object boxes and find the best object box.
 * input_proposal_addr is Enter the address of the object box data;
 * proposal_size is Number of object frames;nms_threshold is Filter the threshold of the object frame
 * output_proposal_addr is Output the address of the object enclosure data
*/
DECL_EXPORT bm_status_t bmcv_nms(
    bm_handle_t     handle,
    bm_device_mem_t input_proposal_addr,
    int             proposal_size,
    float           nms_threshold,
    bm_device_mem_t output_proposal_addr);

/**
 * bmcv_nms Specifies the generalized form of an interface that supports Hard_NMS/Soft_NMS/Adaptive_NMS/SSD_NMS
 * input_proposal_addr is Enter the address of the object box data;
 * proposal_size is Number of object frames;nms_threshold is Filter the threshold of the object frame
 * output_proposal_addr is Output the address of the object enclosure data
 * topk is Ports reserved for possible future expansion
 * score_threshold is Lowest score threshold;nms_alg is The choice of different NMS algorithms
 * sigma is Parameter of the Gaussian re-score function if Soft_NMS or Adaptive_NMS is used
 * weighting_method is Linear and Gaussian weights;densities is Adaptive-NMS density value
 * eta is SSD-NMS coefficient is used to adjust the iou threshold
*/
DECL_EXPORT bm_status_t bmcv_nms_ext(
    bm_handle_t     handle,
    bm_device_mem_t input_proposal_addr,
    int             proposal_size,
    float           nms_threshold,
    bm_device_mem_t output_proposal_addr,
    int             topk            = 1,
    float           score_threshold = 0.0f,
    int             nms_alg         = HARD_NMS,
    float           sigma = 1.0,
    int             weighting_method = 0,
    float         * densities = NULL,
    float           eta = 0.0f);

/*
* Draw one or more line segments on an image
* img is bm_image to be processed
* start is information of starting of line;end is information of ending of line;
* line_num is number of lines drawn;color is line with color;thickness is line width
*/
DECL_EXPORT bm_status_t bmcv_image_draw_lines(
    bm_handle_t handle,
    bm_image img,
    const bmcv_point_t* start,
    const bmcv_point_t* end,
    int line_num,
    bmcv_color_t color,
    int thickness);

enum FFTType {
    FFTType_0D,
    FFTType_1D,
    FFTType_2D,
};

struct FFTPlan {
    enum FFTType type;
};

struct FFT0DPlan {
    struct FFTPlan type;
    int batch;
};

struct FFT1DPlan {
    struct FFTPlan type;
    bm_device_mem_t ER;
    bm_device_mem_t EI;
    bool ER_flag;
    bool EI_flag;
    int batch;
    int L;
    bool forward;
    int* factors;
    size_t factors_size;
};

struct FFT2DPlan {
    struct FFTPlan type;
    bm_device_mem_t TR;
    bm_device_mem_t TI;
    bool TR_flag;
    bool TI_flag;
    void *MP;
    void *NP;
    int trans;
};

/*
* Create, execute, and destroy.
* batch is batch num;len is lenth of len;forward is Whether it is a forward transform
* plan is handle that needs to be used during the execution phase
*/
DECL_EXPORT bm_status_t bmcv_fft_1d_create_plan(
    bm_handle_t handle,
    int batch,
    int len,
    bool forward,
    void** plan);
/**
 * Two-dimensional M*N FFT operation
 * M is The size of the first dimension;N is The size of the second dimension
 * forward is Whether it is a forward transform;
 * plan is handle that needs to be used during the execution phase
*/
DECL_EXPORT bm_status_t bmcv_fft_2d_create_plan(
    bm_handle_t handle,
    int M,
    int N,
    bool forward,
    void** plan);
/**
 * The created plan can then begin the actual execution phase
 * inputReal is Enter the real part of the data address;
 * inputImag is Enter the address of the imaginary part of the data
 * outputReal is Enter the real part of the data address;
 * outputImag is Enter the address of the imaginary part of the data
 * plan is handle that needs to be used during the execution phase
*/
DECL_EXPORT bm_status_t bmcv_fft_execute(
    bm_handle_t handle,
    bm_device_mem_t inputReal,
    bm_device_mem_t inputImag,
    bm_device_mem_t outputReal,
    bm_device_mem_t outputImag,
    const void *plan);
/**
 * The created plan can then begin the actual execution phase
 * inputReal is Enter the real part of the data address
 * inputImag is Enter the address of the imaginary part of the data
 * outputReal is Enter the real part of the data address;
 * outputImag is Enter the address of the imaginary part of the data
 * plan is handle that needs to be used during the execution phase
*/
DECL_EXPORT bm_status_t bmcv_fft_execute_real_input(
    bm_handle_t handle,
    bm_device_mem_t inputReal,
    bm_device_mem_t outputReal,
    bm_device_mem_t outputImag,
    const void *plan);

DECL_EXPORT void bmcv_fft_destroy_plan(bm_handle_t handle, void *plan);

/**
 * XRHost is Enter the real part of the data address
 * XIHost is Enter the address of the imaginary part of the data
 * YRHost is Enter the real part of the data address;
 * YIHost is Enter the address of the imaginary part of the data
 * batch is the num of signal
 * L is the length of signal
 * realInput is the input signal whether real or complex
 * pad_mode is reflect or constant
 * n_fft is the stft do fft length
 * win_mode is hanning or hamming
 * normalize is normalize or not
**/
DECL_EXPORT bm_status_t bmcv_stft(
    bm_handle_t handle,
    float* XRHost, float* XIHost,
    float* YRHost, float* YIHost,
    int batch, int L,
    bool realInput, int pad_mode,
    int n_fft, int win_mode, int hop_len,
    bool normalize);

/**
 * XRHost is Enter the real part of the data address
 * XIHost is Enter the address of the imaginary part of the data
 * YRHost is Enter the real part of the data address;
 * YIHost is Enter the address of the imaginary part of the data
 * batch is the num of signal
 * L is the length of signal
 * realInput is the input signal whether real or complex
 * pad_mode is reflect or constant
 * n_fft is the stft do fft length
 * win_mode is hanning or hamming
 * normalize is normalize or not
**/
DECL_EXPORT bm_status_t bmcv_istft(
    bm_handle_t handle,
    float* XRHost, float* XIHost,
    float* YRHost, float* YIHost,
    int batch, int L,
    bool realInput, int pad_mode,
    int n_fft, int win_mode, int hop_len,
    bool normalize);

/*
* The Euclidean distance between multiple points and a particular point in dimensional space
* input and output are device space that holds len point coordinates
* dim is spatial dimension size;pnt is coordinates of a particular point;len is number of coordinates to be found
*/
DECL_EXPORT bm_status_t bmcv_distance(
    bm_handle_t handle,
    bm_device_mem_t input,
    bm_device_mem_t output,
    int dim,
    const void * pnt,
    int len,
    int dtyte);

/*
* input is input image; output is output image;ksize is size of the Laplacian nucleus,1 or 3;
*/
DECL_EXPORT bm_status_t bmcv_image_laplacian(
    bm_handle_t handle,
    bm_image input,
    bm_image output,
    unsigned int ksize);

/*
* input is enter the matrix data address;output is output matrix data address;
* input_row is enter the number of matrix rows;input_col is enter the number of matrix columns
* output_row is output the number of matrix rows;output_col is output the number of matrix columns
* row_stride is output matrix row step size;col_stride is output matrix column step size
*/
DECL_EXPORT bm_status_t bmcv_as_strided(
    bm_handle_t handle,
    bm_device_mem_t input,
    bm_device_mem_t output,
    int input_row,
    int input_col,
    int output_row,
    int output_col,
    int row_stride,
    int col_stride);

/**
 * Image width and height transpose
 * input is src image;output is dst image
*/
DECL_EXPORT bm_status_t bmcv_image_transpose(
    bm_handle_t handle,
    bm_image input,
    bm_image output);

/**
 * Realize downsampling in image Gaussian pyramid operation
 * input is src image;output is dst image
*/
DECL_EXPORT bm_status_t bmcv_image_pyramid_down(
    bm_handle_t handle,
    bm_image input,
    bm_image output);

/**
 * Convert bayerBG8 or bayerRG8 format images to rgb plannar format
 * @param [in] convd_kernel   convolution kernel value
 * @param [in] input          input image
 * @param [in] output         output image
*/
DECL_EXPORT bm_status_t bmcv_image_bayer2rgb(
    bm_handle_t handle,
    unsigned char* convd_kernel,
    bm_image input,
    bm_image output);

/**
 * @param [in] src_data_addr Enter the address information for the data index
 * @param [in] src_index_addr Output data address information
 * @param [in] dst_data_addr Output the address information of the data index
 * @param [in] dst_index_addr Output the address information of the data index
 * @param [in] buffer_addr Buffer address information
 * @param [in] k ranges [0,100]
 * @param [in] src_index_valid bool
*/
DECL_EXPORT bm_status_t bmcv_batch_topk(
    bm_handle_t     handle,
    bm_device_mem_t src_data_addr,
    bm_device_mem_t src_index_addr,
    bm_device_mem_t dst_data_addr,
    bm_device_mem_t dst_index_addr,
    bm_device_mem_t buffer_addr,
    bool            src_index_valid,
    int             k,
    int             batch,
    int*            per_batch_cnt,
    bool            same_batch_cnt,
    int             src_batch_stride,
    bool            descending);

/**
 * Realization of 8-bit data type matrix multiplication calculation
 * @param [in] M is A C rows @param [in] N is B C columns
 * @param [in] K A B rows
 * @param [] A B C is Save its device address
 * A_sign B_sign is Matrix sign;rshift_bit si right shift of matrix product
 * result_type is Matrix data type;is_B_trans is bool; alpha is a*b coefficient;
 * beta is offest
*/
DECL_EXPORT bm_status_t bmcv_matmul(
    bm_handle_t      handle,
    int              M,
    int              N,
    int              K,
    bm_device_mem_t  A,
    bm_device_mem_t  B,
    bm_device_mem_t  C,
    int              A_sign, /*1: signed 0: unsigned */
    int              B_sign,
    int              rshift_bit,
    int              result_type, /* 0:8bit 1:int16 2:fp32 */
    bool             is_B_trans,
    float            alpha = 1,
    float            beta = 0);

/**
 * histogram
 * @param [in] input input data
 * @param [in] output output data
 * C H W is channels width height channels
 * dims is dimensionality; histSizes is each channel counts the number of copies of the histogram
*/
DECL_EXPORT bm_status_t bmcv_calc_hist(
    bm_handle_t handle,
    bm_device_mem_t input,
    bm_device_mem_t output,
    int C,
    int H,
    int W,
    const int *channels,
    int dims,
    const int *histSizes,
    const float *ranges,
    int inputDtype);

/**
 * Histogram with weights
 * input output is data;weight is each element in the channel when calculating the histogram
 * C H W is channels width height;channels is list of channels that need to be computed for the histogram
 * dims is < 3;histSizes is each channel counts the number of copies of the histogram
 * ranges is Scope of channel participation statistics;inputDtype is Type of input data
*/
DECL_EXPORT bm_status_t bmcv_calc_hist_with_weight(
    bm_handle_t handle,
    bm_device_mem_t input,
    bm_device_mem_t output,
    const float *weight,
    int C,
    int H,
    int W,
    const int *channels,
    int dims,
    const int *histSizes,
    const float *ranges,
    int inputDtype);

/**
 * Calculation of general multiplication of matrices of type float32
 * is_A_trans is_B_trans Controls whether to transpose
 * M is The number of rows of A and C of the matrix;
 * N is The number of columns of B and C of the matrix
 * k is Number of columns A and number of rows B
 * alpha is Multiplicative coefficient;
 * A is Left matrix A data address;lda is A of leading dimension
 * B is Right matrix B data address;ldb is C of leading dimension
 * beta is Multiplicative coefficient;C is Matrix C data address
*/
DECL_EXPORT bm_status_t bmcv_gemm(
    bm_handle_t handle,
    bool is_A_trans,
    bool is_B_trans,
    int M,
    int N,
    int K,
    float alpha,
    bm_device_mem_t A,
    int lda,
    bm_device_mem_t B,
    int ldb,
    float beta,
    bm_device_mem_t C,
    int ldc);

/**
 * is_A_trans is_B_trans is transpose or not
 * M is A C Y line number;N is B C Y columns;K is A columns and B lines
 * alpha is Multiplicative coefficient;A is A data addr;B is B data addr;
 * beta is Multiplicative coefficient;C is C data addr;Y is Y data addr;
 * input_dtype is data types of A, B, and C;output_dtype is data type of Y
*/
DECL_EXPORT bm_status_t bmcv_gemm_ext(
    bm_handle_t handle,
    bool is_A_trans,
    bool is_B_trans,
    int M,
    int N,
    int K,
    float alpha,
    bm_device_mem_t A,
    bm_device_mem_t B,
    float beta,
    bm_device_mem_t C,
    bm_device_mem_t Y,
    bm_image_data_format_ext in_dtype,
    bm_image_data_format_ext out_dtype);

/**
 * network get feature points(float)and compares them with feature points(float) in database
 * input_data_global_addr is address of the feature point data store to be compared
 * db_data_global_addr is address of the database's feature point data store
 * db_feature_global_addr is database feature_size indicates the reciprocal address of the direction mode
 * output_similarity_global_addr is compare the result to the maximum address
 * output_index_global_addr is serial number address in database
 * batch_size is batch size;feature_size is each data feature points;db_size is number of feature points
*/
DECL_EXPORT bm_status_t bmcv_feature_match_normalized(
    bm_handle_t handle,
    bm_device_mem_t input_data_global_addr,
    bm_device_mem_t db_data_global_addr,
    bm_device_mem_t db_feature_global_addr,
    bm_device_mem_t output_similarity_global_addr,
    bm_device_mem_t output_index_global_addr,
    int batch_size,
    int feature_size,
    int db_size);

/**
 * network get feature points(int)and compares them with feature points(int) in database
 * input_data_global_addr is address of the feature point data store to be compared
 * db_data_global_addr is address of the database's feature point data store
 * output_sorted_similarity_global_addr is comparison results Maximum address (descending)
 * output_sorted_index_global_addr is serial number address in database
 * batch_size is batch size; feature_size is number of feature points
 * db_size is number of feature points;sort_cnt is Number of output results;
 * rshiftbits is Result displacement number;
*/
DECL_EXPORT bm_status_t bmcv_feature_match(
    bm_handle_t handle,
    bm_device_mem_t input_data_global_addr,
    bm_device_mem_t db_data_global_addr,
    bm_device_mem_t output_sorted_similarity_global_addr,
    bm_device_mem_t output_sorted_index_global_addr,
    int batch_size,
    int feature_size,
    int db_size,
    int sort_cnt = 1,
    int rshiftbits = 0);

/**
 * Histogram equalization improves contrast
 * input is input image device addr; output is output image device addr;
 * H W is image width height
*/
DECL_EXPORT bm_status_t bmcv_hist_balance(
    bm_handle_t handle,
    bm_device_mem_t input,
    bm_device_mem_t output,
    int H,
    int W);


// dpu api
/**
 * Semi-global block matching algorithm SGBM
 * left_image is reference chart pointer;right_image is search graph pointer
 * disp_image is differential view;dpu_attr is control attr;sgbm_mode is output mode
*/
DECL_EXPORT bm_status_t bmcv_dpu_sgbm_disp(
    bm_handle_t         handle,
    bm_image            *left_image,
    bm_image            *right_image,
    bm_image            *disp_image,
    bmcv_dpu_sgbm_attrs *dpu_attr,
    bmcv_dpu_sgbm_mode  sgbm_mode);

/**
 * Fast global smoothing algorithm FGS
 * guide_image is Guide chart pointer;smooth_image is Smooth chart pointer;
 * disp_image is differential view;fgs_attr is control attr;fgs_mode is output mode
*/
DECL_EXPORT bm_status_t bmcv_dpu_fgs_disp(
    bm_handle_t         handle,
    bm_image            *guide_image,
    bm_image            *smooth_image,
    bm_image            *disp_image,
    bmcv_dpu_fgs_attrs  *fgs_attr,
    bmcv_dpu_fgs_mode   fgs_mode);

/**
 * Semi-global block matching algorithm SGBM
 * left_image is Guide chart pointer;right_image is Smooth chart pointer;
 * disp_image is differential view;dpu_attr is SGBM part control parameters;
 * fgs_attr is FGS part control parameters;online_mode is output mode
*/
DECL_EXPORT bm_status_t bmcv_dpu_online_disp(
    bm_handle_t            handle,
    bm_image               *left_image,
    bm_image               *right_image,
    bm_image               *disp_image,
    bmcv_dpu_sgbm_attrs    *dpu_attr,
    bmcv_dpu_fgs_attrs     *fgs_attr,
    bmcv_dpu_online_mode   online_mode);


// ldc/dwa/blend api
/**
 * Rotation function of lens distortion correction (LDC) module
 * in_image is image to be rotated;out_image is rotated image
 * rot_mode is rotated mode
*/
DECL_EXPORT bm_status_t bmcv_ldc_rot(
    bm_handle_t          handle,
    bm_image             in_image,
    bm_image             out_image,
    bmcv_rot_mode        rot_mode);

/**
 * Lens Distortion correction (LDC) module for geometric distortion correction
 * in_image is distorted image;out_image is distortion corrected image;
 * ldc_attr is parameter configuration list of the GDC function
*/
DECL_EXPORT bm_status_t bmcv_ldc_gdc(
    bm_handle_t          handle,
    bm_image             in_image,
    bm_image             out_image,
    bmcv_gdc_attr        ldc_attr);

/**
 * Geometric distortion correction work of lens Distortion correction (LDC) module
 * in_image is distorted image;out_image is distortion corrected image
 * ldc_attr is ldc control attr
 * dmem is device memory for storing MESH tables
*/
DECL_EXPORT bm_status_t bmcv_ldc_gdc_gen_mesh(
    bm_handle_t          handle,
    bm_image             in_image,
    bm_image             out_image,
    bmcv_gdc_attr        ldc_attr,
    bm_device_mem_t      dmem);

/**
 * Geometric distortion correction work of lens Distortion correction (LDC) module
 * in_image is distorted image;out_image is distortion corrected image
 * dmem is device memory for storing MESH tables
*/
DECL_EXPORT bm_status_t bmcv_ldc_gdc_load_mesh(
    bm_handle_t          handle,
    bm_image             in_image,
    bm_image             out_image,
    bm_device_mem_t      dmem);

/**
 * Dedistorting affine (DWA) module rotation function
 * input_image is image to be rotated;output_image is rotated image
 * rot_mode is rotate mode
*/
DECL_EXPORT bm_status_t bmcv_dwa_rot(
    bm_handle_t          handle,
    bm_image             input_image,
    bm_image             output_image,
    bmcv_rot_mode        rot_mode);

/**
 * The geometric distortion correction function of dedistorted affine (DWA) module
 * input_image is distorted image;output_image is distortion corrected image
 * ldc_attr is parameter configuration list of the GDC function
*/
DECL_EXPORT bm_status_t bmcv_dwa_gdc(
    bm_handle_t          handle,
    bm_image             input_image,
    bm_image             output_image,
    bmcv_gdc_attr        ldc_attr);

/**
 * Affine correction function of dedistorting affine (DWA) module
 * input_image is src image; output_image is Image corrected for affine change
 * affine_attr is affine Parameter configuration list
*/
DECL_EXPORT bm_status_t bmcv_dwa_affine(
    bm_handle_t          handle,
    bm_image             input_image,
    bm_image             output_image,
    bmcv_affine_attr_s   affine_attr);

/**
 * Fish eye distortion correction function of dedistorting Affine (DWA) module
 * input_image is distorted image;output_image is Image corrected for affine change
 * fisheye_attr is Parameter configuration list of Fisheye function
*/
DECL_EXPORT bm_status_t bmcv_dwa_fisheye(
    bm_handle_t          handle,
    bm_image             input_image,
    bm_image             output_image,
    bmcv_fisheye_attr_s  fisheye_attr);

/**
 * Dedistortion correction function of dedistortion affine (DWA) module
 * input_image is distorted image;output_image is Image corrected for affine change
 * grid_info is Object pointer of Grid_Info
*/
DECL_EXPORT bm_status_t bmcv_dwa_dewarp(
    bm_handle_t          handle,
    bm_image             input_image,
    bm_image             output_image,
    bm_device_mem_t      grid_info);

/**
 * It can realize the fusion of 2~4 pictures
 * input_num is image number;input is input image pointer;
 * output is blending output image;stitch_config is parameters of splice fusion
*/
DECL_EXPORT bm_status_t bmcv_blending(
    bm_handle_t handle,
    int       input_num,
    bm_image* input,
    bm_image  output,
    struct stitch_param stitch_config);

// ive api
/**
 * The weighted addition of two images
 * input1 input2 output is 2 src image,1 dst image
 * attr is add control attr
*/
DECL_EXPORT bm_status_t bmcv_ive_add(
    bm_handle_t          handle,
    bm_image             input1,
    bm_image             input2,
    bm_image             output,
    bmcv_ive_add_attr    attr);

/**
 * The and of two images
 * input1 input2 output is 2 src image,1 dst image
*/
DECL_EXPORT bm_status_t bmcv_ive_and(
    bm_handle_t          handle,
    bm_image             input1,
    bm_image             input2,
    bm_image             output);

/**
 * The or of two images
 * input1 input2 output is 2 src image,1 dst image
*/
DECL_EXPORT bm_status_t bmcv_ive_or(
    bm_handle_t          handle,
    bm_image             input1,
    bm_image             input2,
    bm_image             output);

/**
 * The xor of two images
 * input1 input2 output is 2 src image,1 dst image
*/
DECL_EXPORT bm_status_t bmcv_ive_xor(
    bm_handle_t          handle,
    bm_image             input1,
    bm_image             input2,
    bm_image             output);

/**
 * The sub of two images
 * input1 input2 output is 2 src image,1 dst image
 * attr is sub control attr
*/
DECL_EXPORT bm_status_t bmcv_ive_sub(
    bm_handle_t          handle,
    bm_image             input1,
    bm_image             input2,
    bm_image             output,
    bmcv_ive_sub_attr    attr);

/**
 * Supports the threshold task of U8 to U8 data, S16 to 8bit data,U16 to U8 data
 * input is input image;output is output image;
 * thresh_mode is thresh mode;attr is thresh control attr
*/
DECL_EXPORT bm_status_t bmcv_ive_thresh(
    bm_handle_t               handle,
    bm_image                  input,
    bm_image                  output,
    bmcv_ive_thresh_mode      thresh_mode,
    bmcv_ive_thresh_attr      attr);

/**
 * dma amplitude operation
 * image is input and ouput; dma_set_mode is set mode;val is value of setting need
*/
DECL_EXPORT bm_status_t bmcv_ive_dma_set(
    bm_handle_t                      handle,
    bm_image                         image,
    bmcv_ive_dma_set_mode            dma_set_mode,
    unsigned long long               val);

/**
 * Create a direct memory access task, supporting fast copy and interval copy
 * input is input image;output is output image;dma_mode is DMA copy operation mode
 * attr is Control parameter structure required by DMA in interval copy mode
*/
DECL_EXPORT bm_status_t bmcv_ive_dma(
    bm_handle_t                      handle,
    bm_image                         input,
    bm_image                         output,
    bmcv_ive_dma_mode                dma_mode,
    bmcv_ive_interval_dma_attr *     attr);

/**
 * Create a Map to look for values in the Map lookup table for each pixel in the source image
 * input is src image;output is dst image;map_table is stores mapping table information
*/
DECL_EXPORT bm_status_t bmcv_ive_map(
    bm_handle_t             handle,
    bm_image                input,
    bm_image                output,
    bm_device_mem_t         map_table);

/**
 * Traversing the image pixel value to count the number of times the same pixel value appears in the image
 * input is src image;output is output struct of output data
*/
DECL_EXPORT bm_status_t bmcv_ive_hist(
    bm_handle_t          handle,
    bm_image             input,
    bm_device_mem_t      output);

/**
 * Integral graph calculation task for gray image
 * input is src image; output is output data struct;
 * integ_attr is integ control attr
*/
DECL_EXPORT bm_status_t bmcv_ive_integ(
    bm_handle_t              handle,
    bm_image                 input,
    bm_device_mem_t          output,
    bmcv_ive_integ_ctrl_s    integ_attr);

/**
 * The normalized correlation number of two gray level images with the same resolution
 * input1 input2 is src image;output is dst image
*/
DECL_EXPORT bm_status_t bmcv_ive_ncc(
    bm_handle_t          handle,
    bm_image             input1,
    bm_image             input2,
    bm_device_mem_t      output);

/**
 * 3x3 template order statistics filtering task
 * intput is src image;output is dst image;
 * mode is mode of controlling the filtering of order statistics
*/
DECL_EXPORT bm_status_t bmcv_ive_ord_stat_filter(
    bm_handle_t                   handle,
    bm_image                      input,
    bm_image                      output,
    bmcv_ive_ord_stat_filter_mode mode);

/**
 * lbp is an operator that describes the local features of an image
 * input is src; output is dst;lbp_attr is lbp control attr
*/
DECL_EXPORT bm_status_t bmcv_ive_lbp(
    bm_handle_t              handle,
    bm_image                 input,
    bm_image                 output,
    bmcv_ive_lbp_ctrl_attr   lbp_attr);

/**
 * Create a binary image 5x5 template bloat task
 * input output is src and dst;dilate_mask is 5x5 expansion template coefficient array
*/
DECL_EXPORT bm_status_t bmcv_ive_dilate(
    bm_handle_t           handle,
    bm_image              input,
    bm_image              output,
    unsigned char         dilate_mask[25]);

/**
 * Create a binary image 5x5 template erode task
 * input output is src and dst;erode_mask is 5x5 erode template coefficient array
*/
DECL_EXPORT bm_status_t bmcv_ive_erode(
    bm_handle_t           handle,
    bm_image              input,
    bm_image              output,
    unsigned char         erode_mask[25]);

/**
 * Calculate the amplitude and Angle of the gradient of pixel change in gray level map
 * input is src;mag_output is mag dst; ang_output is ang dst;attr is control attr
*/
DECL_EXPORT bm_status_t bmcv_ive_mag_and_ang(
    bm_handle_t                   handle,
    bm_image  *                   input,
    bm_image  *                   mag_output,
    bm_image  *                   ang_output,
    bmcv_ive_mag_and_ang_ctrl     attr);

/**
 * 5x5 template Sobel-like gradient calculation task
 * input is src;output_h is gradient of the H component;
 * output_v is gradient of the V component;sobel_attr is sobel control attr
*/
DECL_EXPORT bm_status_t bmcv_ive_sobel(
    bm_handle_t           handle,
    bm_image *            input,
    bm_image *            output_h,
    bm_image *            output_v,
    bmcv_ive_sobel_ctrl   sobel_attr);

/**
 * Create a normalized gradient calculation task
 * input is enter the bm_image object data pointer
 * output_h is template filter and normalization to s8 post gradient component image (H) pointer
 * output_v is is template filter and normalization to s8 post gradient component image (H) pointer
 * output_hv is templates and transpose templates are filtered and stored in package format after normalization to s8
 * normgrad_attr is control attr
*/
DECL_EXPORT bm_status_t bmcv_ive_norm_grad(
    bm_handle_t              handle,
    bm_image *               input,
    bm_image *               output_h,
    bm_image *               output_v,
    bm_image *               output_hv,
    bmcv_ive_normgrad_ctrl   normgrad_attr);

/**
 * GMM background modeling task
 * input is src;output_fg is output fg dst;output_bg is output bg dst;
 * output_model is GMM model parameters;gmm_attr is control attr
*/
DECL_EXPORT bm_status_t bmcv_ive_gmm(
    bm_handle_t            handle,
    bm_image               input,
    bm_image               output_fg,
    bm_image               output_bg,
    bm_device_mem_t        output_model,
    bmcv_ive_gmm_ctrl      gmm_attr);

/**
 * GMM background modeling task
 * input is src;input_factor is Model update parameter;output_fg is fg dst;
 * output_bg is bg dst;output_match_model_info is Model matching coefficient
 * output_model is GMM model parameters;gmm2_attr is control attr
*/
DECL_EXPORT bm_status_t bmcv_ive_gmm2(
    bm_handle_t            handle,
    bm_image *             input,
    bm_image *             input_factor,
    bm_image *             output_fg,
    bm_image *             output_bg,
    bm_image *             output_match_model_info,
    bm_device_mem_t        output_model,
    bmcv_ive_gmm2_ctrl     gmm2_attr);

/**
 * Calculate the canny edge graph
 * input is src;output_edge is canny infor
 * canny_hys_edge_attr is control attr;
*/
DECL_EXPORT bm_status_t bmcv_ive_canny(
    bm_handle_t                    handle,
    bm_image                       input,
    bm_device_mem_t                output_edge,
    bmcv_ive_canny_hys_edge_ctrl   canny_hys_edge_attr);

/**
 * 5x5 template filtering task
 * input is src; output is dst;
 * filter_attr is control attr
*/
DECL_EXPORT bm_status_t bmcv_ive_filter(
    bm_handle_t                  handle,
    bm_image                     input,
    bm_image                     output,
    bmcv_ive_filter_ctrl         filter_attr);

/**
 * Implement color conversion tasks
 * input is src;output is dst;csc_type is csc type
*/
DECL_EXPORT bm_status_t bmcv_ive_csc(
    bm_handle_t     handle,
    bm_image        input,
    bm_image        output,
    csc_type_t      csc_type);

/**
 * Composite task of 5x5 template filtering and YUV2RGB color space conversion
 * input is src;output is dst;attr is filrter control attr; csc_type is csc type
*/
DECL_EXPORT bm_status_t bmcv_ive_filter_and_csc(
    bm_handle_t             handle,
    bm_image                input,
    bm_image                output,
    bmcv_ive_filter_ctrl    attr,
    csc_type_t              csc_type);

/**
 * Create image scaling tasks, support bilinear interpolation, region interpolation scaling
 * input is src; output is dst;resize_mode is resize mode
*/
DECL_EXPORT bm_status_t bmcv_ive_resize(
    bm_handle_t              handle,
    bm_image                 input,
    bm_image                 output,
    bmcv_resize_algorithm    resize_mode);

/**
 * The calculation of Shi-Tomasi-like corner points (intersection points of two edges) in gray image is completed
 * input is src;output is dst;stcandicorner_attr is control attr
*/
DECL_EXPORT bm_status_t bmcv_ive_stcandicorner(
    bm_handle_t                   handle,
    bm_image                      input,
    bm_image                      output,
    bmcv_ive_stcandicorner_attr   stcandicorner_attr);

/**
 * gradient infor of background image and current frame image is used to calculate gradient foreground
 * input_bgdiff_fg is Background difference foreground;input_fggrad is Current frame gradient map
 * input_bggrad is Background gradient image;output_gradfg is Gradient foreground image
 * gradfg_attr is Gradient foreground control parameter structure
*/
DECL_EXPORT bm_status_t bmcv_ive_gradfg(
    bm_handle_t             handle,
    bm_image                input_bgdiff_fg,
    bm_image                input_fggrad,
    bm_image                input_bggrad,
    bm_image                output_gradfg,
    bmcv_ive_gradfg_attr    gradfg_attr);

/**
 * Calculate the two images as a 16bit/8bit SAD image in 4x4/8x8/16x16 blocks
 * input is src;output_sad is Output SAD image pointer;
 * output_thr is Output the SAD threshold image pointer;
 * sad_attr is SAD control parameter pointer;thresh_attr is SAD thresh mode
*/
DECL_EXPORT bm_status_t bmcv_ive_sad(
    bm_handle_t                handle,
    bm_image *                 input,
    bm_image *                 output_sad,
    bm_image *                 output_thr,
    bmcv_ive_sad_attr *        sad_attr,
    bmcv_ive_sad_thresh_attr*  thresh_attr);

/**
 * The difference between current frame data and background comes to get the foreground data
 * cur_img is Current image;bgmodel_img is Background model;fgflag_img is Foreground status image
 * diff_fg_img is Interframe differential foreground image;stat_data_mem is Foreground state data
 * attr is control attr
*/
DECL_EXPORT bm_status_t bmcv_ive_match_bgmodel(
    bm_handle_t                   handle,
    bm_image                      cur_img,
    bm_image                      bgmodel_img,
    bm_image                      fgflag_img,
    bm_image                      diff_fg_img,
    bm_device_mem_t               stat_data_mem,
    bmcv_ive_match_bgmodel_attr   attr);

/**
 * Input the current image and model to update the background model
 * cur_img is Current image;bgmodel_img is Background model;fgflag_img is Foreground status image
 * bg_img is Background image;chgsta_img is Foreground updates status image;
 * stat_data_mem is Foreground state data structure;attr is control attr
*/
DECL_EXPORT bm_status_t bmcv_ive_update_bgmodel(
    bm_handle_t                    handle,
    bm_image  *                    cur_img,
    bm_image  *                    bgmodel_img,
    bm_image  *                    fgflag_img,
    bm_image  *                    bg_img,
    bm_image  *                    chgsta_img,
    bm_device_mem_t                stat_data_mem,
    bmcv_ive_update_bgmodel_attr   attr);

/**
 * Connected region labeling task for binary image
 * src_dst_image is src and dst image;ccblob_output is Connected region information data structure
 * ccl_attr is control attr
*/
DECL_EXPORT bm_status_t bmcv_ive_ccl(
    bm_handle_t          handle,
    bm_image             src_dst_image,
    bm_device_mem_t      ccblob_output,
    bmcv_ive_ccl_attr    ccl_attr);

/**
 * Bernsen solves the image binarization and realizes the binarization of the image
 * input output is src and dst; attr is control attr
*/
DECL_EXPORT bm_status_t bmcv_ive_bernsen(
    bm_handle_t           handle,
    bm_image              input,
    bm_image              output,
    bmcv_ive_bernsen_attr attr);

/**
 * Create a linear conversion task for 16bit image data to 8bit image data
 * input output is src and dst;attr is control attr
*/
DECL_EXPORT bm_status_t bmcv_ive_16bit_to_8bit(
    bm_handle_t                 handle,
    bm_image                    input,
    bm_image                    output,
    bmcv_ive_16bit_to_8bit_attr attr);

/**
 * Create a background subtraction task
 * input1 input2 is src1 src2;output is dst;
 * attr is control attr
*/
DECL_EXPORT bm_status_t bmcv_ive_frame_diff_motion(
    bm_handle_t                     handle,
    bm_image                        input1,
    bm_image                        input2,
    bm_image                        output,
    bmcv_ive_frame_diff_motion_attr attr);

#if defined(__cplusplus)
}
#endif

#endif
