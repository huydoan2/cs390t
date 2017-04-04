/* Copyright 2017 Stanford University
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include <cstdio>
#include <cassert>
#include <cstdlib>
#include "legion.h"
#include "legion_stl.h"
#include <stdlib.h>

using namespace Legion;
using namespace LegionRuntime::Accessor;
using namespace LegionRuntime::Accessor::AccessorType;
// Legion has a separate namespace which contains
// some useful abstractions for operations on arrays.
// Unsurprisingly it is called the Arrays namespace.
// We'll see an example of one of these operations
// in this example.
using namespace LegionRuntime::Arrays;

enum TaskIDs {
  TOP_LEVEL_TASK_ID,
  INIT_FIELD_TASK_ID,
  QSORT_TASK_ID,
  ONE_TASK_ID,
};

enum FieldIDs {
  FID_X,
  FID_S,
  FID_Z,
};

void top_level_task(const Task *task,
                    const std::vector<PhysicalRegion> &regions,
                    Context ctx, Runtime *runtime)
{
  int num_elements = 20; 
  int num_subregions = 4;
  // See if we have any command line arguments to parse
  // Note we now have a new command line parameter which specifies
  // how many subregions we should make.
  {
    const InputArgs &command_args = Runtime::get_input_args();
    for (int i = 1; i < command_args.argc; i++)
    {
      if (!strcmp(command_args.argv[i],"-n"))
        num_elements = atoi(command_args.argv[++i]);
      if (!strcmp(command_args.argv[i],"-b"))
        num_subregions = atoi(command_args.argv[++i]);
    }
  }
  printf("Partitioning data into %d sub-regions...\n", num_subregions);

  // Create our logical regions using the same schemas as earlier examples
  Rect<1> elem_rect(Point<1>(0),Point<1>(num_elements-1));
  IndexSpace is = runtime->create_index_space(ctx, 
                          Domain::from_rect<1>(elem_rect));
  runtime->attach_name(is, "is");
  
  int total_splitters = num_subregions * (num_subregions-1);
  Rect<1> test_rect(Point<1>(0),Point<1>(total_splitters-1));
  IndexSpace is_splitter = runtime->create_index_space(ctx, 
                          Domain::from_rect<1>(test_rect));
  runtime->attach_name(is_splitter, "is_splitter");

  FieldSpace input_fs = runtime->create_field_space(ctx);
  runtime->attach_name(input_fs, "input_fs");
  {
    FieldAllocator allocator = 
      runtime->create_field_allocator(ctx, input_fs);
    allocator.allocate_field(sizeof(int),FID_X);
    runtime->attach_name(input_fs, FID_X, "X");
  }
  
  FieldSpace output_fs = runtime->create_field_space(ctx);
  runtime->attach_name(output_fs, "output_fs");
  {
    FieldAllocator allocator = 
      runtime->create_field_allocator(ctx, output_fs);
    allocator.allocate_field(sizeof(int),FID_Z);
    runtime->attach_name(output_fs, FID_Z, "Z");
  }

  FieldSpace splitter_fs = runtime->create_field_space(ctx);
  runtime->attach_name(splitter_fs, "splitter_fs");
  {
    FieldAllocator allocator =
      runtime->create_field_allocator(ctx, splitter_fs);
      allocator.allocate_field(sizeof(int), FID_S);
      runtime->attach_name(splitter_fs, FID_S, "S");
  }



  LogicalRegion input_lr = runtime->create_logical_region(ctx, is, input_fs);
  runtime->attach_name(input_lr, "input_lr");
  LogicalRegion output_lr = runtime->create_logical_region(ctx, is, output_fs);
  runtime->attach_name(output_lr, "output_lr");
  LogicalRegion splitter_lr = runtime->create_logical_region(ctx, is_splitter, splitter_fs);
  runtime->attach_name(splitter_lr, "splitter_lr");

  // In addition to using rectangles and domains for launching index spaces
  // of tasks (see example 02), Legion also uses them for performing 
  // operations on logical regions.  Here we create a rectangle and a
  // corresponding domain for describing the space of subregions that we
  // want to create.  Each subregion is assigned a 'color' which is why
  // we name the variables 'color_bounds' and 'color_domain'.  We'll use
  // these below when we partition the region.
  Rect<1> color_bounds(Point<1>(0),Point<1>(num_subregions-1));
  Domain color_domain = Domain::from_rect<1>(color_bounds);

  // Parallelism in Legion is implicit.  This means that rather than
  // explicitly saying what should run in parallel, Legion applications
  // partition up data and tasks specify which regions they access.
  // The Legion runtime computes non-interference as a function of 
  // regions, fields, and privileges and then determines which tasks 
  // are safe to run in parallel.
  //
  // Data partitioning is performed on index spaces.  The partitioning 
  // operation is used to break an index space of points into subsets 
  // of points each of which will become a sub index space.  Partitions 
  // created on an index space are then transitively applied to all the 
  // logical regions created using the index space.  We will show how
  // to get names to the subregions later in this example.
  //
  // Here we want to create the IndexPartition 'ip'.  We'll illustrate
  // two ways of creating an index partition depending on whether the
  // array being partitioned can be evenly partitioned into subsets
  // or not.  There are other methods to partitioning index spaces
  // which are not covered here.  We'll cover the case of coloring
  // individual points in an index space in our capstone circuit example.
  IndexPartition ip, ip_splitter;
  if ((num_elements % num_subregions) != 0)
  {
    // Not evenly divisible
    // Create domain coloring to store the coloring
    // maps from colors to domains for each subregion
    //
    // In this block of code we handle the case where the index space
    // of points is not evenly divisible by the desired number of
    // subregions.  This gives us the opportunity to illustrate a
    // general approach to coloring arrays.  The general idea is
    // to create a map from colors to sub-domains.  Colors correspond
    // to the sub index spaces that will be made and each sub-domain
    // describes the set points to be kept in that domain.

    // Computer upper and lower bounds on the number of elements per subregion
    const int lower_bound = num_elements/num_subregions;
    const int upper_bound = lower_bound+1;
    const int number_small = num_subregions - (num_elements % num_subregions);
    // Create a coloring object to store the domain coloring.  A
    // DomainColoring type is a typedef of an STL map from Colors
    // (unsigned integers) to Domain objects and can be found in
    // legion_types.h along with type declarations for all user-visible
    // Legion types.
    DomainColoring coloring;
    int index = 0;
    // We fill in the coloring by computing the domain of points
    // to assign to each color.  We assign 'elmts_per_subregion'
    // to all colors except the last one when we clamp the
    // value to the maximum number of elements.
    for (int color = 0; color < num_subregions; color++)
    {
      int num_elmts = color < number_small ? lower_bound : upper_bound;
      assert((index+num_elmts) <= num_elements);
      Rect<1> subrect(Point<1>(index),Point<1>(index+num_elmts-1));
      coloring[color] = Domain::from_rect<1>(subrect);
      index += num_elmts;
    }
    // Once we have computed our domain coloring we are now ready
    // to create the partition.  Creating a partition simply
    // involves giving the Legion runtime an index space to
    // partition 'is', a domain of colors, and then a domain
    // coloring.  In addition the application must specify whether
    // the given partition is disjoint or not.  This example is
    // a disjoint partition because there are no overlapping
    // points between any of the sub index spaces.  In debug mode
    // the runtime will check whether disjointness assertions
    // actually hold.  In the next example we'll see an
    // application which makes use of a non-disjoint partition.
    ip = runtime->create_index_partition(ctx, is, color_domain, 
                                      coloring, true/*disjoint*/);
  }
  else
  { 
    // In the case where we know that the number of subregions
    // evenly divides the number of elements, the Array namespace
    // in Legion provide productivity constructs for partitioning
    // an array.  Blockify is one example of these constructs.
    // A Blockify will evenly divide a rectangle into subsets 
    // containing the specified number of elements in each 
    // dimension.  Since we are only dealing with a 1-D rectangle,
    // we need only specify the number of elements to have
    // in each subset.  A Blockify object is mapping from colors
    // to subsets of an index space the same as a DomainColoring,
    // but is implicitly disjoint.  The 'create_index_partition'
    // method on the Legion runtime is overloaded a different
    // instance of it takes mappings like Blockify and returns
    // an IndexPartition.
    Blockify<1> coloring(num_elements/num_subregions);
    ip = runtime->create_index_partition(ctx, is, coloring);
  }
  runtime->attach_name(ip, "ip");

  Blockify<1> coloring(num_subregions-1);
  ip_splitter = runtime->create_index_partition(ctx, is_splitter, coloring);
  runtime->attach_name(ip_splitter, "ip_splitter");

  // The index space 'is' was used in creating two logical regions: 'input_lr'
  // and 'output_lr'.  By creating an IndexPartitiong of 'is' we implicitly
  // created a LogicalPartition for each of the logical regions created using
  // 'is'.  The Legion runtime provides several ways of getting the names for
  // these LogicalPartitions.  We'll look at one of them here.  The
  // 'get_logical_partition' method takes a LogicalRegion and an IndexPartition
  // and returns the LogicalPartition of the given LogicalRegion that corresponds
  // to the given IndexPartition.  
  LogicalPartition input_lp = runtime->get_logical_partition(ctx, input_lr, ip);
  runtime->attach_name(input_lp, "input_lp");

  LogicalPartition output_lp = runtime->get_logical_partition(ctx, output_lr, ip);
  runtime->attach_name(output_lp, "output_lp");

  LogicalPartition splitter_lp = runtime->get_logical_partition(ctx, splitter_lr, ip_splitter);
  runtime->attach_name(splitter_lp, "splitter_lp");

  // Create our launch domain.  Note that is the same as color domain
  // as we are going to launch one task for each subregion we created.
  Domain launch_domain = color_domain; 
  ArgumentMap arg_map;

  // As in previous examples, we now want to launch tasks for initializing 
  // both the fields.  However, to increase the amount of parallelism
  // exposed to the runtime we will launch separate sub-tasks for each of
  // the logical subregions created by our partitioning.  To express this
  // we create an IndexLauncher for launching an index space of tasks
  // the same as example 02.
  IndexLauncher init_launcher(INIT_FIELD_TASK_ID, launch_domain, 
                              TaskArgument(NULL, 0), arg_map);
  // For index space task launches we don't want to have to explicitly
  // enumerate separate region requirements for all points in our launch
  // domain.  Instead Legion allows applications to place an upper bound
  // on privileges required by subtasks and then specify which privileges
  // each subtask receives using a projection function.  In the case of
  // the field initialization task, we say that all the subtasks will be
  // using some subregion of the LogicalPartition 'input_lp'.  Applications
  // may also specify upper bounds using logical regions and not partitions.
  //
  // The Legion implementation assumes that all all points in an index
  // space task launch request non-interfering privileges and for performance
  // reasons this is unchecked.  This means if two tasks in the same index
  // space are accessing aliased data, then they must either both be
  // with read-only or reduce privileges.
  //
  // When the runtime enumerates the launch_domain, it will invoke the
  // projection function for each point in the space and use the resulting
  // LogicalRegion computed for each point in the index space of tasks.
  // The projection ID '0' is reserved and corresponds to the identity 
  // function which simply zips the space of tasks with the space of
  // subregions in the partition.  Applications can register their own
  // projections functions via the 'register_region_projection' and
  // 'register_partition_projection' functions before starting 
  // the runtime similar to how tasks are registered.
  init_launcher.add_region_requirement(
      RegionRequirement(input_lp, 0/*projection ID*/, 
                        WRITE_DISCARD, EXCLUSIVE, input_lr));
  init_launcher.region_requirements[0].add_field(FID_X);
  runtime->execute_index_space(ctx, init_launcher);

  // Modify our region requirement to initialize the other field
  // in the same way.  Note that after we do this we have exposed
  // 2*num_subregions task-level parallelism to the runtime because
  // we have launched tasks that are both data-parallel on
  // sub-regions and task-parallel on accessing different fields.
  // The power of Legion is that it allows programmers to express
  // these data usage patterns and automatically extracts both
  // kinds of parallelism in a unified programming framework.
  runtime->execute_index_space(ctx, init_launcher);

  // We launch the subtasks for performing the daxpy computation
  // in a similar way to the initialize field tasks.  Note we
  // again make use of two RegionRequirements which use a
  // partition as the upper bound for the privileges for the task.

  // Sort each sub-region locally    
  IndexLauncher qsort_launcher(QSORT_TASK_ID, launch_domain,
                TaskArgument(&num_subregions, sizeof(num_subregions)), arg_map);
  qsort_launcher.add_region_requirement(
      RegionRequirement(input_lp, 0/*projection ID*/,
                        WRITE_DISCARD, EXCLUSIVE, input_lr));
  qsort_launcher.region_requirements[0].add_field(FID_X);
  qsort_launcher.add_region_requirement(
      RegionRequirement(splitter_lp, 0,
                        WRITE_DISCARD, EXCLUSIVE, splitter_lr));
  qsort_launcher.region_requirements[1].add_field(FID_S);
  runtime->execute_index_space(ctx, qsort_launcher);



  TaskLauncher check_launcher(ONE_TASK_ID, TaskArgument(&num_subregions, sizeof(num_subregions)));
  check_launcher.add_region_requirement(
      RegionRequirement(input_lr, READ_ONLY, EXCLUSIVE, input_lr));
  check_launcher.region_requirements[0].add_field(FID_X);
  check_launcher.add_region_requirement(
      RegionRequirement(splitter_lr, READ_ONLY, EXCLUSIVE, splitter_lr));
  check_launcher.region_requirements[1].add_field(FID_S);
  runtime->execute_task(ctx, check_launcher);
  
  // While we could also issue parallel subtasks for the checking
  // task, we only issue a single task launch to illustrate an
  // important Legion concept.  Note the checking task operates
  // on the entire 'input_lr' and 'output_lr' regions and not
  // on the subregions.  Even though the previous tasks were
  // all operating on subregions, Legion will correctly compute
  // data dependences on all the subtasks that generated the
  // data in these two regions.  
  
  runtime->destroy_logical_region(ctx, input_lr);
  runtime->destroy_logical_region(ctx, output_lr);
  runtime->destroy_logical_region(ctx, splitter_lr);

  runtime->destroy_field_space(ctx, splitter_fs);
  runtime->destroy_field_space(ctx, input_fs);
  runtime->destroy_field_space(ctx, output_fs);

  runtime->destroy_index_space(ctx, is_splitter);
  runtime->destroy_index_space(ctx, is);
}

