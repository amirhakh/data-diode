#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>
#include "fec.h"

#include "fec.c"



#if 0

static void
mul2(gf *dst1, gf *src1, gf c, int sz)
{
    USE_GF_MULC ;

    GF_MULC0(c) ;

    if(((unsigned long)dst1 % 8) || ((unsigned long)src1 % 8) || (sz % 8)) {
	slow_mul1(dst1, src1, c, sz);
	return;
    }

    asm volatile("    xorl %%eax,%%eax;\n"
		 "    xorl %%edx,%%edx;\n"
		 "1: "
		 "    addl  $8, %%edi;\n"
		 
		 "    movb  (%%esi), %%al;\n"
		 "    movb 4(%%esi), %%dl;\n"
		 "    movb  (%%ebx,%%eax), %%al;\n"
		 "    movb  (%%ebx,%%edx), %%dl;\n"
		 "    movb  %%al,  (%%edi);\n"
		 "    movb  %%dl, 4(%%edi);\n"
		 
		 "    movb 1(%%esi), %%al;\n"
		 "    movb 5(%%esi), %%dl;\n"
		 "    movb  (%%ebx,%%eax), %%al;\n"
		 "    movb  (%%ebx,%%edx), %%dl;\n"
		 "    movb  %%al, 1(%%edi);\n"
		 "    movb  %%dl, 5(%%edi);\n"
		 
		 "    movb 2(%%esi), %%al;\n"
		 "    movb 6(%%esi), %%dl;\n"
		 "    movb  (%%ebx,%%eax), %%al;\n"
		 "    movb  (%%ebx,%%edx), %%dl;\n"
		 "    movb  %%al, 2(%%edi);\n"
		 "    movb  %%dl, 6(%%edi);\n"
		 
		 "    movb 3(%%esi), %%al;\n"
		 "    movb 7(%%esi), %%dl;\n"
		 "    addl  $8, %%esi;\n"
		 "    movb  (%%ebx,%%eax), %%al;\n"
		 "    movb  (%%ebx,%%edx), %%dl;\n"
		 "    movb  %%al, 3(%%edi);\n"
		 "    movb  %%dl, 7(%%edi);\n"
		 
		 "    cmpl  %%ecx, %%esi;\n"
		 "    jb 1b;\n"
		 
		 : : 
		 
		 "b" (__gf_mulc_),
		 "D" (dst1-8),
		 "S" (src1),
		 "c" (sz+src1) :
		 "memory", "eax", "edx"
	);
}

#endif

static inline long long rdtsc(void)
{
    unsigned long low, hi;
    asm volatile ("rdtsc" : "=d" (hi), "=a" (low));
    return ( (((long long)hi) << 32) | ((long long) low));
}


static inline void prefetch0(char *ptr) {
    asm volatile ("prefetcht0 (%%eax)" : : "a" (ptr));
}

static inline void prefetch1(char *ptr) {
    asm volatile ("prefetcht1 (%%eax)" : : "a" (ptr));
}

static inline void prefetch2(char *ptr) {
    asm volatile ("prefetcht2 (%%eax)" : : "a" (ptr));
}

static inline void prefetchnta(char *ptr) {
    asm volatile ("prefetchnta (%%eax)" : : "a" (ptr));
}

unsigned long long globalstart;
int timeptr=0;
unsigned long long times[1024];

#if 0
#define MYTICK() 1
#define MYTOCK() 1

#else
#define MYTICK() globalstart=rdtsc()
#define MYTOCK() times[timeptr++]=rdtsc()-globalstart
#endif

#if 0
int main(int argc, char **argv) {
    int width=atoi(argv[1]);
    fec_code_t fec = fec_new(width, width*2);

    fast_fec_new(width);

    if(width > 16)
	width=16;

    printf("w=%d\n", width);

    fec_print(fec, width);
    return 0;
}

#endif

#define SIZE (8192)


/*
static unsigned char highbit_test[] __attribute__ ((aligned (16))) =
{ 0x80, 0x80, 0x80, 0x80, 
  0x80, 0x80, 0x80, 0x80 };

static unsigned char multable[16] __attribute__ ((aligned (16)));
*/

//static unsigned char test_result[16] __attribute__ ((aligned (16)));

static void emms(void) {
    asm volatile("emms");
}

#define BITBUFSIZE 512

static unsigned char exp8[(255+8)*16] __attribute__ ((aligned (4096)));

static void initFastTable(void) {
    int i;
    unsigned char *ptr=exp8;

    for(i=0; i< (255+8); i++) {
	int j;
	for(j=0; j<8; j++, ptr++)
	    *ptr = gf_exp[(254+8-i)%255];
    }
}



