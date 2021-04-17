/***********************************************************************************
  Implementing Breadth first search on CUDA using algorithm given in HiPC'07
  paper "Accelerating Large Graph Algorithms on the GPU using CUDA"

  Copyright (c) 2008 International Institute of Information Technology -
 Hyderabad.
  All rights reserved.

  Permission to use, copy, modify and distribute this software and its
 documentation for
  educational purpose is hereby granted without fee, provided that the above
 copyright
  notice and this permission notice appear in all copies of this software and
 that you do
  not sell the software.

  THE SOFTWARE IS PROVIDED "AS IS" AND WITHOUT WARRANTY OF ANY KIND,EXPRESS,
 IMPLIED OR
  OTHERWISE.

  Created by Pawan Harish.
 ************************************************************************************/
#include <cuda.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_THREADS_PER_BLOCK 512

#include "../benchmark_common.h"

int no_of_nodes;
int edge_list_size;
FILE* fp;

// Structure to hold a node information
struct Node {
  int starting;
  int no_of_edges;
};

#include "kernel.cu"
#include "kernel2.cu"

void BFSGraph(cudaStream_t stream_app, pthread_mutex_t* mutexapp, bool flag);

////////////////////////////////////////////////////////////////////////////////
// Main Program
////////////////////////////////////////////////////////////////////////////////

// int main( int argc, char** argv)
int main_BFS2(cudaStream_t stream_app, pthread_mutex_t* mutexapp, bool flag) {
  no_of_nodes = 0;
  edge_list_size = 0;
  BFSGraph(stream_app, mutexapp, flag);
  return 0;
}

