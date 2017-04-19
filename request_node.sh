#!/bin/bash
echo "Requesting vis node for" ${1-1:00:00}
srun -p vis -t ${1-1:00:00} -n 1 --pty /bin/bash -l

