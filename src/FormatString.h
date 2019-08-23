#ifndef __PRINTF_H__
#define __PRINTF_H__

#include <stdarg.h>

typedef void FormatOutputFunction(char c,void *context);

static int FormatString(FormatOutputFunction *outputfunc,void *context,const char *format,va_list args);

static int sprintf(char *buffer,const char *format,...);

#endif
