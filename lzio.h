/*
** $Id: lzio.h $
** Buffered streams
** See Copyright Notice in lum.h
*/


#ifndef lzio_h
#define lzio_h

#include "lum.h"

#include "lmem.h"


#define EOZ	(-1)			/* end of stream */

typedef struct Zio ZIO;

#define zgetc(z)  (((z)->n--)>0 ?  cast_uchar(*(z)->p++) : lumZ_fill(z))


typedef struct Mbuffer {
  char *buffer;
  size_t n;
  size_t buffsize;
} Mbuffer;

#define lumZ_initbuffer(L, buff) ((buff)->buffer = NULL, (buff)->buffsize = 0)

#define lumZ_buffer(buff)	((buff)->buffer)
#define lumZ_sizebuffer(buff)	((buff)->buffsize)
#define lumZ_bufflen(buff)	((buff)->n)

#define lumZ_buffremove(buff,i)	((buff)->n -= cast_sizet(i))
#define lumZ_resetbuffer(buff) ((buff)->n = 0)


#define lumZ_resizebuffer(L, buff, size) \
	((buff)->buffer = lumM_reallocvchar(L, (buff)->buffer, \
				(buff)->buffsize, size), \
	(buff)->buffsize = size)

#define lumZ_freebuffer(L, buff)	lumZ_resizebuffer(L, buff, 0)


LUMI_FUNC void lumZ_init (lum_State *L, ZIO *z, lum_Reader reader,
                                        void *data);
LUMI_FUNC size_t lumZ_read (ZIO* z, void *b, size_t n);	/* read next n bytes */

LUMI_FUNC const void *lumZ_getaddr (ZIO* z, size_t n);


/* --------- Private Part ------------------ */

struct Zio {
  size_t n;			/* bytes still unread */
  const char *p;		/* current position in buffer */
  lum_Reader reader;		/* reader function */
  void *data;			/* additional data */
  lum_State *L;			/* Lum state (for reader) */
};


LUMI_FUNC int lumZ_fill (ZIO *z);

#endif
