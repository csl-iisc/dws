// Copyright (c) 2009-2011, Tor M. Aamodt, Wilson W.L. Fung
// The University of British Columbia
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
// Redistributions in binary form must reproduce the above copyright notice, this
// list of conditions and the following disclaimer in the documentation and/or
// other materials provided with the distribution.
// Neither the name of The University of British Columbia nor the names of its
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "stream_manager.h"
#include "abstract_hardware_model.h"
#include "gpgpusim_entrypoint.h"
#include "cuda-sim/cuda-sim.h"
#include "gpgpu-sim/gpu-sim.h"
#include "gpgpu-sim/gpu-cache.h"
#include "gpgpu-sim/mem_fetch.h"
#include "gpgpu-sim/ConfigOptions.h"
#include "gpgpu-sim/l2cache.h"

#include <sstream>
#include <string>
#include <vector>

FILE *output = fopen("dump.txt", "w");

unsigned CUstream_st::next_stream_id = 0;

CUstream_st::CUstream_st() {
  this->current_op_pending = false;
  this->one_time_only = false;
  this->stream_id = CUstream_st::next_stream_id++;
  pthread_mutex_init(&this->stream_lock, NULL);
}

unsigned CUstream_st::get_stream_id() {
  return this->stream_id;
}

bool CUstream_st::empty() {
  return this->operations.empty();
}

bool CUstream_st::busy() {
  return this->current_op_pending;
}

void CUstream_st::synchronize() {
  while (!this->empty()) 
  {
    sleep(1);
  }
    /* ; // spin wait for operations to empty */
}

void CUstream_st::push(const stream_operation &op) {
  if (op.is_done_once()) {
    this->one_time_only = 1;
  }
  pthread_mutex_lock(&this->stream_lock);
  this->operations.push_back(op);
  pthread_mutex_unlock(&this->stream_lock);
}

void CUstream_st::record_next_done() {
  // called by gpu thread
  pthread_mutex_lock(&this->stream_lock);
  assert(this->current_op_pending);
  this->operations.pop_front();
  this->current_op_pending= false;
  pthread_mutex_unlock(&this->stream_lock);
}

stream_operation CUstream_st::next() {
  // called by gpu thread
  this->current_op_pending = true;
  return this->operations.front();
}

void CUstream_st::print(FILE *fp) {
  pthread_mutex_lock(&this->stream_lock);
  fprintf(fp, "GPGPU-Sim API:    stream %u has %zu operations\n", this->stream_id,
      this->operations.size());
  unsigned n = 0;
  for (std::list<stream_operation>::iterator i = this->operations.begin();
      i != this->operations.end(); i++) {
    stream_operation &op = *i;
    fprintf(fp, "GPGPU-Sim API:       %u : ", n++);
    op.print(fp);
    fprintf(fp, "\n");
  }
  pthread_mutex_unlock(&this->stream_lock);
}

