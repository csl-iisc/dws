/***************************************************************************
 *cr
 *cr            (C) Copyright 2010 The Board of Trustees of the
 *cr                        University of Illinois
 *cr                         All Rights Reserved
 *cr
 ***************************************************************************/

/*
 * Main entry of dense matrix-matrix multiplication kernel
 */

#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <iostream>
#include <vector>
#include "../FFT/parboil.h"
#include "../benchmark_common.h"
#include "sgemm_kernel.cu"
// I/O routines
extern bool readColMajorMatrixFile(const char* fn,
                                   int& nr_row,
                                   int& nr_col,
                                   std::vector<float>& v);
extern bool writeColMajorMatrixFile(const char* fn,
                                    int,
                                    int,
                                    std::vector<float>&);

extern "C" void computeGold(float*,
                            const float*,
                            const float*,
                            unsigned int,
                            unsigned int,
                            unsigned int);

// int
// main (int argc, char *argv[]) {
int main_MM(cudaStream_t stream_app, pthread_mutex_t* mutexapp, bool flag) {
  struct pb_TimerSet timers;

  float *dA, *dB, *dC;
  size_t A_sz, B_sz, C_sz;
  int matArow, matAcol;
  int matBrow, matBcol;
  std::vector<float> matA, matBT;

  pb_InitializeTimerSet(&timers);

  /* Read command line. Expect 3 inputs: A, B and B^T
     in column-major layout*/
  /*params = pb_ReadParameters();
  if ((params->inpFiles[0] == NULL)
      || (params->inpFiles[1] == NULL)
      || (params->inpFiles[2] == NULL)
      || (params->inpFiles[3] != NULL))
    {
      fprintf(stderr, "Expecting three input filenames\n");
      exit(-1);
    }*/

  /* Read in data */
  pb_SwitchToTimer(&timers, pb_TimerID_IO);

  // load A
  readColMajorMatrixFile((char*)"/home/pratheek-htc/masksim/v3.x/pthread_benchmark/MM/matrix1.txt", matArow, matAcol, matA);
  // copy A to device memory
  A_sz = matArow * matAcol * sizeof(float);

  // load B^T
  readColMajorMatrixFile((char*)"/home/pratheek-htc/masksim/v3.x/pthread_benchmark/MM/matrix2t.txt", matBcol, matBrow, matBT);

  pb_SwitchToTimer(&timers, pb_TimerID_COMPUTE);
  B_sz = matBrow * matBcol * sizeof(float);

  // allocate space for C
  C_sz = matArow * matBcol * sizeof(float);

  // CUDA memory allocation
  std::vector<float> matC(matArow * matBcol);
  cudaMalloc((void**)&dA, A_sz);
  cudaMalloc((void**)&dB, B_sz);
  cudaMalloc((void**)&dC, C_sz);

  // Copy A and B^T into device memory
  pb_SwitchToTimer(&timers, pb_TimerID_COPY);
  cudaMemcpyAsync(dA, &matA.front(), A_sz, cudaMemcpyHostToDevice, stream_app);
  cudaMemcpyAsync(dB, &matBT.front(), B_sz, cudaMemcpyHostToDevice, stream_app);

  pb_SwitchToTimer(&timers, pb_TimerID_GPU);
  std::cout << "flag = " << flag << std::endl;
  // Use standard sgemm interface
  regtileSgemm('N', 'T', matArow, matBcol, matAcol, 1.0f, dA, matArow, dB,
               matBcol, 0.0f, dC, matArow, stream_app, mutexapp, flag);
  std::cout << "kernel launch finishes" << std::endl;
  // if (params->outFile) {
  pb_SwitchToTimer(&timers, pb_TimerID_COPY);
  cudaMemcpyAsync(&matC.front(), dC, C_sz, cudaMemcpyDeviceToHost, stream_app);
  /* Write C to file */
  pb_SwitchToTimer(&timers, pb_TimerID_IO);
  writeColMajorMatrixFile((char*)"/home/pratheek-htc/masksim/v3.x/pthread_benchmark/MM/matrix3.txt", matArow, matBcol, matC);
  //}

  pb_SwitchToTimer(&timers, pb_TimerID_NONE);

  double GPUtime = pb_GetElapsedTime(&(timers.timers[pb_TimerID_GPU]));
  std::cout << "GFLOPs = " << 2. * matArow * matBcol * matAcol / GPUtime / 1e9
            << std::endl;
  pb_PrintTimerSet(&timers);
  cudaFree(dA);
  cudaFree(dB);
  cudaFree(dC);
  return 0;
}
