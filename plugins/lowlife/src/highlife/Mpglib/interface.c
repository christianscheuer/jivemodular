/*
 * External functions declared as __declspec(dllexport)
 * to work in a Win32 DLL (use mpglibdll.h to access)
 */

#include <stdlib.h>
#include <stdio.h>

#include "mpg123.h"
#include "mpglib.h"


/* Global mp .. it's a hack */
struct mpstr *gmp;


inline int InitMP3 (struct mpstr *mp)
{
	memset(mp,0,sizeof(struct mpstr));

	mp->framesize = 0;
	mp->fsizeold = -1;
	mp->bsize = 0;
	mp->head = mp->tail = NULL;
	mp->fr.single = -1;
	mp->bsnum = 0;
	mp->synth_bo = 1;

	make_decode_tables(32767);
	init_layer3(SBLIMIT);

	mp->fr.II_sblimit=SBLIMIT;
	init_layer2();

	return !0;
}

inline void  ExitMP3(struct mpstr *mp)
{
	struct buf *b,*bn;

	b = mp->tail;
	while(b) {
		free(b->pnt);
		bn = b->next;
		free(b);
		b = bn;
	}
}

inline static struct buf *addbuf(struct mpstr *mp,char *buf,int size)
{
	struct buf *nbuf;

	nbuf = (struct buf*) malloc( sizeof(struct buf) );
	if(!nbuf) {
#ifndef BE_QUIET
		fprintf(stderr,"Out of memory!\n");
#endif
		return NULL;
	}
	nbuf->pnt = (unsigned char*) malloc(size);
	if(!nbuf->pnt) {
		free(nbuf);
		return NULL;
	}
	nbuf->size = size;
	memcpy(nbuf->pnt,buf,size);
	nbuf->next = NULL;
	nbuf->prev = mp->head;
	nbuf->pos = 0;

	if(!mp->tail) {
		mp->tail = nbuf;
	}
	else {
	  mp->head->next = nbuf;
	}

	mp->head = nbuf;
	mp->bsize += size;

	return nbuf;
}

inline static void remove_buf(struct mpstr *mp)
{
  struct buf *buf = mp->tail;

  mp->tail = buf->next;
  if(mp->tail)
    mp->tail->prev = NULL;
  else {
    mp->tail = mp->head = NULL;
  }

  free(buf->pnt);
  free(buf);

}

inline static int read_buf_byte(struct mpstr *mp)
{
	unsigned int b;

	int pos;

	pos = mp->tail->pos;
	while(pos >= mp->tail->size) {
		remove_buf(mp);
		pos = mp->tail->pos;
		if(!mp->tail) {
#ifndef BE_QUIET
			fprintf(stderr,"Fatal error!\n");
#endif
			exit(1);
		}
	}

	b = mp->tail->pnt[pos];
	mp->bsize--;
	mp->tail->pos++;


	return b;
}

inline static void read_head(struct mpstr *mp)
{
	unsigned long head;

	head = read_buf_byte(mp);
	head <<= 8;
	head |= read_buf_byte(mp);
	head <<= 8;
	head |= read_buf_byte(mp);
	head <<= 8;
	head |= read_buf_byte(mp);

	mp->header = head;
}

inline int decodeMP3(struct mpstr *mp, char *in, int isize,
		char *out, int osize, int *done)
{
	int len;

	gmp = mp;

	if(osize < 4608) {
#ifndef BE_QUIET
		fprintf(stderr,"To less out space\n");
#endif
		return MP3_ERR;
	}

	if(in) {
		if(addbuf(mp, in, isize) == NULL) {
			return MP3_ERR;
		}
	}


	/* First decode header */
	if(mp->framesize == 0) {
		if(mp->bsize < 4) {
			return MP3_NEED_MORE;
		}
		read_head(mp);
		if (decode_header(&mp->fr,mp->header) <= 0 )
            return MP3_ERR;

		mp->framesize = mp->fr.framesize;
	}

	/*	  printf(" fr.framesize = %i \n",mp->fr.framesize);
		  printf(" bsize        = %i \n",mp->bsize);
	*/

	if(mp->fr.framesize > mp->bsize) {
	  return MP3_NEED_MORE;
	}
	wordpointer = mp->bsspace[mp->bsnum] + 512;
	mp->bsnum = (mp->bsnum + 1) & 0x1;
	bitindex = 0;

	len = 0;
	while(len < mp->framesize) {
		int nlen;
		int blen = mp->tail->size - mp->tail->pos;
		if( (mp->framesize - len) <= blen) {
                  nlen = mp->framesize-len;
		}
		else {
                  nlen = blen;
                }
		memcpy(wordpointer+len,mp->tail->pnt+mp->tail->pos,nlen);
                len += nlen;
                mp->tail->pos += nlen;
		mp->bsize -= nlen;
                if(mp->tail->pos == mp->tail->size) {
                   remove_buf(mp);
                }
	}

	*done = 0;
	if(mp->fr.error_protection)
           getbits(16);

	// FOR mpglib.dll: see if error and return it
	if ((&mp->fr)->do_layer(&mp->fr, (unsigned char*) out, done) < 0)
      return MP3_ERR;

	mp->fsizeold = mp->framesize;
	mp->framesize = 0;
	return MP3_OK;
}

inline int set_pointer(long backstep)
{
  unsigned char *bsbufold;
  if(gmp->fsizeold < 0 && backstep > 0) {
#ifndef BE_QUIET
    fprintf(stderr,"Can't step back %ld!\n",backstep);
#endif
    return MP3_ERR;
  }
  bsbufold = gmp->bsspace[gmp->bsnum] + 512;
  wordpointer -= backstep;
  if (backstep)
    memcpy(wordpointer,bsbufold+gmp->fsizeold-backstep,backstep);
  bitindex = 0;
  return MP3_OK;
}


inline void MessageI(int i)
{
	char s[100];
	sprintf(s, "%d", i);
//	MessageBox (NULL, s, "Debug/Integer", 0);
}
