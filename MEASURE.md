WHAT TO MEASURE
===============

Requirements
------------

* Disable hyperthreading

Scalability
-----------

Use different sizes of random data.

* Run tests on different number of cores. 
* Run tests on single node with different number of threads.

Breakdown
---------

Compare the time of separate phases. Use 1 node, 4 threads, 10M data.

Balance
-------

Test the workload balance by using `perf` to measure CPU cycles on each core:

```$ perf record```

Robustness
----------

Run different types of data. Compare the performance deviation.

Use 1 node, 4 threads, 10M data.
