//------------------------------------------
//--cambine:helper function for OpenCL
//--programmer:	Jianbin Fang
//--date:	27/12/2010
//------------------------------------------
#ifndef _CL_HELPER_
#define _CL_HELPER_

// Determine if necessary for the target platform. NVIDIA GPUs still seem to be running on 1.2.
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#include <CL/cl.h>
#include <vector>
#include <iostream>
#include <fstream>
#include <string>

using std::cerr;
using std::cout;
using std::endl;
using std::ifstream;
using std::string;
//#pragma OPENCL EXTENSION cl_nv_compiler_options:enable
#define WORK_DIM 2 //work-items dimensions

struct oclHandleStruct
{
    cl_context context;
    cl_device_id *devices;
    cl_command_queue queue;
    cl_program program;
    cl_int cl_status;
    std::string error_str;
    std::vector<cl_kernel> kernel;
};

struct oclHandleStruct oclHandles;

char kernel_file[100] = "Kernels.cl";
int total_kernels = 1;
string kernel_names[2] = {"BFS_1"};
size_t work_group_size = 512;
int device_id_inuse = 0;
bool cpu = false;

/*
 * Converts the contents of a file into a string
 */
string FileToString(const string fileName)
{
    ifstream f(fileName.c_str(), ifstream::in | ifstream::binary);

    try
    {
        size_t size;
        char *str;
        string s;

        if (f.is_open())
        {
            size_t fileSize;
            f.seekg(0, ifstream::end);
            size = fileSize = f.tellg();
            f.seekg(0, ifstream::beg);

            str = new char[size + 1];
            if (!str)
            {
                throw(string("Could not allocate memory"));
            }

            f.read(str, fileSize);
            f.close();
            str[size] = '\0';

            s = str;
            delete[] str;
            return s;
        }
    }
    catch (std::string msg)
    {
        cerr << "Exception caught in FileToString(): " << msg << endl;
        if (f.is_open())
        {
            f.close();
        }
    }
    catch (...)
    {
        cerr << "Exception caught in FileToString()" << endl;
        if (f.is_open())
        {
            f.close();
        }
    }
    throw("FileToString()::Error: Unable to open file " + fileName);
}

//---------------------------------------
//Read command line parameters
//
void _clCmdParams(int argc, char *argv[], int* source, int* iterations, bool* undirected)
{
    for (int i = 2; i < argc; i++)
    {
        if(argv[i][0] != '-') {
            continue;
        }

        switch (argv[i][1])
        {
        case 'g': //--g stands for size of work group
            if (++i < argc)
            {
                sscanf(argv[i], "%lu", &work_group_size);
#ifdef VERBOSE
                printf("Setting work group size to %lu\n", work_group_size);
#endif
            }
            else
            {
                std::cerr << "Could not read argument after option " << argv[i - 1] << std::endl;
                throw;
            }
            break;
        case 'd': //--d stands for device id used in computaion
            if (++i < argc)
            {
                sscanf(argv[i], "%u", &device_id_inuse);
#ifdef VERBOSE
                printf("Setting device id to %u\n", device_id_inuse);
#endif
            }
            else
            {
                std::cerr << "Could not read argument after option " << argv[i - 1] << std::endl;
                throw;
            }
            break;
        case 'c':
            cpu = true;
#ifdef VERBOSE
            printf("Attempting to use CPU instead of GPU.\n");
#endif
            break;
        case 'u':
            *undirected = true;
#ifdef VERBOSE
            printf("Forcing undirected graph type.\n");
#endif
            break;
        case 's':
             if (++i < argc)
            {
                sscanf(argv[i], "%d", source);
#ifdef VERBOSE
                printf("Setting source to %d\n", *source);
#endif
            }
            else
            {
                std::cerr << "Could not read argument after option " << argv[i - 1] << std::endl;
                throw;
            }
            break;
        case 'i':
             if (++i < argc)
            {
                sscanf(argv[i], "%d", iterations);
#ifdef VERBOSE
                printf("Setting iterations to %d\n", *iterations);
#endif
            }
            else
            {
                std::cerr << "Could not read argument after option " << argv[i - 1] << std::endl;
                throw;
            }
            break;
        default:;
        }
    }
}

