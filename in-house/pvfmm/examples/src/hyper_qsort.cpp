#include <mpi.h>
#include <pvfmm_common.hpp>
#include <cstdlib>
#include <iostream>
#include <omp.h>
#include <stdio.h>
#include <profile.hpp>
#include <parUtils.h>
#include <utils.hpp>



template <class Real_t>
void testsort(size_t N, MPI_Comm comm){

  // Find out number of OMP thereads.
  int omp_p=omp_get_max_threads();

  // Find out my identity in the default communicator
  int myrank, p;
  MPI_Comm_rank(comm, &myrank);
  MPI_Comm_size(comm,&p);

  //Various parameters.
  //Set particle coordinates and values.
  std::vector<Real_t> input, output;
  input.resize(N);
  output.resize(N);

  std::cout << "line " << __LINE__ << std::endl;
  for(size_t i=0;i<N;i++) input.push_back(Real_t(drand48()));
  std::cout << "line " << __LINE__ << std::endl;

  // 0 task prints various parameters.
  if(!myrank){
    std::cout<<std::setprecision(2)<<std::scientific;
    std::cout<<"Number of MPI processes: "<<p<<'\n';
    std::cout<<"Number of OpenMP threads: "<<omp_p<<'\n';
    std::cout<<"Number of points per processor: "<<N<<'\n';
  }
 
  std::cout << "line " << __LINE__ << std::endl;

  if(!myrank) std::cout << "warm-up sort starts\n";
  pvfmm::par::HyperQuickSort(input, output, comm);
  if(!myrank) std::cout << "warm-up sort ends\n";

  for(size_t i=0;i<2;i++){ // Compute potential   (twice to get good average statistics)
    pvfmm::Profile::Tic("TotalTime",&comm,true);
    pvfmm::par::HyperQuickSort(input, output, comm);
    pvfmm::Profile::Toc();
  }

}

int main(int argc, char **argv){

  MPI_Init(&argc, &argv);
  MPI_Comm comm=MPI_COMM_WORLD;

  std::cout << "start\n";
  // Read command line options.
  commandline_option_start(argc, argv);
  omp_set_num_threads( atoi(commandline_option(argc, argv,  "-omp",     "1", false, "-omp  <int> =  (1)   : Number of OpenMP threads."          )));
  size_t   N=(size_t)strtod(commandline_option(argc, argv,    "-N",     "1",  true, "-N    <int>          : Number of points  per rank."                  ),NULL);
  commandline_option_end(argc, argv);

  std::cout << "done with options, N= " << N <<  "\n";
  pvfmm::Profile::Enable(true);

  // Run FMM with above options.
   pvfmm::Profile::Tic("Test sort",&comm,true);
  std::cout << "call sort\n";
  testsort<float >( N,comm);
  pvfmm::Profile::Toc();
  //Output Profiling results.
  pvfmm::Profile::print(&comm);
  // Shut down MPI
  MPI_Finalize();
  return 0;
}

