#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include "mm.h"
#include "memlib.h"

#define DEBUG
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
#else
# define dbg_printf(...)
#endif

#ifdef DRIVER
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif 

#define WSIZE 4
#define OVERHEAD  8
#define DSIZE 8
#define PACK(size,alloc) ((size)|(alloc))
#define GET(p)       (*(size_t *)(p))
#define PUT(p, val)  (*(size_t *)(p) = (val))
#define GET_SIZE(p)  (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define HDRP(bp)       ((char *)(bp) - WSIZE)
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define FL_NEXT(bp)       ((char *)(bp) )
#define FL_PREV(bp)       ((char *)(bp) + WSIZE)
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

#define NEXT_FLKP(bp) GET((char*)(bp))
#define PREV_FLKP(bp) GET((char*)(bp)+WSIZE)

void extend(size_t asize);
void *place(void *bp,size_t asize);
void *malloc(size_t size);
static void *find_fit(size_t asize);
static void merge(void *bp , size_t size, int state);
static void print(void *bp);

void *init;			//list

//Init
int mm_init(void)
{
	void *bp;
	init = NULL;
	bp = mem_sbrk(WSIZE*4);
	PUT(bp,0);
	PUT(bp+WSIZE,PACK(8,1));
	PUT(bp+DSIZE,PACK(8,1));
	PUT(bp+WSIZE+DSIZE,PACK(0,1));
}

void *malloc(size_t size)
{
//	dbg_printf("\n\t\t\t\t\t\tmalloc\n");
	if ( size <= 0 )
		return ;

	void *bp;
	size_t asize;
	if ( size <= DSIZE)
		asize = DSIZE+OVERHEAD;
	else
		asize = DSIZE * (( size + (OVERHEAD) + (DSIZE-1))/DSIZE);
	while(1)
	{
		if( init )			//exist
		{
			if ( ( bp = find_fit(asize)) != NULL )
			{
				bp =  place(bp,asize);
				if ( bp == NULL )
					extend(asize);
				else
					return bp;
			}
			else
				extend(asize);
		}
		else
			extend(asize);
	}
}
void *place(void *bp,size_t asize)
{
//	dbg_printf("place\n");
	void *p = PREV_FLKP(bp);		//preview free_block
	void *n = NEXT_FLKP(bp);		//next free_block
	void *return_bp = bp;
	size_t size = GET_SIZE(HDRP(bp));
//	dbg_printf("size : %d\tasize : %d\n",size,asize);
	PUT(HDRP(bp),PACK(asize,1));
	PUT(FTRP(bp),PACK(asize,1));
	bp = NEXT_BLKP(bp);
	
//	PUT(HDRP(bp),PACK(size-asize,0));
//	PUT(FTRP(bp),PACK(size-asize,0));

	if( ( size-asize ) == 0 )
	{
//		dbg_printf("p : %d\tn : %d\n",p,n);
		if ( p == NULL && n == NULL )
		{
			init = NULL;
		}
		else if ( p != NULL && n != NULL)
		{
			PUT(FL_NEXT(p),n);
			PUT(FL_PREV(n),p);
		}
		else if ( p!= NULL && n == NULL )
		{
			PUT(FL_NEXT(p),n);
		}
		else if ( p==NULL && n!=NULL)
		{
			init = n;
			PUT(FL_PREV(n),p);
		}
	}
	else
	{
		PUT(HDRP(bp),PACK(size-asize,0));
		PUT(FTRP(bp),PACK(size-asize,0));
//		dbg_printf("p : %d\tn : %d\n",p,n);
		
		if ( p == NULL && n == NULL)
		{
			init = bp;
			PUT(FL_NEXT(bp),NULL);
			PUT(FL_PREV(bp),NULL);
		}
		else if ( p == NULL && n != NULL )
		{
			
			
			init = bp;
			PUT(FL_PREV(bp),p);
			PUT(FL_NEXT(bp),n);
			PUT(FL_PREV(n),bp);
		}
		else if ( p != NULL && n == NULL )
		{
			PUT(FL_PREV(bp),p);
			PUT(FL_NEXT(p),bp);
			PUT(FL_NEXT(bp),n);
		}
		else if ( p != NULL && n != NULL )
		{
			PUT(FL_PREV(bp),p);
			PUT(FL_NEXT(p),bp);
			PUT(FL_PREV(n),bp);
			PUT(FL_NEXT(bp),n);
		}
	}
	return return_bp;
}
void extend(size_t asize)
{
//	dbg_pritnf("extend\n");
	void *bp;
	bp = mem_sbrk(asize);
//	dbg_printf("bp : %d\tasize : %d\n",bp,asize);
	PUT(HDRP(bp),PACK(asize,0));
	PUT(FTRP(bp),PACK(asize,0));
	PUT(HDRP(NEXT_BLKP(bp)),PACK(0,1));
	free(bp);
}
void free(void *bp)
{
//	dbg_printf("free\n");
	if ( !bp )
		return NULL;
 	int next_alloc_state = GET_ALLOC(HDRP(NEXT_BLKP(bp)));		//decide block state
	int prev_alloc_state = GET_ALLOC(FTRP(PREV_BLKP(bp)));	   //decide block state
	int size = GET_SIZE(HDRP(bp));
	PUT(HDRP(bp),PACK(size,0));
	PUT(FTRP(bp),PACK(size,0));
	
	if ( prev_alloc_state && next_alloc_state )
		merge(bp,size,1);
	else if ( prev_alloc_state && !next_alloc_state)
		merge(bp,size,2);
	else if ( !prev_alloc_state && next_alloc_state)
		merge(bp,size,3);
	else if ( !prev_alloc_state && !next_alloc_state)
		merge(bp,size,4);
//	dbg_printf("~~~\n");
//	print(init);
//	dbg_printf("~~~\n");
		
return ;
}

