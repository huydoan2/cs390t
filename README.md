# cs390t
Benchmark different parallel frameworks including Galois, Legion, starPU, and in-house implementation

starPU Setup
============

1. Download the source: http://starpu.gforge.inria.fr/files/starpu-1.2.1/starpu-1.2.1.tar.gz
2. Install hwloc
    sudo apt-get install hwloc
3. In starPU folder, make and install
  $ ./configure
  $ make
  $ make install