void init_field_task(const Task *task,
                     const std::vector<PhysicalRegion> &regions,
                     Context ctx, Runtime *runtime)
{
  
  assert(regions.size() == 1); 
  assert(task->regions.size() == 1);
  assert(task->regions[0].privilege_fields.size() == 1);

  FieldID fid = *(task->regions[0].privilege_fields.begin());
  const int point = task->index_point.point_data[0];
  printf("Initializing field %d for block %d...\n", fid, point);

  RegionAccessor<AccessorType::Generic, int> acc = 
    regions[0].get_field_accessor(fid).typeify<int>();
  srand(fid);
  // Note here that we get the domain for the subregion for
  // this task from the runtime which makes it safe for running
  // both as a single task and as part of an index space of tasks.
  Domain dom = runtime->get_index_space_domain(ctx, 
      task->regions[0].region.get_index_space());
  Rect<1> rect = dom.get_rect<1>();
  for (GenericPointInRectIterator<1> pir(rect); pir; pir++)
  {
    acc.write(DomainPoint::from_point<1>(pir.p), lrand48()%100);
  }
}

int compare (const void * a, const void * b)
{
  return ( *(int*)a - *(int*)b );
}

void qsort_task(const Task *task,
                const std::vector<PhysicalRegion> &regions,
                Context ctx, Runtime *runtime)
{
  assert(regions.size() == 2);
  assert(task->regions.size() == 2);
  
  const int point = task->index_point.point_data[0];
  printf("Running qsort computation with for point %d...\n", 
          point);


  const int num_subregions = *((const int*)task->args);
  printf("Num sub regions is %d\n", num_subregions);


  RegionAccessor<AccessorType::Generic, int> acc_x = 
    regions[0].get_field_accessor(FID_X).typeify<int>();
  Domain dom = runtime->get_index_space_domain(ctx, 
      task->regions[0].region.get_index_space());
  Rect<1> rect = dom.get_rect<1>();



  RegionAccessor<AccessorType::Generic, int> acc_s = 
    regions[1].get_field_accessor(FID_S).typeify<int>();
  Domain dom_splitter = runtime->get_index_space_domain(ctx,
      task->regions[1].region.get_index_space());
  Rect<1> splitter_rect = dom_splitter.get_rect<1>();

  int length = 0;
  printf("\nSanity Check for Values:\n");
  for (GenericPointInRectIterator<1> pir(rect); pir; pir++)
  {
    printf("Value at [%d] is %d\n", length, acc_x.read(DomainPoint::from_point<1>(pir.p)));
    length++;
  }

  LegionRuntime::Accessor::ByteOffset byte_offset(0);
  Rect<1> sub_rect;

  void * check_ptr = acc_x.raw_rect_ptr<1>(rect, sub_rect, &byte_offset);

  // This assert makes sure rect is not out of bounds
  assert (sub_rect == rect);
  printf("\nBefore local sort\n");

  int index_ptr = 0;
  while (index_ptr < length){
    int check_value = *((int *) (check_ptr) + index_ptr);
    printf("Value at [%d] start is %d\n", index_ptr, check_value);
    index_ptr++;
  }

  qsort(check_ptr, length, sizeof(int), compare);
  printf("After local sort\n");
  index_ptr = 0;
  while (index_ptr < length){
    int check_value = *((int *) (check_ptr) + index_ptr);
    printf("Value at [%d] start is %d\n", index_ptr, check_value);
    index_ptr++;
  }
  printf("\n\n\n");

  // Choose splitters locally and store in S
  GenericPointInRectIterator<1> pir(splitter_rect);
  for (int i = 0; i < num_subregions-1; i++)
  {
    //int value = *((int *) (check_ptr) + ((length/(num_subregions*num_subregions)) * (i+1)));
    int value = ((int *) check_ptr)[(length / num_subregions) * (i+1)];
    printf("Value selected for splitter is %d\n", value);
    acc_s.write(DomainPoint::from_point<1>(pir.p), value);
    pir++;
  }

  length = 0;
  for (GenericPointInRectIterator<1> pir(splitter_rect); pir; pir++)
  {
    printf("Splitter Value at [%d] is %d\n", length, acc_s.read(DomainPoint::from_point<1>(pir.p)));
    length++;
  }

}