//---------------------------------------
//Initlize CL objects
//--description: there are 5 steps to initialize all the OpenCL objects needed
//--revised on 04/01/2011: get the number of devices  and
//  devices have no relationship with context
void _clInit()
{
    int DEVICE_ID_inuse = device_id_inuse;
    cl_int resultCL;

    oclHandles.context = NULL;
    oclHandles.devices = NULL;
    oclHandles.queue = NULL;
    oclHandles.program = NULL;

    cl_uint deviceListSize;

    //-----------------------------------------------
    //--cambine-1: find the available platforms and select one

    cl_uint numPlatforms;
    cl_platform_id targetPlatform = NULL;

    resultCL = clGetPlatformIDs(0, NULL, &numPlatforms);
    if (resultCL != CL_SUCCESS)
    {
        throw(string("InitCL()::Error: Getting number of platforms (clGetPlatformIDs)"));
    }

#ifdef VERBOSE
    printf("Number of platforms: %d\n", numPlatforms);
#endif

    if (!(numPlatforms > 0))
    {
        throw(string("InitCL()::Error: No platforms found (clGetPlatformIDs)"));
    }

    cl_platform_id *allPlatforms = (cl_platform_id *)malloc(numPlatforms * sizeof(cl_platform_id));

    resultCL = clGetPlatformIDs(numPlatforms, allPlatforms, NULL);
    if (resultCL != CL_SUCCESS)
        throw(string("InitCL()::Error: Getting platform ids (clGetPlatformIDs)"));

    /* Select the target platform. Default: first platform */
    targetPlatform = allPlatforms[cpu ? 1 : 0];
    for (int i = 0; i < numPlatforms; i++)
    {
        char pbuff[128];
        resultCL = clGetPlatformInfo(allPlatforms[i],
                                     CL_PLATFORM_VENDOR,
                                     sizeof(pbuff),
                                     pbuff,
                                     NULL);
        if (resultCL != CL_SUCCESS)
            throw(string("InitCL()::Error: Getting platform info (clGetPlatformInfo)"));

#ifdef VERBOSE
        printf("Vendor of platform %d is %s\n", i, pbuff);
#endif
    }

#ifdef VERBOSE
    printf("Using platform %d.\n", cpu ? 1 : 0);
#endif

    free(allPlatforms);

    //-----------------------------------------------
    //--cambine-2: create an OpenCL context
    cl_context_properties cprops[3] = {CL_CONTEXT_PLATFORM, (cl_context_properties)targetPlatform, 0};
    oclHandles.context = clCreateContextFromType(cprops,
                                                 cpu ? CL_DEVICE_TYPE_CPU : CL_DEVICE_TYPE_GPU,
                                                 NULL,
                                                 NULL,
                                                 &resultCL);

    if ((resultCL != CL_SUCCESS) || (oclHandles.context == NULL))
        throw(string("InitCL()::Error: Creating Context (clCreateContextFromType)"));
    //-----------------------------------------------
    //--cambine-3: detect OpenCL devices
    /* First, get the size of device list */
    oclHandles.cl_status = clGetDeviceIDs(targetPlatform, cpu ? CL_DEVICE_TYPE_CPU : CL_DEVICE_TYPE_GPU, 0, NULL, &deviceListSize);
    if (oclHandles.cl_status != CL_SUCCESS)
    {
        throw(string("exception in _clInit -> clGetDeviceIDs"));
    }
    if (deviceListSize == 0)
        throw(string("InitCL()::Error: No devices found."));

    //std::cout<<"device number:"<<deviceListSize<<std::endl;

    /* Now, allocate the device list */
    oclHandles.devices = (cl_device_id *)malloc(deviceListSize * sizeof(cl_device_id));

    if (oclHandles.devices == 0)
        throw(string("InitCL()::Error: Could not allocate memory."));

    /* Next, get the device list data */
    oclHandles.cl_status = clGetDeviceIDs(targetPlatform, cpu ? CL_DEVICE_TYPE_CPU : CL_DEVICE_TYPE_GPU, deviceListSize,
                                          oclHandles.devices, NULL);
    if (oclHandles.cl_status != CL_SUCCESS)
    {
        throw(string("exception in _clInit -> clGetDeviceIDs-2"));
    }

    cl_bool result;
    resultCL = clGetDeviceInfo(oclHandles.devices[DEVICE_ID_inuse], CL_DEVICE_AVAILABLE, sizeof(result), &result, NULL);

    if (resultCL != CL_SUCCESS)
        throw(string("InitCL()::Error: Getting device info (clGetDeviceInfo)"));

    if (!result) {
        throw(string("Device %d is not available.\n", DEVICE_ID_inuse));
    }

    char vendor[128];
    resultCL = clGetDeviceInfo(oclHandles.devices[DEVICE_ID_inuse], CL_DEVICE_VENDOR, sizeof(vendor), vendor, NULL);

    if (resultCL != CL_SUCCESS)
        throw(string("InitCL()::Error: Getting device info (clGetDeviceInfo-2)"));

#ifdef VERBOSE
    printf("Vendor of selected device %d is %s\n", DEVICE_ID_inuse, vendor);
#endif

    //-----------------------------------------------
    //--cambine-4: Create an OpenCL command queue
    #ifdef PROFILING
    oclHandles.queue = clCreateCommandQueue(oclHandles.context,
                                            oclHandles.devices[DEVICE_ID_inuse],
                                            CL_QUEUE_PROFILING_ENABLE,
                                            &resultCL);
    
    #else
    oclHandles.queue = clCreateCommandQueue(oclHandles.context,
                                            oclHandles.devices[DEVICE_ID_inuse],
                                            0,
                                            &resultCL);
    #endif

    if ((resultCL != CL_SUCCESS) || (oclHandles.queue == NULL))
        throw(string("InitCL()::Creating Command Queue. (clCreateCommandQueue)"));
    //-----------------------------------------------
    //--cambine-5: Load CL file, build CL program object, create CL kernel object
    std::string source_str = FileToString(kernel_file);
    const char *source = source_str.c_str();
    size_t sourceSize[] = {source_str.length()};

    oclHandles.program = clCreateProgramWithSource(oclHandles.context,
                                                   1,
                                                   &source,
                                                   sourceSize,
                                                   &resultCL);

    if ((resultCL != CL_SUCCESS) || (oclHandles.program == NULL))
        throw(string("InitCL()::Error: Loading Binary into cl_program. (clCreateProgramWithBinary)"));
    //insert debug information
    //std::string options= "-cl-nv-verbose"; //Doesn't work on AMD machines
    //options += " -cl-nv-opt-level=3";
    resultCL = clBuildProgram(oclHandles.program, deviceListSize, oclHandles.devices, NULL, NULL, NULL);

    if ((resultCL != CL_SUCCESS) || (oclHandles.program == NULL))
    {
        cerr << "InitCL()::Error: In clBuildProgram" << endl;

        size_t length;
        resultCL = clGetProgramBuildInfo(oclHandles.program,
                                         oclHandles.devices[DEVICE_ID_inuse],
                                         CL_PROGRAM_BUILD_LOG,
                                         0,
                                         NULL,
                                         &length);
        if (resultCL != CL_SUCCESS)
            throw(string("InitCL()::Error: Getting Program build info(clGetProgramBuildInfo)"));

        char *buffer = (char *)malloc(length);
        resultCL = clGetProgramBuildInfo(oclHandles.program,
                                         oclHandles.devices[DEVICE_ID_inuse],
                                         CL_PROGRAM_BUILD_LOG,
                                         length,
                                         buffer,
                                         NULL);
        if (resultCL != CL_SUCCESS)
            throw(string("InitCL()::Error: Getting Program build info(clGetProgramBuildInfo)"));

        cerr << buffer << endl;
        free(buffer);

        throw(string("InitCL()::Error: Building Program (clBuildProgram)"));
    }

//get program information in intermediate representation
#ifdef PTX_MSG
    size_t binary_sizes[deviceListSize];
    char *binaries[deviceListSize];
    //figure out number of devices and the sizes of the binary for each device.
    oclHandles.cl_status = clGetProgramInfo(oclHandles.program, CL_PROGRAM_BINARY_SIZES, sizeof(size_t) * deviceListSize, &binary_sizes, NULL);
    if (oclHandles.cl_status != CL_SUCCESS)
    {
        throw(string("--cambine:exception in _InitCL -> clGetProgramInfo-2"));
    }

    std::cout << "--cambine:" << binary_sizes << std::endl;
    //copy over all of the generated binaries.
    for (int i = 0; i < deviceListSize; i++)
        binaries[i] = (char *)malloc(sizeof(char) * (binary_sizes[i] + 1));
    oclHandles.cl_status = clGetProgramInfo(oclHandles.program, CL_PROGRAM_BINARIES, sizeof(char *) * deviceListSize, binaries, NULL);
    if (oclHandles.cl_status != CL_SUCCESS)
    {
        throw(string("--cambine:exception in _InitCL -> clGetProgramInfo-3"));
    }
    for (int i = 0; i < deviceListSize; i++)
        binaries[i][binary_sizes[i]] = '\0';
    std::cout << "--cambine:writing ptd information..." << std::endl;
    FILE *ptx_file = fopen("cl.ptx", "w");
    if (ptx_file == NULL)
    {
        throw(string("exceptions in allocate ptx file."));
    }
    fprintf(ptx_file, "%s", binaries[DEVICE_ID_inuse]);
    fclose(ptx_file);
    std::cout << "--cambine:writing ptd information done." << std::endl;
    for (int i = 0; i < deviceListSize; i++)
        free(binaries[i]);
#endif

    for (int nKernel = 0; nKernel < total_kernels; nKernel++)
    {
        /* get a kernel object handle for a kernel with the given name */
        cl_kernel kernel = clCreateKernel(oclHandles.program,
                                          (kernel_names[nKernel]).c_str(),
                                          &resultCL);

        if ((resultCL != CL_SUCCESS) || (kernel == NULL))
        {
            string errorMsg = "InitCL()::Error: Creating Kernel (clCreateKernel) \"" + kernel_names[nKernel] + "\"";
            throw(errorMsg);
        }

        oclHandles.kernel.push_back(kernel);
    }
    //get resource alocation information
#ifdef RES_MSG
    char *build_log;
    size_t ret_val_size;
    oclHandles.cl_status = clGetProgramBuildInfo(oclHandles.program, oclHandles.devices[DEVICE_ID_inuse], CL_PROGRAM_BUILD_LOG, 0, NULL, &ret_val_size);
    if (oclHandles.cl_status != CL_SUCCESS)
    {
        throw(string("exceptions in _InitCL -> getting resource information"));
    }

    build_log = (char *)malloc(ret_val_size + 1);
    oclHandles.cl_status = clGetProgramBuildInfo(oclHandles.program, oclHandles.devices[DEVICE_ID_inuse], CL_PROGRAM_BUILD_LOG, ret_val_size, build_log, NULL);
    if (oclHandles.cl_status != CL_SUCCESS)
    {
        throw(string("exceptions in _InitCL -> getting resources allocation information-2"));
    }
    build_log[ret_val_size] = '\0';
    std::cout << "--cambine:" << build_log << std::endl;
    free(build_log);
#endif

    char name[128];
    char driver_version[128];
    char opencl_version[128];
    cl_device_type device_type;
    cl_uint clockfreq;
    cl_uint computeunits;
    size_t groupsize;
    
    cl_int result1 = clGetDeviceInfo(oclHandles.devices[DEVICE_ID_inuse], CL_DEVICE_NAME, sizeof(name), name, NULL);
    cl_int result2 = clGetDeviceInfo(oclHandles.devices[DEVICE_ID_inuse], CL_DEVICE_TYPE, sizeof(device_type), &device_type, NULL);
    cl_int result3 = clGetDeviceInfo(oclHandles.devices[DEVICE_ID_inuse], CL_DEVICE_MAX_CLOCK_FREQUENCY, sizeof(clockfreq), &clockfreq, NULL);
    cl_int result4 = clGetDeviceInfo(oclHandles.devices[DEVICE_ID_inuse], CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(computeunits), &computeunits, NULL);
    cl_int result5 = clGetDeviceInfo(oclHandles.devices[DEVICE_ID_inuse], CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(groupsize), &groupsize, NULL);
    cl_int result6 = clGetDeviceInfo(oclHandles.devices[DEVICE_ID_inuse], CL_DRIVER_VERSION, sizeof(driver_version), &driver_version, NULL);
    cl_int result7 = clGetDeviceInfo(oclHandles.devices[DEVICE_ID_inuse], CL_DEVICE_VERSION, sizeof(opencl_version), &opencl_version, NULL);

    if(result1 != CL_SUCCESS || result2 != CL_SUCCESS || result3 != CL_SUCCESS || result4 != CL_SUCCESS || result5 != CL_SUCCESS || result6 != CL_SUCCESS || result7 != CL_SUCCESS)
    {
        printf("Succ: %d, invalidval: %d, 1: %d, 2: %d, 3: %d, 4: %d, 5: %d, 6: %d, 7: %d\n", CL_SUCCESS, CL_INVALID_VALUE, result1, result2, result3, result4, result5, result6, result7);
        string errorMsg = "InitCL()::Error: Could not retrieve full device information";
        throw(errorMsg);
    }

    const char* type = device_type == CL_DEVICE_TYPE_CPU ? "CPU" : device_type == CL_DEVICE_TYPE_GPU ? "GPU" : "Other";

    printf("Name: %s, vendor: %s, type: %s, version: %s, opencl: %s. Max clock frequency: %u MHz. Parallel cores: %u. Max work group size: %lu\n", name, vendor, type, driver_version, opencl_version, clockfreq, computeunits, groupsize);
}

