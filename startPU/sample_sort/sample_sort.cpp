#include <starpu.h>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <stdlib.h> // for sequential sort
#include <vector>
#include <sys/time.h>

using namespace std;

//#define SYNC

int NUM_CPU = 4;

void printTime(struct timeval &tv1, struct timeval &tv2, const char *msg) {
    printf ("%s = %f seconds\n", msg,
        (double) (tv2.tv_usec - tv1.tv_usec) / 1000000 +
        (double) (tv2.tv_sec - tv1.tv_sec));
}

/// Test callback
void cb(void *msg)
{
    cout << (*(stringstream *)msg).str() << endl;
}

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
        .modes = {STARPU_RW, STARPU_RW},
        .nbuffers = 2};

// II - Get the global splitters
/*
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
*/

// III - Bucket sort
void cpu_phaseIII(void *buffers[], void *cl_arg)
{
    // get the array and length
    int len = STARPU_VECTOR_GET_NX(buffers[0]);
    int *arr = (int *)STARPU_VARIABLE_GET_PTR(buffers[0]);
    int g_splitters_len = STARPU_VECTOR_GET_NX(buffers[1]);
    int *g_splitters = (int *)STARPU_VARIABLE_GET_PTR(buffers[1]);
    vector<int> *buckets = (vector<int> *)STARPU_VARIABLE_GET_PTR(buffers[2]);

    // int idx = *(int *)(cl_arg); // Processor index
    // int reg_blk_size = len / NUM_CPU;
    // int last_blk_size = (len % NUM_CPU == 0) ? len / NUM_CPU : len % NUM_CPU;
    // int array_base = idx * reg_blk_size; // base index starting of the corresponding array
    //int blk_size = (idx == NUM_CPU - 1) ? last_blk_size : reg_blk_size;
    int s_idx = 0; // global spiltter index

    for (int i = 0; i < len; ++i)
    {
        if (s_idx < NUM_CPU - 1)
        {

            if (arr[i] < g_splitters[s_idx])
                buckets[s_idx].push_back(arr[i]);
            else
            {
                ++s_idx;
                --i;
            }
        }
        else
            buckets[s_idx].push_back(arr[i]);
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
/*
void cpu_get_size(void *buffers[], void *cl_arg)
{
    // get the array and length
    vector<int> **buckets = (vector<int> **)STARPU_VARIABLE_GET_PTR(buffers[0]);
    int *bucket_size = (int *)STARPU_VARIABLE_GET_PTR(buffers[1]);
    int idx = *(int *)(cl_arg); // Processor index

    *bucket_size = 0;
    for (int i = 0; i < NUM_CPU; ++i)
    {
        *bucket_size += buckets[idx][i].size();
    }
}

struct starpu_codelet get_size_codelet =
    {
        .name = "get_size_codelet",
        .cpu_funcs = {cpu_get_size},
        .cpu_funcs_name = {"cpu_get_size"},
        .modes = {STARPU_R, STARPU_W},
        .nbuffers = 2};
*/

// IV - Local sort each block
void cpu_phaseIV(void *buffers[], void *cl_arg)
{
    vector<int> **buckets = (vector<int> **)STARPU_VARIABLE_GET_PTR(buffers[0]);
    int *out = (int *)STARPU_VARIABLE_GET_PTR(buffers[1]);
    int out_len = STARPU_VECTOR_GET_NX(buffers[1]);
    int idx = *(int *)cl_arg;

    // gather elements from bucket to the processor's output
    int k = 0;
    for (int i = 0; i < NUM_CPU; ++i)
    {
        for (int j = 0; j < (*buckets[i]).size(); ++j)
            out[k++] = (*buckets[i])[j];
    }

    // local sort the outputd
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
    NUM_CPU = atoi(argv[1]);

    int len = 0;
    char *input = argv[2];
    cout << input << endl;
    ifstream in_file(input);

    in_file >> len;

    cout << "NUM_CPU " << NUM_CPU << endl;
    cout << "Length: " << len << endl;

#ifdef SYNC
    cout << "SYNC enabled\n";
#else
    cout << "SYNC disabled; phase measurement is not accurate\n";
#endif

    int *arr = new int[len];
    int *splitters = new int[(NUM_CPU - 1) * NUM_CPU];
    int *g_splitters = new int[NUM_CPU - 1];

    // read the entire line into a string (a CSV record is terminated by a newline)
    string line;
    getline(in_file, line); // pass the first line with the size

    // now we'll use a stringstream to separate the fields out of the line
    // stringstream ss;
    // int v;

    // ss << in_file.rdbuf();
    for (int i = 0; i < len; ++i)
    {
        // ss >> v; // this could be use for other type like float or double
        // // add the newly-converted field to the end of the record
        // arr[i] = v;
        // if (ss.peek() == ',')
        //     ss.ignore();
        in_file >> arr[i];
    }
    in_file.close();

    int ret;

    /* Initialize StarPU with default configuration */
    ret = starpu_init(NULL);
    if (ret == -ENODEV)
        return 77;
    STARPU_CHECK_RETURN_VALUE(ret, "starpu_init");

    /* Enable profiling */
    starpu_profiling_status_set(STARPU_PROFILING_ENABLE);

    int reg_blk_size = len / NUM_CPU;
    int last_blk_size = (len % NUM_CPU == 0) ? reg_blk_size : len % NUM_CPU;

    // Start the timer
    struct timeval tv0, tv1, tv2;
    gettimeofday(&tv1, NULL);
    tv0 = tv1;

    // ********************************************/
    // I - Local sort every block and pick splitters//
    starpu_data_handle_t *blk_handles = new starpu_data_handle_t[NUM_CPU];
    starpu_data_handle_t *splitter_handles = new starpu_data_handle_t[NUM_CPU];

    for (int i = 0; i < NUM_CPU; ++i)
    {
        int blk_size = (i == NUM_CPU - 1) ? last_blk_size : reg_blk_size;
        starpu_vector_data_register(&blk_handles[i], STARPU_MAIN_RAM, (uintptr_t)(arr + reg_blk_size * i), blk_size, sizeof(int));
        starpu_vector_data_register(&splitter_handles[i], STARPU_MAIN_RAM, (uintptr_t)(splitters + i * (NUM_CPU - 1)), (NUM_CPU - 1), sizeof(int));
    }

    for (int i = 0; i < NUM_CPU; ++i)
    {
        /*
        int blk_size = (i == NUM_CPU - 1) ? last_blk_size : reg_blk_size;
        starpu_data_handle_t blk_handle;
        starpu_vector_data_register(&blk_handle, STARPU_MAIN_RAM, (uintptr_t)(arr + reg_blk_size * i), blk_size, sizeof(int));
        starpu_data_handle_t splitter_handle;
        starpu_vector_data_register(&splitter_handle, STARPU_MAIN_RAM, (uintptr_t)(splitters +i*(NUM_CPU-1)), (NUM_CPU - 1), sizeof(int));
        */

        struct starpu_task *task = starpu_task_create();
        task->cl = &phaseI_codelet;
        task->handles[0] = blk_handles[i];
        task->handles[1] = splitter_handles[i];
        task->cl_arg = &i;
        task->cl_arg_size = sizeof(int);

        ret = starpu_task_submit(task);
        if (ret != -ENODEV)
            STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");

        //starpu_data_unregister(blk_handle);
        //starpu_data_unregister(splitter_handle);
    }

    for (int i = 0; i < NUM_CPU; ++i)
    {
        starpu_data_unregister(blk_handles[i]);
        starpu_data_unregister(splitter_handles[i]);
    }

#ifdef SYNC
    starpu_task_wait_for_all(); //sync
#endif

    gettimeofday(&tv2, NULL);
    printTime(tv1, tv2, "Phase I (local sort)");

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

    gettimeofday(&tv1, NULL);
    printTime(tv2, tv1, "Phase II (gather splitters)");

    // ********************************************/
    // III - Each processor sort data to buckets
    //vector< vector<int> > buckets(NUM_CPU);
    //starpu_data_handle_t buckets_handle[NUM_CPU];

    // for( int i = 0; i < NUM_CPU; ++i){
    //    starpu_vector_data_register(&buckets_handle[i], STARPU_MAIN_RAM, (uintptr_t)buckets[i], 1, sizeof(vector<int>));
    // }

    // starpu_data_handle_t array_handle;
    // starpu_vector_data_register(&array_handle, STARPU_MAIN_RAM, (uintptr_t)arr, len, sizeof(int));

    vector<int> **buckets = new vector<int> *[NUM_CPU];
    starpu_data_handle_t *buckets_handles = new starpu_data_handle_t[NUM_CPU];

    for (int i = 0; i < NUM_CPU; ++i)
    {
        buckets[i] = new vector<int>[ NUM_CPU ];
        int blk_size = (i == NUM_CPU - 1) ? last_blk_size : reg_blk_size;
        starpu_vector_data_register(&blk_handles[i], STARPU_MAIN_RAM, (uintptr_t)(arr + reg_blk_size * i), blk_size, sizeof(int));
        starpu_vector_data_register(&buckets_handles[i], STARPU_MAIN_RAM, (uintptr_t)buckets[i], NUM_CPU, sizeof(vector<int>));
    }

    for (int i = 0; i < NUM_CPU; ++i)
    {
        // each processor has an array of bucket vectors
        starpu_data_handle_t buckets_handle;
        starpu_vector_data_register(&buckets_handle, STARPU_MAIN_RAM, (uintptr_t)buckets[i], NUM_CPU, sizeof(vector<int>));

        struct starpu_task *bucket_task = starpu_task_create();
        bucket_task->cl = &phaseIII_codelet;
        bucket_task->handles[0] = blk_handles[i];
        bucket_task->handles[1] = g_splitters_handle;
        bucket_task->handles[2] = buckets_handles[i];

        bucket_task->cl_arg = &i;
        bucket_task->cl_arg_size = sizeof(int);

        ret = starpu_task_submit(bucket_task);
        if (ret != -ENODEV)
            STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");

        starpu_data_unregister(buckets_handle);
    }
#ifdef SYNC
    starpu_task_wait_for_all(); //sync
#endif

    for (int i = 0; i < NUM_CPU; ++i)
    {
        starpu_data_unregister(blk_handles[i]);
        starpu_data_unregister(buckets_handles[i]);
    }

    // starpu_data_unregister(array_handle);
    starpu_data_unregister(g_splitters_handle);

    // ********************************************/
    // Get the size of the bucket for each processor

    //starpu_data_handle_t* buckets_handles = new starpu_data_handle_t[NUM_CPU];

    int *bucket_sizes = new int[NUM_CPU];
    starpu_data_handle_t *size_handles = new starpu_data_handle_t[NUM_CPU];

    vector<int> ***p_buckets = new vector<int> **[NUM_CPU];

    for (int i = 0; i < NUM_CPU; ++i)
    {
        bucket_sizes[i] = 0;
        p_buckets[i] = new vector<int> *[NUM_CPU];
        for (int j = 0; j < NUM_CPU; ++j)
        {
            p_buckets[i][j] = &buckets[j][i];
            bucket_sizes[i] += buckets[j][i].size();
        }
        //starpu_vector_data_register(&pbuckets_handles[i], STARPU_MAIN_RAM, (uintptr_t)(p_buckets[i]), NUM_CPU, sizeof(vector<int> **));
        //starpu_vector_data_register(&size_handles[i], STARPU_MAIN_RAM, (uintptr_t)(&bucket_sizes[i]), 1, sizeof(int));
    }

    // for (int i = 0; i < NUM_CPU; ++i)
    // {
    //     starpu_data_handle_t size_handle;
    //     starpu_vector_data_register(&size_handle, STARPU_MAIN_RAM, (uintptr_t)(&bucket_sizes[i]), 1, sizeof(int));

    //     struct starpu_task *task = starpu_task_create();
    //     task->cl = &get_size_codelet;
    //     task->handles[0] = buckets_handles[i];
    //     task->handles[1] = size_handles[i];
    //     task->cl_arg = &i;
    //     task->cl_arg_size = sizeof(int);

    //     //ret = starpu_task_submit(task);
    //     if (ret != -ENODEV)
    //         STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");

    //     starpu_data_unregister(size_handle);
    // }
    // starpu_task_wait_for_all();
    // for(int i = 0; i < NUM_CPU; ++i)
    // {
    //     starpu_data_unregister(size_handles[i]);
    //     //starpu_data_unregister(buckets_handles[i]);
    // }

    // TODO
    // Need to paralellize this reduction
    int *sizes_redux = new int[NUM_CPU];
    sizes_redux[0] = 0;
    for (int i = 1; i < NUM_CPU; ++i)
        sizes_redux[i] = sizes_redux[i - 1] + bucket_sizes[i - 1];

    gettimeofday(&tv2, NULL);
    printTime(tv1, tv2, "Phase III (collect buckets)");

    // ********************************************/
    // IV - Local sort each bucket
    int *out = new int[len];
    starpu_data_handle_t *out_handles = new starpu_data_handle_t[NUM_CPU];
    starpu_data_handle_t *pbuckets_handles = new starpu_data_handle_t[NUM_CPU];
    for (int i = 0; i < NUM_CPU; ++i)
    {
        starpu_vector_data_register(&pbuckets_handles[i], STARPU_MAIN_RAM, (uintptr_t)(p_buckets[i]), NUM_CPU, sizeof(vector<int> *));
        starpu_vector_data_register(&out_handles[i], STARPU_MAIN_RAM, (uintptr_t)(out + sizes_redux[i]), bucket_sizes[i], sizeof(int));
    }

    for (int i = 0; i < NUM_CPU; ++i)
    {
        starpu_data_handle_t out_handle;
        starpu_vector_data_register(&out_handle, STARPU_MAIN_RAM, (uintptr_t)(out + sizes_redux[i]), bucket_sizes[i], sizeof(int));

        struct starpu_task *task = starpu_task_create();
        task->cl = &phaseIV_codelet;
        task->handles[0] = pbuckets_handles[i];
        task->handles[1] = out_handles[i];
        task->cl_arg = &i;
        task->cl_arg_size = sizeof(int);

        ret = starpu_task_submit(task);
        if (ret != -ENODEV)
            STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");

        starpu_data_unregister(out_handle);
    }
//#ifdef SYNC
    starpu_task_wait_for_all(); //sync
//#endif

    // End the timer
    gettimeofday(&tv1, NULL);
    printTime(tv2, tv1, "Phase IV (sort buckets)");
    printTime(tv0, tv1, "Total time");

    for (int i = 0; i < NUM_CPU; ++i)
    {
        starpu_data_unregister(out_handles[i]);
        starpu_data_unregister(pbuckets_handles[i]);
    }

    /* Display the occupancy of all workers during the test */
    ofstream perf_file("worker.txt");
    perf_file << std::fixed;
    perf_file << std::setprecision(2);

    unsigned worker;
    for (worker = 0; worker < starpu_worker_get_count(); worker++)
    {
        struct starpu_profiling_worker_info worker_info;
        ret = starpu_profiling_worker_get_info(worker, &worker_info);
        STARPU_ASSERT(!ret);

        double total_time = starpu_timing_timespec_to_us(&worker_info.total_time);
        double executing_time = starpu_timing_timespec_to_us(&worker_info.executing_time);
        double sleeping_time = starpu_timing_timespec_to_us(&worker_info.sleeping_time);
        double overhead_time = total_time - executing_time - sleeping_time;

        float executing_ratio = 100.0 * executing_time / total_time;
        float sleeping_ratio = 100.0 * sleeping_time / total_time;
        float overhead_ratio = 100.0 - executing_ratio - sleeping_ratio;

        char workername[128];
        starpu_worker_get_name(worker, workername, 128);
        perf_file << "Worker " << workername << endl;
        perf_file << "\ttotal time : " << total_time * 1e-3 << " ms" << endl;
        perf_file << "\texec time  : " << executing_time * 1e-3 << " ms (" << executing_ratio << "%%)" << endl;
        perf_file << "\tblocked time  : " << sleeping_time * 1e-3 << " ms (" << sleeping_ratio << "%%)" << endl;
        perf_file << "\toverhead time: " << overhead_time * 1e-3 << " ms (" << overhead_ratio << "%%)" << endl;
    }
    perf_file.close();

    /* terminate StarPU */
    starpu_shutdown();

    // Verification
    for (int i = 1; i < len; i++)
        if (out[i] < out[i-1]) {
            cout << "Verification failed\n";
            break;
        }

    if (argv[3] != NULL)
    {
        ofstream out_file(argv[3]);
        int blk_size;
        int cpu;

        for (int i = 0; i < len; ++i)
        {
            out_file << out[i] << " ";
        }
        out_file << endl;

        out_file.close();
    }

    // clean up data handles
    delete[] blk_handles;
    delete[] splitter_handles;
    delete[] buckets_handles;
    delete[] size_handles;
    delete[] out_handles;
    delete[] pbuckets_handles;

    // clean up data
    delete[] arr;
    delete[] out;
    delete[] splitters;
    delete[] g_splitters;
    delete[] bucket_sizes;
    delete[] sizes_redux;
    delete[] p_buckets;

    for (int i = 0; i < NUM_CPU; ++i)
        delete[] buckets[i];
    delete[] buckets;

    return 0;
}
