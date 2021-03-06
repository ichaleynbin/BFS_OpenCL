#define __CL_ENABLE_EXCEPTIONS
#include <cstdlib>
#include <iostream>
#include <string>
#include <cstring>
#include <unordered_set>
#include <sys/time.h>

#include "CLHelper.h"
#include "util.h"
#include "matrixmarket/mmio.h"

#define MAX_THREADS_PER_BLOCK 256

int iterations = 1;
int source = 0;
bool undirected = false;

typedef unsigned long long timestamp_t;

static timestamp_t get_timestamp ()
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return  now.tv_usec + (timestamp_t)now.tv_sec * 1000000;
}

struct Node
{
    int starting;
    int no_of_edges;
};

//----------------------------------------------------------
//--Reference bfs on cpu
//--programmer:	jianbin
//--date:	26/01/2011
//--note: width is changed to the new_width
//----------------------------------------------------------
void run_bfs_cpu(int no_of_nodes, Node *h_nodes, int no_of_edges, int *h_edges, char* h_mask, char* h_new_mask, char* h_visited, int *h_cost_ref)
{
#ifdef PROFILING
    timestamp_t t0 = get_timestamp();
#endif

    int amtloops = 0;
    char shouldContinue;
    do
    {
        amtloops++;
        shouldContinue = false;
        for (int tid = 0; tid < no_of_nodes; tid++)
        {
            if (h_mask[tid])
            {
                h_mask[tid] = false;
                for (int i = h_nodes[tid].starting; i < h_nodes[tid].starting + h_nodes[tid].no_of_edges; i++)
                {
                    int id = h_edges[i]; //--cambine: node id is connected with node tid
                    if (!h_visited[id])
                    {
                        h_cost_ref[id] = h_cost_ref[tid] + 1;
                        h_new_mask[id] = true;
                    }
                }
            }
        }

        for (int tid = 0; tid < no_of_nodes; tid++)
        {
            if (h_new_mask[tid] == true)
            {
                h_mask[tid] = true;
                h_visited[tid] = true;
                shouldContinue = true;
                h_new_mask[tid] = false;
            }
        }
    } while (shouldContinue);

#ifdef VERBOSE
    printf("Took %d loops\n", amtloops);
#endif
#ifdef PROFILING
    timestamp_t t1 = get_timestamp();
    double secs = (t1 - t0) / 1000.0L;
    std::cout << "\treference time (sequential)(ms):" << secs << std::endl;
#endif
}

void waitAndTime(int count, cl_event* events, string* strings, cl_ulong* timer)
{
    _clWait(count, events);
    _clFinish();
       
    for(int i = 0; i < count; i++) {
        cl_ulong time_start;
        cl_ulong time_end;

        clGetEventProfilingInfo(events[i], CL_PROFILING_COMMAND_START, sizeof(time_start), &time_start, NULL);
        clGetEventProfilingInfo(events[i], CL_PROFILING_COMMAND_END, sizeof(time_end), &time_end, NULL);
        *timer += time_end - time_start;
            
#ifdef VERBOSE
        printf("%s took %0.8f\n", strings[i].c_str(), (time_end - time_start) / 1000000.0);
#endif
    }
}

void waitAndTime(int count, cl_event* events, cl_ulong* timer)
{
    _clWait(count, events);
    _clFinish();
       
    for(int i = 0; i < count; i++) {
        cl_ulong time_start;
        cl_ulong time_end;

        clGetEventProfilingInfo(events[i], CL_PROFILING_COMMAND_START, sizeof(time_start), &time_start, NULL);
        clGetEventProfilingInfo(events[i], CL_PROFILING_COMMAND_END, sizeof(time_end), &time_end, NULL);
        *timer += time_end - time_start;
    }
}