////////////////////////////////////////////////////////////////////////////////
// Apply BFS on a Graph using CUDA
////////////////////////////////////////////////////////////////////////////////
void BFSGraph(cudaStream_t stream_app, pthread_mutex_t* mutexapp, bool flag) {
  static char* input_file_name;
  // printf("argc=%d\n", argc);
  /*if (argc == 2 ) {
                  input_file_name = argv[1];
                  printf("Input file: %s\n", input_file_name);
  }
  else
  {
                  input_file_name = "SampleGraph.txt";
                  printf("No input file specified, defaulting to
  SampleGraph.txt\n");
  }*/

  input_file_name = "./BFS2/data/graph32k500kedges_SV.txt";
  input_file_name = "/home/pratheek-htc/masksim/v3.x/pthread_benchmark/BFS2/data/graph1MW_6.txt";

  printf("Reading File\n");
  // Read in Graph from a file
  fp = fopen(input_file_name, "r");
  if (!fp) {
    printf("Error Reading graph file\n");
    return;
  }

  int source = 0;

  fscanf(fp, "%d", &no_of_nodes);

  int num_of_blocks = 1;
  int num_of_threads_per_block = no_of_nodes;

  // Make execution Parameters according to the number of nodes
  // Distribute threads across multiple Blocks if necessary
  if (no_of_nodes > MAX_THREADS_PER_BLOCK) {
    num_of_blocks = (int)ceil(no_of_nodes / (double)MAX_THREADS_PER_BLOCK);
    num_of_threads_per_block = MAX_THREADS_PER_BLOCK;
  }

  // allocate host memory
  Node* h_graph_nodes = (Node*)malloc(sizeof(Node) * no_of_nodes);
  bool* h_graph_mask = (bool*)malloc(sizeof(bool) * no_of_nodes);
  bool* h_updating_graph_mask = (bool*)malloc(sizeof(bool) * no_of_nodes);
  bool* h_graph_visited = (bool*)malloc(sizeof(bool) * no_of_nodes);

  int start, edgeno;
  // initalize the memory
  for (unsigned int i = 0; i < no_of_nodes; i++) {
    fscanf(fp, "%d %d", &start, &edgeno);
    h_graph_nodes[i].starting = start;
    h_graph_nodes[i].no_of_edges = edgeno;
    h_graph_mask[i] = false;
    h_updating_graph_mask[i] = false;
    h_graph_visited[i] = false;
  }

  // read the source node from the file
  fscanf(fp, "%d", &source);
  source = 0;

  // set the source node as true in the mask
  h_graph_mask[source] = true;
  h_graph_visited[source] = true;

  fscanf(fp, "%d", &edge_list_size);

  int id, cost;
  int* h_graph_edges = (int*)malloc(sizeof(int) * edge_list_size);
  for (int i = 0; i < edge_list_size; i++) {
    fscanf(fp, "%d", &id);
    fscanf(fp, "%d", &cost);
    h_graph_edges[i] = id;
  }

  if (fp)
    fclose(fp);

  printf("Read File\n");

  // Copy the Node list to device memory
  Node* d_graph_nodes;
  cudaMalloc((void**)&d_graph_nodes, sizeof(Node) * no_of_nodes);
  cudaMemcpyAsync(d_graph_nodes, h_graph_nodes, sizeof(Node) * no_of_nodes,
                  cudaMemcpyHostToDevice, stream_app);

  // Copy the Edge List to device Memory
  int* d_graph_edges;
  cudaMalloc((void**)&d_graph_edges, sizeof(int) * edge_list_size);
  cudaMemcpyAsync(d_graph_edges, h_graph_edges, sizeof(int) * edge_list_size,
                  cudaMemcpyHostToDevice, stream_app);

  // Copy the Mask to device memory
  bool* d_graph_mask;
  cudaMalloc((void**)&d_graph_mask, sizeof(bool) * no_of_nodes);
  cudaMemcpyAsync(d_graph_mask, h_graph_mask, sizeof(bool) * no_of_nodes,
                  cudaMemcpyHostToDevice, stream_app);

  bool* d_updating_graph_mask;
  cudaMalloc((void**)&d_updating_graph_mask, sizeof(bool) * no_of_nodes);
  cudaMemcpyAsync(d_updating_graph_mask, h_updating_graph_mask,
                  sizeof(bool) * no_of_nodes, cudaMemcpyHostToDevice,
                  stream_app);

  // Copy the Visited nodes array to device memory
  bool* d_graph_visited;
  cudaMalloc((void**)&d_graph_visited, sizeof(bool) * no_of_nodes);
  cudaMemcpyAsync(d_graph_visited, h_graph_visited, sizeof(bool) * no_of_nodes,
                  cudaMemcpyHostToDevice, stream_app);

  // allocate mem for the result on host side
  int* h_cost = (int*)malloc(sizeof(int) * no_of_nodes);
  for (int i = 0; i < no_of_nodes; i++)
    h_cost[i] = -1;
  h_cost[source] = 0;

  // allocate device memory for result
  int* d_cost;
  cudaMalloc((void**)&d_cost, sizeof(int) * no_of_nodes);
  cudaMemcpyAsync(d_cost, h_cost, sizeof(int) * no_of_nodes,
                  cudaMemcpyHostToDevice, stream_app);

  // make a bool to check if the execution is over
  bool* d_over;
  cudaMalloc((void**)&d_over, sizeof(bool));

  printf("Copied Everything to GPU memory\n");

  // setup execution parameters
  dim3 grid(num_of_blocks, 1, 1);
  dim3 threads(num_of_threads_per_block, 1, 1);

  int k = 0;

  bool stop;
  // Call the Kernel untill all the elements of Frontier are not false
  do {
    // if no thread changes this value then the loop stops
    stop = false;
    cudaMemcpyAsync(d_over, &stop, sizeof(bool), cudaMemcpyHostToDevice,
                    stream_app);
    Kernel<<<grid, threads, 0, stream_app>>>(
        d_graph_nodes, d_graph_edges, d_graph_mask, d_updating_graph_mask,
        d_graph_visited, d_cost, no_of_nodes);
    // check if kernel execution generated and error

    Kernel2<<<grid, threads, 0, stream_app>>>(
        d_graph_mask, d_updating_graph_mask, d_graph_visited, d_over,
        no_of_nodes);
    // check if kernel execution generated and error

    cudaMemcpyAsync(&stop, d_over, sizeof(bool), cudaMemcpyDeviceToHost,
                    stream_app);
    pthread_mutex_unlock(mutexapp);
    if (flag)
      cutilSafeCall(cudaStreamSynchronize(stream_app));
    else
      cutilSafeCall(cudaThreadSynchronize());
    pthread_mutex_lock(mutexapp);
    k++;
  } while (stop);

  pthread_mutex_unlock(mutexapp);
  printf("Kernel Executed %d times\n", k);

  // copy result from device to host
  cudaMemcpyAsync(h_cost, d_cost, sizeof(int) * no_of_nodes,
                  cudaMemcpyDeviceToHost, stream_app);

  if (flag)
    cutilSafeCall(cudaStreamSynchronize(stream_app));

  // Store the result into a file
  FILE* fpo = fopen("result.txt", "w");
  for (int i = 0; i < no_of_nodes; i++)
    fprintf(fpo, "%d) cost:%d\n", i, h_cost[i]);
  fclose(fpo);
  printf("Result stored in result.txt\n");

  // cleanup memory
  free(h_graph_nodes);
  free(h_graph_edges);
  free(h_graph_mask);
  free(h_updating_graph_mask);
  free(h_graph_visited);
  free(h_cost);
  cudaFree(d_graph_nodes);
  cudaFree(d_graph_edges);
  cudaFree(d_graph_mask);
  cudaFree(d_updating_graph_mask);
  cudaFree(d_graph_visited);
  cudaFree(d_cost);
}