static unsigned char dst2[SIZE] __attribute__ ((aligned (4096)));

static void mmx_addmul1(gf *dst1, gf *src1, gf c, int sz)
{
    /* Register allocation:
     * mm0: source
     * mm1: target
     * eax: high-bit bitmask
     * ebx: table base-pointer
     */

#if 0
    fprintf(stderr, "src=%p dst=%p %p %d\n", src1, dst1,
	    mul_results, mul_results[64]);
#endif
#if 1
    /* first initialize bitmap */
    MYTICK();
    asm volatile(
	"1:"
	"	movq         (%%esi),%%mm2;\n"	    
	"	movq         (%%edi),%%mm1;\n"
	"   movq          %%mm2,%%mm3;\n"
	
	"   pcmpgtb	      %%mm0,%%mm3;\n"
	"   pandn	    (%%ebx),%%mm3;\n"
	"   psllw        $1,%%mm2;\n"
	"   movq          %%mm2,%%mm4;\n"
	"   pxor	      %%mm3,%%mm1;\n"
	
	"   pcmpgtb	      %%mm0,%%mm3;\n"
	"   pandn	 0x08(%%ebx),%%mm4;\n"
	"   psllw        $1,%%mm2;\n"
	"   movq          %%mm2,%%mm3;\n"
	"   pxor	      %%mm4,%%mm1;\n"
	
	"   pcmpgtb	      %%mm0,%%mm3;\n"
	"   psllw        $1,%%mm2;\n"
	"   pandn	 0x10(%%ebx),%%mm3;\n"
	"   movq          %%mm2,%%mm4;\n"
	"   pxor	      %%mm3,%%mm1;\n"

	"   pcmpgtb	      %%mm0,%%mm3;\n"
	"   psllw        $1,%%mm2;\n"
	"   pandn	 0x18(%%ebx),%%mm4;\n"
	"   movq          %%mm2,%%mm3;\n"
	"   pxor	      %%mm4,%%mm1;\n"
	
	"   pcmpgtb	      %%mm0,%%mm3;\n"
	"   pandn	 0x20(%%ebx),%%mm3;\n"
	"   psllw        $1,%%mm2;\n"
	"   movq          %%mm2,%%mm4;\n"
	"   pxor	      %%mm3,%%mm1;\n"
	
	"   pcmpgtb	      %%mm0,%%mm3;\n"
	"   psllw        $1,%%mm2;\n"
	"   pandn	 0x28(%%ebx),%%mm4;\n"
	"   movq          %%mm2,%%mm3;\n"
	"   pxor	      %%mm4,%%mm1;\n"
	
	"   pcmpgtb	      %%mm0,%%mm3;\n"
	"   psllw        $1,%%mm2;\n"
	"   pandn	 0x30(%%ebx),%%mm3;\n"
	
	"   pcmpgtb	      %%mm0,%%mm2;\n"
	"   pxor	      %%mm3,%%mm1;\n"
	"   pandn	 0x38(%%ebx),%%mm4;\n"
	"   addl           $8,%%edi;\n"
	"   pxor	      %%mm4,%%mm1;\n"
	"   addl           $8,%%esi;\n"
	
	"   movq      %%mm1,-8(%%edi);\n"
	
	"	cmpl %%ecx,%%esi;\n"
	"	jb 1b;\n" :  :
	"b" (exp8+(255-gf_log[c])*8),
	"c" (sz+src1), "S" (src1), "D" (dst1) : "eax");
    MYTOCK();
#endif
}


#if 1
/* ENCDEC */

void print(unsigned char *data, int s) {
    int i;
    for(i=0; i<s; i++)
	fprintf(stderr,"%02x ", data[i]);
    fprintf(stderr, "\n");
}

