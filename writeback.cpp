// #define _GNU_SOURCE
#include <pthread.h>
#include <numa.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
//vtune markers!
#include <ittnotify.h>

#include <fcntl.h>
#include <unistd.h>
#include <string>
#define ARRAY_SIZE 9961472

//long *array;
long * arrays[64];

 void vtune_example(){
    
    auto vtune_event_find =
    __itt_event_create("finding", strlen("finding"));
    __itt_event_start(vtune_event_find);

        // // some code

     __itt_event_end(vtune_event_find);
 }

// void *writer_thread(void *arg) {
//      // Allocate memory on socket X (NUMA node X)
//     array = (long*) numa_alloc_onnode(ARRAY_SIZE * sizeof(long), sched_getcpu());    
    
//     if (!array) {
//         perror("Failed to allocate memory on socket");
//         exit(EXIT_FAILURE);
//     }
//     printf("initialized array in socket: %d\n", sched_getcpu());

//     for (int i = 0; i < ARRAY_SIZE; i++) {
//         array[i] = 1;  // Write to the array
//     }
//     printf("Writer completed on CPU %d\n", sched_getcpu());

//     return NULL;
// }


void *reader_thread(void *arg) {
    //From dramhit Application.cpp
                // // Control hw prefetcher msr
                // if (vm["hw-pref"].as<bool>()) {
                //   // XXX: 0x1a4 is prefetch control;
                //   // 0 - all enabled; f - all disabled
                //   this->msr_ctrl->write_msr(0x1a4, 0x0);
                // } else {
                //   this->msr_ctrl->write_msr(0x1a4, 0xf);
                // }

    uint64_t write_value = 0xf;  // Write f to disable hardware prefetching
    uint64_t msr = 0x1a4;  // MSR address for disabling hardware prefetching
    uint32_t cpu_id = sched_getcpu();
    
    // Open MSR device for this CPU

    int msr_fd = open(("/dev/cpu/" + std::to_string(cpu_id) + "/msr").c_str(), O_RDWR);
    if (msr_fd == -1) {
        perror("Failed to open MSR device");
        return NULL;
    }

    // Write 0 to MSR 0x1A4 to disable hardware prefetching for this CPU
    ssize_t ret = pwrite(msr_fd, &write_value, sizeof(write_value), msr);
    if (ret != sizeof(write_value)) {
        perror("pwrite failed to disable prefetching on CPU");
        close(msr_fd);
        return NULL;
    }

    printf("Disabled hardware prefetching on CPU %d by writing 0xf to MSR 0x1A4\n", cpu_id);

    // Close the MSR file descriptor after use
    close(msr_fd);

    printf("starting read test on socket: %d\n",sched_getcpu());
    fflush(stdout);

    
    long * array = arrays[sched_getcpu()];
    long sum = 0;
    auto vtune_event =
    __itt_event_create("reading", strlen("reading"));
    __itt_event_start(vtune_event);


     for(int j = 0; j<100; j++){
        for (int i = 0; i < ARRAY_SIZE; i++) {

            //Prefetch, false= read, true= write
            if(i%32 == 0){
            __builtin_prefetch(&array[i], false, 3);
            __builtin_prefetch(&array[i+8], false, 3);
            __builtin_prefetch(&array[i+16], false, 3);
            __builtin_prefetch(&array[i+24], false, 3);

            //array[i] +=1;
            }

            sum += array[i];  // Read from the array
        }
    }

    __itt_event_end(vtune_event);

    printf("Reader completed on CPU %d, sum: %ld\n", sched_getcpu(), sum);
    return NULL;
}

