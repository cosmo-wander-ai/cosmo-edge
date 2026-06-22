
#ifndef TYPES_H
#define TYPES_H

typedef unsigned char 		uint8_t;
typedef unsigned short 		uint16_t;
typedef unsigned int 		uint32_t;
typedef unsigned long long 	uint64_t;

#if 0
#define avio32(x)                                \
    (((uint32_t)((const uint8_t*)(x))[0] << 24) |    \
               (((const uint8_t*)(x))[1] << 16) |    \
               (((const uint8_t*)(x))[2] <<  8) |    \
                ((const uint8_t*)(x))[3])
#endif

#endif //TYPES_H