/* example: ./fec-test 10 256 10 </bin/ls >/tmp/x */
int main(int argc, char **argv) {
    struct stat buf;
    int size;
    int redundancy=atoi(argv[1]);
    int blocksize=atoi(argv[2]);
    int corrupted=atoi(argv[3]);
    unsigned char *data, *fec_data, *fec_data2;
    unsigned char *data_blocks[128], *fec_blocks[128], *dec_fec_blocks[128];
    unsigned int fec_block_nos[128], erased_blocks[128];
    int fec_size;
    int n;
    int nrBlocks;
    int i,j;
    int zilch[1024];
    struct timeval tv;
    int seed;
    long long begin, end;
    
#if 1
    gettimeofday(&tv, 0);
    seed = tv.tv_sec ^ tv.tv_usec;
#endif
    seed=996035588;
    srandom(seed);
    fprintf(stderr,"%d\n", seed);


    fstat(0, &buf);
    size=buf.st_size;
    if(size > blocksize * 128)
	size = blocksize * 128;
    nrBlocks = (size+blocksize-1)/blocksize;
    fprintf(stderr, "Size=%d nr=%d\n", size, nrBlocks);

    data = xmalloc(size+4096);
    data += 4096 - ((unsigned long) data) % 4096; 
    fec_size = blocksize*redundancy;
    fec_data = xmalloc(fec_size+4096);
    fec_data += 4096 - ((unsigned long) fec_data) % 4096; 
    fec_data2 = xmalloc(fec_size+4096);
    fec_data2 += 4096 - ((unsigned long) fec_data2) % 4096; 
    n = read(0, data, size);
    if(n < size) {
	fprintf(stderr, "Short read\n");
	exit(1);
    }

    begin = rdtsc(0);
    init_fec();
    initFastTable();
    end = rdtsc(1);
    fprintf(stderr, "%d cycles to create FEC buffer\n",
	    (unsigned int) (end-begin));
    for(i=0, j=0; i<size; i+=blocksize, j++) {
	data_blocks[j] = data + i;
    }
    for(i=0, j=0; j<redundancy; i+=blocksize, j++) {
	fec_blocks[j] = fec_data + i;
    }

    begin = rdtsc();
    fec_encode(blocksize, data_blocks, nrBlocks, fec_blocks, redundancy);
    end = rdtsc();

    fprintf(stderr,"times %ld %f %f\n", 
	    (unsigned long) (end-begin),
	    ((double) (end-begin)) / 
	    blocksize / nrBlocks / redundancy,
	    ((double) (end-begin)) / blocksize / nrBlocks);

    fprintf(stderr, "old:\n");

    for(i=0; i<1024; i++)
	zilch[i]=0;

    for(i=0; i < corrupted; i++) {	
	int corr = random() % nrBlocks;
	memset(data+blocksize*corr,137,blocksize);
	fprintf(stderr, "Corrupting %d\n", corr);
	zilch[corr]=1;
    }

    {
	int i;
	int fec_pos=0;
	int nr_fec_blocks=0;

	for(i=0; i<nrBlocks; i++) {
	    if(zilch[i]) {
		fec_pos++;
		if(random() % 2)
		    fec_pos++;
		erased_blocks[nr_fec_blocks] = i;
		fec_block_nos[nr_fec_blocks] = fec_pos;
		fprintf(stderr, "Fixing %d with %d (%p)\n",
			i, fec_pos, fec_blocks[fec_pos]);		
		dec_fec_blocks[nr_fec_blocks] = fec_blocks[fec_pos];
		nr_fec_blocks++;
		assert(fec_pos <= redundancy);
		assert(nr_fec_blocks <= redundancy);
	    }
	}
#if 0
	for(stripe=0; stripe<stripes; stripe++) {
	    fprintf(stderr, "stripe%d: ", stripe);
	    for(i=0; i<fec_blocks[stripe]; i++) {
		int pos = i * stripes+stripe;
		fprintf(stderr, "%p.%d.%d ",d[pos].adr, 
		       d[pos].erasedBlockNo, d[pos].fecBlockNo);
	    }
	    fprintf(stderr,"\n");
	}
#endif	
	begin = rdtsc();
	fec_decode(blocksize, data_blocks, nrBlocks, 
		   dec_fec_blocks, fec_block_nos, erased_blocks, 
		   nr_fec_blocks);
	end = rdtsc();
	fprintf(stderr,"times %ld %f %f\n", 
		(unsigned long) (end-begin),
		((double) (end-begin)) / 
		blocksize / nrBlocks / redundancy,
		((double) (end-begin)) / blocksize / nrBlocks);	
    }
    /* printDetail();*/

    write(1, data, size);
    exit(0);
}



#endif




#if 0

/* XORASM */






#define TDIFF(a,b) \
    (((a).tv_sec - (b).tv_sec)*1000000+(a).tv_usec-(b).tv_usec)

    unsigned char block[SIZE*5] __attribute__((aligned(16)));
    unsigned char block2[SIZE*5] __attribute__((aligned(16)));
    unsigned char block3[SIZE*5] __attribute__((aligned(16)));
    unsigned char block4[SIZE*5] __attribute__((aligned(16)));


    unsigned char in[16] __attribute__((aligned(16)));
    unsigned char out1[16] __attribute__((aligned(16)));
    unsigned char out2[16] __attribute__((aligned(16)));


