# cs390t
Benchmark different parallel frameworks including Galois, Legion, starPU, and in-house implementation

Data Generation
===============

```
$ cd data && make
$ ./generator 1000 1K
```

`1K` is the suffix of the filename. It generates four types of input data.

The input data is put in `/work/04009/yuhc/cs395t-data/data`. It is shared among course group members.

TODO: separate the input data into files for multiple nodes to read.

Perf the performance
====================

```
$ perf stat -B -e L1-dcache-loads,L1-dcache-load-misses,L1-dcache-stores,L1-dcache-store-misses,dTLB-loads,dTLB-load-misses,dTLB-stores,dTLB-store-misses ./binary [parameters]
```

For most benchmarks, you may use something like `-f /path/to/data/inc_1K.txt` as input. For example:

```
$ perf stat -B -e L1-dcache-loads,L1-dcache-load-misses,L1-dcache-stores,L1-dcache-store-misses,dTLB-loads,dTLB-load-misses,dTLB-stores,dTLB-store-misses ./ex1.x -f /work/04009/yuhc/cs395t-data/data/inc_10M.txt
```

Legion Setup
============

1. Download Stanford Legion

   ```$ git clone https://github.com/StanfordLegion/legion.git```

2. Add Legion runtime directory to environment

   ```$ export LG_RT_DIR=/path/to/legion/runtime```

3. Load gcc-4.9.3

   ```$ module load gcc/4.9.3```

3. Compile sample_sort by

   ```$ make```

4. Run sample_sort (arguments: -f input filename, -n number of elements,
   -b number of subregions)

   ```
   $ ./sample_sort -f input.txt -n 10 -b 2
   $ ./sample_sort -f /work/04009/yuhc/cs395t-data/data/inc_10M.txt -b 1 -ll:cpu 1 -ll:csize 10000
   ```


starPU Setup
============

1. Download the source:

   ```$ wget http://starpu.gforge.inria.fr/files/starpu-1.2.0/starpu-1.2.0.tar.gz```

2. Install hwloc (skip if on TACC)

   ```$ sudo apt-get install hwloc```

3. Install FxT - a performance benchmark tool.

   ```
   $ wget http://download.savannah.gnu.org/releases/fkt/fxt-0.2.11.tar.gz
   $ tar xfz fxt-0.2.11.tar.gz
   $ export FXTDIR=/path/to/fxt-0.2.11
   $ cd $FXTDIR
   $ ./configure --prefix=$FXTDIR
   $ make
   $ make install
   ```

4. In starPU folder, make and install (add `--disable-debug --disable-fortran --disable-opencl --disable-build-examples` flags to disable debugging mode.
   ```
   $ module load autotools/1.1
   $ ./autogen.sh
   $ ./configure --with-fxt=$FXTDIR --prefix=/path/to/starPU
   $ make
   $ make install
   $ export STARPU_PATH="/path/to/starpu-1.2.0"
   $ export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:$STARPU_PATH:$STARPU_PATH/lib/pkgconfig
   $ export LD_LIBRARY_PATH=$STARPU_PATH/lib:$LD_LIBRARY_PATH

   ```

5. Install clang++ and all of the build-essential packages

   ```$ module load llvm```

6. Compile the sample_sort code by running `make`
7. Set the STARPU_GENERATE_TRACE=1
7. Run it by `SYNC=1 STARPU_SCHED=lws ./sample_sort 16 1m.txt` using syncing 16 processors the 1m.txt input and scheduler locality work stealing
  ..* Output is in output.txt
  ..* Trace is in .trace, use vite to open

The command the measurement uses:

```
$ SYNC=1 STARPU_SCHED=prio ./sample_sort 1 /work/04009/yuhc/cs395t-data/data/rnd_100M.txt
```


Galois Setup
============

1. Download stable Galois version

   ```$ wget http://iss.ices.utexas.edu/projects/galois/downloads/Galois-2.2.1.tar.gz```

2. Install Galois

   ```
   $ module load gcc/4.7.1
   $ module load boost/1.51.0
   $ tar xzvf Galois-2.2.1.tar.gz
   $ cd Galois-2.2.1/build
   $ mkdir release
   $ cd release
   $ cmake ../.. -DCMAKE_C_COMPILER=gcc -DBoost_INCLUDE_DIR=$TACC_BOOST_INC
   $ make
   ```
3. Build sample_sort:

   Add `add_subdirectory(sample_sort)` to `/path/to/Galois-2.2.1/apps/CMakeLists.txt`

   Copy `./galois/sample_sort` to `/path/to/Galois-2.2.1/apps`

   Copy input file to `/path/to/Galois-2.2.1/build/release`

   Run `make` in `release` again

4. Run sample_sort (arguments: input filename, number of threads)

   ```
   $ cd /path/to/release
   $ ./apps/sample_sort/array input.txt 2
   ```
The command the measurement uses:

```
$ ./apps/sample_sort/array /work/04009/yuhc/cs395t-data/data/rnd_100M.txt 2
```

PVFMM Setup
===========

1. Build PVFMM

   Copy `hyper_qsort.c` to `pvfmm_dir/examples/src`. Then:

   ```
   $ module load intel fftw3 cxx11/4.9.1 autotools/1.1
   $ ./autogen.sh

   $ ./configure MPICXX=mpicxx --with-openmp-flag='openmp' CXXFLAGS=" -qno-offload -Ofast -xhost -DNDEBUG -std=c++11 -I$TACC_MKL_INC" --with-fftw-include="$TACC_MKL_INC/fftw" --with-fftw-lib="-mkl" --with-blas='-mkl' --with-lapack='-mkl' --prefix=/path/to/install/pvfmm/dir

   or (not work in my case)

   $ idev -A CS395_Parallel_Algor
   $ ./configure --with-fftw=$TACC_FFTW3_DIR --prefix=/path/to/install/pvfmm/dir

   $ make && make install
   $ export PVFMM_DIR=/path/to/install/pvfmm/dir/share/pvfmm
   $ cd examples && make
   $ ibrun -np 2 bin/hyper_qsort -N 100000 -omp 1
   $ ibrun -np 2 bin/hyper_qsort -f /work/04009/yuhc/cs395t-data/data/rnd_10M.txt -omp 1
   ```

MergeSort
=========

```
$ cd in-house/ompsort; make
$ OMP_NUM_THREADS=2 ./ex1.x -f /work/04009/yuhc/cs395t-data/data/rnd_100M.txt
```
