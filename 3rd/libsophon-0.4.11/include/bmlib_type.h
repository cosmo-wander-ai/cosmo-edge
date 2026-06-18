#ifndef BM_TYPE_H_
#define BM_TYPE_H_
//#if defined (__cplusplus)
//extern "C" {
//#endif
#ifdef __cplusplus
extern "C" {
#endif

#ifdef __x86_64__
void print_trace(void);
#else
#define print_trace() \
  do {                \
  } while (0)
#endif

#define FLOAT_SIZE 4

#ifndef USING_CMODEL
        #ifdef __arm__
                extern jmp_buf error_stat;
                void fw_log(int level, char *fmt, ...);
                #define hang(_ret)               \
                {                                \
                        fw_log(0, "%s assert\n", __func__); \
                        longjmp(error_stat,1);   \
                }
        #else
                #define hang(_ret) while (1)
        #endif
#else
#ifndef hang
#define hang(_ret) exit(_ret)
#endif
#endif

#ifdef NO_PRINTF_IN_ASSERT
#define ASSERT_INFO(_cond, fmt, ...) \
  do {                               \
    if (!(_cond)) {                  \
      hang(-1);                      \
    }                                \
  } while (0)
#else
#ifndef ASSERT_INFO
#define ASSERT_INFO(_cond, fmt, ...)                                           \
  do {                                                                         \
    if (!(_cond)) {                                                            \
      printf("ASSERT %s: %s: %d: %s\n", __FILE__, __func__, __LINE__, #_cond); \
      printf("ASSERT info: " fmt "\n", ##__VA_ARGS__);                         \
      hang(-1);                                                                \
    }                                                                          \
  } while (0)
#endif
#endif

#define ASSERT(_cond) ASSERT_INFO(_cond, "none.")

#ifndef ASSERT_RANGE
#define ASSERT_RANGE(val, min, max) \
  ASSERT_INFO((val) >= (min) && (val) <= (max), #val "=%d must be in [%d, %d]", (val), (min), (max))
#endif

#define INLINE inline

#define UNUSED(x) (void)(x)

#define __ALIGN_MASK(x, mask) (((x) + (mask)) & ~(mask))
//#define ALIGN(x, a) __ALIGN_MASK(x, (__typeof__(x))(a)-1)
#ifndef ALIGN
#define ALIGN(x, a) __ALIGN_MASK(x, (int)(a)-1)
#endif
#define ALIGN_SHIFT(x, s) (ALIGN(x, (1 << (s))))
#define NUM_ALIGN_SHFIT(x, s) (ALIGN_SHIFT((x), (s)) >> (s))

#define bm_min(x, y) (((x)) < ((y)) ? (x) : (y))
#define bm_max(x, y) (((x)) > ((y)) ? (x) : (y))

typedef union {
  int ival;
  float fval;
} IF_VAL;

#ifdef _WIN32
typedef unsigned long int U32;
typedef signed long int   S32;

//#define false 0
//#define true 1

#else
typedef unsigned long int U64;
typedef signed long int   S64;
#endif

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef signed char  s8;
typedef signed short s16;
typedef signed int   s32;
typedef signed long long int s64;

typedef u32 stride_type;
typedef u32 size_type;

//#if defined (__cplusplus)
//}
#ifdef __cplusplus
}
#endif
//#endif
#endif
