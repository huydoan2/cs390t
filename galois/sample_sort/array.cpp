/** My first Galois program -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2013, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS
 * SOFTWARE AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR ANY PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF
 * PERFORMANCE, AND ANY WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF
 * DEALING OR USAGE OF TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH
 * RESPECT TO THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect, direct or
 * consequential damages or loss of profits, interruption of business, or
 * related expenses which may arise from use of Software or Documentation,
 * including but not limited to those resulting from defects in Software and/or
 * Documentation, or loss or inaccuracy of data of any kind.
 *
 * @section Description
 *
 * My first Galois program. Prints "Hello World" in parallel.
 *
 * @author Donald Nguyen <ddn@cs.utexas.edu>
 */
#include "Galois/Galois.h"
#include <boost/iterator/counting_iterator.hpp>
#include <iostream>
#include <stdlib.h>
#include <sstream>
#include <fstream>
#include <string>
#include <stdio.h>
#include <string.h>

#define localsplitter 3


using namespace std;
struct ArrayOp
{
    int* a;
    int** b;
    int n;
    int numThreads;
    void operator ()(int i)
    {
        int j;//int b[numThreads][5];
        // std::cout << "a = " << a << ", i = " << i << std::endl;
        for( j=i*n; j<i*n+n; j++)
        {
            b[i][j%n]=a[j];
            //std::cout <<i<<" = " <<a[j] << "/" << std::endl;
        }

        //quicksort(&b[i][0]);
    }
};

struct index_cal
{
    int **b;
    int **index_mat;
    int n;
    int *finpar;
    int **size;
    int numThreads;
    void operator ()(int i)
    {
        int xx,j;
        for(j=0; j<n; j++)
        {
            for(xx=0; xx<=numThreads-1; xx++)
            {
                if(xx==0)
                {
                    if(b[i][j]<=finpar[xx] )
                    {
                        if(index_mat[i][xx]==-1)
                            index_mat[i][xx]=j;
                        size[i][xx]++;
                        break;
                    }
                }
                else if(b[i][j]<=finpar[xx]&&b[i][j]>finpar[xx-1])
                {
                    if(index_mat[i][xx]==-1)
                        index_mat[i][xx]=j;
                    size[i][xx]++;
                    break;
                }
            }
        }
        index_mat[i][numThreads]=n;
    }
};
struct fin_merge
{
    int **b;
    int **bb;
    int **size;
    int **sum;
    int **index_mat;
    int n;
    int numThreads;
    void operator ()(int i)
    {
        int xx=0,j;
        for(j=index_mat[i][n]; j<size[i][n]+index_mat[i][n]; j++)
        {
            bb[n][sum[n][i]+xx]=b[i][j];
            xx++;
        }
    }
};

void quickSort(int* arr, int left, int right)
{
    int i = left, j = right;
    int tmp;
    int pivot = arr[(left + right) / 2];

    /* partition */
    while (i <= j)
    {
        while (arr[i] < pivot)
            i++;
        while (arr[j] > pivot)
            j--;
        if (i <= j)
        {
            tmp = arr[i];
            arr[i] = arr[j];
            arr[j] = tmp;
            i++;
            j--;
        }
    };

    /* recursion */
    if (left < j)
        quickSort(arr, left, j);
    if (i < right)
        quickSort(arr, i, right);
}


struct Sort
{
    int* a;
    int** b;
    int n;
    int numThreads;
    void operator () (int i)
    {

        quickSort(&b[i][0],0,n-1);
    }
};
struct Sort_post
{
    int* a;
    int **b;
    int *n;
    int numThreads;
    void operator () (int i)
    {

        quickSort(&b[i][0],0,n[i]-1);
    }
};


struct gathersplit
{
    int* gath;
    int **b;
    int n;
    int p;
    int numThreads;
    void operator () (int i)
    {
        int j;
        int x=0;
        for(j=n/p; j<n; j+=n/p+1)
        {
            gath[i*(p-1)+x]=b[i][j];
            x++;
        }
    }
};

