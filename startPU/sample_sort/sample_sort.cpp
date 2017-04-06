#include <starpu.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <stdlib.h> // for sequential sort
#include <vector>

using namespace std;

#define NUM_CPU 3

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
        int dst_index = i + idx * (NUM_CPU - 1);
        splitters[dst_index] = arr[src_index];
    }

    /*
    int *g_splitters = (int *)STARPU_VARIABLE_GET_PTR(buffers[2]);

    // Each processor sort its block
    int blk_size = len / NUM_CPU;
    int reg_blk_size = blk_size;
    int last_blk_size = (len % NUM_CPU == 0) ? blk_size : len % NUM_CPU;

    int i;
    for (i = 0; i < NUM_CPU - 1; ++i)
    {
        qsort(arr + i * blk_size, blk_size, sizeof(int), compare);
    }
    qsort(arr + i * blk_size, last_blk_size, sizeof(int), compare);

    // Get local splitters, gather them in the splitters array

    for (int proc = 0; proc < NUM_CPU; ++proc)
    {
        blk_size = proc != (NUM_CPU - 1) ? reg_blk_size : last_blk_size;

        for (i = 0; i < (NUM_CPU - 1); i++)
        {
            int src_index = (int)((float)(blk_size) / (NUM_CPU) * (i + 1) + proc * blk_size);
            int dst_index = i + proc * (NUM_CPU - 1);
            splitters[dst_index] = arr[src_index];
        }
    }

    // sort the splitters array
    qsort(splitters, NUM_CPU * (NUM_CPU - 1), sizeof(int), compare);

    // choose global splitters
    for (i = 0; i < NUM_CPU - 1; ++i)
    {
        g_splitters[i] = splitters[(NUM_CPU - 1) * (i + 1)];
    }

    // each processor
    // **** Creating Numprocs Buckets ****
    int *buckets = new int[(len + NUM_CPU) * NUM_CPU];

    int j = 0;
    int k = 1;

    for (int proc = 0; proc < NUM_CPU; ++proc)
    {
        blk_size = proc == NUM_CPU - 1 ? last_blk_size : reg_blk_size;
        for (i = 0; i < blk_size; i++)
        {
            if (j < (NUM_CPU - 1))
            {
                if (arr[i] < g_splitters[j])
                    buckets[((blk_size + 1) * j) + k++] = arr[i];
                else
                {
                    buckets[(blk_size + 1) * j] = k - 1;
                    k = 1;
                    j++;
                    i--;
                }
            }
            else
                buckets[((blk_size + 1) * j) + k++] = arr[i];
        }
        buckets[(blk_size + 1) * j] = k - 1;
    }


    */
}

struct starpu_codelet phaseI_codelet =
    {
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
        .cpu_funcs = {cpu_phaseII},
        .cpu_funcs_name = {"cpu_phaseII"},
        .modes = {STARPU_RW, STARPU_RW},
        .nbuffers = 2};

// III - Bucket sort
void cpu_phaseIII(void *buffers[], void *cl_arg)
{
    // get the array and length
    int len = STARPU_VECTOR_GET_NX(buffers[0]);
    int *arr = (int *)STARPU_VARIABLE_GET_PTR(buffers[0]);
    int g_splitters_len = STARPU_VECTOR_GET_NX(buffers[1]);
    int *g_splitters = (int *)STARPU_VARIABLE_GET_PTR(buffers[1]);
    vector< vector<int> > *buckets = (vector< vector<int> > *)STARPU_VARIABLE_GET_PTR(buffers[2]);

    // for(int i = 1; i <= NUM_CPU; ++i)
    //     buckets[i] = (vector<int> *)STARPU_VARIABLE_GET_PTR(buffers[i]);

    buckets = (vector< vector<int> > *)STARPU_VARIABLE_GET_PTR(buffers[2]);
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
                (*buckets)[s_idx].push_back(arr[i + array_base]);
            else
            {
                ++s_idx;
                --i;
            }
        }
        else
            (*buckets)[s_idx].push_back(arr[i + array_base]);
    }
}

struct starpu_codelet phaseIII_codelet =
    {
        .cpu_funcs = {cpu_phaseIII},
        .cpu_funcs_name = {"cpu_phaseIII"},
        .modes = {STARPU_R, STARPU_R, STARPU_RW},
        .nbuffers = 3};

// IV - Local sort each block
void cpu_phaseIV(void *buffers[], void *cl_arg)
{
    // get the array and length
    int len = STARPU_VECTOR_GET_NX(buffers[0]);
    int *arr = (int *)STARPU_VARIABLE_GET_PTR(buffers[0]);

    qsort(arr, len, sizeof(int), compare);
}