//---------------------------------------
//release CL objects
void _clRelease()
{
    char errorFlag = false;

    for (int nKernel = 0; nKernel < oclHandles.kernel.size(); nKernel++)
    {
        if (oclHandles.kernel[nKernel] != NULL)
        {
            cl_int resultCL = clReleaseKernel(oclHandles.kernel[nKernel]);
            if (resultCL != CL_SUCCESS)
            {
                cerr << "ReleaseCL()::Error: In clReleaseKernel" << endl;
                errorFlag = true;
            }
            oclHandles.kernel[nKernel] = NULL;
        }
        oclHandles.kernel.clear();
    }

    if (oclHandles.program != NULL)
    {
        cl_int resultCL = clReleaseProgram(oclHandles.program);
        if (resultCL != CL_SUCCESS)
        {
            cerr << "ReleaseCL()::Error: In clReleaseProgram" << endl;
            errorFlag = true;
        }
        oclHandles.program = NULL;
    }

    if (oclHandles.queue != NULL)
    {
        cl_int resultCL = clReleaseCommandQueue(oclHandles.queue);
        if (resultCL != CL_SUCCESS)
        {
            cerr << "ReleaseCL()::Error: In clReleaseCommandQueue" << endl;
            errorFlag = true;
        }
        oclHandles.queue = NULL;
    }

    free(oclHandles.devices);

    if (oclHandles.context != NULL)
    {
        cl_int resultCL = clReleaseContext(oclHandles.context);
        if (resultCL != CL_SUCCESS)
        {
            cerr << "ReleaseCL()::Error: In clReleaseContext" << endl;
            errorFlag = true;
        }
        oclHandles.context = NULL;
    }

    if (errorFlag) {
        throw(string("ReleaseCL()::Error encountered."));
    }
}

