
#ifndef LOG_H
#define LOG_H

#if 0	/* modify by liwei */
#define DEBUG
#else
#undef	DEBUG
#endif

#ifdef DEBUG
#define PrintErr(fmt, x...)  printf("%s:%s:%d: " fmt " errno:%d, errMsg:%s\n", __FILE__, __FUNCTION__, __LINE__, ##x, errno, strerror(errno));
#define PrintInfo(fmt, x...)  printf("%s:%s:%d: " fmt "\n", __FILE__, __FUNCTION__, __LINE__, ##x);
#else
#define PrintErr(fmt, x...)  //printf("%s:%s:%d: " fmt " errno:%d, errMsg:%s\n", __FILE__, __FUNCTION__, __LINE__, ##x, errno, strerror(errno));
#define PrintInfo(fmt, x...)  //printf("%s:%s:%d: " fmt "\n", __FILE__, __FUNCTION__, __LINE__, ##x);

#endif


#define BOOL	int
#define TRUE	1
#define FALSE	0
#endif //LOG_H