void stream_operation::do_operation(gpgpu_sim *gpu) {
  if (is_noop())
    return;

  assert(!m_done && m_stream);

  App* app = App::get_app(App::get_app_id(this->m_stream->get_stream_id()));

  if (g_debug_execution >= 3)
    printf("GPGPU-Sim API: stream %u performing ", m_stream->get_stream_id());
  switch (m_type) {
    case stream_memcpy_host_to_device:
      if (g_debug_execution >= 3) {
        printf("memcpy host-to-device\n");
      }
      app->mem_flag = true;
      gpu->memcpy_to_gpu(m_device_address_dst, m_host_address_src, m_cnt, app->appid);
      m_stream->record_next_done();
      break;
    case stream_memcpy_device_to_host:
      if (g_debug_execution >= 3) {
        printf("memcpy device-to-host\n");
      }
      app->mem_flag = true;
      gpu->memcpy_from_gpu(m_host_address_dst, m_device_address_src, m_cnt,
          app->appid);
      m_stream->record_next_done();
      break;
    case stream_memcpy_device_to_device:
      if (g_debug_execution >= 3) {
        printf("memcpy device-to-device\n");
      }
      app->mem_flag = true;
      gpu->memcpy_gpu_to_gpu(m_device_address_dst, m_device_address_src, m_cnt, app->appid);
      m_stream->record_next_done();
      break;
    case stream_memcpy_to_symbol:
      if (g_debug_execution >= 3) {
        printf("memcpy to symbol\n");
      }
      app->mem_flag = true;
      gpgpu_ptx_sim_memcpy_symbol(m_symbol, m_host_address_src, m_cnt, m_offset, 1, gpu);
      m_stream->record_next_done();
      break;
    case stream_memcpy_from_symbol:
      if (g_debug_execution >= 3) {
        printf("memcpy from symbol\n");
      }
      app->mem_flag = true;
      gpgpu_ptx_sim_memcpy_symbol(m_symbol, m_host_address_dst, m_cnt, m_offset, 0, gpu);
      m_stream->record_next_done();
      break;
    case stream_kernel_launch:
      app->mem_flag = false;
      if (gpu->can_start_kernel()) {
        gpu->set_cache_config(m_kernel->name());
        printf("kernel \'%s\' transfer to GPU hardware scheduler\n", m_kernel->name().c_str());
        if (m_sim_mode)
        {
          gpgpu_cuda_ptx_sim_main_func(*m_kernel);
        }
        else
        {
          printf("Launching GPGPU-sim for kernel %s \n", m_kernel->name().c_str());
          gpu->launch(m_kernel);
        }
      }
      break;
    case stream_event: {
      if (g_debug_execution >= 3) {
        printf("event update\n");
      }
      app->countevent++;
      bool set_flags = true;
      for (unsigned i = 0; i < ConfigOptions::n_apps; i++) {
        app = App::get_app(App::get_app_id(i));
        if (app->countevent <= 0) {
          set_flags = false;
          break;
        }
      }
      if (set_flags) {
        for (unsigned i = 0; i < ConfigOptions::n_apps; i++) {
          app = App::get_app(App::get_app_id(i));
          app->mem_flag = false;
        }
      }
      // the original logic here had tautologies
      // if exactly one app has count > 0
      std::vector<bool> over_one;
      for (unsigned i = 0; i < ConfigOptions::n_apps; i++) {
        app = App::get_app(App::get_app_id(i));
        over_one.push_back(app->countevent > 0);
      }
      unsigned stream_id = m_stream->get_stream_id();
      unsigned appid = App::get_app_id(stream_id);
      app = App::get_app(appid);

      if (count(over_one.begin(), over_one.end(), true) == 1) {
        app->stat_flag = true;
        gpu->app_cache_flush(stream_id);
        gpu->m_shared_tlb->flush(appid);
        gpu->flushL2(appid);
        printf("Flushing Caches -- %u\n", stream_id);
        std::stringstream output_file;
        output_file << "stream" << stream_id << ".txt";
        std::string ostr = output_file.str();
        output = freopen(ostr.c_str(), "w+", app->output);

        fprintf(output, "statistics from finished app %u\n", stream_id);
        fprintf(output, "-------------------------------------------------\n");
        gpu->gpu_print_stat_file(output);
        fflush(output);

        //pratheek
        //print only essential stats into essential.txt
        gpu->print_essential(stream_id);

        printf("COMPLETED\n");
        printf("total number of l1 accesses = %d\n", l1_cache_access);
        printf("total number of l1 accesses after tlb hit = %d\n",
            l1_cache_access_tlb_hit);
        printf("total number of coalesced accesses = %d\n", total_access_after_coalesce);
        printf("number of repeat accesses = %d\n", repeat_access);
        printf("number of cache reservation fails =%d\n", reservation_fail_count);
        printf("number of read mshr cache fails =%d\n", r_mshr_miss);
        printf("number of write mshr cache fails =%d\n", w_mshr_miss);
        printf("number of read other cache fails =%d\n", r_other_fail);
        printf("number of write other cache fails =%d\n", w_other_fail);
        for(int i = 0; i < 5; i++)
          printf("number of page walks depth %d returning =%d\n", i, page_walks_returning_depth[i]);
        printf("total number of tlb requests generated =%d\n", tlb_requests_generated);
        printf("total number of page walk mem_fetches generated =%d\n", page_walks_generated);
        printf("total number of page walk cache hits = %d\n", pw_cache_hits);
        printf("total number of page walk enquees= %d\n", pw_cache_latency_queue_pushes);
        printf("total tlb requests sent to memory = %d\n", tlb_requests_to_memory);
        printf("total tlb misses = %d\n", tlb_misses_counter);
        printf("last cycle page walk = %d\n", last_page_walk_return);

        printf("\n");
        printf("l2 pagetable hits = %d\n", tlb_related_l2_hit);
        printf("l2 pagetable miss = %d\n", tlb_related_l2_miss);
        printf("l2 pagetable mshr = %d\n", tlb_related_l2_mshr);
        printf("pte requests queued into l2 in total= %d\n", tlb_requests_into_l2);
        printf("pte requests queued into l2 from ROP= %d\n", tlb_requests_into_l2_rop);

        if(ConfigOptions::n_apps == 1)
        {
          exit(0);
        }

      } else {
        std::stringstream output_file;
        output_file << "stream" << stream_id << ".txt";
        std::string ostr = output_file.str();
        output = freopen(ostr.c_str(), "w+", app->output);

        fprintf(output, "statistics when all apps are finished %u\n", stream_id);
        fprintf(output, "-------------------------------------------------\n");

        gpu->gpu_print_stat_file(output);
        fflush(output);

        //pratheek
        //print only essential stats into essential.txt
        gpu->print_essential(stream_id);

        fclose(output);
        printf("BOTH APPS ARE FINISHED\n");
        abort();

      }

      time_t wallclock = time((time_t *) NULL);
      m_event->update(gpu_tot_sim_cycle, wallclock);
      m_stream->record_next_done();
    }
      break;
    default:
      abort();
  }
  m_done = true;
  fflush(stdout);
}