cl_mem _clCreateBuffer(cl_mem_flags flags, size_t size, void *host_ptr)
{
    cl_mem d_mem;
    d_mem = clCreateBuffer(oclHandles.context, flags, size, host_ptr, &oclHandles.cl_status);

#ifdef ERRMSG
    oclHandles.error_str = "exception in _clCreateBuffer -> ";
    switch(oclHandles.cl_status) {
        case CL_INVALID_CONTEXT:
            oclHandles.error_str += "CL_INVALID_CONTEXT";
            break;
        case CL_INVALID_VALUE:
            oclHandles.error_str += "CL_INVALID_VALUE";
            break;
        case CL_INVALID_HOST_PTR:
            oclHandles.error_str += "CL_INVALID_HOST_PTR";
            break;
        case CL_MEM_OBJECT_ALLOCATION_FAILURE:
            oclHandles.error_str += "CL_MEM_OBJECT_ALLOCATION_FAILURE";
            break;
        case CL_OUT_OF_HOST_MEMORY:
            oclHandles.error_str += "CL_OUT_OF_HOST_MEMORY";
            break;
    }

    if (oclHandles.cl_status != CL_SUCCESS)
        throw(oclHandles.error_str);
#endif
    return d_mem;
}

//--------------------------------------------------------
//--cambine:create buffer and then copy data from host to device
cl_mem _clCreateAndCpyMem(int size, void *h_mem_source)
{
    return _clCreateBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, size, h_mem_source);
}
//-------------------------------------------------------
//--cambine:	create read only  buffer for devices
//--date:	17/01/2011
cl_mem _clMallocRW(int size)
{
    return _clCreateBuffer(CL_MEM_READ_WRITE, size, NULL);
}
//-------------------------------------------------------
//--cambine:	create read and write buffer for devices
//--date:	17/01/2011
cl_mem _clMalloc(int size, void *h_mem_ptr)
{
    return _clCreateBuffer(CL_MEM_WRITE_ONLY | CL_MEM_COPY_HOST_PTR, size, h_mem_ptr);
}