int main() {

    /*
    From lscpu:
        L1d cache:                          1 MiB
        L1i cache:                          1 MiB
        L2 cache:                           32 MiB
        L3 cache:                           44 MiB
        NUMA node0 CPU(s):                  0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50,52,54,56,58,60,62
        NUMA node1 CPU(s):                  1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31,33,35,37,39,41,43,45,47,49,51,53,55,57,59,61,63
    */

    pthread_t writer, reader;
    cpu_set_t writer_cpuset, reader_cpuset;
    long i = 0;
    printf("numa_available status: %d\n", numa_available());
    printf("size of long (in bytes): %ld\n", sizeof(i));

    // // Ensure NUMA is available
    if (numa_available() == -1) {
        printf("NUMA is unavailable.\n");
        return 1;
    }
    // lscpu combines cache numbers so per core we have = L1-.5mib, L2-16mib, L3(shared)-22mib
    // 38Mib, should basically fill the cache of 1 core.
    // 1Mib= 1048576 bytes
    // 38Mib = [39845888] bytes
    // 39845888/8 = [4980736] , this means we can have an array of 4980736 8 byte elements in cache,
    // Since we want to ensure we will at least replace most of the cache at least once 4980736x2 = [9961472]
    // ie if we cached every element in a long[9961472], we should always encounter data that wasn't on anything previously in the cache.

    //create an array on socket 0 for each thread we'll run on socket 1. That way each socket 1 thread can read from its respective
    // remote array durring the test and saturate the badwidth to get a clearer picture in vtune
    for(int i = 0; i<32; i++){
        int cpu_id = i*2 +1; //1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31,33,35,37,39,41,43,45,47,49,51,53,55,57,59,61,63

        arrays[cpu_id] = (long*) numa_alloc_onnode(ARRAY_SIZE * sizeof(long), 0);    
    
        if (!arrays[cpu_id]) {
            perror("Failed to allocate memory on socket");
            exit(EXIT_FAILURE);
        }
        printf("initialized array in socket: %d\n",0);

        for (int i = 0; i < ARRAY_SIZE; i++) {
            arrays[cpu_id][i] = 1;  // Write to the array
        }

        printf("finished writing to arrays[%d] \n",cpu_id);
    }// end arrays init loop.

    // make 32 threads that run on:
//1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31,33,35,37,39,41,43,45,47,49,51,53,55,57,59,61,63
    // and each call simultaniously: void *reader_thread(void *arg)

    // Create 32 threads
    pthread_t threads[32];
    int cpu_ids[32];
    
    for (int i = 0; i < 32; i++) {
        cpu_ids[i] = i * 2 + 1;  // CPU IDs: 1, 3, 5, 7, ..., 63

        // Set the CPU affinity for the thread
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_ids[i], &cpuset);

        // Create the thread
        if (pthread_create(&threads[i], NULL, reader_thread, (void *)&cpu_ids[i]) != 0) {
            perror("Failed to create thread");
            exit(EXIT_FAILURE);
        }

        if (pthread_setaffinity_np(threads[i], sizeof(cpu_set_t), &cpuset) != 0) {
            perror("Failed to set thread CPU affinity");
            exit(EXIT_FAILURE);
        }

        
    }

    // Wait for all threads to finish
    for (int i = 0; i < 32; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("All threads have finished.\n");
   
    // Pin writer thread to socket 1
    // CPU_ZERO(&writer_cpuset);
    // CPU_SET(1, &writer_cpuset);  // Adjust based on socket 1 cores
    // pthread_create(&writer, NULL, writer_thread, NULL);
    // pthread_setaffinity_np(writer, sizeof(cpu_set_t), &writer_cpuset);

    // // Wait for threads to complete
    // pthread_join(writer, NULL);

    // CPU_ZERO(&reader_cpuset);
    // CPU_SET(0, &reader_cpuset);  // Adjust based on socket 0 cores
    // pthread_create(&reader, NULL, reader_thread, NULL);
    // pthread_setaffinity_np(reader, sizeof(cpu_set_t), &reader_cpuset);

    // pthread_join(reader, NULL);

    // Free NUMA-allocated memory
    //numa_free(array, ARRAY_SIZE * sizeof(int));

    return 0;
}