//----------------------------------------------------------
//--breadth first search on the OpenCL device
//----------------------------------------------------------
void run_bfs_opencl(int no_of_nodes, Node *h_nodes, int no_of_edges, int *h_edges, char *h_mask, char *h_new_mask, char *h_visited, int *h_cost)
{
    char h_done = true;
    cl_mem d_nodes, d_edges, d_mask, d_new_mask, d_visited, d_cost, d_done;

#ifdef PROFILING
    cl_ulong kernel_timer = 0;
    cl_ulong h2d_timer = 0;
    cl_ulong d2h_timer = 0;
#endif

    try
    {
        //--1 transfer data from host to device
        d_nodes = _clMallocRW(no_of_nodes * sizeof(Node));
        d_edges = _clMallocRW(no_of_edges * sizeof(int));
        d_mask = _clMallocRW(no_of_nodes * sizeof(char));
        d_new_mask = _clMallocRW(no_of_nodes * sizeof(char));
        d_visited = _clMallocRW(no_of_nodes * sizeof(char));

        d_cost = _clMallocRW(no_of_nodes * sizeof(int));
        d_done = _clMallocRW(sizeof(char));

        cl_event h2dpreevents[6];
        h2dpreevents[0] = _clMemcpyH2D(d_nodes, no_of_nodes * sizeof(Node), h_nodes);
        h2dpreevents[1] = _clMemcpyH2D(d_edges, no_of_edges * sizeof(int), h_edges);
        h2dpreevents[2] = _clMemcpyH2D(d_mask, no_of_nodes * sizeof(char), h_mask);
        h2dpreevents[3] = _clMemcpyH2D(d_new_mask, no_of_nodes * sizeof(char), h_new_mask);
        h2dpreevents[4] = _clMemcpyH2D(d_visited, no_of_nodes * sizeof(char), h_visited);
        h2dpreevents[5] = _clMemcpyH2D(d_cost, no_of_nodes * sizeof(int), h_cost);

#ifdef PROFILING
        waitAndTime(6, h2dpreevents, &h2d_timer);
#endif
        //--2 invoke kernel
        int amtloops = 0;

        cl_event h2devents[1];
        cl_event kernelevents[1];
        string kernelstrings[1];
        cl_event d2hevents[1];
        do
        {
            amtloops++;

            h_done = true; 
            h2devents[0] = _clMemcpyH2D(d_done, sizeof(char), &h_done);

#ifdef PROFILING
            waitAndTime(1, h2devents, &h2d_timer);
#endif
            clReleaseEvent(h2devents[0]);

            //--kernel 0
            int kernel_id = 0;
            int kernel_idx = 0;
            _clSetArgs(kernel_id, kernel_idx++, d_nodes);
            _clSetArgs(kernel_id, kernel_idx++, d_edges);
            _clSetArgs(kernel_id, kernel_idx++, d_mask);
            _clSetArgs(kernel_id, kernel_idx++, d_new_mask);
            _clSetArgs(kernel_id, kernel_idx++, d_visited);
            _clSetArgs(kernel_id, kernel_idx++, d_cost);
            _clSetArgs(kernel_id, kernel_idx++, d_done);
            _clSetArgs(kernel_id, kernel_idx++, &no_of_nodes, sizeof(int));

            //int work_items = no_of_nodes;
            kernelstrings[0] = "Top_Down cycle w/ size: unknown"; 
            kernelevents[0] = _clInvokeKernel(kernel_id, no_of_nodes, work_group_size);
#ifdef PROFILING
            waitAndTime(1, kernelevents, kernelstrings, &kernel_timer);
#endif
            clReleaseEvent(kernelevents[0]);

            d2hevents[0] = _clMemcpyD2H(d_done, sizeof(char), &h_done);
#ifdef PROFILING
            waitAndTime(1, d2hevents, &d2h_timer);
#endif
            clReleaseEvent(d2hevents[0]);
            
            cl_mem tmp = d_mask;
            d_mask = d_new_mask;
            d_new_mask = tmp;
        } while (!h_done);

#ifdef VERBOSE
        printf("Took %d loops\n", amtloops);
#endif
        _clFinish();

        //--3 transfer data from device to host
        cl_event d2hevent[1];
        d2hevent[0] = _clMemcpyD2H(d_cost, no_of_nodes * sizeof(int), h_cost);

#ifdef PROFILING
        waitAndTime(1, d2hevent, &d2h_timer);
#endif
        clReleaseEvent(d2hevent[0]);
    }
    catch (std::string msg)
    {
        throw("in run_bfs_opencl -> " + msg);
    }

    //--4 release cl resources.
    _clFree(d_nodes);
    _clFree(d_edges);
    _clFree(d_mask);
    _clFree(d_new_mask);
    _clFree(d_visited);
    _clFree(d_cost);
    _clFree(d_done);

#ifdef PROFILING
    
    #ifdef VERBOSE
    printf("\tTotal h2d time is: %0.3f milliseconds \n", (h2d_timer) / 1000000.0);
    printf("\tTotal kernel time is: %0.3f milliseconds \n", (kernel_timer) / 1000000.0);
    printf("\tTotal d2h time is: %0.3f milliseconds \n", (d2h_timer) / 1000000.0);
    printf("\tTotal time: %0.3f milliseconds \n", (h2d_timer + kernel_timer + d2h_timer) / 1000000.0);
    #else
    printf("%0.3f %0.3f %0.3f %0.3f\n", (h2d_timer) / 1000000.0, (kernel_timer) / 1000000.0, (d2h_timer) / 1000000.0, (h2d_timer + kernel_timer + d2h_timer) / 1000000.0);
    #endif
#endif
}