//-------------------------------------------------------
//--cambine:	transfer data from host to device
//--date:	17/01/2011
cl_event _clMemcpyH2D(cl_mem d_mem, int size, const void *h_mem_ptr)
{
    cl_event event;
    oclHandles.cl_status = clEnqueueWriteBuffer(oclHandles.queue, d_mem, CL_TRUE, 0, size, h_mem_ptr, 0, NULL, &event);
#ifdef ERRMSG
    if (oclHandles.cl_status != CL_SUCCESS)
        throw(string("exception in _clMemcpyH2D"));
#endif
    return event;
}
//--------------------------------------------------------
//--cambine:create buffer and then copy data from host to device with pinned
// memory
cl_mem _clCreateAndCpyPinnedMem(int size, float *h_mem_source)
{
    cl_mem d_mem, d_mem_pinned;
    float *h_mem_pinned = NULL;
    d_mem_pinned = clCreateBuffer(oclHandles.context, CL_MEM_READ_ONLY | CL_MEM_ALLOC_HOST_PTR,
                                  size, NULL, &oclHandles.cl_status);
#ifdef ERRMSG
    if (oclHandles.cl_status != CL_SUCCESS)
        throw(string("exception in _clCreateAndCpyMem()->d_mem_pinned"));
#endif
    //------------
    d_mem = clCreateBuffer(oclHandles.context, CL_MEM_READ_ONLY,
                           size, NULL, &oclHandles.cl_status);
#ifdef ERRMSG
    if (oclHandles.cl_status != CL_SUCCESS)
        throw(string("exception in _clCreateAndCpyMem() -> d_mem "));
#endif
    //----------
    h_mem_pinned = (cl_float *)clEnqueueMapBuffer(oclHandles.queue, d_mem_pinned, CL_TRUE,
                                                  CL_MAP_WRITE, 0, size, 0, NULL,
                                                  NULL, &oclHandles.cl_status);
#ifdef ERRMSG
    if (oclHandles.cl_status != CL_SUCCESS)
        throw(string("exception in _clCreateAndCpyMem() -> clEnqueueMapBuffer"));
#endif
    int element_number = size / sizeof(float);
#pragma omp parallel for
    for (int i = 0; i < element_number; i++)
    {
        h_mem_pinned[i] = h_mem_source[i];
    }
    //----------
    oclHandles.cl_status = clEnqueueWriteBuffer(oclHandles.queue, d_mem,
                                                CL_TRUE, 0, size, h_mem_pinned,
                                                0, NULL, NULL);
#ifdef ERRMSG
    if (oclHandles.cl_status != CL_SUCCESS)
        throw(string("exception in _clCreateAndCpyMem() -> clEnqueueWriteBuffer"));
#endif

    return d_mem;
}

//--------------------------------------------------------
//--cambine:create write only buffer on device
cl_mem _clMallocWO(int size)
{
    cl_mem d_mem;
    d_mem = clCreateBuffer(oclHandles.context, CL_MEM_WRITE_ONLY, size, 0, &oclHandles.cl_status);
#ifdef ERRMSG
    if (oclHandles.cl_status != CL_SUCCESS)
        throw(string("exception in _clCreateMem()"));
#endif
    return d_mem;
}