void one_task(const Task *task,
                const std::vector<PhysicalRegion> &regions,
                Context ctx, Runtime *runtime)
{
  assert(regions.size() == 2);
  assert(task->regions.size() == 2);
  const int num_subregions = *((const int*)task->args);

  RegionAccessor<AccessorType::Generic, int> acc_x = 
    regions[0].get_field_accessor(FID_X).typeify<int>();
  
  RegionAccessor<AccessorType::Generic, int> acc_s = 
    regions[1].get_field_accessor(FID_S).typeify<int>();

  printf("\n\n\n\n\nPicking splitters...\n");
  Domain dom = runtime->get_index_space_domain(ctx, 
      task->regions[0].region.get_index_space());
  Rect<1> rect = dom.get_rect<1>();

  Domain dom_splitter = runtime->get_index_space_domain(ctx,
    task->regions[1].region.get_index_space());
  Rect<1> split_rect = dom_splitter.get_rect<1>();

  // Sort splitters
  Rect<1> sub_rect;
  LegionRuntime::Accessor::ByteOffset byte_offset(0);

  void * split_ptr = acc_s.raw_rect_ptr<1>(split_rect, sub_rect, &byte_offset);
  qsort(split_ptr, num_subregions*(num_subregions-1), sizeof(int), compare);

  // Choose global splitters
  for (int i = 0; i < num_subregions-1; i++){
    
  }

  for (GenericPointInRectIterator<1> pir(rect); pir; pir++)
  {
    int expected = acc_x.read(DomainPoint::from_point<1>(pir.p));
    printf("Value is %d\n", expected);
  }

  for (GenericPointInRectIterator<1> pir(split_rect); pir; pir++)
  {
    int expected = acc_s.read(DomainPoint::from_point<1>(pir.p));
    printf("Splitter Value is %d\n", expected);
  }
}


int main(int argc, char **argv)
{
  Runtime::set_top_level_task_id(TOP_LEVEL_TASK_ID);
  Runtime::register_legion_task<top_level_task>(TOP_LEVEL_TASK_ID,
      Processor::LOC_PROC, true/*single*/, false/*index*/,
      AUTO_GENERATE_ID, TaskConfigOptions(), "top_level");
  // Note we mark that all of these tasks are capable of being
  // run both as single tasks and as index space tasks
  Runtime::register_legion_task<init_field_task>(INIT_FIELD_TASK_ID,
      Processor::LOC_PROC, true/*single*/, true/*index*/,
      AUTO_GENERATE_ID, TaskConfigOptions(true), "init_field");
  
  Runtime::register_legion_task<qsort_task>(QSORT_TASK_ID,
      Processor::LOC_PROC, true/*single*/, true/*index*/,
      AUTO_GENERATE_ID, TaskConfigOptions(true), "qsort");

  Runtime::register_legion_task<one_task>(ONE_TASK_ID,
      Processor::LOC_PROC, true/*single*/, true/*index*/,
      AUTO_GENERATE_ID, TaskConfigOptions(true), "one_task");

  return Runtime::start(argc, argv);
}