int main(int argc, char *argv[])
{
    MM_typecode matcode;

    int no_of_nodes;

    Node *h_nodes;
    char *h_mask;
    char *h_new_mask;
    char *h_visited;
    int *h_edges;
    int *I;
    int *J;
    double *val;

    try
    {
        if (argc < 2)
        {
            fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
            fprintf(stderr, "Flags:\n");
            fprintf(stderr, "\t-g <int>: work group size.\n");
            fprintf(stderr, "\t-d <int>: device id to use.\n");
            fprintf(stderr, "\t-c: use cpu instead of gpu.\n");
            fprintf(stderr, "\t-s <int>: use value as source node (def 0).\n");
            fprintf(stderr, "\t-i <int>: use amount of iterations (def 1).\n");
            exit(0);
        }

        _clCmdParams(argc, argv, &source, &iterations, &undirected);

        //Read in Graph from a file
        char *input_f = argv[1];
        printf("%s\n", input_f);

        FILE *fp = fopen(input_f, "r");
        if (!fp)
        {
            printf("Error Reading graph file\n");
            return 1;
        }

        if (mm_read_banner(fp, &matcode) != 0)
        {
            printf("Could not process Matrix Market banner.\n");
            exit(1);
        }

        // Only supports a subset of the Matrix Market data types.
        if (mm_is_complex(matcode) && mm_is_matrix(matcode) && 
                mm_is_sparse(matcode) )
        {
            printf("Sorry, this application does not support ");
            printf("Market Market type: [%s]\n", mm_typecode_to_str(matcode));
            exit(1);
        }

        if(mm_is_symmetric(matcode)) {
            undirected = true;
        }

        // find out size of sparse matrix ....
        int N, nz;   
        if (mm_read_mtx_crd_size(fp, &no_of_nodes, &N, &nz) != 0)
            exit(1);

        if(no_of_nodes != N) {
            printf("[WARNING] Not sure if non-square matrices work properly...");
        }

        /* reserve memory for matrices */
        val = (double *) malloc(nz * sizeof(double));

        /* NOTE: when reading in doubles, ANSI C requires the use of the "l"  */
        /*   specifier as in "%lg", "%lf", "%le", otherwise errors will occur */
        /*  (ANSI C X3.159-1989, Sec. 4.9.6.2, p. 136 lines 13-15)            */

#ifdef VERBOSE
        printf("Amt nodes: %d\n", no_of_nodes);
        printf("Undirected: %s\n", undirected ? "True" : "False");
#endif
        std::unordered_set<int>* construction_set = new std::unordered_set<int>[no_of_nodes];

        if(mm_is_pattern(matcode))
        {
           for (int i = 0; i < nz; i++)
            {
                int x, y;
                if(fscanf(fp, "%d %d\n", &x, &y) != 2) {
                    printf("Failed to read line %d\n", i);
                }
                x--;  /* adjust from 1-based to 0-based */
                y--;

                construction_set[x].insert(y);
                if(undirected) {
                    construction_set[y].insert(x);
                }
            }
        }
        else
        {
            for (int i = 0; i < nz; i++)
            {
                int x, y;
                if(fscanf(fp, "%d %d %lg\n", &x, &y, &val[i]) != 3) {
                    printf("Failed to read line %d\n", i);
                }
                x--;  /* adjust from 1-based to 0-based */
                y--;

                construction_set[x].insert(y);
                if(undirected) {
                    construction_set[y].insert(x);
                }
            }
        }
        
        if (fp !=stdin) fclose(fp);

        int no_of_edges = nz * 2;
        h_edges = (int*) malloc(sizeof(int) * no_of_edges);

        // Distribute threads across multiple Blocks if necessary
        work_group_size = no_of_nodes > MAX_THREADS_PER_BLOCK ? MAX_THREADS_PER_BLOCK : no_of_nodes;

        // Allocate host memory
        h_nodes = (Node *)malloc(sizeof(Node) * no_of_nodes);
        h_mask = (char *)malloc(sizeof(char) * no_of_nodes);
        h_new_mask = (char *)malloc(sizeof(char) * no_of_nodes);
        h_visited = (char *)malloc(sizeof(char) * no_of_nodes);

        int index = 0;
        for (int i = 0; i < no_of_nodes; i++)
        {
            h_nodes[i].starting = index;
            h_nodes[i].no_of_edges = construction_set[i].size();
            std::copy(construction_set[i].begin(), construction_set[i].end(), &h_edges[index]);
            index += construction_set[i].size();
            construction_set[i].clear();

            h_mask[i] = false;
            h_new_mask[i] = false;
            h_visited[i] = false;   
        }

        _clInit();

        // Allocate mem for the result on host side and run bfs
        int **h_cost;
        h_cost = (int**) malloc(iterations * sizeof(int*));    
        for(int i = 0; i < iterations; i++)
        {    
            h_cost[i] = (int*) malloc(no_of_nodes * sizeof(int));
            for (int j = 0; j < no_of_nodes; j++)
            {
                h_cost[i][j] = -1;
            }

    #ifdef VERBOSE
            printf("Running opencl...\n");
    #endif

            //---------------------------------------------------------
            //--opencl entry
            h_cost[i][source] = 0;
            h_mask[source] = true;
            h_visited[source] = true;
            run_bfs_opencl(no_of_nodes, h_nodes, no_of_edges, h_edges, h_mask, h_new_mask, h_visited, h_cost[i]);
        }

        _clRelease();

#ifndef NO_CHECK
        //---------------------------------------------------------
        //--cpu entry
        // Initialize the memory again

        int *h_cost_ref = (int *)malloc(sizeof(int) * no_of_nodes);

        for (int i = 0; i < no_of_nodes; i++)
        {
            h_cost_ref[i] = -1;
            h_mask[i] = false;
            h_new_mask[i] = false;
            h_visited[i] = false;
        }

#ifdef VERBOSE
        printf("Running cpu...\n");
#endif
        // Set the source node as true in the mask4
        h_cost_ref[source] = 0;
        h_mask[source] = true;
        h_visited[source] = true;
        run_bfs_cpu(no_of_nodes, h_nodes, no_of_edges, h_edges, h_mask, h_new_mask, h_visited, h_cost_ref);
        //---------------------------------------------------------
        //--result verification
        for(int i = 0; i < iterations; i++) {
            compare_results<int>(h_cost_ref, h_cost[i], no_of_nodes);
            free(h_cost[i]);
        }
        free(h_cost);
#endif
    }
    catch (std::string msg)
    {
        std::cout << "--cambine: exception in main ->" << msg << std::endl;
    }

    // Release host memory
    free(I);
    free(J);
    free(val);
    free(h_nodes);
    
    free(h_mask);
    free(h_new_mask);
    free(h_visited);
    free(h_edges);

    return 0;
}
