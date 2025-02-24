/**
 * fwq.c : Fixed Work Quantum microbenchmark
 *
 * Written by Matthew Sottile (matt@cs.uoregon.edu)
 * Modified by Mark Seager (seager@llnl.gov)
 *
 * This is a complete rewrite of the original ftq code by
 * Ron and Matt, in order to use the fixed amount of work quanta rather than
 * a fixed time quanta
 *
 * 12/30/2007 : Updated to add pthreads support for FTQ execution on
 *              shared memory multiprocessors (multicore and SMPs).
 *              Also, removed unused TAU support.
 * 06/05/2008 : Rewritten to be fixed work quanta.
 *
 * Licensed under the terms of the GNU Public Licence.  See LICENCE_GPL 
 * for details.
 */
#define _GNU_SOURCE
#include "ftq.h"

/* affinity */
#ifdef _WITH_PTHREADS_
#include <sys/syscall.h>
#include <sys/types.h>
#include <sched.h>
#endif

/**
 * macros and defines
 */

/** defaults **/
#define MAX_SAMPLES    2000000
#define MIN_SAMPLES    1000
#define DEFAULT_COUNT  10000
#define DEFAULT_BITS   20
#define MAX_BITS       30
#define MIN_BITS       3
#define MULTIITER
#define ITERCOUNT      32
#define VECLEN         1024

/**
 * global variables
 */

/* samples: each sample has a timestamp and a work count. */
static unsigned long long *samples;
static long long work_length;
static int work_bits = DEFAULT_BITS;
static unsigned long numsamples = DEFAULT_COUNT;

/**
 * usage()
 */
void usage(char *av0) {
#ifdef _WITH_PTHREADS_
  fprintf(stderr,"usage: %s [-t threads] [-n samples] [-w bits] [-h] [-o outname] [-s]\n",
	  av0);
#else
  fprintf(stderr,"usage: %s [-n samples] [-w bits] [-h] [-o outname] [-s]\n",
	  av0);
#endif
  exit(EXIT_FAILURE);
}

/*************************************************************************
 * FWQ core: does the measurement                                        *
 *************************************************************************/