void merge(void *bp , size_t size, int state)
{
	void *next_free,*prev_free;
	void *next_block, *prev_block;
	int current_size = GET_SIZE(HDRP(bp));
//	dbg_printf("merge\n");
	switch (state)		//X means current blcok
	{
	case 1:		//AXA
		{
	//		dbg_printf("case : AXA\n");
			PUT(FL_NEXT(bp),init);
			PUT(FL_PREV(bp),NULL);
			init = bp;
			if ( NEXT_FLKP(bp))
				PUT(FL_PREV(NEXT_FLKP(bp)),bp);
			return ;
		}
		
	case 2:		//AXF
		{
	//		dbg_printf("case2 : AXF\n");
			next_free = NEXT_FLKP(NEXT_BLKP(bp));
			prev_free = PREV_FLKP(NEXT_BLKP(bp));
			if ( prev_free == NULL && next_free == NULL )
			{
				init = NULL;
			}
			else if ( prev_free == NULL && next_free != NULL)
			{	
				init = next_free;
				PUT(FL_PREV(next_free),bp);
			}
			else if ( prev_free != NULL && next_free == NULL )
			{
				PUT(FL_NEXT(prev_free),next_free);
			}
			else if ( prev_free != NULL && next_free != NULL)
			{
				PUT(FL_NEXT(prev_free),next_free);
				PUT(FL_PREV(next_free),prev_free);
			}
			

			size+=GET_SIZE(HDRP(NEXT_BLKP(bp)));
			
			PUT(HDRP(bp),PACK(size,0));
			PUT(FTRP(bp),PACK(size,0));
		
			PUT(FL_NEXT(bp),init);
			PUT(FL_PREV(bp),NULL);
			if ( (NEXT_FLKP(bp)) )
				PUT(FL_PREV(NEXT_FLKP(bp)),bp);
			init = bp;
			return ;
		}
	case 3:		//FXA
		{
	//		dbg_printf("case3 : FXA\n");
			prev_block = PREV_BLKP(bp);
			next_free = NEXT_FLKP(PREV_BLKP(bp));
			prev_free = PREV_FLKP(PREV_BLKP(bp));
			if ( prev_free == NULL && next_free == NULL )
			{
				init = NULL;
			}
			else if ( prev_free == NULL && next_free != NULL )
			{
				init = next_free;
				PUT(FL_PREV(next_free),prev_block);
			}
			else if ( prev_free != NULL && next_free == NULL )
			{
				PUT(FL_NEXT(prev_free),next_free);
			}
			else if ( prev_free != NULL && next_free != NULL )
			{
				PUT(FL_NEXT(prev_free),next_free);
				PUT(FL_PREV(next_free),prev_free);
			}

			size+=GET_SIZE(HDRP(PREV_BLKP(bp)));
			PUT(FTRP(bp),PACK(size,0));
			PUT(HDRP(PREV_BLKP(bp)),PACK(size,0));

			PUT(FL_NEXT(prev_block),init);
			PUT(FL_PREV(prev_block),NULL);
						
						   
			if ( NEXT_FLKP(prev_block))
				PUT(FL_PREV(NEXT_FLKP(prev_block)),prev_block);
			
			init = prev_block;
			return ;
		}
	case 4:		//FXF
		{
//			dbg_printf("case4 : FXF\n");
			next_free = NEXT_FLKP(PREV_BLKP(bp));
			prev_free = PREV_FLKP(PREV_BLKP(bp));

			if ( prev_free == NULL && next_free == NULL)
			{
				init = NULL;
			}
			else if ( prev_free == NULL && next_free != NULL )
			{
				init = (next_free);
				if ( init == NEXT_BLKP(bp) )
				{
					init = NEXT_FLKP(NEXT_BLKP(bp));
					if ( init != NULL )
						PUT(FL_PREV(NEXT_FLKP(NEXT_BLKP(bp))),PREV_BLKP(bp));
				}
				else
				{
					PUT(FL_PREV(next_free),PREV_BLKP(bp));
				}
					
			}
			else if ( prev_free != NULL && next_free == NULL )
			{
				PUT(FL_NEXT(prev_free),next_free);
			}
			else if ( prev_free != NULL && next_free != NULL )
			{
				PUT(FL_NEXT(prev_free),next_free);
				PUT(FL_PREV(next_free),prev_free);
			}
			
			next_free = NEXT_FLKP(NEXT_BLKP(bp));
			prev_free = PREV_FLKP(NEXT_BLKP(bp));
			if ( prev_free == NULL && next_free == NULL )
			{
				init = NULL;
			}
			else if ( prev_free == NULL && next_free != NULL )
			{
				init = next_free;
				if ( next_free == PREV_BLKP(bp) )
				{
					init = NEXT_FLKP(PREV_BLKP(bp));
					if ( init != NULL)
						PUT(FL_PREV(NEXT_FLKP(PREV_BLKP(bp))),PREV_BLKP(bp));
				}
				else
				{
				PUT(FL_PREV(next_free),PREV_BLKP(bp));
				}
			}
			else if ( prev_free != NULL && next_free == NULL )
			{
				PUT(FL_NEXT(prev_free),next_free);
			}
			else if ( prev_free != NULL && next_free != NULL )
			{
				PUT(FL_NEXT(prev_free),next_free);
				PUT(FL_PREV(next_free),prev_free);
			}
	
			void *a;
			a = PREV_BLKP(bp);
			size+=GET_SIZE(HDRP(PREV_BLKP(bp)))+GET_SIZE(FTRP(NEXT_BLKP(bp)));
			PUT(HDRP(PREV_BLKP(bp)),PACK(size,0));
			PUT(FTRP(NEXT_BLKP(bp)),PACK(size,0));
			PUT(FL_NEXT(a),init);
			PUT(FL_PREV(a),NULL);
			init = a;
			if ( NEXT_FLKP(a))
				PUT(FL_PREV(NEXT_FLKP(a)),a);
			return ;
		}
	}
}
void *find_fit(size_t asize)
{
	void *bp;
//print(init);	
	for ( bp = init ; bp != NULL ; bp = NEXT_FLKP(bp) ) 
	{
		if ( (GET_SIZE(HDRP(bp)) >= asize+16) || (GET_SIZE(HDRP(bp)) == asize) )
			return bp;
	}
		return NULL;
}





















static void print(void *bp)
{
	if( bp )
	{
		dbg_printf("prev : %d\tcurrent : %d\tnext : %d\tsize : %d\n",PREV_FLKP(bp),bp,NEXT_FLKP(bp),GET_SIZE(HDRP(bp)));
		print(NEXT_FLKP(bp));
	}
}
void *calloc(size_t nmemb, size_t size)
{
	return NULL;
}
void *realloc(void *oldptr, size_t size)
{
//	dbg_printf("realloc ���� �ΰ�\n");
	if ( oldptr == NULL)
		return malloc(size);
	size_t oldsize=GET_SIZE(HDRP(oldptr));
	void *newptr;
	if ( size <= 0 )
	{
		free(oldptr);
		return 0;
	}
//	if (size == oldsize)
//		return oldptr;
	newptr = malloc(size);
	if( size < oldsize)
		oldsize = size;

	memcpy(newptr,oldptr,oldsize);
	free(oldptr);
	return newptr;
}