//--------------------------------------------------------
//transfer data from device to host
cl_event _clMemcpyD2H(cl_mem d_mem, int size, void *h_mem)
{
    cl_event event;
    oclHandles.cl_status = clEnqueueReadBuffer(oclHandles.queue, d_mem, CL_TRUE, 0, size, h_mem, 0, 0, &event);
#ifdef ERRMSG
    oclHandles.error_str = "exception in _clCpyMemD2H -> ";
    switch (oclHandles.cl_status)
    {
    case CL_INVALID_COMMAND_QUEUE:
        oclHandles.error_str += "CL_INVALID_COMMAND_QUEUE";
        break;
    case CL_INVALID_CONTEXT:
        oclHandles.error_str += "CL_INVALID_CONTEXT";
        break;
    case CL_INVALID_MEM_OBJECT:
        oclHandles.error_str += "CL_INVALID_MEM_OBJECT";
        break;
    case CL_INVALID_VALUE:
        oclHandles.error_str += "CL_INVALID_VALUE";
        break;
    case CL_INVALID_EVENT_WAIT_LIST:
        oclHandles.error_str += "CL_INVALID_EVENT_WAIT_LIST";
        break;
    case CL_MISALIGNED_SUB_BUFFER_OFFSET:
        oclHandles.error_str += "CL_MISALIGNED_SUB_BUFFER_OFFSET";
        break;
    case CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST:
        oclHandles.error_str += "CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST";
        break;
    case CL_MEM_OBJECT_ALLOCATION_FAILURE:
        oclHandles.error_str += "CL_MEM_OBJECT_ALLOCATION_FAILURE";
        break;
    case CL_INVALID_OPERATION:
        oclHandles.error_str += "CL_INVALID_OPERATION";
        break;
    case CL_OUT_OF_RESOURCES:
        oclHandles.error_str += "CL_OUT_OF_RESOURCES";
        break;
    case CL_OUT_OF_HOST_MEMORY:
        oclHandles.error_str += "CL_OUT_OF_HOST_MEMORY";
        break;
    default:
        oclHandles.error_str += "Unknown reason";
        break;
    }
    if (oclHandles.cl_status != CL_SUCCESS)
        throw(oclHandles.error_str);
#endif
    return event;
}

