# basic makefile - yes, this makefile is not written well.  it was
# a quick hack.  feel free to fix it and contribute it back if it
# offends you - hacks like this don't bother me.

CXX = c++
CC = mpixlc_r -qasm=gcc
CC = gcc
#--> flags for BGP
#CFLAGS =-qasm=gcc  -I../common -O0 -qunroll -DBGP

#--> flags for x86-64 using default work loop increment (to get 10
#--> minute runtime on a 3.0GHz processor use -w 14 -n 280000).  Note
#--> that with gcc V4.3 and -g generates memory traffic in the work
#--> loop. This is bad because it memory reference latency produces
#--> alot of noise.  Changing the optimization level to -O1 or above
#--> eliminates the work loop entirely.  This is even worse.  It is
#--> recommended to use assembly language below if you have an x86
#--> compatible processor.  If not, write your own!
#CFLAGS =-I../common -O1 -march=native -mtune=native -m64 -static flags
#--> for x86-64 using assembly code work (use -w 19 -n 500000)
CFLAGS = -DASMx8664 -O1 -fexpensive-optimizations -march=native -mtune=native -m64 -malign-double -static
#--> flags for x86-64 with vectorization using daxpy work (use -w 14 -n 500000)
#CFLAGS =  -I../common -DDAXPY -O3 -ffast-math -funroll-loops -fexpensive-optimizations -march=native -mtune=native -msse4.2 -m64 -malign-double -static -ftree-vectorizer-verbose=3
#--> flags for x86-64 without vectorization using daxpy work (use -w 14 -n 500000)
#CFLAGS =  -I../common -DDAXPY -O1 -ffast-math -funroll-loops -fexpensive-optimizations -march=native -mtune=native -msse4.2 -m64 -malign-double -static

LIBS = $(TAU_LIBS)
LDFLAGS = $(USER_OPT)

all: ftq fwq t_ftq t_fwq

single: ftq fwq

threaded: t_ftq t_fwq

# Fixed TIME quanta benchmark without threads
ftq: ftq.h ftq.c
	$(CC) $(CFLAGS)  ftq.c -o ftq

# Fixed TIME quanta benchmark for use with mutiple threads
t_ftq: ftq.h ftq.c
	$(CC) $(CFLAGS) ftq.c -D_WITH_PTHREADS_ -DCORE63 -o t_ftq -lpthread

# Fixed WORK quanta benchmark without threads
fwq: ftq.h fwq.c
	$(CC) $(CFLAGS)  fwq.c -o fwq

# Fixed WORK quanta benchmark without threads assembly language
# output. This is most useful to view and verify the loop you think
# you are running is the loop the cores/threads are actually
# executing.
fwq.s: ftq.h fwq.c
	$(CC) $(CFLAGS)  -S fwq.c

# Fixed WORK quanta benchmark for use with mutiple threads
t_fwq: ftq.h fwq.c
	$(CC) $(CFLAGS) fwq.c -D_WITH_PTHREADS_ -o t_fwq -lpthread


ftq_openmp:
	$(CC) $(CFLAGS) ftq_omp.c  -D_WITH_OMP -qsmp=omp:noauto -qthreaded -o omp_ftq -lpthread
	$(CC) $(CFLAGS) ftq_omp.c  -D_WITH_OMP -qsmp=omp:noauto -qthreaded -DCORE15 -o omp_ftq15 -lpthread
	$(CC) $(CFLAGS) ftq_omp.c  -D_WITH_OMP -qsmp=omp:noauto -qthreaded -DCORE31 -o omp_ftq31 -lpthread
	$(CC) $(CFLAGS) ftq_omp.c  -D_WITH_OMP -qsmp=omp:noauto -qthreaded -DCORE63 -o omp_ftq63 -lpthread

clean:
	rm -f ftq.o ftq ftq15 ftq31 ftq63 t_ftq t_ftq15 t_ftq31 t_ftq63 omp_ftq omp_ftq15 omp_ftq31 omp_ftw63 fwq t_fwq
