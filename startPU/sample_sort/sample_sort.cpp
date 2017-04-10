#include <starpu.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <stdlib.h> // for sequential sort
#include <vector>

using namespace std;

#define NUM_CPU 4

/// Compare function for qsort
int compare(const void *a, const void *b)
{
    return (*(int *)a - *(int *)b);
}

void cpu_init(void *buffers[], void *cl_arg)
{
    int len = STARPU_VECTOR_GET_NX(buffers[0]);
    int *arr = (int *)STARPU_VARIABLE_GET_PTR(buffers[0]);

    ifstream *in_file = (ifstream *)cl_arg;

    // read the entire line into a string (a CSV record is terminated by a newline)
    string line;
    getline(*in_file, line); // pass the first line with the size
    getline(*in_file, line);
    // now we'll use a stringstream to separate the fields out of the line
    stringstream ss(line);
    string value;
    for (int i = 0; i < len; ++i)
    {
        getline(ss, value, ',');
        stringstream fs(value);
        int v = 0; // (default value is 0)
        fs >> v;   // this could be use for other type like float or double

        // add the newly-converted field to the end of the record
        arr[i] = v;
    }
}

struct starpu_codelet init_codelet =
    {
        .name = "init_codelet",
        .cpu_funcs = {cpu_init},
        .cpu_funcs_name = {"cpu_init"},
        .modes = {STARPU_W},
        .nbuffers = 1};

// I - Local sort each block and get splitters
void cpu_phaseI(void *buffers[], void *cl_arg)
{
    // get the array and length
    int len = STARPU_VECTOR_GET_NX(buffers[0]);
    int *arr = (int *)STARPU_VARIABLE_GET_PTR(buffers[0]);
    int *splitters = (int *)STARPU_VARIABLE_GET_PTR(buffers[1]);
    int idx = *(int *)(cl_arg);

    qsort(arr, len, sizeof(int), compare);

    for (int i = 0; i < NUM_CPU - 1; ++i)
    {
        int src_index = (int)((float)(len) / (NUM_CPU) * (i + 1));        
        splitters[i] = arr[src_index];
    }
}

struct starpu_codelet phaseI_codelet =
    {
        .name = "phaseI_codelet",
        .cpu_funcs = {cpu_phaseI},
        .cpu_funcs_name = {"cpu_phaseI"},
        .modes = {STARPU_RW, STARPU_RW, STARPU_RW},
        .nbuffers = 2};

// II - Get the global splitters
void cpu_phaseII(void *buffers[], void *cl_arg)
{
    // get the array and length
    int len = STARPU_VECTOR_GET_NX(buffers[0]);
    int *splitters = (int *)STARPU_VARIABLE_GET_PTR(buffers[0]);
    int *g_splitters = (int *)STARPU_VARIABLE_GET_PTR(buffers[1]);
    int idx = *(int *)(cl_arg);

    g_splitters[idx] = splitters[(idx + 1) * (NUM_CPU - 1)];
}

struct starpu_codelet phaseII_codelet =
    {
        .name = "phaseII_codelet",
        .cpu_funcs = {cpu_phaseII},
        .cpu_funcs_name = {"cpu_phaseII"},
        .modes = {STARPU_R, STARPU_W},
        .nbuffers = 2};

// III - Bucket sort
void cpu_phaseIII(void *buffers[], void *cl_arg)
{
    // get the array and length
    int len = STARPU_VECTOR_GET_NX(buffers[0]);
    int *arr = (int *)STARPU_VARIABLE_GET_PTR(buffers[0]);
    int g_splitters_len = STARPU_VECTOR_GET_NX(buffers[1]);
    int *g_splitters = (int *)STARPU_VARIABLE_GET_PTR(buffers[1]);
    vector<int> *buckets = (vector<int> *)STARPU_VARIABLE_GET_PTR(buffers[2]);

    // for(int i = 1; i <= NUM_CPU; ++i)
    //     buckets[i] = (vector<int> *)STARPU_VARIABLE_GET_PTR(buffers[i]);

    int idx = *(int *)(cl_arg); // Processor index
    int reg_blk_size = len / NUM_CPU;
    int last_blk_size = (len % NUM_CPU == 0) ? len / NUM_CPU : len % NUM_CPU;
    int array_base = idx * reg_blk_size; // base index starting of the corresponding array
    int s_idx = 0;                       // global spiltter index
    int blk_size = (idx == NUM_CPU - 1) ? last_blk_size : reg_blk_size;

    for (int i = 0; i < blk_size; ++i)
    {
        if (s_idx < NUM_CPU - 1)
        {

            if (arr[i + array_base] < g_splitters[s_idx])
                buckets[s_idx].push_back(arr[i + array_base]);
            else
            {
                ++s_idx;
                --i;
            }
        }
        else
            buckets[s_idx].push_back(arr[i + array_base]);
    }
}