void stream_operation::print(FILE *fp) const {
  fprintf(fp, " stream operation ");
  switch (m_type) {
    case stream_event:
      fprintf(fp, "event");
      break;
    case stream_kernel_launch:
      fprintf(fp, "kernel");
      break;
    case stream_memcpy_device_to_device:
      fprintf(fp, "memcpy device-to-device");
      break;
    case stream_memcpy_device_to_host:
      fprintf(fp, "memcpy device-to-host");
      break;
    case stream_memcpy_host_to_device:
      fprintf(fp, "memcpy host-to-device");
      break;
    case stream_memcpy_to_symbol:
      fprintf(fp, "memcpy to symbol");
      break;
    case stream_memcpy_from_symbol:
      fprintf(fp, "memcpy from symbol");
      break;
    case stream_no_op:
      fprintf(fp, "no-op");
      break;
  }
}

stream_manager::stream_manager(gpgpu_sim *gpu, bool cuda_launch_blocking) {
  m_gpu = gpu;
  m_cuda_launch_blocking = cuda_launch_blocking;
  pthread_mutex_init(&m_lock, NULL);
}

bool stream_manager::operation(bool * sim) {
  pthread_mutex_lock(&m_lock);
  bool check = check_finished_kernel();
  if (check)
    m_gpu->print_stats();
  stream_operation op = front();
  op.do_operation(m_gpu);
  pthread_mutex_unlock(&m_lock);
  //pthread_mutex_lock(&m_lock);
  // simulate a clock cycle on the GPU
  return check;
}

bool stream_manager::check_finished_kernel() {

  unsigned grid_uid = m_gpu->finished_kernel();
  bool check = register_finished_kernel(grid_uid);
  return check;

}