int main(int argc, char **argv) {
    int i;
    int x=0;
    long long lbegin;
    long long lend;
    unsigned long diffc,diffa,diffcp,diffm;

    /*
    for(i=0; i<8; i++)
	in[i] = i | 0x40;

    asm volatile("      xorl %%eax,%%eax;\n"
		 "      xorl %%edx,%%edx;\n"

		 "      pxor %%mm2, %%mm2;\n"
		 "1:"
		 "	movq in, %%mm0;\n"
		 "	movq %%mm0, %%mm1;\n"
		 "      punpcklbw %%mm2, %%mm0;\n"
		 "      punpckhbw %%mm2, %%mm1;\n"
		 "	packuswb %%mm1, %%mm0;\n"
		 "	movq %%mm0, out1;\n"
		 "	movq %%mm1, out2;\n"
	: : "S" (1) );

    for(i=0; i<8; i++)
	fprintf(stderr, "%2d %02x %02x\n", i, out1[i],out2[i]);
    exit(0);
    */

    init_fec();
    initFastTable();

    for(i=0; i<SIZE*5; i++) {
	block[i] = random();
	block2[i] = block[i];
	block3[i] = block[i];
	block4[i] = block[i];
    }


    usleep(1);
    for(i=0; i<SIZE; i+=32)
	x+=block[SIZE+i];
    for(i=0; i<SIZE; i+=32)
	x+=block[3*SIZE+i];
    for(i=0; i<256; i+=32)
	x+=gf_mul_table[256*0x33+i];
    // gettimeofday(&begin,0);
    lbegin = rdtsc();
    slow_addmul1(block+SIZE, block+3*SIZE, 0x33, SIZE);
    lend = rdtsc();
    diffc = (unsigned long)(lend - lbegin);
    // gettimeofday(&end,0);
    //printf("ref time=%ld\n", TDIFF(end, begin));


    usleep(1);

#if 1
     for(i=0; i<SIZE; i+=32) {
	x+=block2[SIZE+i];
	x+=block2[3*SIZE+i];
    }
    for(i=0; i<256; i+=32)
	x+=gf_mul_table[256*0x33+i];
#endif
    lbegin = rdtsc();
    addmul1(block2+SIZE, block2+3*SIZE, 0x33, SIZE);
    lend = rdtsc();
    diffa = (unsigned long)(lend - lbegin);

    usleep(10000);
    sleep(1);
#if 1
     for(i=0; i<SIZE; i+=32) {
	 x+=block4[SIZE+i];
	 x+=dst2[i];
	 x+=block4[3*SIZE+i];
     }
     for(i=0; i<(255+8)*16; i+=32)
	 prefetchnta(&exp8[i]);
//	 x+=exp16[i];
#endif
     //block4[SIZE]=0;
     //block4[3*SIZE]=0x1;

    lbegin = rdtsc();
    mmx_addmul1(block4+SIZE, block4+3*SIZE, 0x33, SIZE);
    lend = rdtsc();
    diffm = (unsigned long)(lend - lbegin);

    emms();

     printf("x=%d\n",x);

    //x+=block3[SIZE];

//    block3[3*SIZE]=55;
    //  memcpy(block3+SIZE, block3+3*SIZE, SIZE);


    for(i=0; i<SIZE*5; i++) {
	if(block[i] != block2[i]) {
	    fprintf(stderr, "%05d %02x %02x %02x\n", i, 
		    block[i], block2[i], block3[i]);
	    break;
	}
    }

    for(i=0; i<SIZE*5; i++) {
	if(block[i] != block4[i]) {
	    fprintf(stderr, "mmx %05d %02x %02x %02x\n", i, 
		    block[i], block4[i], block3[i]);
	    break;
	}
    }

#if 1
    for(i=0; i<SIZE; i+=32) {
	x+=block3[SIZE+i];
	x+=block3[3*SIZE+i];
    }
#endif
    lbegin = rdtsc();
    memcpy(block3+SIZE, block3+3*SIZE, SIZE);
    lend = rdtsc();
    diffcp = (unsigned long)(lend - lbegin);

    printf("  c=%ld %f\n", diffc, ((float)diffc)/SIZE);
    printf("asm=%ld %f\n", diffa, ((float)diffa)/SIZE);
    printf("mmx=%ld %f\n", diffm, ((float)diffm)/SIZE);
    printf("cpy=%ld %f\n", diffcp, ((float)diffcp)/SIZE);


    for(i=0; i<timeptr; i++)
	printf("%d %lld\n", i, times[i]);

    printf("%d\n",x);
    return 0;
}

#endif

#if 0

int main(int argc, char **argv) {
    char testing[] = "linux for ever";

    int i = atoi(argv[1]);
    char result='X';

    asm("addl %%ecx, %%ebx; \n"
	"movb (%%ebx), %%al" : 
	"=al" (result) : 
	"ebx" (i), "ecx" (testing):
	"esi");
    
    printf("%c\n", result);
    return 0;
}

#endif