struct starpu_codelet phaseIII_codelet =
    {
        .name = "phaseIII_codelet",
        .cpu_funcs = {cpu_phaseIII},
        .cpu_funcs_name = {"cpu_phaseIII"},
        .modes = {STARPU_R, STARPU_R, STARPU_RW},
        .nbuffers = 3};

// Get the size of each final bucket
void cpu_get_size(void *buffers[], void *cl_arg)
{
    // get the array and length    
    vector<int> **buckets = (vector<int> **)STARPU_VARIABLE_GET_PTR(buffers[0]);
    int *bucket_size = (int*)STARPU_VARIABLE_GET_PTR(buffers[1]);
    int idx = *(int *)(cl_arg); // Processor index
    
    *bucket_size = 0;
    for (int i = 0; i < NUM_CPU; ++i)
    {
        *bucket_size += buckets[i][idx].size();
    }
}

struct starpu_codelet get_size_codelet =
    {
        .name = "get_size_codelet",
        .cpu_funcs = {cpu_get_size},
        .cpu_funcs_name = {"cpu_get_size"},
        .modes = {STARPU_R, STARPU_W},
        .nbuffers = 2};

// IV - Local sort each block
void cpu_phaseIV(void *buffers[], void *cl_arg)
{      
    vector<int> ** buckets = (vector<int> **)STARPU_VARIABLE_GET_PTR(buffers[0]);
    int *out = (int*)STARPU_VARIABLE_GET_PTR(buffers[1]);
    int out_len = STARPU_VECTOR_GET_NX(buffers[1]);
    int idx = *(int*)cl_arg;

    // gather elements from bucket to the processor's output
    int k = 0;
    for(int i = 0; i < NUM_CPU; ++i)
    {
        for(int j = 0; j < buckets[i][idx].size(); ++j)
            out[k++] = buckets[i][idx][j];
    }

    // local sort the output
    qsort(out, out_len, sizeof(int), compare);
}

struct starpu_codelet phaseIV_codelet =
    {
        .name = "phaseIV_codelet",
        .cpu_funcs = {cpu_phaseIV},
        .cpu_funcs_name = {"cpu_phaseIV"},
        .modes = {STARPU_R, STARPU_RW},
        .nbuffers = 2};