bool stream_manager::register_finished_kernel(unsigned grid_uid) {
  // called by gpu simulation thread
  if (grid_uid > 0) {
    CUstream_st *stream = m_grid_id_to_stream[grid_uid];
    kernel_info_t *kernel = stream->front().get_kernel();
    assert(grid_uid == kernel->get_uid());
    stream->record_next_done();
    m_grid_id_to_stream.erase(grid_uid);
    delete kernel;
    return true;
  } else {
    return false;
  }
  return false;
}

stream_operation stream_manager::front() {
  // called by gpu simulation thread
  stream_operation result;
  std::list<struct CUstream_st*>::iterator s;
  for (s = m_streams.begin(); s != m_streams.end(); s++) {
    CUstream_st *stream = *s;
    if (!stream->busy() && !stream->empty()) {
      result = stream->next();
      if (result.is_kernel()) {
        unsigned grid_id = result.get_kernel()->get_uid();
        result.get_kernel()->set_stream_id(stream->get_stream_id());
        result.get_kernel()->set_done_id(stream->getoncedone());
        m_grid_id_to_stream[grid_id] = stream;
      }
      break;
    }
  }
  return result;
}

void stream_manager::add_stream(struct CUstream_st *stream) {
  // called by host thread
  pthread_mutex_lock(&m_lock);
  m_streams.push_back(stream);
  pthread_mutex_unlock(&m_lock);
}

void stream_manager::destroy_stream(CUstream_st *stream) {
  // called by host thread
  pthread_mutex_lock(&m_lock);
  while (!stream->empty())
    ;
  std::list<CUstream_st *>::iterator s;
  for (s = m_streams.begin(); s != m_streams.end(); s++) {
    if (*s == stream) {
      m_streams.erase(s);
      break;
    }
  }
  delete stream;
  pthread_mutex_unlock(&m_lock);
}

bool stream_manager::concurrent_streams_empty() {
  bool result = true;
  // called by gpu simulation thread
  std::list<struct CUstream_st *>::iterator s;
  pthread_mutex_lock(&m_lock);
  for (s = m_streams.begin(); s != m_streams.end(); ++s) {
    struct CUstream_st *stream = *s;
    if (!stream->empty()) {
      result = false;
    }
  }
  pthread_mutex_unlock(&m_lock);
  return result;
}

bool stream_manager::empty() {
  return concurrent_streams_empty();
}

void stream_manager::print(FILE *fp) {
  pthread_mutex_lock(&m_lock);
  print_impl(fp);
  pthread_mutex_unlock(&m_lock);
}

void stream_manager::print_impl(FILE *fp) {
  fprintf(fp, "GPGPU-Sim API: Stream Manager State\n");
  std::list<struct CUstream_st *>::iterator s;
  for (s = m_streams.begin(); s != m_streams.end(); ++s) {
    struct CUstream_st *stream = *s;
    if (!stream->empty())
      stream->print(fp);
  }
}

void stream_manager::push(stream_operation op) {
  struct CUstream_st *stream = op.get_stream();
  assert(stream && "Launched operation on null stream?????");
  // block if stream 0 (or concurrency disabled) and pending concurrent operations exist
  if (m_cuda_launch_blocking)
    while (!concurrent_streams_empty()) ; // spin waiting for empty

  pthread_mutex_lock(&m_lock);
  stream->push(op);

  if (g_debug_execution >= 3)
    print_impl(stdout);
  pthread_mutex_unlock(&m_lock);

  if (m_cuda_launch_blocking) {
    unsigned int wait_amount = 100;
    unsigned int wait_cap = 100000; // 100ms
    while (!empty()) {
      // sleep to prevent CPU hog by empty spin
      usleep(wait_amount);
      wait_amount *= 2;
      if (wait_amount > wait_cap)
        wait_amount = wait_cap;
    }
  }
}

