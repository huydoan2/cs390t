# cs390t
Benchmark different parallel frameworks including Galois, Legion, starPU, and in-house implementation

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


starPU Setup
============

1. Download the source:

   ```$ wget http://starpu.gforge.inria.fr/files/starpu-1.2.0/starpu-1.2.0.tar.gz```

2. Install hwloc

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

4. In starPU folder, make and install
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
7. Run it by `STARPU_SCHED=lws ./sample_sort 1m.txt` using the 1m.txt input and scheduler locality work stealing
  ..* Output is in output.txt
  ..* Trace is in .trace, use vite to open


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

   Run `make` in `/path/to/Galois-2.2.1/build/release` again

4. Run sample_sort

   ```
   $ cd /path/to/release/apps/sample_sort
   $ ./array
   ```
