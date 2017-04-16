# cs390t
Benchmark different parallel frameworks including Galois, Legion, starPU, and in-house implementation

starPU Setup
============

1. Download the source: http://starpu.gforge.inria.fr/files/starpu-1.2.1/starpu-1.2.1.tar.gz
2. Install hwloc
..* `sudo apt-get install hwloc`
3. Install FxT - a performance benchmark tool.
..* `$ wget http://download.savannah.gnu.org/releases/fkt/fxt-0.2.11.tar.gz`
..* Create FxT folder and export the path to the folder to variable `FXTDIR`
..* `$ ./configure --prefix=$FXTDIR`
..* `$ make`
..* `$ make install`
4. In starPU folder, make and install
..* `$ ./configure --with-fxt=$FXTDIR`
..* `$ make`
..* `$ make install`
5. Install clang++ and all of the build-essential packages
6. Compile the sample_sort code by running make
7. Set the STARPU_GENERATE_TRACE=1
7. Run it by `STARPU_SCHED=lwc ./sample_sort 1m.txt` using the 1m.txt input and scheduler locality work stealing
..* Output is in output.txt
..* Trace is in .trace, use vite to open