void print(int** b,int numThreads, int n)
{
    int i,j;
    std::cout<<""<<std::endl;
    for(i=0; i<numThreads; i++)
    {
        for(j=0; j<n; j++)
        {
            std::cout<<i<<","<<j<<"="<<b[i][j]<<std::endl;
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: %s <input_file> <thread_num>\n", argv[0]);
        return -1;
    }

    ifstream myfile;
    myfile.open(argv[1]);
    int N;
    myfile >> N;
    ofstream output;
    output.open("output.txt");

    int* a = new int[N];
    for (int i = 0; i < N; ++i) {
        myfile >> a[i];
        // cout << a[i] << " ";
    }
    // cout << endl;

    int n;
    int numThreads = atoi(argv[2]);
    int m = N;

    int i=0;
    int finpar[numThreads];


    int **b=new int*[numThreads];
    n=ceil(m/numThreads);
    int j;
    int p=localsplitter;
    int gath[numThreads*(p-1)];
    for(i=0; i<numThreads; i++)
    {
        b[i]=new int[m];
    }
    std::cout << "Using " << numThreads << " threads and " << n << " iterations\n";
    numThreads = Galois::setActiveThreads(numThreads);
    int **bb=new int*[numThreads];
    int **index_mat=new int*[numThreads];
    int **size=new int*[numThreads];
    int **sum=new int*[numThreads];
    for(i=0; i<numThreads; i++)
    {
        bb[i]=new int[m];
        index_mat[i]=new int[numThreads+1];
        size[i]=new int[numThreads];
        sum[i]=new int[numThreads];
    }

    for(i=0; i<numThreads; i++)
    {
        for(j=0; j<=numThreads; j++)
        {
            index_mat[i][j]=-1;
            if(j<numThreads)
            {
                size[i][j]=0;
                sum[i][j]=0;
            }
        }
    }
    int xo[numThreads];
    for(int kk=0; kk<numThreads; kk++)
    {
        xo[kk]=0;
    }
    for(int k=0; k<numThreads; k++)
    {
        for(int kk=0; kk<m; kk++)
        {
            bb[k][kk]=0;
        }
    }
    const clock_t begin_time = clock();
// std::cout << "Using a function object\n";
//  Galois::do_all(boost::make_counting_iterator<int>(0), boost::make_counting_iterator<int>(numThreads), HelloWorld());

// std::cout << "Using a function pointer\n";
    Galois::do_all(boost::make_counting_iterator<int>(0), boost::make_counting_iterator<int>(numThreads), ArrayOp{a,b,n,numThreads});

//std::cout<<"HERE"<<std::endl;
//print(b,numThreads,n);
    const clock_t c1=clock();
    Galois::do_all(boost::make_counting_iterator<int>(0), boost::make_counting_iterator<int>(numThreads), Sort{a,b,n,numThreads});
// std::cout<<"Sorted"<<std::endl;
//print(b,numThreads,n);

    Galois::do_all(boost::make_counting_iterator<int>(0), boost::make_counting_iterator<int>(numThreads), gathersplit{gath,b,n,p,numThreads});
    //std::cout<<"n/p = "<<n/p;


    //printf("\nGathering:\n");
    /*for (i=0;i<numThreads*(p-1);i++)
    {
    	std::cout<<" " <<gath[i];
    }*/

    quickSort(gath,0,numThreads*(p-1)-1);
    //printf("\nSorted:\n");

    /*for (i=0;i<numThreads*(p-1);i++)
    {
    	std::cout<<" " <<gath[i];
    }	*/
    int x=0;
    int y=numThreads*(p-1);
    for (i=y/numThreads-1; i<y; i+=y/numThreads+1)
    {
        finpar[x]=gath[i];
        x++;
    }
    //printf("Finpar:\n%d\n",x);
    /*for (i=0;i<x;i++)
    {
    	std::cout<<" " <<finpar[i];
    }*/

    finpar[x]=10000;

    y=0;

    /*	int xx=0;
    	 for(i=0;i<numThreads;i++)
    		{
    		  for(j=0;j<n;j++)
    			{
    				for(xx=0;xx<=numThreads-1;xx++)
    				{
    					if(xx==0)
    					{if(b[i][j]<=finpar[xx])
    						{bb[xx][xo[xx]]=b[i][j];xo[xx]++;}
    					}
    					else
    					if(b[i][j]<=finpar[xx]&&b[i][j]>finpar[xx-1])
    					{bb[xx][xo[xx]]=b[i][j];xo[xx]++;}
    				}

    		}
    	}*/

    Galois::do_all(boost::make_counting_iterator<int>(0), boost::make_counting_iterator<int>(numThreads), index_cal{b,index_mat,n,finpar,size,numThreads});

    /*printf("\nAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAa\n");
    for(int k=0;k<numThreads;k++)
    {
    	for(int kk=0;kk<=numThreads;kk++)
    	{	std::cout<<index_mat[k][kk]<<std::endl;
    	}
    }
    printf("\nAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAa\n");*/

    for(int k=0; k<numThreads; k++)
    {
        for(int kk=1; kk<numThreads; kk++)
        {
            sum[k][kk]=sum[k][kk-1]+size[kk-1][k];

        }
    }
    std::cout << float( clock () - c1 ) /  CLOCKS_PER_SEC<<std::endl;
    /*for(int k=0;k<numThreads;k++)
    {
    	for(int kk=0;kk<numThreads;kk++)
    	{	std::cout<<size[k][kk]<<std::endl;

    	}
    }
    printf("\nAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAa\n");*/

    /*for(int k=0;k<numThreads;k++)
    {
    	for(int kk=0;kk<numThreads;kk++)
    	{	std::cout<<sum[k][kk]<<std::endl;

    	}
    }*/

    for(int k=0; k<numThreads; k++)
    {
        Galois::do_all(boost::make_counting_iterator<int>(0), boost::make_counting_iterator<int>(numThreads), fin_merge{b,bb,size,sum,index_mat,k,numThreads});
    }
    printf("\nDone\n");


    for(int k=0; k<numThreads; k++)
    {
        xo[k]=size[numThreads-1][k]+sum[k][numThreads-1];
    }

    cout<<"----------------------------------------------------------------------"<<endl;

    /*for(int k=0;k<numThreads;k++)
    {
    	for(int kk=0;kk<xo[k];kk++)
    	{	std::cout<<bb[k][kk]<<std::endl;
    	}
    }*/
    Galois::do_all(boost::make_counting_iterator<int>(0), boost::make_counting_iterator<int>(numThreads), Sort_post{a,bb,xo,numThreads});
    std::cout << float( clock () - begin_time ) /  CLOCKS_PER_SEC;

    for(int k=0; k<numThreads; k++)
    {
        for(int kk=0; kk<xo[k]; kk++)
        {
            output<<bb[k][kk]<<endl;
        }
    }
    bb=NULL;
    b=NULL;
    a=NULL;
    /*delete []bb;
    delete []b;
    delete []a;
    delete []finpar;
    delete []gath;*/
    return 0;
}