struct starpu_codelet phaseIV_codelet =
    {
        .cpu_funcs = {cpu_phaseIV},
        .cpu_funcs_name = {"cpu_phaseIV"},
        .modes = {STARPU_RW},
        .nbuffers = 1};

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

    starpu_data_handle_t array_handle;
    starpu_vector_data_register(&array_handle, STARPU_MAIN_RAM, (uintptr_t)arr, len, sizeof(int));
    starpu_data_handle_t splitters_handle;
    starpu_vector_data_register(&splitters_handle, STARPU_MAIN_RAM, (uintptr_t)splitters, NUM_CPU * (NUM_CPU - 1), sizeof(int));
    starpu_data_handle_t g_splitters_handle;
    starpu_vector_data_register(&g_splitters_handle, STARPU_MAIN_RAM, (uintptr_t)g_splitters, NUM_CPU - 1, sizeof(int));

    int ret;
    /* Initialize StarPU with default configuration */
    ret = starpu_init(NULL);
    if (ret == -ENODEV)
        return 77;
    STARPU_CHECK_RETURN_VALUE(ret, "starpu_init");
    struct starpu_task *task_init = starpu_task_create();
    task_init->cl = &init_codelet;
    task_init->handles[0] = array_handle;
    task_init->cl_arg = &in_file;
    task_init->cl_arg_size = sizeof(in_file);

    /* starpu_task_submit will be a blocking call. If unset,
    starpu_task_wait() needs to be called after submitting the task. */
    task_init->synchronous = 1;
    /* submit the task to StarPU */
    ret = starpu_task_submit(task_init);
    if (ret != -ENODEV)
        STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");

    int reg_blk_size = len / NUM_CPU;
    int last_blk_size = (len % NUM_CPU == 0) ? reg_blk_size : len % NUM_CPU;

    // ********************************************/
    // I - Local sort every block and pick splitters//
    for (int i = 0; i < NUM_CPU; ++i)
    {
        int blk_size = (i == NUM_CPU - 1) ? last_blk_size : reg_blk_size;
        starpu_data_handle_t blk_handle;
        starpu_vector_data_register(&blk_handle, STARPU_MAIN_RAM, (uintptr_t)(arr + reg_blk_size * i), blk_size, sizeof(int));
        struct starpu_task *task = starpu_task_create();
        task->cl = &phaseI_codelet;
        task->handles[0] = blk_handle;
        task->handles[1] = splitters_handle;
        //task->handles[2] = g_splitters_handle;
        task->cl_arg = &i;
        task->cl_arg_size = sizeof(int);
        task->synchronous = 1;
        ret = starpu_task_submit(task);
        if (ret != -ENODEV)
            STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");

        starpu_data_unregister(blk_handle);
    }

    // Sort the splitters array
    qsort(splitters, NUM_CPU * (NUM_CPU - 1), sizeof(int), compare);

    // ********************************************/
    // Choose global splitter
    for (int i = 0; i < NUM_CPU; ++i)
    {
        struct starpu_task *task = starpu_task_create();
        task->cl = &phaseII_codelet;
        task->handles[0] = splitters_handle;
        task->handles[1] = g_splitters_handle;
        //task->handles[2] = g_splitters_handle;
        task->cl_arg = &i;
        task->cl_arg_size = sizeof(int);
        task->synchronous = 1;
        ret = starpu_task_submit(task);
        if (ret != -ENODEV)
            STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");
    }

    // ********************************************/
    // III - Each processor sort data to buckets
    vector< vector<int> > buckets(NUM_CPU);
    //starpu_data_handle_t buckets_handle[NUM_CPU];

    // for( int i = 0; i < NUM_CPU; ++i){
    //    starpu_vector_data_register(&buckets_handle[i], STARPU_MAIN_RAM, (uintptr_t)buckets[i], 1, sizeof(vector<int>));
    // }

    starpu_data_handle_t buckets_handle;
    starpu_vector_data_register(&buckets_handle, STARPU_MAIN_RAM, (uintptr_t)&buckets, 1, sizeof(vector< vector<int> >));

    for (int i = 0; i < NUM_CPU; ++i)
    {
        struct starpu_task *bucket_task = starpu_task_create();
        bucket_task->cl = &phaseIII_codelet;
        bucket_task->handles[0] = array_handle;
        bucket_task->handles[1] = g_splitters_handle;

        //for(int j = 1; j <= NUM_CPU; ++j)
        //{
        //    bucket_task->handles[j] = buckets_handle[j];
        //}

        bucket_task->handles[2] = buckets_handle;

        bucket_task->cl_arg = &i;
        bucket_task->cl_arg_size = sizeof(int);
        bucket_task->synchronous = 1;

        ret = starpu_task_submit(bucket_task);
        if (ret != -ENODEV)
            STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");
    }

    // ********************************************/
    // IV - Local sort each bucket
    for (int i = 0; i < NUM_CPU; ++i)
    {
        int blk_size = (i == NUM_CPU - 1) ? last_blk_size : reg_blk_size;
        starpu_data_handle_t blk_handle;
        starpu_vector_data_register(&blk_handle, STARPU_MAIN_RAM, (uintptr_t)(buckets[i].data()), buckets[i].size(), sizeof(vector<int>));
        struct starpu_task *task = starpu_task_create();
        task->cl = &phaseIV_codelet;
        task->handles[0] = blk_handle;                
        task->synchronous = 1;
        ret = starpu_task_submit(task);
        if (ret != -ENODEV)
            STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");

        starpu_data_unregister(blk_handle);
    }

    /* StarPU does not need to manipulate the array anymore so we can stop
 	 * monitoring it */
    starpu_data_unregister(array_handle);
    starpu_data_unregister(splitters_handle);
    starpu_data_unregister(g_splitters_handle);

    //for (int i = 0; i < NUM_CPU; ++i){
    //    starpu_data_unregister(buckets_handle[i]);
    //}

    starpu_data_unregister(buckets_handle);

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

    for (int i = 0; i < (NUM_CPU); ++i)
    {
        for(int j = 0; j < buckets[i].size(); ++j)
        out_file << buckets[i][j] << " ";
    }
    out_file << endl;

    out_file.close();

    delete[] arr;
    delete[] splitters;
    delete[] g_splitters;
    return 0;
}