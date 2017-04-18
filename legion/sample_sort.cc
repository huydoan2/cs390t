
#include <cstdio>
#include <cassert>
#include <cstdlib>
#include "legion.h"
#include "legion_stl.h"
#include <stdlib.h>
#include <fstream>

using namespace Legion;
using namespace LegionRuntime::Accessor;
using namespace LegionRuntime::Accessor::AccessorType;
using namespace LegionRuntime::Arrays;

enum TaskIDs {
  TOP_LEVEL_TASK_ID,
  INIT_FIELD_TASK_ID,
  QSORT_TASK_ID,
  ONE_TASK_ID,
  P_BUCKET_TASK_ID,
  QSORT_BUCKET_ID,
  CHECKER_TASK_ID,
};

enum FieldIDs {
  FID_X,
};

void top_level_task(const Task *task,
                    const std::vector<PhysicalRegion> &regions,
                    Context ctx, Runtime *runtime)
{
  int num_elements = 20; 
  int num_subregions = 2;
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

  // ----------------- Index Spaces -----------------

  // Input/Output
  Rect<1> input_rect(Point<1>(0),Point<1>(num_elements-1));
  IndexSpace is_io = runtime->create_index_space(ctx, 
                          Domain::from_rect<1>(input_rect));
  runtime->attach_name(is_io, "is_io");

  // Local Splitters
  int total_splitters = num_subregions * (num_subregions-1);
  Rect<1> l_split_rect(Point<1>(0),Point<1>(total_splitters-1));
  IndexSpace is_l_split = runtime->create_index_space(ctx, 
                          Domain::from_rect<1>(l_split_rect));
  runtime->attach_name(is_l_split, "is_l_split");


  // Global Splitters
  Rect<1> g_split_rect(Point<1>(0), Point<1>(num_subregions-2));
  IndexSpace is_g_split = runtime->create_index_space(ctx,
                          Domain::from_rect<1>(g_split_rect));
  runtime->attach_name(is_g_split, "is_g_split");

  // Element Index for Buckets  
  int number_of_buckets = num_subregions*num_subregions;
  Rect<1> i_split_rect(Point<1>(0), Point<1>(number_of_buckets - 1));
  IndexSpace is_i_split =  runtime->create_index_space(ctx,
                              Domain::from_rect<1>(i_split_rect));
  runtime->attach_name(is_i_split, "is_i_split");

  // One per bucket
  Rect<1> one_per_bucket_rect(Point<1>(0), Point<1>(number_of_buckets - 1));
  IndexSpace is_one_per_bucket =  runtime->create_index_space(ctx,
                              Domain::from_rect<1>(one_per_bucket_rect));
  runtime->attach_name(is_one_per_bucket, "is_one_per_bucket");
  // ------------------------------------------------------------


  // ----------------- Field Spaces -----------------------------
  // TODO: Clean up code, use one field space

  // Input Field Space
  FieldSpace input_fs = runtime->create_field_space(ctx);
  runtime->attach_name(input_fs, "input_fs");
  {
    FieldAllocator allocator = 
      runtime->create_field_allocator(ctx, input_fs);
    allocator.allocate_field(sizeof(int),FID_X);
    runtime->attach_name(input_fs, FID_X, "X");
  }

  // ------------------------------------------------------------


  // ----------------- Logical Regions -----------------------------

  LogicalRegion input_lr = runtime->create_logical_region(ctx, is_io, input_fs);
  runtime->attach_name(input_lr, "input_lr");
  LogicalRegion output_lr = runtime->create_logical_region(ctx, is_io, input_fs);
  runtime->attach_name(output_lr, "output_lr");


  LogicalRegion splitter_lr = runtime->create_logical_region(ctx, is_l_split, input_fs);
  runtime->attach_name(splitter_lr, "splitter_lr");

  LogicalRegion g_splitters_lr = runtime->create_logical_region(ctx, is_g_split, input_fs);
  runtime->attach_name(g_splitters_lr, "g_splitters_lr");

  LogicalRegion split_index_lr = runtime->create_logical_region(ctx, is_i_split, input_fs);
  runtime->attach_name(split_index_lr, "split_index_lr");

  printf("DEBUG\n");
  LogicalRegion one_per_bucket_lr = runtime->create_logical_region(ctx, is_one_per_bucket, input_fs);
  runtime->attach_name(one_per_bucket_lr, "one_per_bucket_lr");
  printf("DEBUG\n");
  // ------------------------------------------------------------

  Rect<1> color_bounds(Point<1>(0),Point<1>(num_subregions-1));
  Domain color_domain = Domain::from_rect<1>(color_bounds);


  IndexPartition ip, ip_splitter, ip_index, ip_one_per_bucket;
  if ((num_elements % num_subregions) != 0)
  {

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

    ip = runtime->create_index_partition(ctx, is_io, color_domain, 
                                      coloring, true/*disjoint*/);
  }
  else
  { 
    Blockify<1> coloring(num_elements/num_subregions);
    ip = runtime->create_index_partition(ctx, is_io, coloring);
  }
  runtime->attach_name(ip, "ip");


  Blockify<1> coloring(num_subregions-1);
  ip_splitter = runtime->create_index_partition(ctx, is_l_split, coloring);
  Blockify<1> bucket_coloring(num_subregions);
  ip_index          = runtime->create_index_partition(ctx, is_i_split, bucket_coloring);
  ip_one_per_bucket = runtime->create_index_partition(ctx, is_one_per_bucket, bucket_coloring);
  runtime->attach_name(ip_splitter, "ip_splitter");
  runtime->attach_name(ip_index, "ip_index");
  runtime->attach_name(ip_one_per_bucket, "ip_one_per_bucket");
  printf("DEBUG\n");
  LogicalPartition input_lp = runtime->get_logical_partition(ctx, input_lr, ip);
  runtime->attach_name(input_lp, "input_lp");

/*  LogicalPartition output_lp = runtime->get_logical_partition(ctx, output_lr, ip);
  runtime->attach_name(output_lp, "output_lp");*/

  LogicalPartition splitter_lp = runtime->get_logical_partition(ctx, splitter_lr, ip_splitter);
  runtime->attach_name(splitter_lp, "splitter_lp");

  LogicalPartition split_index_lp = runtime->get_logical_partition(ctx, split_index_lr, ip_index);
  runtime->attach_name(split_index_lp, "split_index_lp");

  LogicalPartition one_per_bucket_lp = runtime->get_logical_partition(ctx, one_per_bucket_lr, ip_one_per_bucket);
  runtime->attach_name(one_per_bucket_lp, "one_per_bucket_lp");

  // Create our launch domain.  Note that is the same as color domain
  // as we are going to launch one task for each subregion we created.
  Domain launch_domain = color_domain; 
  ArgumentMap arg_map;

  IndexLauncher init_launcher(INIT_FIELD_TASK_ID, launch_domain, 
                              TaskArgument(NULL, 0), arg_map);

  init_launcher.add_region_requirement(
      RegionRequirement(input_lp, 0/*projection ID*/, 
                        WRITE_DISCARD, EXCLUSIVE, input_lr));
  init_launcher.region_requirements[0].add_field(FID_X);
  runtime->execute_index_space(ctx, init_launcher);

  runtime->execute_index_space(ctx, init_launcher);

  // Sort each sub-region locally  
  IndexLauncher qsort_launcher(QSORT_TASK_ID, launch_domain,
                TaskArgument(&num_subregions, sizeof(int)), arg_map);
  qsort_launcher.add_region_requirement(
      RegionRequirement(input_lp, 0,
                        WRITE_DISCARD, EXCLUSIVE, input_lr));
  qsort_launcher.region_requirements[0].add_field(FID_X);

  qsort_launcher.add_region_requirement(
      RegionRequirement(splitter_lp, 0,
                        WRITE_DISCARD, EXCLUSIVE, splitter_lr));
  qsort_launcher.region_requirements[1].add_field(FID_X);
  runtime->execute_index_space(ctx, qsort_launcher);


  TaskLauncher gather_splitter_task(ONE_TASK_ID, TaskArgument(&num_subregions, sizeof(int)));

  gather_splitter_task.add_region_requirement(
      RegionRequirement(input_lr, READ_ONLY, EXCLUSIVE, input_lr));
  gather_splitter_task.region_requirements[0].add_field(FID_X);
  gather_splitter_task.add_region_requirement(
      RegionRequirement(splitter_lr, READ_ONLY, EXCLUSIVE, splitter_lr));
  gather_splitter_task.region_requirements[1].add_field(FID_X);
  gather_splitter_task.add_region_requirement(
      RegionRequirement(g_splitters_lr, WRITE_DISCARD, EXCLUSIVE, g_splitters_lr));
  gather_splitter_task.region_requirements[2].add_field(FID_X);
  runtime->execute_task(ctx, gather_splitter_task);
  

  // Write indexes for each bucket
  
  IndexLauncher p_bucket_task_launcher(P_BUCKET_TASK_ID, launch_domain,
                TaskArgument(&num_subregions, sizeof(int)), arg_map);

  p_bucket_task_launcher.add_region_requirement(
      RegionRequirement(input_lp, 0,
                        READ_ONLY, EXCLUSIVE, input_lr));
  p_bucket_task_launcher.region_requirements[0].add_field(FID_X);

  p_bucket_task_launcher.add_region_requirement(
      RegionRequirement(g_splitters_lr, 0,
        READ_ONLY, EXCLUSIVE, g_splitters_lr));
  p_bucket_task_launcher.region_requirements[1].add_field(FID_X);

  p_bucket_task_launcher.add_region_requirement(
      RegionRequirement(split_index_lp, 0,
                        WRITE_DISCARD, EXCLUSIVE, split_index_lr));
  p_bucket_task_launcher.region_requirements[2].add_field(FID_X);
  runtime->execute_index_space(ctx, p_bucket_task_launcher);


  IndexLauncher qsort_bucket_task_launcher(QSORT_BUCKET_ID, launch_domain,
                TaskArgument(&num_subregions, sizeof(int)), arg_map);
  qsort_bucket_task_launcher.add_region_requirement(
      RegionRequirement(split_index_lr, 0,
        READ_ONLY, SIMULTANEOUS, split_index_lr));
  qsort_bucket_task_launcher.region_requirements[0].add_field(FID_X);

  qsort_bucket_task_launcher.add_region_requirement(
      RegionRequirement(input_lr, 0,
        READ_ONLY, SIMULTANEOUS, input_lr));
  qsort_bucket_task_launcher.region_requirements[1].add_field(FID_X);

  qsort_bucket_task_launcher.add_region_requirement(
      RegionRequirement(one_per_bucket_lp, 0,
        READ_WRITE, EXCLUSIVE, one_per_bucket_lr));
  qsort_bucket_task_launcher.region_requirements[2].add_field(FID_X);

  qsort_bucket_task_launcher.add_region_requirement(
      RegionRequirement(split_index_lp, 0,
                        READ_ONLY, SIMULTANEOUS, split_index_lr));
  qsort_bucket_task_launcher.region_requirements[3].add_field(FID_X);
  
  qsort_bucket_task_launcher.add_region_requirement(
      RegionRequirement(output_lr, 0,
        READ_ONLY, SIMULTANEOUS, output_lr));
  qsort_bucket_task_launcher.region_requirements[4].add_field(FID_X);
  runtime->execute_index_space(ctx, qsort_bucket_task_launcher);



  TaskLauncher check_launcher(CHECKER_TASK_ID, TaskArgument(NULL, 0));

  check_launcher.add_region_requirement(
      RegionRequirement(one_per_bucket_lr, READ_ONLY, EXCLUSIVE, one_per_bucket_lr));
  check_launcher.region_requirements[0].add_field(FID_X);
  check_launcher.add_region_requirement(
      RegionRequirement(output_lr, READ_ONLY, EXCLUSIVE, output_lr));
  check_launcher.region_requirements[1].add_field(FID_X);
  runtime->execute_task(ctx, check_launcher);

  
  runtime->destroy_logical_region(ctx, input_lr);
  runtime->destroy_logical_region(ctx, output_lr);
  runtime->destroy_logical_region(ctx, splitter_lr);

  //runtime->destroy_field_space(ctx, splitter_fs);
  runtime->destroy_field_space(ctx, input_fs);
  //runtime->destroy_field_space(ctx, output_fs);

  runtime->destroy_index_space(ctx, is_l_split);
  runtime->destroy_index_space(ctx, is_i_split);
  runtime->destroy_index_space(ctx, is_g_split);
  runtime->destroy_index_space(ctx, is_io);
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

  Domain dom = runtime->get_index_space_domain(ctx, 
      task->regions[0].region.get_index_space());
  Rect<1> rect = dom.get_rect<1>();
  int value = 10;
  for (GenericPointInRectIterator<1> pir(rect); pir; pir++)
  {
    //int val = lrand48()%100;
    acc.write(DomainPoint::from_point<1>(pir.p), value);
    printf("Value[%d] written is %d\n", point, value);
    value--;
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
    regions[1].get_field_accessor(FID_X).typeify<int>();
  Domain dom_splitter = runtime->get_index_space_domain(ctx,
      task->regions[1].region.get_index_space());
  Rect<1> splitter_rect = dom_splitter.get_rect<1>();

  int length = 0;
  for (GenericPointInRectIterator<1> pir(rect); pir; pir++)
  {
    length++;
  }

  LegionRuntime::Accessor::ByteOffset byte_offset(0);
  Rect<1> sub_rect;

  void * check_ptr = acc_x.raw_rect_ptr<1>(rect, sub_rect, &byte_offset);

  // This assert makes sure rect is not out of bounds
  assert (sub_rect == rect);

/*
  {
    int index_ptr = 0;
    while (index_ptr < length){
    int check_value = *((int *) (check_ptr) + index_ptr);
    printf("Value at [%d] start is %d\n", index_ptr, check_value);
    index_ptr++;
  }
*/

  qsort(check_ptr, length, sizeof(int), compare);
/*
  {
    int index_ptr = 0;
    while (index_ptr < length){
      int check_value = *((int *) (check_ptr) + index_ptr);
      index_ptr++;
    }
  }
*/

  GenericPointInRectIterator<1> pir(splitter_rect);
  for (int i = 0; i < num_subregions-1; i++)
  {
    int value = ((int *) check_ptr)[(length / num_subregions) * (i+1)];
    acc_s.write(DomainPoint::from_point<1>(pir.p), value);
    pir++;
  }

}



void p_bucket_task(const Task *task,
                const std::vector<PhysicalRegion> &regions,
                Context ctx, Runtime *runtime)
{

  FieldID fid_input  = *(task->regions[0].privilege_fields.begin());
  FieldID fid_split  = *(task->regions[1].privilege_fields.begin());
  FieldID fid_bucket = *(task->regions[2].privilege_fields.begin());

  RegionAccessor<AccessorType::Generic, int> acc_input = 
    regions[0].get_field_accessor(fid_input).typeify<int>();

  RegionAccessor<AccessorType::Generic, int> acc_split = 
    regions[1].get_field_accessor(fid_split).typeify<int>();

  RegionAccessor<AccessorType::Generic, int> acc_bucket = 
    regions[2].get_field_accessor(fid_bucket).typeify<int>();


  LegionRuntime::Accessor::ByteOffset byte_offset(0);

  // Accessor for input
  Domain input_domain = runtime->get_index_space_domain(ctx,
      task->regions[0].region.get_index_space());
  Rect<1> input_rect = input_domain.get_rect<1>();

  // Accessor for splitters
  Domain splitter_domain = runtime->get_index_space_domain(ctx,
      task->regions[1].region.get_index_space());
  Rect<1> split_rect = splitter_domain.get_rect<1>();
  Rect<1> sub_rect;
  

  // Accessor for bucket
  Domain bucket_domain = runtime->get_index_space_domain(ctx,
      task->regions[2].region.get_index_space());
  Rect<1> bucket_rect = bucket_domain.get_rect<1>();
  Rect<1> sub_rect2;

  GenericPointInRectIterator<1>split_iter(split_rect);
  GenericPointInRectIterator<1>prev_split_iter(split_rect);
  GenericPointInRectIterator<1>next_split_iter(split_iter);
  GenericPointInRectIterator<1>bucket_iter(bucket_rect);
  GenericPointInRectIterator<1>next_bucket_iter(bucket_rect);
  GenericPointInRectIterator<1>input_iter(input_rect);
  // Initialize all index with -1
  int num_buckets = 0;
  for (GenericPointInRectIterator<1>pir(bucket_rect); pir; pir++) {
    acc_bucket.write(DomainPoint::from_point<1>(pir.p), -1);
    num_buckets++;
  }

  int current_index = 0;

  while (split_iter){

    int value          = acc_input.read(DomainPoint::from_point<1>(input_iter.p));
    // int bucket_value   = acc_bucket.read(DomainPoint::from_point<1>(bucket_iter.p));
    int splitter_value = acc_split.read(DomainPoint::from_point<1>(split_iter.p));


    if (value < splitter_value){
      acc_bucket.write(DomainPoint::from_point<1>(bucket_iter.p), current_index);
      while (value < splitter_value && input_iter){
        input_iter++;
        current_index++;
        value = acc_input.read(DomainPoint::from_point<1>(input_iter.p));
      }
    }
    next_split_iter = split_iter;
    next_split_iter++;

    bucket_iter++;
    if (!next_split_iter && input_iter){
      acc_bucket.write(DomainPoint::from_point<1>(bucket_iter.p), current_index);
    }

    split_iter++;
    

    if (!input_iter) {  
      break;
    }
  }

/*
  current_index = 0;
  for (GenericPointInRectIterator<1>pir(bucket_rect); pir; pir++) {
      int value = acc_bucket.read(DomainPoint::from_point<1>(pir.p));
      current_index++;
  }
*/

  printf("p_bucket_task: done\n");
}

void qsort_bucket_task(const Task *task,
                        const std::vector<PhysicalRegion> &regions,
                        Context ctx, Runtime *runtime)
{

  assert(regions.size() == 5);
  // FieldID fid = *(task->regions[0].privilege_fields.begin());
  const int bucket = task->index_point.point_data[0];

  RegionAccessor<AccessorType::Generic, int> acc_index = 
    regions[0].get_field_accessor(FID_X).typeify<int>();
  
  RegionAccessor<AccessorType::Generic, int> acc_input = 
    regions[1].get_field_accessor(FID_X).typeify<int>();

  RegionAccessor<AccessorType::Generic, int> acc_one_per_bucket = 
    regions[2].get_field_accessor(FID_X).typeify<int>();

  RegionAccessor<AccessorType::Generic, int> acc_output = 
    regions[4].get_field_accessor(FID_X).typeify<int>();


  Domain input_domain = runtime->get_index_space_domain(ctx,
    task->regions[1].region.get_index_space());

  Domain index_domain = runtime->get_index_space_domain(ctx,
    task->regions[0].region.get_index_space());
  
  Domain one_per_bucket_dom = runtime->get_index_space_domain(ctx,
    task->regions[2].region.get_index_space());

  Domain output_domain = runtime->get_index_space_domain(ctx,
    task->regions[4].region.get_index_space());

  Rect<1> input_rect  = input_domain.get_rect<1>();
  Rect<1> index_rect  = index_domain.get_rect<1>();
  Rect<1> output_rect = output_domain.get_rect<1>();
  Rect<1> one_per_bucket_rect = one_per_bucket_dom.get_rect<1>();
  
  const int total_elements = input_rect.dim_size(0);
  const int num_subregions = *((const int*)task->args);
  const int size_index     = num_subregions * num_subregions;
  const int elements_per_bucket = total_elements/num_subregions;


  LegionRuntime::Accessor::ByteOffset byte_offset(0);
  Rect<1> index_sub_rect, output_sub_rect, input_sub_rect;

  int * input_ptr = (int *) acc_input.raw_rect_ptr<1>(input_rect, input_sub_rect, &byte_offset);
  assert(input_rect == input_sub_rect);
  int * input_checker_ptr = input_ptr;
  if (bucket == 0){
      for (int i = 0; i < total_elements; i++){
    printf("(Before Write: Input value[%d] = %d\n", i, *input_checker_ptr);
    input_checker_ptr++;
   } 
  }
  input_checker_ptr = input_ptr;

  int * index_ptr = (int *) acc_index.raw_rect_ptr<1>(index_rect, index_sub_rect, &byte_offset);
  assert(index_rect == index_sub_rect);

  int * output_ptr = (int *) acc_output.raw_rect_ptr<1>(output_rect, output_sub_rect, &byte_offset);
  int * output_offset = output_ptr;
  assert(output_rect == output_sub_rect);

  int total_elements_in_bucket = 0;


  // Only find offsets for buckets less than your own
  int element_offset = 0;
  for (int c_bucket = 0; c_bucket < bucket; c_bucket++) {
      for (int i = c_bucket; i < size_index; i += num_subregions){
        int elements = *(index_ptr + i);
        if (elements == -1)
          continue;  
          int k = i+1;
          while (k < (i+num_subregions-c_bucket) && *(index_ptr+k) == -1){
            k++;
          }
          if (k < i+num_subregions-c_bucket)
            element_offset += (*(index_ptr+k) - elements);
          else 
            element_offset += (elements_per_bucket - elements);
      }
    }
    output_ptr += element_offset;
    // printf("Bucket[%d] Start offset = %d\n", bucket, element_offset);

    // Elements in your bucket
    int input_index_offset = 0;
    for (int i = bucket; i < size_index; i += num_subregions){
      int elements = *(index_ptr + i);
      int elements_in_this_bucket = 0;

      if (elements == -1){
        input_index_offset += (total_elements / num_subregions);
        continue;
      }
          
       *output_ptr = *(input_ptr + elements + input_index_offset);
       // printf("Bucket[%d] Writing %d to output[%ld]\n", bucket, *(output_ptr), output_ptr - output_offset);
        int k = i+1;
        while (k < (i+num_subregions-bucket) && *(index_ptr+k) == -1){
          k++;
        }
        if (k < i + num_subregions-bucket) {
          elements_in_this_bucket = (*(index_ptr+k) - elements);
          total_elements_in_bucket += (*(index_ptr+k) - elements);
        }
        else {
          elements_in_this_bucket = (elements_per_bucket - elements);
          total_elements_in_bucket += (elements_per_bucket - elements);
        }
        int local_offset = 1;
        output_ptr++;
        k = i + 1;
        while (local_offset < elements_in_this_bucket){
          *output_ptr =  *(input_ptr + local_offset + input_index_offset + elements);
          // printf("Bucket[%d] Writing %d to output[%ld]\n", bucket, *(output_ptr), output_ptr - output_offset);
          local_offset++;
          output_ptr++;
        }

        input_index_offset += (total_elements / num_subregions);
  }

  printf("For bucket %d: Total Elements =  %d\n", bucket, total_elements_in_bucket);
  GenericPointInRectIterator<1>pir(one_per_bucket_rect);
  acc_one_per_bucket.write(DomainPoint::from_point<1>(pir.p), total_elements_in_bucket);
 
  // Do qsort on the buckets
  output_ptr = (int *) acc_output.raw_rect_ptr<1>(output_rect, output_sub_rect, &byte_offset);
  output_ptr += element_offset;
  qsort(output_ptr, total_elements_in_bucket, sizeof(int), compare);

/*
  if (bucket == 0){
      for (int i = 0; i < total_elements; i++){
    printf("(After Write: Input value[%d] = %d\n", i, *input_checker_ptr);
    input_checker_ptr++;
   } 
  }
*/
}

void one_task(const Task *task,
                const std::vector<PhysicalRegion> &regions,
                Context ctx, Runtime *runtime)
{
  assert(regions.size() == 3);
  assert(task->regions.size() == 3);
  const int num_subregions = *((const int*)task->args);

//  RegionAccessor<AccessorType::Generic, int> acc_x = 
//    regions[0].get_field_accessor(FID_X).typeify<int>();
  
  RegionAccessor<AccessorType::Generic, int> acc_s = 
    regions[1].get_field_accessor(FID_X).typeify<int>();

  RegionAccessor<AccessorType::Generic, int> acc_g = 
    regions[2].get_field_accessor(FID_X).typeify<int>();

//  Domain dom = runtime->get_index_space_domain(ctx, 
//      task->regions[0].region.get_index_space());
//  Rect<1> rect = dom.get_rect<1>();

  Domain dom_splitter = runtime->get_index_space_domain(ctx,
    task->regions[1].region.get_index_space());
  Rect<1> split_rect = dom_splitter.get_rect<1>();

  // Sort splitters
  Rect<1> sub_rect;
  LegionRuntime::Accessor::ByteOffset byte_offset(0);

  void * split_ptr = acc_s.raw_rect_ptr<1>(split_rect, sub_rect, &byte_offset);
  qsort(split_ptr, num_subregions*(num_subregions-1), sizeof(int), compare);


  // Choose global splitters
  Domain dom_g_split = runtime->get_index_space_domain(ctx,
    task->regions[2].region.get_index_space());
  Rect<1> g_split_rect = dom_g_split.get_rect<1>();
  Rect<1> sub_g_split_rect;
  int * g_split_ptr = (int *) (acc_g.raw_rect_ptr<1>(g_split_rect, sub_g_split_rect, &byte_offset));

  for (int i = 0; i < num_subregions-1; i++){

      g_split_ptr[i] = ((int*)(split_ptr))[(num_subregions-1)*(i+1)];
  }

  for (GenericPointInRectIterator<1> pir(g_split_rect); pir; pir++)
  {
    int expected = acc_g.read(DomainPoint::from_point<1>(pir.p));
    printf("Global Splitter Value is %d\n", expected);
  }
}

void checker_task(const Task *task,
                const std::vector<PhysicalRegion> &regions,
                Context ctx, Runtime *runtime)
{
  assert(regions.size() == 2);
  RegionAccessor<AccessorType::Generic, int> acc_bucket = 
    regions[0].get_field_accessor(FID_X).typeify<int>();

  RegionAccessor<AccessorType::Generic, int> acc_output = 
    regions[1].get_field_accessor(FID_X).typeify<int>();

  Domain bucket_domain = runtime->get_index_space_domain(ctx,
    task->regions[0].region.get_index_space());

  Domain output_domain = runtime->get_index_space_domain(ctx,
    task->regions[1].region.get_index_space());

  Rect<1> bucket_rect = bucket_domain.get_rect<1>();
  Rect<1> output_rect = output_domain.get_rect<1>();



  printf("Printing out size of each final bucket:\n");
  int counter = 0;
  for (GenericPointInRectIterator<1> pir(bucket_rect); pir; pir++) {
    printf("Value[%d] = %d\n", counter, acc_bucket.read(DomainPoint::from_point<1>(pir.p)));
    counter++;
  }



  std::ofstream myfile ("output.txt");
  if (!myfile.is_open()) {
    printf("ERROR: Could not open file output.txt\n");
  }
  else {
    GenericPointInRectIterator<1> pir(output_rect);
    int prev_value = acc_output.read(DomainPoint::from_point<1>(pir.p));
    myfile << prev_value << " ";
    pir++;
    int index = 1;
    for (; pir; pir++) {
      int current_value = acc_output.read(DomainPoint::from_point<1>(pir.p));
      if (current_value < prev_value) {
       printf("ERROR: Sorting mistake at output[%d]\n", index);
       myfile.close();
       return;
      }
      myfile << current_value << " ";
    }
    myfile.close();    
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

  Runtime::register_legion_task<p_bucket_task>(P_BUCKET_TASK_ID,
      Processor::LOC_PROC, true/*single*/, true/*index*/,
      AUTO_GENERATE_ID, TaskConfigOptions(true), "p_bucket_task");

  Runtime::register_legion_task<qsort_bucket_task>(QSORT_BUCKET_ID,
      Processor::LOC_PROC, true/*single*/, true/*index*/,
      AUTO_GENERATE_ID, TaskConfigOptions(true), "qsort_bucket_task");

  Runtime::register_legion_task<checker_task>(CHECKER_TASK_ID,
      Processor::LOC_PROC, true/*single*/, true/*index*/,
      AUTO_GENERATE_ID, TaskConfigOptions(true), "checker_task");


  return Runtime::start(argc, argv);
}