void *fwq_core(void *arg) {
  /* thread number, zero based. */
  int thread_num = (int)(intptr_t)arg;
  int i=0,offset;

  ticks tick, tock;
  register unsigned long done;
  register long long count;
  register long long wl = -work_length;
#ifdef DAXPY
  double da, dx[VECLEN], dy[VECLEN];
  void daxpy();
#endif

  printf("Starting FWQ_CORE with work_length = %lld, wl = %lld, count = %lld\n",
    work_length, wl, count);
#ifdef DAXPY
  /* Intialize FP work */
  da = 1.0e-6;
  for( i=0; i<VECLEN; i++ ) {
    dx[i] = 0.3141592654;
    dy[i] = 0.271828182845904523536;
  }
#endif

#ifdef _WITH_PTHREADS_
  /* affinity stuff */
  cpu_set_t *set;
  int ret;
  size_t size;
  int numthreads = thread_num + 1;

  set = CPU_ALLOC(numthreads);
  size = CPU_ALLOC_SIZE(numthreads);
  CPU_ZERO_S(size, set);
  CPU_SET_S(thread_num, size, set);
  ret = sched_setaffinity(0, size, set);
  if (ret < 0) {
    fprintf(stderr, "failed to set CPU affinity: pid %d, thread: %d, %m\n",
	    getpid(), thread_num);
    exit(1);
  }
#endif

  offset = thread_num * numsamples;

  /***************************************************/
  /* first, warm things up with 1000 test iterations */
  /***************************************************/
  for(done=0; done<1000; done++ ) {

#ifdef __x86_64__
    /* Core work construct written as loop in gas (GNU Assembler) for
       x86-64 in 64b mode with 16 NOPs in the loop. If your running in
       on x86 compatible hardware in 32b mode change "incq" to "incl"
       and "cmpq" to "cmpl".  You can also add/remove "nop"
       instructions to minimize instruction cache turbulence and/or
       increase/decrease the work for each pass of the loop. Verify by
       inspecting the compiler generated assembly code listing.
     */
    count = wl;
    tick = getticks();
      __asm__ __volatile__(
			   "myL1:\tincq %0\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
/* Group of 16 NOPs */
			   "cmpq $0, %1\n\t"
			   "js myL1"
			   : "=r"(count)          /* output */
			   : "0"(count),"r"(count) /* input */
			   );
#elif DAXPY
      /* Work construct based on a function call and a vector update
	 operation. VECLEN should be chosen so that this work
	 construct fits into L1 cache (for all hardware threads
	 sharing a core) and have minimal hardware induced runtime
	 variation.
      */
      count = wl;
      tick = getticks();
      for(count = wl; count<0; count++) {
	daxpy( VECLEN, da, dx, 1, dy, 1 );
      }
#elif defined(__aarch64__)
    /* Core work loop in gas */
    count = wl;
    tick = getticks();
    __asm__ __volatile__(
        "myL1:\n\t"
        "add %0, %0, #1\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "cmp %1, #0\n\t"
        "b.ne myL1"
        : "=r"(count)          /* output */
        : "0"(count), "r"(count)  /* input */
        : "cc", "memory"       /* clobbered registers */
    );
#else
    /* This is the default work construct. Be very careful with this
      as it is most important that "count" variable be in a register
      and the loop not get optimized away by over zealous compiler
      optimizers.  If "count" not in a register you will get alot of
      variations in runtime due to memory latency.  If the loop is
      optimized away, then the sample runtime will be very short and
      not change even if the work length is increased.  For example,
      with gcc v4.3 -g optimization puts "count" in a memory
      location. -O1 and above optimization levels removes the "count"
      loop entirely.  This is why we wrote the assembly code
      above. The only way to verify what is actually happening is to
      carefully review the compiler generated assembly language.
      */
      count = wl;
      tick = getticks();
      for( count=wl; count<0; ) {
#ifdef MULTIITER
	register int k;
	for (k=0;k<ITERCOUNT;k++)
	  count++;
	for (k=0;k<(ITERCOUNT-1);k++)
	  count--;
#else
	count++;
#endif /* MULTIPLIER */
      }
#endif /* ASMx8664 or DAXPY or default */
      tock = getticks();
      samples[offset+done] = tock-tick;
  }

  /****************************/
  /* now do the real sampling */
  /****************************/

  for(done=0; done<numsamples; done++ ) {

#ifdef __x86_64__
    /* Core work loop in gas */
    count = wl;
    tick = getticks();
      __asm__ __volatile__("myL2:\tincq %0\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
			   "nop\n\t"
/* Group of 16 NOPs */
			   "cmpq $0, %1\n\t"
			   "js myL2"
			   : "=r"(count)          /* output */
			   : "0"(count),"r"(count) /* input */
			   );
#elif DAXPY
      /* Core work as function all and 64b FP vector update loop */
      count = wl;
      tick = getticks();
      for(count = wl; count<0; count++) {
	daxpy( VECLEN, da, dx, 1, dy, 1 );
      }
#elif defined(__aarch64__)
    /* Core work loop in gas */
    count = wl;
    tick = getticks();
    __asm__ __volatile__(
        "myL2:\n\t"
        "add %0, %0, #1\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "cmp %1, #0\n\t"
        "b.ne myL2"
        : "=r"(count)          /* output */
        : "0"(count), "r"(count)  /* input */
        : "cc", "memory"       /* clobbered registers */
    );
#else
      /* Default core work loop */
      count = wl;
      tick = getticks();
      for( count=wl; count<0; ) {
#ifdef MULTIITER
	register int k;
	for (k=0;k<ITERCOUNT;k++)
	  count++;
	for (k=0;k<(ITERCOUNT-1);k++)
	  count--;
#else
	count++;
#endif /* MULTIITER */
      }
#endif /* ASMx86 or DAXPY or default */
      tock = getticks();
      samples[offset+done] = tock-tick;
  }

  return NULL;
}
void daxpy( int n, double da, double *dx, int incx, double *dy, int incy )
{
  register int k;
  for( k=0; k<n; k++ ) {
    dx[k] += da*dy[k];
  }
  return;
}

/**
 * main()
 */
