#include <vector>
#include <iostream>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include "ompUtils.h"
#include <sys/time.h>

int main(int argc, char **argv)
{
    std::vector<double> input;
    int N=10000;
    if (argc>1)
    {
        if (!strcmp(argv[1], "-n"))
        {
            N=atoi(argv[2]);
            input.resize(N);
            for (int i=0; i<N; i++)  input[i]=drand48();
        }
        else if (!strcmp(argv[1], "-f"))
        {
            FILE *fp = fopen(argv[2], "r");
            fscanf(fp, "%d\n", &N);
            input.resize(N);
            for (int i=0; i<N; i++)  fscanf(fp, "%d", &input[i]);
        }
        else if (!strcmp(argv[1], "-h"))
        {
            printf("%s [-n num] [-f input_file] [-h]\n", argv[0]);
            return 0;
        }
    }
    else {
        printf("%s [-n num] [-f input_file] [-h]\n", argv[0]);
        return 0;
    }

std::cout << "Number of elements: " << N << std::endl;

#ifdef VERBOSE
    std::cout << "\n\t Before sorting\n";
    for (int i=0; i<N; i++) std::cout << input[i] << std::endl;
#endif

    struct timeval tv1, tv2;
    gettimeofday(&tv1, NULL);

    omp_par::merge_sort( &input[0],&input[0]+N );

    gettimeofday(&tv2, NULL);
    printf ("Total time = %f seconds\n",
        (double) (tv2.tv_usec - tv1.tv_usec) / 1000000 +
        (double) (tv2.tv_sec - tv1.tv_sec));

#ifdef VERBOSE
    std::cout << "\n\t After sorting\n";
    for (int i=0; i<N; i++) std::cout << input[i] << std::endl;
#endif



    return 0;
}
