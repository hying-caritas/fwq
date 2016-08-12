/**
 * ftq.c : Fixed Time Quantum microbenchmark
 *
 * Written by Matthew Sottile (matt@cs.uoregon.edu)
 *
 * This is a complete rewrite of the original tscbase code by
 * Ron and Matt, in order to use a better set of portable timers,
 * and more flexible argument handling and parameterization.
 *
 * 12/30/2007 : Updated to add pthreads support for FTQ execution on
 *              shared memory multiprocessors (multicore and SMPs).
 *              Also, removed unused TAU support.
 * 06/05/2008 : Fixed bug in pthread bind call.
 *
 * Licensed under the terms of the GNU Public Licence.  See LICENCE_GPL 
 * for details.
 */
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
#define DEFAULT_COUNT  10000
#define DEFAULT_BITS   20
#define MAX_BITS       30
#define MIN_BITS       3

/**
 * set up for coarser work grains than default
 */
#ifdef CORE15
#define MULTIITER
#define ITERCOUNT      8
#endif
#ifdef CORE31
#define MULTIITER
#define ITERCOUNT      16
#endif
#ifdef CORE63
#define MULTIITER
#define ITERCOUNT      32
#endif

/**
 * global variables
 */

/* samples: each sample has a timestamp and a work count. */
static unsigned long long *samples;
static unsigned long long interval_length;
static int interval_bits = DEFAULT_BITS;
static unsigned long numsamples = DEFAULT_COUNT;

/**
 * usage()
 */
void usage(char *av0) {
#ifdef _WITH_PTHREADS_
  fprintf(stderr,"usage: %s [-t threads] [-n samples] [-i bits] [-h] [-o outname] [-s]\n",
	  av0);
#else
  fprintf(stderr,"usage: %s [-n samples] [-i bits] [-h] [-o outname] [-s]\n",
	  av0);
#endif
  exit(EXIT_FAILURE);
}

/*************************************************************************
 * FTQ core: does the measurement                                        *
 *************************************************************************/
void *ftq_core(void *arg) {
  /* thread number, zero based. */
  int thread_num = (int)(intptr_t)arg;
  int i,offset;
#ifdef MULTIITER
  int k;
#endif

  ticks now, last, endinterval;
  unsigned long done;
  unsigned long long count;

#ifdef _WITH_PTHREADS_ 

  /* affinity stuff */
  unsigned long mask = 0x1;

  mask = mask<<thread_num;
  printf("thread number = %d with affinity mask = %d\n", thread_num, mask);
  if (pthread_setaffinity_np( pthread_self(), sizeof ( mask ), &mask ) < 0 ) {
    perror("pthread_setaffinity_np");
  }

#endif

  offset = thread_num * numsamples * 2;
  done = 0;
  count = 0;

  last = getticks();
  endinterval = (last + interval_length) & (~(interval_length - 1));

  /***************************************************/
  /* first, warm things up with 1000 test iterations */
  /***************************************************/
  for (i = 0; i < 1000; i++) {
    count = 0;
    
    for (now = last; now < endinterval; ) {
#ifdef MULTIITER
      for (k=0;k<ITERCOUNT;k++)
	count++;
      for (k=0;k<(ITERCOUNT-1);k++)
	count--;
#else
      count++;
#endif
      
      now = getticks();
    }
    
    samples[(done*2)+offset] = last;
    samples[(done*2)+1+offset] = count;
    
    done++;
    
    if (done >= numsamples)
      break;
    
    last = getticks();
    
    endinterval = (last + interval_length) & (~(interval_length - 1));
  }

  /****************************/
  /* now do the real sampling */
  /****************************/
  done = 0;

  while (1) {
    count = 0;
    
    for (now = last; now < endinterval; ) {
#ifdef MULTIITER
      for (k=0;k<ITERCOUNT;k++)
	count++;
      for (k=0;k<(ITERCOUNT-1);k++)
	count--;
#else
      count++;
#endif
      
      now = getticks();
    }
    
    samples[(done*2)+offset] = last;
    samples[(done*2)+1+offset] = count;
    
    done++;
    
    if (done >= numsamples)
      break;
    
    last = getticks();
    
    endinterval = (last + interval_length) & (~(interval_length - 1));
  }

  return NULL;
}

/**
 * main()
 */
int main(int argc, char **argv) {
  /* local variables */
  char fname_times[1024], fname_counts[1024], buf[32], outname[255];
  int i,j;
  int numthreads = 1, use_threads = 0;
  int fp;
  int use_stdout = 0;
#ifdef _WITH_PTHREADS_
  int rc;
  pthread_t *threads;
  unsigned long mask = 1;
#endif

  /* default output name prefix */
  sprintf(outname,"ftq");

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
  case 'i':
    interval_bits = atoi(ARGF());
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
	 {"interval",0,0,'i'},
	 {"outname",0,0,'o'},
	 {"stdout",0,0,'s'},
	 {"threads",0,0,'t'},
	 {0,0,0,0}
       };
    
       c = getopt_long(argc, argv, "n:hsi:o:t:",
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
       case 'i':
	 interval_bits = atoi(optarg);
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
  
  /* allocate sample storage */
  samples = malloc(sizeof(unsigned long long)*numsamples*2*numthreads);
  assert(samples != NULL);

  if (interval_bits > MAX_BITS || interval_bits < MIN_BITS) {
    fprintf(stderr,"WARNING: interval bits invalid.  set to %d.\n",
	    MAX_BITS);
    interval_bits = MAX_BITS;
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
     cache and pipeline */
  interval_length = 1 << interval_bits;  

  if (use_threads == 1) {
#ifdef _WITH_PTHREADS_
    if (sched_setaffinity(0, sizeof(mask), &mask) < 0 ) {
      perror("sched_setaffinity");
    }
    threads = malloc(sizeof(pthread_t)*numthreads);
    assert(threads != NULL);

    printf("numthreads = %d\n", numthreads);
    for (i=1;i<numthreads;i++) {
      printf("thread number %d being created.\n",i);
      rc = pthread_create(&threads[i], NULL, ftq_core, (void *)(intptr_t)i);
      if (rc) {
        fprintf(stderr,"ERROR: pthread_create() failed.\n");
        exit(EXIT_FAILURE);
      }
    }
    ftq_core(0);

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
    ftq_core(0);
  }

  if (use_stdout == 1) {
    for (i=0;i<numsamples;i++) {
      fprintf(stdout,"%lld %lld\n",samples[i*2],samples[i*2 + 1]);
    }
  } else {

    for (j=0;j<numthreads;j++) {
      sprintf(fname_times,"%s_%d_times.dat",outname,j);
      sprintf(fname_counts,"%s_%d_counts.dat",outname,j);

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
	sprintf(buf, "%lld\n", samples[(i*2)+(numsamples*j)]);
	write(fp, buf, strlen(buf));
      }
      close(fp);
      
#ifdef Plan9
      fp = create(fname_counts, OWRITE, 700);
#else
      fp = open(fname_counts, O_CREAT|O_TRUNC|O_WRONLY, 0644);
#endif 
      if(fp < 0) {
	perror("can not create file");
	exit(EXIT_FAILURE);
      }
      for (i=0;i<numsamples;i++) {
	sprintf(buf, "%lld\n", samples[i*2 + 1 + (numsamples*j)]);
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