//--------------------------------------------------------
//set kernel arguments
void _clSetArgs(int kernel_id, int arg_idx, void *d_mem, int size = 0)
{
    if (!size)
    {
        oclHandles.cl_status = clSetKernelArg(oclHandles.kernel[kernel_id], arg_idx, sizeof(d_mem), &d_mem);
#ifdef ERRMSG
        oclHandles.error_str = "exception in _clSetKernelArg() ";
        switch (oclHandles.cl_status)
        {
        case CL_INVALID_KERNEL:
            oclHandles.error_str += "CL_INVALID_KERNEL";
            break;
        case CL_INVALID_ARG_INDEX:
            oclHandles.error_str += "CL_INVALID_ARG_INDEX";
            break;
        case CL_INVALID_ARG_VALUE:
            oclHandles.error_str += "CL_INVALID_ARG_VALUE";
            break;
        case CL_INVALID_MEM_OBJECT:
            oclHandles.error_str += "CL_INVALID_MEM_OBJECT";
            break;
        case CL_INVALID_SAMPLER:
            oclHandles.error_str += "CL_INVALID_SAMPLER";
            break;
        case CL_INVALID_ARG_SIZE:
            oclHandles.error_str += "CL_INVALID_ARG_SIZE";
            break;
        case CL_OUT_OF_RESOURCES:
            oclHandles.error_str += "CL_OUT_OF_RESOURCES";
            break;
        case CL_OUT_OF_HOST_MEMORY:
            oclHandles.error_str += "CL_OUT_OF_HOST_MEMORY";
            break;
        default:
            oclHandles.error_str += "Unknown reason";
            break;
        }
        if (oclHandles.cl_status != CL_SUCCESS)
            throw(oclHandles.error_str);
#endif
    }
    else
    {
        oclHandles.cl_status = clSetKernelArg(oclHandles.kernel[kernel_id], arg_idx, size, d_mem);
#ifdef ERRMSG
        oclHandles.error_str = "exception in _clSetKernelArg() ";
        switch (oclHandles.cl_status)
        {
        case CL_INVALID_KERNEL:
            oclHandles.error_str += "CL_INVALID_KERNEL";
            break;
        case CL_INVALID_ARG_INDEX:
            oclHandles.error_str += "CL_INVALID_ARG_INDEX";
            break;
        case CL_INVALID_ARG_VALUE:
            oclHandles.error_str += "CL_INVALID_ARG_VALUE";
            break;
        case CL_INVALID_MEM_OBJECT:
            oclHandles.error_str += "CL_INVALID_MEM_OBJECT";
            break;
        case CL_INVALID_SAMPLER:
            oclHandles.error_str += "CL_INVALID_SAMPLER";
            break;
        case CL_INVALID_ARG_SIZE:
            oclHandles.error_str += "CL_INVALID_ARG_SIZE";
            break;
        case CL_OUT_OF_RESOURCES:
            oclHandles.error_str += "CL_OUT_OF_RESOURCES";
            break;
        case CL_OUT_OF_HOST_MEMORY:
            oclHandles.error_str += "CL_OUT_OF_HOST_MEMORY";
            break;
        default:
            oclHandles.error_str += "Unknown reason";
            break;
        }
        if (oclHandles.cl_status != CL_SUCCESS)
            throw(oclHandles.error_str);
#endif
    }
}
void _clFinish()
{
    oclHandles.cl_status = clFinish(oclHandles.queue);
#ifdef ERRMSG
    oclHandles.error_str = "exception in _clFinish";
    switch (oclHandles.cl_status)
    {
    case CL_INVALID_COMMAND_QUEUE:
        oclHandles.error_str += "CL_INVALID_COMMAND_QUEUE";
        break;
    case CL_OUT_OF_RESOURCES:
        oclHandles.error_str += "CL_OUT_OF_RESOURCES";
        break;
    case CL_OUT_OF_HOST_MEMORY:
        oclHandles.error_str += "CL_OUT_OF_HOST_MEMORY";
        break;
    default:
        oclHandles.error_str += "Unknown reasons";
        break;
    }
    if (oclHandles.cl_status != CL_SUCCESS)
    {
        throw(oclHandles.error_str);
    }
#endif
}
//--------------------------------------------------------
//--cambine:enqueue kernel
cl_event _clInvokeKernel(int kernel_id, size_t work_items, size_t work_group_size)
{
    cl_uint work_dim = WORK_DIM;
    cl_event event;
    if (work_items % work_group_size != 0) //process situations that work_items cannot be divided by work_group_size
        work_items = work_items + (work_group_size - (work_items % work_group_size));
    size_t local_work_size[] = {work_group_size, 1};
    size_t global_work_size[] = {work_items, 1};
    oclHandles.cl_status = clEnqueueNDRangeKernel(oclHandles.queue, oclHandles.kernel[kernel_id], work_dim, 0,
                                                  global_work_size, local_work_size, 0, 0, &event);
#ifdef ERRMSG
    oclHandles.error_str = "exception in _clInvokeKernel() -> ";
    switch (oclHandles.cl_status)
    {
    case CL_INVALID_PROGRAM_EXECUTABLE:
        oclHandles.error_str += "CL_INVALID_PROGRAM_EXECUTABLE";
        break;
    case CL_INVALID_COMMAND_QUEUE:
        oclHandles.error_str += "CL_INVALID_COMMAND_QUEUE";
        break;
    case CL_INVALID_KERNEL:
        oclHandles.error_str += "CL_INVALID_KERNEL";
        break;
    case CL_INVALID_CONTEXT:
        oclHandles.error_str += "CL_INVALID_CONTEXT";
        break;
    case CL_INVALID_KERNEL_ARGS:
        oclHandles.error_str += "CL_INVALID_KERNEL_ARGS";
        break;
    case CL_INVALID_WORK_DIMENSION:
        oclHandles.error_str += "CL_INVALID_WORK_DIMENSION";
        break;
    case CL_INVALID_GLOBAL_WORK_SIZE:
        oclHandles.error_str += "CL_INVALID_GLOBAL_WORK_SIZE";
        break;
    case CL_INVALID_WORK_GROUP_SIZE:
        oclHandles.error_str += "CL_INVALID_WORK_GROUP_SIZE";
        break;
    case CL_INVALID_WORK_ITEM_SIZE:
        oclHandles.error_str += "CL_INVALID_WORK_ITEM_SIZE";
        break;
    case CL_INVALID_GLOBAL_OFFSET:
        oclHandles.error_str += "CL_INVALID_GLOBAL_OFFSET";
        break;
    case CL_OUT_OF_RESOURCES:
        oclHandles.error_str += "CL_OUT_OF_RESOURCES";
        break;
    case CL_MEM_OBJECT_ALLOCATION_FAILURE:
        oclHandles.error_str += "CL_MEM_OBJECT_ALLOCATION_FAILURE";
        break;
    case CL_INVALID_EVENT_WAIT_LIST:
        oclHandles.error_str += "CL_INVALID_EVENT_WAIT_LIST";
        break;
    case CL_OUT_OF_HOST_MEMORY:
        oclHandles.error_str += "CL_OUT_OF_HOST_MEMORY";
        break;
    default:
        oclHandles.error_str += "Unkown reseason";
        break;
    }
    if (oclHandles.cl_status != CL_SUCCESS)
        throw(oclHandles.error_str);
#endif
    return event;
    //_clFinish();
    // oclHandles.cl_status = clWaitForEvents(1, &e[0]);
    // #ifdef ERRMSG
    // if (oclHandles.cl_status!= CL_SUCCESS)
    //     throw(string("exception in _clEnqueueNDRange() -> clWaitForEvents"));
    // #endif
}
void _clInvokeKernel2D(int kernel_id, size_t range_x, size_t range_y, size_t group_x, size_t group_y)
{
    cl_uint work_dim = WORK_DIM;
    size_t local_work_size[] = {group_x, group_y};
    size_t global_work_size[] = {range_x, range_y};
    cl_event e[1];
    /*if(work_items%work_group_size != 0)	//process situations that work_items cannot be divided by work_group_size
      work_items = work_items + (work_group_size-(work_items%work_group_size));*/
    oclHandles.cl_status = clEnqueueNDRangeKernel(oclHandles.queue, oclHandles.kernel[kernel_id], work_dim, 0,
                                                  global_work_size, local_work_size, 0, 0, &(e[0]));
#ifdef ERRMSG
    oclHandles.error_str = "exception in _clInvokeKernel() -> ";
    switch (oclHandles.cl_status)
    {
    case CL_INVALID_PROGRAM_EXECUTABLE:
        oclHandles.error_str += "CL_INVALID_PROGRAM_EXECUTABLE";
        break;
    case CL_INVALID_COMMAND_QUEUE:
        oclHandles.error_str += "CL_INVALID_COMMAND_QUEUE";
        break;
    case CL_INVALID_KERNEL:
        oclHandles.error_str += "CL_INVALID_KERNEL";
        break;
    case CL_INVALID_CONTEXT:
        oclHandles.error_str += "CL_INVALID_CONTEXT";
        break;
    case CL_INVALID_KERNEL_ARGS:
        oclHandles.error_str += "CL_INVALID_KERNEL_ARGS";
        break;
    case CL_INVALID_WORK_DIMENSION:
        oclHandles.error_str += "CL_INVALID_WORK_DIMENSION";
        break;
    case CL_INVALID_GLOBAL_WORK_SIZE:
        oclHandles.error_str += "CL_INVALID_GLOBAL_WORK_SIZE";
        break;
    case CL_INVALID_WORK_GROUP_SIZE:
        oclHandles.error_str += "CL_INVALID_WORK_GROUP_SIZE";
        break;
    case CL_INVALID_WORK_ITEM_SIZE:
        oclHandles.error_str += "CL_INVALID_WORK_ITEM_SIZE";
        break;
    case CL_INVALID_GLOBAL_OFFSET:
        oclHandles.error_str += "CL_INVALID_GLOBAL_OFFSET";
        break;
    case CL_OUT_OF_RESOURCES:
        oclHandles.error_str += "CL_OUT_OF_RESOURCES";
        break;
    case CL_MEM_OBJECT_ALLOCATION_FAILURE:
        oclHandles.error_str += "CL_MEM_OBJECT_ALLOCATION_FAILURE";
        break;
    case CL_INVALID_EVENT_WAIT_LIST:
        oclHandles.error_str += "CL_INVALID_EVENT_WAIT_LIST";
        break;
    case CL_OUT_OF_HOST_MEMORY:
        oclHandles.error_str += "CL_OUT_OF_HOST_MEMORY";
        break;
    default:
        oclHandles.error_str += "Unkown reseason";
        break;
    }
    if (oclHandles.cl_status != CL_SUCCESS)
        throw(oclHandles.error_str);
#endif
    //_clFinish();
    /*oclHandles.cl_status = clWaitForEvents(1, &e[0]);

    #ifdef ERRMSG

        if (oclHandles.cl_status!= CL_SUCCESS)

            throw(string("exception in _clEnqueueNDRange() -> clWaitForEvents"));

    #endif*/
}