int main(int argc, char **argv)
{
    int len = 0;
    char *input = argv[1];
    cout << input << endl;
    ifstream in_file(input);

    in_file >> len;

    cout << "Length: " << len << endl;

    int *arr = new int[len];
    int *splitters = new int[(NUM_CPU - 1) * NUM_CPU];
    int *g_splitters = new int[NUM_CPU - 1];

    // read the entire line into a string (a CSV record is terminated by a newline)
    string line;
    getline(in_file, line); // pass the first line with the size
    getline(in_file, line);
    // now we'll use a stringstream to separate the fields out of the line
    stringstream ss(line);
    string value;
    for (int i = 0; i < len; ++i)
    {
        getline(ss, value, ',');
        stringstream fs(value);
        int v = 0; // (default value is 0)
        fs >> v;   // this could be use for other type like float or double

        // add the newly-converted field to the end of the record
        arr[i] = v;
    }

    int ret;
    
    
    //starpu_data_handle_t array_handle;
    //starpu_vector_data_register(&array_handle, STARPU_MAIN_RAM, (uintptr_t)arr, len, sizeof(int));

    /* Initialize StarPU with default configuration */
    ret = starpu_init(NULL);
    if (ret == -ENODEV)
        return 77;
    STARPU_CHECK_RETURN_VALUE(ret, "starpu_init");

    
    //struct starpu_task *task_init = starpu_task_create();
    //task_init->cl = &init_codelet;
    //task_init->handles[0] = array_handle;
    //task_init->cl_arg = &in_file;
    //task_init->cl_arg_size = sizeof(in_file);
    //task_init->synchronous = 1;
    /* submit the task to StarPU */
    //ret = starpu_task_submit(task_init);
    //if (ret != -ENODEV)
    //    STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");

    //starpu_data_unregister(array_handle);


    //starpu_data_handle_t splitters_handle;
    //starpu_vector_data_register(&splitters_handle, STARPU_MAIN_RAM, (uintptr_t)splitters, NUM_CPU * (NUM_CPU - 1), sizeof(int));

    int reg_blk_size = len / NUM_CPU;
    int last_blk_size = (len % NUM_CPU == 0) ? reg_blk_size : len % NUM_CPU;

    // ********************************************/
    // I - Local sort every block and pick splitters//
    for (int i = 0; i < NUM_CPU; ++i)
    {
        int blk_size = (i == NUM_CPU - 1) ? last_blk_size : reg_blk_size;
        starpu_data_handle_t blk_handle;
        starpu_vector_data_register(&blk_handle, STARPU_MAIN_RAM, (uintptr_t)(arr + reg_blk_size * i), blk_size, sizeof(int));
        starpu_data_handle_t splitter_handle;
        starpu_vector_data_register(&splitter_handle, STARPU_MAIN_RAM, (uintptr_t)(splitters +i*(NUM_CPU-1)), (NUM_CPU - 1), sizeof(int));

        struct starpu_task *task = starpu_task_create();
        task->cl = &phaseI_codelet;
        task->handles[0] = blk_handle;
        task->handles[1] = splitter_handle;
        task->cl_arg = &i;
        task->cl_arg_size = sizeof(int);
        //task->synchronous = 1;
        ret = starpu_task_submit(task);
        if (ret != -ENODEV)
            STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");

        starpu_data_unregister(blk_handle);
        starpu_data_unregister(splitter_handle);
    }
    //starpu_task_wait_for_all(); //sync
    

    // Sort the splitters array
    qsort(splitters, NUM_CPU * (NUM_CPU - 1), sizeof(int), compare);

    // ********************************************/
    // Choose global splitter

    for (int i = 0; i < NUM_CPU; ++i)
    {
        g_splitters[i] = splitters[(i + 1) * (NUM_CPU - 1)];
    }

    starpu_data_handle_t g_splitters_handle;
    starpu_vector_data_register(&g_splitters_handle, STARPU_MAIN_RAM, (uintptr_t)g_splitters, NUM_CPU - 1, sizeof(int));
    
    /*
    for (int i = 0; i < NUM_CPU; ++i)
    {
        struct starpu_task *task = starpu_task_create();
        task->cl = &phaseII_codelet;
        task->handles[0] = splitters_handle;
        task->handles[1] = g_splitters_handle;
        //task->handles[2] = g_splitters_handle;
        task->cl_arg = &i;
        task->cl_arg_size = sizeof(int);
        //task->synchronous = 1;
        ret = starpu_task_submit(task);
        if (ret != -ENODEV)
            STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");
    }
    starpu_task_wait_for_all();      //sync

    */

    // ********************************************/
    // III - Each processor sort data to buckets
    //vector< vector<int> > buckets(NUM_CPU);
    //starpu_data_handle_t buckets_handle[NUM_CPU];

    // for( int i = 0; i < NUM_CPU; ++i){
    //    starpu_vector_data_register(&buckets_handle[i], STARPU_MAIN_RAM, (uintptr_t)buckets[i], 1, sizeof(vector<int>));
    // }

    starpu_data_handle_t array_handle2;
    starpu_vector_data_register(&array_handle2, STARPU_MAIN_RAM, (uintptr_t)arr, len, sizeof(int));


    vector<int> **buckets = new vector<int>*[ NUM_CPU ];
    for(int i = 0; i < NUM_CPU; ++i)
        buckets[i] = new vector<int>[NUM_CPU];

    for (int i = 0; i < NUM_CPU; ++i)
    {
        // each processor has an array of bucket vectors
        starpu_data_handle_t buckets_handle;
        starpu_vector_data_register(&buckets_handle, STARPU_MAIN_RAM, (uintptr_t)buckets[i], NUM_CPU, sizeof(vector<int>));

        struct starpu_task *bucket_task = starpu_task_create();
        bucket_task->cl = &phaseIII_codelet;
        bucket_task->handles[0] = array_handle2;
        bucket_task->handles[1] = g_splitters_handle;
        bucket_task->handles[2] = buckets_handle;

        bucket_task->cl_arg = &i;
        bucket_task->cl_arg_size = sizeof(int);
        //bucket_task->synchronous = 1;

        ret = starpu_task_submit(bucket_task);
        if (ret != -ENODEV)
            STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");

        starpu_data_unregister(buckets_handle);
    }
    //starpu_task_wait_for_all(); //sync
    starpu_data_unregister(array_handle2);
    starpu_data_unregister(g_splitters_handle);

    
    starpu_data_handle_t buckets_handle;
    starpu_vector_data_register(&buckets_handle, STARPU_MAIN_RAM, (uintptr_t)(buckets), NUM_CPU, sizeof(vector<int> *));
    
    // ********************************************/
    // Get the size of the bucket for each processor
    int *bucket_sizes = new int[NUM_CPU];
    for (int i = 0; i < NUM_CPU; ++i)
    {
        starpu_data_handle_t size_handle;
        starpu_vector_data_register(&size_handle, STARPU_MAIN_RAM, (uintptr_t)(&bucket_sizes[i]), 1, sizeof(int));

        struct starpu_task *task = starpu_task_create();
        task->cl = &get_size_codelet;
        task->handles[0] = buckets_handle;
        task->handles[1] = size_handle;        
        task->cl_arg = &i;
        task->cl_arg_size = sizeof(int);

        ret = starpu_task_submit(task);
        if (ret != -ENODEV)
            STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");

        starpu_data_unregister(size_handle);
    }
    starpu_task_wait_for_all();

    // TODO
    // Need to paralellize this reduction
    int sizes_redux[NUM_CPU] = {0};    
    for(int i = 1; i < NUM_CPU; ++i)
        sizes_redux[i] = sizes_redux[i-1] + bucket_sizes[i-1];

    // ********************************************/
    // IV - Local sort each bucket
    int *out = new int[len];
    for (int i = 0; i < NUM_CPU; ++i)
    {
        int blk_size = (i == NUM_CPU - 1) ? last_blk_size : reg_blk_size;
        starpu_data_handle_t blk_handle;
        starpu_vector_data_register(&blk_handle, STARPU_MAIN_RAM, (uintptr_t)(out + sizes_redux[i]), bucket_sizes[i], sizeof(int));
        struct starpu_task *task = starpu_task_create();
        task->cl = &phaseIV_codelet;
        task->handles[0] = buckets_handle;
        task->handles[1] = blk_handle;
        task->cl_arg = &i;
        task->cl_arg_size = sizeof(int);

        ret = starpu_task_submit(task);
        if (ret != -ENODEV)
            STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");

        starpu_data_unregister(blk_handle);
    }
    starpu_task_wait_for_all(); //sync    

    starpu_data_unregister(buckets_handle);

    //for (int i = 0; i < NUM_CPU; ++i){
    //    starpu_data_unregister(buckets_handle[i]);
    //}

    

    /* terminate StarPU */
    starpu_shutdown();

    ofstream out_file("output.txt");
    int blk_size;
    int cpu;

    // print the local sorted blocks
    /*    for (cpu = 0; cpu < NUM_CPU - 1; ++cpu)
    {
        blk_size = len / NUM_CPU;

        for (int i = 0; i < blk_size; ++i)
        {
            out_file << arr[cpu * blk_size + i] << " ";
        }
        out_file << endl
                 << "***********************************" << endl;
    }

    for (int i = 0; i < len % NUM_CPU; ++i)
    {
        out_file << arr[cpu * blk_size + i] << " ";
    }
    out_file << endl;*/

    /*
    for (int i = 0; i < (NUM_CPU); ++i)
    {
        for (int j = 0; j < buckets[i].size(); ++j)
            out_file << buckets[i][j] << " ";
    }
    */
    for (int i = 0; i < len; ++i)
    {
        out_file << out[i] << " ";
    }
    out_file << endl;

    out_file.close();

    delete[] arr;
    delete[] out;
    delete[] splitters;
    delete[] g_splitters;
    delete[] bucket_sizes;

    for (int i = 0; i < NUM_CPU; ++i)
        delete[] buckets[i];
    delete[] buckets;

    return 0;
}