int main(int argc, char **argv) {
  /* local variables */
  char fname_times[1024], buf[32], outname[255];
  int i,j;
  int numthreads = 1, use_threads = 0;
  int fp;
  int use_stdout = 0;
#ifdef _WITH_PTHREADS_
  int rc;
  pthread_t *threads;
  cpu_set_t cpu_set;
#endif

  /* default output name prefix */
  sprintf(outname,"fwq");

#ifdef Plan9
  ARGBEGIN{
  case 's':
    use_stdout = 1;
    break;
  case 'o':
    char *tmp = ARGF();
    if(tmp == nil)
      usage(argv0);
      sprintf(outname,"%s",tmp);
    break;
  case 'w':
    work_bits = atoi(ARGF());
    break;
  case 'n':
    numsamples = atoi(ARGF());
    break;
  case 'h':
  default:
    usage(argv0);
  }ARGEND
#else
     /*
      * getopt_long to parse command line options.
      * basically the code from the getopt man page.
      */
     while (1) {
       int c;
       int option_index = 0;
       static struct option long_options[] = {
	 {"help",0,0,'h'},
	 {"numsamples",0,0,'n'},
	 {"work",0,0,'w'},
	 {"outname",0,0,'o'},
	 {"stdout",0,0,'s'},
	 {"threads",0,0,'t'},
	 {0,0,0,0}
       };

       c = getopt_long(argc, argv, "n:hsw:o:t:",
		       long_options, &option_index);
       if (c == -1)
	 break;

       switch (c) {
       case 't':
#ifndef _WITH_PTHREADS_
	 fprintf(stderr,"ERROR: ftq not compiled with pthreads support.\n");
	 exit(EXIT_FAILURE);
#endif
	 numthreads = atoi(optarg);
	 use_threads = 1;
	 break;
       case 's':
	 use_stdout = 1;
	 break;
       case 'o':
	 sprintf(outname,"%s",optarg);
	 break;
       case 'w':
	 work_bits = atoi(optarg);
	 break;
       case 'n':
	 numsamples = atoi(optarg);
	 break;
       case 'h':
       default:
	 usage(argv[0]);
	 break;
       }
     }
#endif /* Plan9 */

  /* sanity check */
  if (numsamples > MAX_SAMPLES) {
    fprintf(stderr,"WARNING: sample count exceeds maximum.\n");
    fprintf(stderr,"         setting count to maximum.\n");
    numsamples = MAX_SAMPLES;
  }
  if (numsamples < MIN_SAMPLES) {
    fprintf(stderr,"WARNING: sample count less than minimum.\n");
    fprintf(stderr,"         setting count to minimum.\n");
    numsamples = MIN_SAMPLES;
  }

  /* allocate sample storage */
  samples = malloc(sizeof(unsigned long long)*numsamples*numthreads);
  assert(samples != NULL);

  if (work_bits > MAX_BITS || work_bits < MIN_BITS) {
    fprintf(stderr,"WARNING: work bits invalid. set to %d.\n", MAX_BITS);
    work_bits = MAX_BITS;
  }

  if (use_threads == 1 && numthreads < 2) {
    fprintf(stderr,"ERROR: >1 threads required for multithread mode.\n");
    exit(EXIT_FAILURE);
  }

  if (use_threads == 1 && use_stdout == 1) {
    fprintf(stderr,"ERROR: cannot output to stdout for multithread mode.\n");
    exit(EXIT_FAILURE);
  }

  /* set up sampling.  first, take a few bogus samples to warm up the
   *  cache and pipeline */
  work_length = 1 << work_bits;

  if (use_threads == 1) {
#ifdef _WITH_PTHREADS_
    CPU_ZERO(&cpu_set);
    CPU_SET(0, &cpu_set);
    if (sched_setaffinity(0, sizeof(cpu_set), &cpu_set) < 0 ) {
      perror("sched_setaffinity");
    }
    threads = malloc(sizeof(pthread_t)*numthreads);
    assert(threads != NULL);

    printf("numthreads = %d\n", numthreads);
    for (i=1;i<numthreads;i++) {
      printf("thread number %d being created.\n",i);
      rc = pthread_create(&threads[i], NULL, fwq_core, (void *)(intptr_t)i);
      if (rc) {
        fprintf(stderr,"ERROR: pthread_create() failed.\n");
        exit(EXIT_FAILURE);
      }
    }
    fwq_core(0);

    for (i=1;i<numthreads;i++) {
      rc = pthread_join(threads[i],NULL);
      if (rc) {
	fprintf(stderr,"ERROR: pthread_join() failed.\n");
	exit(EXIT_FAILURE);
      }
    }

    free(threads);
#endif /* _WITH_PTHREADS_ */
  } else {
    fwq_core(0);
  }

  if (use_stdout == 1) {
    for (i=0;i<numsamples;i++) {
      fprintf(stdout,"%lld\n",samples[i]);
    }
  } else {

    for (j=0;j<numthreads;j++) {
      sprintf(fname_times,"%s_%d_times.dat",outname,j);

#ifdef Plan9
      fp = create(fname_times, OWRITE, 700);
#else
      fp = open(fname_times, O_CREAT|O_TRUNC|O_WRONLY, 0644);
#endif
      if(fp < 0) {
	perror("can not create file");
	exit(EXIT_FAILURE);
      }
      for (i=0;i<numsamples;i++) {
	sprintf(buf, "%lld\n", samples[i+(numsamples*j)]);
	write(fp, buf, strlen(buf));
      }
      close(fp);

    }
  }

  free(samples);

#ifdef _WITH_PTHREADS_
  pthread_exit(NULL);
#endif

  exit(EXIT_SUCCESS);
}