//--------------------------------------------------------
//release OpenCL objects
void _clFree(cl_mem ob)
{
    if (ob != NULL)
        oclHandles.cl_status = clReleaseMemObject(ob);
#ifdef ERRMSG
    oclHandles.error_str = "exception in _clFree() ->";
    switch (oclHandles.cl_status)
    {
    case CL_INVALID_MEM_OBJECT:
        oclHandles.error_str += "CL_INVALID_MEM_OBJECT";
        break;
    case CL_OUT_OF_RESOURCES:
        oclHandles.error_str += "CL_OUT_OF_RESOURCES";
        break;
    case CL_OUT_OF_HOST_MEMORY:
        oclHandles.error_str += "CL_OUT_OF_HOST_MEMORY";
        break;
    default:
        oclHandles.error_str += "Unkown reseason";
        break;
    }
    if (oclHandles.cl_status != CL_SUCCESS)
        throw(oclHandles.error_str);
#endif
}

void _clWait(cl_int num_of_events, const cl_event* events)
{
    oclHandles.cl_status = clWaitForEvents(num_of_events, events);
#ifdef ERRMSG
    oclHandles.error_str = "exception in _clWait() ->";
    switch(oclHandles.cl_status)
    {
        case CL_INVALID_VALUE:
            oclHandles.error_str += "CL_INVALID_VALUE";
            break;
        case CL_INVALID_CONTEXT:
            oclHandles.error_str += "CL_INVALID_CONTEXT";
            break;
        case CL_INVALID_EVENT:
            oclHandles.error_str += "CL_INVALID_EVENT";
            break;
        case CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST:
            oclHandles.error_str += "CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST";
            break;
        case CL_OUT_OF_RESOURCES:
            oclHandles.error_str += "CL_OUT_OF_RESOURCES";
            break;
        case CL_OUT_OF_HOST_MEMORY:
            oclHandles.error_str += "CL_OUT_OF_HOST_MEMORY";
            break;
    }
    if(oclHandles.cl_status != CL_SUCCESS)
    {
        throw(oclHandles.error_str);
    }
#endif
}

#endif //_CL_HELPER_
