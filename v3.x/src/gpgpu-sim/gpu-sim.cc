// Copyright (c) 2009-2011, Tor M. Aamodt, Wilson W.L. Fung, George L. Yuan,
// Ali Bakhoda, Andrew Turner, Ivan Sham
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


#include "gpu-sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "zlib.h"


#include "shader.h"
#include "dram.h"
#include "mem_fetch.h"

#include <time.h>
#include "gpu-cache.h"
#include "gpu-misc.h"
#include "delayqueue.h"
#include "shader.h"
#include "icnt_wrapper.h"
#include "dram.h"
#include "addrdec.h"
#include "stat-tool.h"
#include "l2cache.h"

#include "../cuda-sim/ptx-stats.h"
#include "../statwrapper.h"
#include "../abstract_hardware_model.h"
#include "../debug.h"
#include "../gpgpusim_entrypoint.h"
#include "../cuda-sim/cuda-sim.h"
#include "../stream_manager.h"
#include "../trace.h"
#include "mem_latency_stat.h"
#include "power_stat.h"
#include "visualizer.h"
#include "stats.h"


#ifdef GPGPUSIM_POWER_MODEL
#include "power_interface.h"
#else
class  gpgpu_sim_wrapper {};
#endif

#include <stdio.h>
#include <string.h>
#include <iostream>
#include <sstream>
#include <string>

#include "App.h"

#define MAX(a,b) (((a)>(b))?(a):(b))

bool g_interactive_debugger_enabled = false;

extern mmu * g_mmu;


unsigned long long  gpu_sim_cycle = 0;
int count_tlp = 0;
unsigned long long  gpu_tot_sim_cycle = 0;



// performance counter for stalls due to congestion.
unsigned int gpu_stall_dramfull = 0;
unsigned int gpu_stall_icnt2sh = 0;
//new
int gpu_sms = 30;

int my_active_sms = 0;

/* Clock Domains */

#define  CORE  0x01
#define  L2    0x02
#define  DRAM  0x04
#define  ICNT  0x08
#define MEM_LATENCY_STAT_IMPL

#include "mem_latency_stat.h"

void power_config::reg_options(class OptionParser * opp)
{
  option_parser_register(opp, "-gpuwattch_xml_file", OPT_CSTR,
      &g_power_config_name, "GPUWattch XML file",
      "gpuwattch.xml");
  option_parser_register(opp, "-power_simulation_enabled", OPT_BOOL,
      &g_power_simulation_enabled, "Turn on power simulator (1=On, 0=Off)",
      "0");
  option_parser_register(opp, "-power_per_cycle_dump", OPT_BOOL,
      &g_power_per_cycle_dump, "Dump detailed power output each cycle",
      "0");
  // Output Data Formats
  option_parser_register(opp, "-power_trace_enabled", OPT_BOOL,
      &g_power_trace_enabled, "produce a file for the power trace (1=On, 0=Off)",
      "0");
  option_parser_register(opp, "-power_trace_zlevel", OPT_INT32,
      &g_power_trace_zlevel, "Compression level of the power trace output log (0=no comp, 9=highest)",
      "6");
  option_parser_register(opp, "-steady_power_levels_enabled", OPT_BOOL,
      &g_steady_power_levels_enabled, "produce a file for the steady power levels (1=On, 0=Off)",
      "0");
  option_parser_register(opp, "-steady_state_definition", OPT_CSTR,
      &gpu_steady_state_definition, "allowed deviation:number of samples",
      "8:4");
}

void memory_config::reg_options(class OptionParser * opp)
{
  option_parser_register(opp, "-gpgpu_dram_scheduler", OPT_INT32, &scheduler_type,
      "0 = fifo, 1 = FR-FCFS (defaul)", "1");
  option_parser_register(opp, "-gpgpu_dram_partition_queues", OPT_CSTR, &gpgpu_L2_queue_config,
      "i2$:$2d:d2$:$2i",
      "8:8:8:8");
  option_parser_register(opp, "-l2_ideal", OPT_BOOL, &l2_ideal,
      "Use a ideal L2 cache that always hit",
      "0");
  option_parser_register(opp, "-gpgpu_cache:dl2", OPT_CSTR, &m_L2_config.m_config_string,
      "unified banked L2 data cache config "
      " {<nsets>:<bsize>:<assoc>,<rep>:<wr>:<alloc>:<wr_alloc>,<mshr>:<N>:<merge>,<mq>}",
      "64:128:8,L:B:m:N,A:16:4,4");
  option_parser_register(opp, "-gpgpu_cache:dl2_texture_only", OPT_BOOL, &m_L2_texure_only,
      "L2 cache used for texture only",
      "1");
  option_parser_register(opp, "-gpgpu_n_mem", OPT_UINT32, &m_n_mem,
      "number of memory modules (e.g. memory controllers) in gpu",
      "8");
  option_parser_register(opp, "-gpgpu_n_sub_partition_per_mchannel", OPT_UINT32, &m_n_sub_partition_per_memory_channel,
      "number of memory subpartition in each memory module",
      "1");
  option_parser_register(opp, "-gpgpu_n_mem_per_ctrlr", OPT_UINT32, &gpu_n_mem_per_ctrlr,
      "number of memory chips per memory controller",
      "1");
  option_parser_register(opp, "-gpgpu_memlatency_stat", OPT_INT32, &gpgpu_memlatency_stat,
      "track and display latency statistics 0x2 enables MC, 0x4 enables queue logs",
      "0");
  option_parser_register(opp, "-gpgpu_frfcfs_dram_sched_queue_size", OPT_INT32, &gpgpu_frfcfs_dram_sched_queue_size,
      "0 = unlimited (default); # entries per chip",
      "0");
  option_parser_register(opp, "-gpgpu_dram_return_queue_size", OPT_INT32, &gpgpu_dram_return_queue_size,
      "0 = unlimited (default); # entries per chip",
      "0");
  option_parser_register(opp, "-gpgpu_num_groups", OPT_INT32, &gpgpu_num_groups, //new
      "number of containers (application equal partitions)",
      "2");
  option_parser_register(opp, "-gpgpu_dram_buswidth", OPT_UINT32, &busW,
      "default = 4 bytes (8 bytes per cycle at DDR)",
      "4");
  option_parser_register(opp, "-gpgpu_dram_burst_length", OPT_UINT32, &BL,
      "Burst length of each DRAM request (default = 4 data bus cycle)",
      "4");
  option_parser_register(opp, "-dram_data_command_freq_ratio", OPT_UINT32, &data_command_freq_ratio,
      "Frequency ratio between DRAM data bus and command bus (default = 2 times, i.e. DDR)",
      "2");
  option_parser_register(opp, "-gpgpu_dram_timing_opt", OPT_CSTR, &gpgpu_dram_timing_opt,
      "DRAM timing parameters = {nbk:tCCD:tRRD:tRCD:tRAS:tRP:tRC:CL:WL:tCDLR:tWR:nbkgrp:tCCDL:tRTPL}",
      "4:2:8:12:21:13:34:9:4:5:13:1:0:0");
  option_parser_register(opp, "-gpgpu_subarray_timing_opt", OPT_CSTR, &gpgpu_dram_subarray_timing_opt,
      "DRAM subarray timing parameters = {nsa:sCCD:sRRD:sRCD:sRAS:sRP:sRC:sCL:sWL:sCDLR:sWR:sCCDL:sRTPL}",
      "16:2:8:12:21:13:34:9:4:5:13:0:0");
  option_parser_register(opp, "-rop_latency", OPT_UINT32, &rop_latency,
      "ROP queue latency (default 85)",
      "85");
  option_parser_register(opp, "-dram_latency", OPT_UINT32, &dram_latency,
      "DRAM latency (default 30)",
      "30");

  //pratheek
  option_parser_register(opp, "-fixed_latency_enabled", OPT_BOOL, &fixed_latency_enabled,
      "Fixed DRAM latency", "false");
  option_parser_register(opp, "-l2_tlb_latency", OPT_INT32, &l2_tlb_latency,
      "Level 2 TLB latency", "10");
  option_parser_register(opp, "-l2_tlb_ports", OPT_INT32, &l2_tlb_ports,
      "Level 2 TLB ports", "4");

  option_parser_register(opp, "-gpu_char", OPT_INT32, &gpu_char,  //new
      "gpu char activated",
      "0");
  //Page/TLB studies
  option_parser_register(opp, "-get_shader_warp_stat", OPT_UINT32, &get_shader_avail_warp_stat,  //new
      "Collect number of schedulable warps statistics in shader.cc", "0");
  option_parser_register(opp, "-enable_PCIe", OPT_BOOL, &enable_PCIe,  //new
      "Enable PCIe latency (otherwise PCIe latency = 0)", "false");
  option_parser_register(opp, "-capture_VA", OPT_BOOL, &capture_VA,  //new
      "Tracing Virtual Address from the runs (true/false)", "false");
  option_parser_register(opp, "-va_trace_file", OPT_CSTR, &va_trace_file,  //new
      "Output file of the virtual address", "VA.trace");
  option_parser_register(opp, "-va_mask", OPT_CSTR, &va_mask,  //new
      "Mask of the virtual address for PT walk routine (should match with tlb_levels)", "11111222223333344444000000000000");
  option_parser_register(opp, "-pw_cache_enable", OPT_BOOL, &pw_cache_enable,  //new
      "Enabling PW cache (0 = false, 1 = true)", "0");
  option_parser_register(opp, "-enable_subarray", OPT_UINT32, &enable_subarray,  //new
      "Enabling Subarray (0 = false, 1 = true)", "0");
  option_parser_register(opp, "-channel_partition", OPT_UINT32, &channel_partition,  //new
      "Enabling Channel Partitioning (0 = false, 1 = policy 1, etc.)", "0");
  option_parser_register(opp, "-app1_channel", OPT_UINT32, &app1_channel,  //new
      "If Channel partitioning, initial app1 channel", "2");
  option_parser_register(opp, "-app2_channel", OPT_UINT32, &app2_channel,  //new
      "If Channel partitioning, initial app1 channel", "2");
  option_parser_register(opp, "-app3_channel", OPT_UINT32, &app3_channel,  //new
      "If Channel partitioning, initial app1 channel", "2");
  option_parser_register(opp, "-app1_bank", OPT_UINT32, &app1_bank,  //new
      "If Bank partitioning, initial app1 bank", "2");
  option_parser_register(opp, "-app2_bank", OPT_UINT32, &app2_bank,  //new
      "If Bank partitioning, initial app1 bank", "2");
  option_parser_register(opp, "-app3_bank", OPT_UINT32, &app3_bank,  //new
      "If Bank partitioning, initial app1 bank", "2");
  //Copy/Zero
  option_parser_register(opp, "-RC_enabled", OPT_UINT32, &RC_enabled,  //new
      "Enable Row Clone", "0");
  option_parser_register(opp, "-LISA_enabled", OPT_UINT32, &LISA_enabled,  //new
      "Enable LISA", "0");
  option_parser_register(opp, "-MASA_enabled", OPT_UINT32, &MASA_enabled,  //new
      "Enabling Subarray (0 = false, 1 = true)", "0");
  option_parser_register(opp, "-SALP_enabled", OPT_UINT32, &SALP_enabled,  //new
      "Enabling Subarray (0 = false, 1 = true)", "0");
  option_parser_register(opp, "-interSA_latency", OPT_UINT32, &interSA_latency,  //new
      "Inter subarray copy latency", "50");
  option_parser_register(opp, "-intraSA_latency", OPT_UINT32, &intraSA_latency,  //new
      "Intra subarray copy latency", "1000");
  option_parser_register(opp, "-lisa_latency", OPT_UINT32, &lisa_latency,  //new
      "Intra subarray copy latency using LISA", "100");
  option_parser_register(opp, "-RCintraSA_latency", OPT_UINT32, &RCintraSA_latency,  //new
      "Intra subarray copy latency using RC", "1000");
  option_parser_register(opp, "-RCzero_latency", OPT_UINT32, &RCzero_latency,  //new
      "Zero a page latency using RC", "100");
  option_parser_register(opp, "-zero_latency", OPT_UINT32, &zero_latency,  //new
      "Zeroing a page latency", "1000");
  option_parser_register(opp, "-interBank_latency", OPT_UINT32, &interBank_latency,  //new
      "Copy a page across bank", "1000");
  option_parser_register(opp, "-RCpsm_latency", OPT_UINT32, &RCpsm_latency,  //new
      "Copy a page across bank using RC psm", "1000");
  option_parser_register(opp, "-bank_partition", OPT_UINT32, &bank_partition,  //new
      "Enabling Bank Partitioning (0 = false, 1 = policy 1, etc.)", "0");
  option_parser_register(opp, "-subarray_partition", OPT_UINT32, &subarray_partition,  //new
      "Enabling Subarray Partitioning (0 = false, 1 = policy 1, etc.)", "0");
  option_parser_register(opp, "-pw_cache_latency", OPT_UINT32, &pw_cache_latency,  //new
      "PW cache latency", "10");
  option_parser_register(opp, "-pw_cache_num_ports", OPT_UINT32, &pw_cache_num_ports,  //new
      "Number of ports for the PW cache", "4");
  option_parser_register(opp, "-tlb_pw_cache_entries", OPT_UINT32, &tlb_pw_cache_entries,  //new
      "Number of PW cache entries", "64");
  option_parser_register(opp, "-tlb_pw_cache_ways", OPT_UINT32, &tlb_pw_cache_ways,  //new
      "Number of PW cache ways", "8");
  option_parser_register(opp, "-tlb_replacement_policy", OPT_UINT32, &tlb_replacement_policy,  //new
      "TLB Replacement policy (0 = LRU (default), 1 = WID based", "0");
  option_parser_register(opp, "-tlb_fixed_latency_enabled", OPT_UINT32, &tlb_fixed_latency_enabled,  //new
      "Enabling TLB Fixed latency instead of consecutive DRAM requests (0=disable, 1=enable (applies to TLB miss))", "0");
  option_parser_register(opp, "-tlb_fixed_latency", OPT_UINT32, &tlb_fixed_latency,  //new
      "If TLB latency is a fixed number, what is the latency (default = 100)", "100");

  //pratheek
  option_parser_register(opp, "-second_app_ideal_page_walk", OPT_BOOL, &second_app_ideal_page_walk,  //new
      "The second application has 1 cycle page walks, but DOES AFFECT TLBs", "false");

  option_parser_register(opp, "-epoch_file", OPT_CSTR, &epoch_file,  //new
      "Input page table trace", "epoch.trace");
  option_parser_register(opp, "-epoch_length", OPT_UINT32, &epoch_length,  //new
      "Stat collection epoch length", "10000");
  option_parser_register(opp, "-epoch_length", OPT_BOOL, &epoch_enabled,  //new
      "Stat collection epoch (true/false), default - true", "true");
  option_parser_register(opp, "-l2_tlb_ways", OPT_UINT32, &l2_tlb_ways,  //new
      "L2 TLB ways total (shared)", "16");
  option_parser_register(opp, "-l2data_tlb_way_reset", OPT_UINT32, &l2_tlb_way_reset,  //new
      "When will L2 data TLB way yield to normal data", "100000");
  option_parser_register(opp, "-tlb_cache_part", OPT_UINT32, &tlb_cache_part,  //new
      "L2 partitioning for TLB", "0");
  option_parser_register(opp, "-l2_tlb_entries", OPT_UINT32, &l2_tlb_entries,  //new
      "L2 TLB entires (total entries) (shared)", "1024");
  option_parser_register(opp, "-use_old_Alloc", OPT_UINT32, &use_old_Alloc,  //new
      "Using the old Allocator from memory_owner.cc", "0");
  option_parser_register(opp, "-tlb_high_prio_level", OPT_UINT32, &tlb_high_prio_level,  //new
      "At which level tlb gets more priority in DRAM", "2");
  option_parser_register(opp, "-tlb_dram_aware", OPT_UINT32, &tlb_dram_aware,  //new
      "DRAM treat tlb differently", "0");
  option_parser_register(opp, "-dram_switch_factor", OPT_UINT32, &dram_switch_factor,  //new
      "DRAM policy 5, how often apps prioritization is switched (random factor = this_factor/100 ops (bw_factor), default = 100)", "100");
  option_parser_register(opp, "-dram_switch_max", OPT_UINT32, &dram_switch_max,  //new
      "DRAM policy 5, maximum before DRAM sched switch the app, default = 1000)", "1000");
  option_parser_register(opp, "-dram_switch_threshold", OPT_UINT32, &dram_switch_threshold,  //new
      "DRAM policy 5, threshold before DRAM sched switch the app, default = 100)", "100");
  option_parser_register(opp, "-dram_high_prio_chance", OPT_UINT32, &dram_high_prio_chance,  //new
      "DRAM policy 6, how likely a data request goes into high prio queue (probability = concurrent_request/this number, default = 100)", "100");
  option_parser_register(opp, "-dram_scheduling_policy", OPT_UINT32, &dram_scheduling_policy,  //new
      "DRAM scheduling policy (new for MASK), 0 = FR-FCFS, 1=FCFS", "0");
  option_parser_register(opp, "-dram_always_prioritize_app", OPT_UINT32, &dram_always_prioritize_app,  //new
      "DRAM always put request from this app to high priority queue (only for tlb_dram_aware policy 4: first app = 0, second app = 1, etc.", "100");
  option_parser_register(opp, "-max_DRAM_high_prio_wait", OPT_UINT32, &max_DRAM_high_prio_wait,  //new
      "DRAM row coalescing for high_prio queue", "100");
  option_parser_register(opp, "-max_DRAM_high_prio_combo", OPT_UINT32, &max_DRAM_high_prio_combo,  //new
      "How many consecutive high prio requests are issued", "8");
  option_parser_register(opp, "-dram_batch", OPT_BOOL, &dram_batch,  //new
      "Batch high priority DRAM requests (true/false)", "false");
  option_parser_register(opp, "-page_transfer_time", OPT_UINT32, &page_transfer_time,  //new
      "PCIe latency to transfer a page (default = 1000)", "1000");
  option_parser_register(opp, "-tlb_size", OPT_UINT32, &tlb_size,  //new
      "Size of TLB per SM", "64");
  option_parser_register(opp, "-tlb_prio_max_level", OPT_UINT32, &tlb_prio_max_level,  //new
      "Max level of TLBs that gets high priority", "0");
  option_parser_register(opp, "-tlb_bypass_enabled", OPT_UINT32, &tlb_bypass_enabled,  //new
      "Bypass L2 Cache for TLB requests (0 = Disable, 1 = Static policy, 2 = dynamic policy using threshold)", "0");
  option_parser_register(opp, "-tlb_bypass_level", OPT_UINT32, &tlb_bypass_level,  //new
      "Bypass L2 cache for TLB level starting at N", "2");

  option_parser_register(opp, "-concurrent_page_walks", OPT_UINT32,
      &concurrent_page_walks,
      "max number of page walks that can be in flight at a time", "16");

  option_parser_register(opp, "-vm_config", OPT_UINT32, &vm_config,
      "Which Virtual Memory Configuration to Run: default = baseline", "0");

  option_parser_register(opp, "-enable_page_coalescing", OPT_UINT32, &enable_page_coalescing,  //new
      "Enable page coalescing (default = 0)", "0");
  option_parser_register(opp, "-enable_costly_coalesce", OPT_UINT32, &enable_costly_coalesce,  //new
      "Enable page coalescing even when there are pages from other app within the coalesce range (default = 0)", "0");
  option_parser_register(opp, "-page_size_list", OPT_CSTR, &page_size_list,  //new
      "List of differing page sizes (in bytes) (default = 2MB and 4KB page sizes)", "2097152:4096");
  option_parser_register(opp, "-DRAM_size", OPT_UINT32, &DRAM_size,  //new
      "Size of the DRAM", "3221225472");
  option_parser_register(opp, "-page_walk_queue_size", OPT_UINT32,
      &page_walk_queue_size,  //new
      "Size of the Page Walk Queue", "192");

  option_parser_register(opp, "-dwsp_queue_threshold", OPT_DOUBLE,
      &dwsp_queue_threshold, "Queue Threshold for using DWS++", "0.17");

  option_parser_register(opp, "-dwsp_occupancy_threshold", OPT_DOUBLE,
      &dwsp_occupancy_threshold, "occupancy Threshold for using DWS++", "0.7");

  option_parser_register(opp, "-dwsp_occupancy_threshold_1", OPT_DOUBLE,
      &dwsp_occupancy_threshold_1, "Occupancy Threshold for using DWS++", "0.4");
  option_parser_register(opp, "-dwsp_occupancy_threshold_2", OPT_DOUBLE,
      &dwsp_occupancy_threshold_2, "Occupancy Threshold for using DWS++", "0.6");
  option_parser_register(opp, "-dwsp_occupancy_threshold_3", OPT_DOUBLE,
      &dwsp_occupancy_threshold_3, "Occupancy Threshold for using DWS++", "0.8");
  option_parser_register(opp, "-dwsp_occupancy_threshold_4", OPT_DOUBLE,
      &dwsp_occupancy_threshold_4, "Occupancy Threshold for using DWS++", "0.9");
  option_parser_register(opp, "-dwsp_occupancy_threshold_5", OPT_DOUBLE,
      &dwsp_occupancy_threshold_5, "Occupancy Threshold for using DWS++", "1.0");

  option_parser_register(opp, "-diff_walks_threshold_1", OPT_DOUBLE,
      &diff_walks_threshold_1, "Occupancy Threshold for using DWS++", "1.5");
  option_parser_register(opp, "-diff_walks_threshold_2", OPT_DOUBLE,
      &diff_walks_threshold_2, "Occupancy Threshold for using DWS++", "2.0");
  option_parser_register(opp, "-diff_walks_threshold_3", OPT_DOUBLE,
      &diff_walks_threshold_3, "Occupancy Threshold for using DWS++", "3.0");
  option_parser_register(opp, "-diff_walks_threshold_4", OPT_DOUBLE,
      &diff_walks_threshold_4, "Occupancy Threshold for using DWS++", "4.0");
  option_parser_register(opp, "-diff_walks_threshold_5", OPT_DOUBLE,
      &diff_walks_threshold_5, "Occupancy Threshold for using DWS++", "9.0");

  option_parser_register(opp, "-stealing_freq_threshold", OPT_UINT32,
      &stealing_freq_threshold, "Stealing theshold", "1");

  option_parser_register(opp, "-stealing_latency_enabled", OPT_UINT32,
      &stealing_latency_enabled, "Fixed Stealing latency", "0");

  option_parser_register(opp, "-dwsp_epoch_length", OPT_UINT32,
      &dwsp_epoch_length, "dwsp_epoch_length (in page walks)", "200");

  option_parser_register(opp, "-mask_initial_tokens", OPT_UINT32,
      &mask_initial_tokens, "Initial number of tokens per application", "1000");
  option_parser_register(opp, "-mask_epoch_length", OPT_UINT32,
      &mask_epoch_length, "Epoch length for MASK", "10000");

  m_address_mapping.addrdec_setoption(opp);
}

void shader_core_config::reg_options(class OptionParser * opp)
{
  option_parser_register(opp, "-gpgpu_simd_model", OPT_INT32, &model,
      "1 = post-dominator", "1");
  option_parser_register(opp, "-gpgpu_shader_core_pipeline", OPT_CSTR, &gpgpu_shader_core_pipeline_opt,
      "shader core pipeline config, i.e., {<nthread>:<warpsize>}",
      "1024:32");
  option_parser_register(opp, "-gpgpu_tex_cache:l1", OPT_CSTR, &m_L1T_config.m_config_string,
      "per-shader L1 texture cache  (READ-ONLY) config "
      " {<nsets>:<bsize>:<assoc>,<rep>:<wr>:<alloc>:<wr_alloc>,<mshr>:<N>:<merge>,<mq>:<rf>}",
      "8:128:5,L:R:m:N,F:128:4,128:2");
  option_parser_register(opp, "-gpgpu_const_cache:l1", OPT_CSTR, &m_L1C_config.m_config_string,
      "per-shader L1 constant memory cache  (READ-ONLY) config "
      " {<nsets>:<bsize>:<assoc>,<rep>:<wr>:<alloc>:<wr_alloc>,<mshr>:<N>:<merge>,<mq>} ",
      "64:64:2,L:R:f:N,A:2:32,4");
  option_parser_register(opp, "-gpgpu_cache:il1", OPT_CSTR, &m_L1I_config.m_config_string,
      "shader L1 instruction cache config "
      " {<nsets>:<bsize>:<assoc>,<rep>:<wr>:<alloc>:<wr_alloc>,<mshr>:<N>:<merge>,<mq>} ",
      "4:256:4,L:R:f:N,A:2:32,4");
  option_parser_register(opp, "-gpgpu_cache:dl1", OPT_CSTR, &m_L1D_config.m_config_string,
      "per-shader L1 data cache config "
      " {<nsets>:<bsize>:<assoc>,<rep>:<wr>:<alloc>:<wr_alloc>,<mshr>:<N>:<merge>,<mq> | none}",
      "none");
  option_parser_register(opp, "-gpgpu_cache:dl1PrefL1", OPT_CSTR, &m_L1D_config.m_config_stringPrefL1,
      "per-shader L1 data cache config "
      " {<nsets>:<bsize>:<assoc>,<rep>:<wr>:<alloc>:<wr_alloc>,<mshr>:<N>:<merge>,<mq> | none}",
      "none");
  option_parser_register(opp, "-gpgpu_cache:dl1PreShared", OPT_CSTR, &m_L1D_config.m_config_stringPrefShared,
      "per-shader L1 data cache config "
      " {<nsets>:<bsize>:<assoc>,<rep>:<wr>:<alloc>:<wr_alloc>,<mshr>:<N>:<merge>,<mq> | none}",
      "none");
  option_parser_register(opp, "-gmem_skip_L1D", OPT_BOOL, &gmem_skip_L1D,
      "global memory access skip L1D cache (implements -Xptxas -dlcm=cg, default=no skip)",
      "0");
  option_parser_register(opp, "-gpgpu_perfect_mem", OPT_BOOL, &gpgpu_perfect_mem,
      "enable perfect memory mode (no cache miss)",
      "0");
  option_parser_register(opp, "-n_regfile_gating_group", OPT_UINT32, &n_regfile_gating_group,
      "group of lanes that should be read/written together)",
      "4");
  option_parser_register(opp, "-gpgpu_clock_gated_reg_file", OPT_BOOL, &gpgpu_clock_gated_reg_file,
      "enable clock gated reg file for power calculations",
      "0");
  option_parser_register(opp, "-gpgpu_clock_gated_lanes", OPT_BOOL, &gpgpu_clock_gated_lanes,
      "enable clock gated lanes for power calculations",
      "0");
  option_parser_register(opp, "-gpgpu_shader_registers", OPT_UINT32, &gpgpu_shader_registers,
      "Number of registers per shader core. Limits number of concurrent CTAs. (default 8192)",
      "8192");
  option_parser_register(opp, "-gpgpu_shader_cta", OPT_UINT32, &max_cta_per_core,
      "Maximum number of concurrent CTAs in shader (default 8)",
      "8");
  option_parser_register(opp, "-gpgpu_n_clusters", OPT_UINT32, &n_simt_clusters,
      "number of processing clusters",
      "10");
  option_parser_register(opp, "-gpgpu_n_cores_per_cluster", OPT_UINT32, &n_simt_cores_per_cluster,
      "number of simd cores per cluster",
      "3");
  option_parser_register(opp, "-gpgpu_n_cluster_ejection_buffer_size", OPT_UINT32, &n_simt_ejection_buffer_size,
      "number of packets in ejection buffer",
      "8");
  option_parser_register(opp, "-gpgpu_n_ldst_response_buffer_size", OPT_UINT32, &ldst_unit_response_queue_size,
      "number of response packets in ld/st unit ejection buffer",
      "2");
  option_parser_register(opp, "-gpgpu_shmem_size", OPT_UINT32, &gpgpu_shmem_size,
      "Size of shared memory per shader core (default 16kB)",
      "16384");
  option_parser_register(opp, "-gpgpu_shmem_size", OPT_UINT32, &gpgpu_shmem_sizeDefault,
      "Size of shared memory per shader core (default 16kB)",
      "16384");
  option_parser_register(opp, "-gpgpu_shmem_size_PrefL1", OPT_UINT32, &gpgpu_shmem_sizePrefL1,
      "Size of shared memory per shader core (default 16kB)",
      "16384");
  option_parser_register(opp, "-gpgpu_shmem_size_PrefShared", OPT_UINT32, &gpgpu_shmem_sizePrefShared,
      "Size of shared memory per shader core (default 16kB)",
      "16384");
  option_parser_register(opp, "-gpgpu_shmem_num_banks", OPT_UINT32, &num_shmem_bank,
      "Number of banks in the shared memory in each shader core (default 16)",
      "16");
  option_parser_register(opp, "-gpgpu_shmem_limited_broadcast", OPT_BOOL, &shmem_limited_broadcast,
      "Limit shared memory to do one broadcast per cycle (default on)",
      "1");
  option_parser_register(opp, "-gpgpu_shmem_warp_parts", OPT_INT32, &mem_warp_parts,
      "Number of portions a warp is divided into for shared memory bank conflict check ",
      "2");
  option_parser_register(opp, "-gpgpu_warpdistro_shader", OPT_INT32, &gpgpu_warpdistro_shader,
      "Specify which shader core to collect the warp size distribution from",
      "-1");
  option_parser_register(opp, "-gpgpu_warp_issue_shader", OPT_INT32, &gpgpu_warp_issue_shader,
      "Specify which shader core to collect the warp issue distribution from",
      "0");
  option_parser_register(opp, "-gpgpu_local_mem_map", OPT_BOOL, &gpgpu_local_mem_map,
      "Mapping from local memory space address to simulated GPU physical address space (default = enabled)",
      "1");
  option_parser_register(opp, "-gpgpu_num_reg_banks", OPT_INT32, &gpgpu_num_reg_banks,
      "Number of register banks (default = 8)",
      "8");
  option_parser_register(opp, "-gpgpu_reg_bank_use_warp_id", OPT_BOOL, &gpgpu_reg_bank_use_warp_id,
      "Use warp ID in mapping registers to banks (default = off)",
      "0");
  option_parser_register(opp, "-gpgpu_operand_collector_num_units_sp", OPT_INT32, &gpgpu_operand_collector_num_units_sp,
      "number of collector units (default = 4)",
      "4");
  option_parser_register(opp, "-gpgpu_operand_collector_num_units_sfu", OPT_INT32, &gpgpu_operand_collector_num_units_sfu,
      "number of collector units (default = 4)",
      "4");
  option_parser_register(opp, "-gpgpu_operand_collector_num_units_mem", OPT_INT32, &gpgpu_operand_collector_num_units_mem,
      "number of collector units (default = 2)",
      "2");
  option_parser_register(opp, "-gpgpu_operand_collector_num_units_gen", OPT_INT32, &gpgpu_operand_collector_num_units_gen,
      "number of collector units (default = 0)",
      "0");
  option_parser_register(opp, "-gpgpu_operand_collector_num_in_ports_sp", OPT_INT32, &gpgpu_operand_collector_num_in_ports_sp,
      "number of collector unit in ports (default = 1)",
      "1");
  option_parser_register(opp, "-gpgpu_operand_collector_num_in_ports_sfu", OPT_INT32, &gpgpu_operand_collector_num_in_ports_sfu,
      "number of collector unit in ports (default = 1)",
      "1");
  option_parser_register(opp, "-gpgpu_operand_collector_num_in_ports_mem", OPT_INT32, &gpgpu_operand_collector_num_in_ports_mem,
      "number of collector unit in ports (default = 1)",
      "1");
  option_parser_register(opp, "-gpgpu_operand_collector_num_in_ports_gen", OPT_INT32, &gpgpu_operand_collector_num_in_ports_gen,
      "number of collector unit in ports (default = 0)",
      "0");
  option_parser_register(opp, "-gpgpu_operand_collector_num_out_ports_sp", OPT_INT32, &gpgpu_operand_collector_num_out_ports_sp,
      "number of collector unit in ports (default = 1)",
      "1");
  option_parser_register(opp, "-gpgpu_operand_collector_num_out_ports_sfu", OPT_INT32, &gpgpu_operand_collector_num_out_ports_sfu,
      "number of collector unit in ports (default = 1)",
      "1");
  option_parser_register(opp, "-gpgpu_operand_collector_num_out_ports_mem", OPT_INT32, &gpgpu_operand_collector_num_out_ports_mem,
      "number of collector unit in ports (default = 1)",
      "1");
  option_parser_register(opp, "-gpgpu_operand_collector_num_out_ports_gen", OPT_INT32, &gpgpu_operand_collector_num_out_ports_gen,
      "number of collector unit in ports (default = 0)",
      "0");
  option_parser_register(opp, "-gpgpu_coalesce_arch", OPT_INT32, &gpgpu_coalesce_arch,
      "Coalescing arch (default = 13, anything else is off for now)",
      "13");
  option_parser_register(opp, "-gpgpu_num_sched_per_core", OPT_INT32, &gpgpu_num_sched_per_core,
      "Number of warp schedulers per core",
      "1");
  option_parser_register(opp, "-gpgpu_max_insn_issue_per_warp", OPT_INT32, &gpgpu_max_insn_issue_per_warp,
      "Max number of instructions that can be issued per warp in one cycle by scheduler",
      "2");
  option_parser_register(opp, "-gpgpu_simt_core_sim_order", OPT_INT32, &simt_core_sim_order,
      "Select the simulation order of cores in a cluster (0=Fix, 1=Round-Robin)",
      "1");
  option_parser_register(opp, "-gpgpu_pipeline_widths", OPT_CSTR, &pipeline_widths_string,
      "Pipeline widths "
      "ID_OC_SP,ID_OC_SFU,ID_OC_MEM,OC_EX_SP,OC_EX_SFU,OC_EX_MEM,EX_WB",
      "1,1,1,1,1,1,1");
  option_parser_register(opp, "-gpgpu_num_sp_units", OPT_INT32, &gpgpu_num_sp_units,
      "Number of SP units (default=1)",
      "1");
  option_parser_register(opp, "-gpgpu_num_sfu_units", OPT_INT32, &gpgpu_num_sfu_units,
      "Number of SF units (default=1)",
      "1");
  option_parser_register(opp, "-gpgpu_num_mem_units", OPT_INT32, &gpgpu_num_mem_units,
      "Number if ldst units (default=1) WARNING: not hooked up to anything",
      "1");
  option_parser_register(opp, "-gpgpu_scheduler", OPT_CSTR, &gpgpu_scheduler_string,
      "Scheduler configuration: < lrr | gto | two_level_active > "
      "If two_level_active:<num_active_warps>:<inner_prioritization>:<outer_prioritization>"
      "For complete list of prioritization values see shader.h enum scheduler_prioritization_type"
      "Default: gto",
      "gto");
}

void gpgpu_sim_config::reg_options(option_parser_t opp)
{
  gpgpu_functional_sim_config::reg_options(opp);
  m_shader_config.reg_options(opp);
  m_memory_config.reg_options(opp);
  power_config::reg_options(opp);
  option_parser_register(opp, "-gpgpu_max_cycle", OPT_INT32, &gpu_max_cycle_opt,
      "terminates gpu simulation early (0 = no limit)",
      "0");
  option_parser_register(opp, "-gpgpu_max_insn", OPT_INT32, &gpu_max_insn_opt,
      "terminates gpu simulation early (0 = no limit)",
      "1000000000"); //500M
  option_parser_register(opp, "-gpgpu_max_cta", OPT_INT32, &gpu_max_cta_opt,
      "terminates gpu simulation early (0 = no limit)",
      "0");
  option_parser_register(opp, "-gpgpu_runtime_stat", OPT_CSTR, &gpgpu_runtime_stat,
      "display runtime statistics such as dram utilization {<freq>:<flag>}",
      "10000:0");
  option_parser_register(opp, "-liveness_message_freq", OPT_INT64, &liveness_message_freq,
      "Minimum number of seconds between simulation liveness messages (0 = always print)",
      "1");
  option_parser_register(opp, "-gpgpu_flush_l1_cache", OPT_BOOL, &gpgpu_flush_l1_cache,
      "Flush L1 cache at the end of each kernel call",
      "0");
  option_parser_register(opp, "-gpgpu_flush_l2_cache", OPT_BOOL, &gpgpu_flush_l2_cache,
      "Flush L2 cache at the end of each kernel call",
      "0");
  option_parser_register(opp, "-gpgpu_deadlock_detect", OPT_BOOL, &gpu_deadlock_detect,
      "Stop the simulation at deadlock (1=on (default), 0=off)",
      "1");
  option_parser_register(opp, "-gpgpu_ptx_instruction_classification", OPT_INT32,
      &gpgpu_ptx_instruction_classification,
      "if enabled will classify ptx instruction types per kernel (Max 255 kernels now)",
      "0");
  option_parser_register(opp, "-gpgpu_ptx_sim_mode", OPT_INT32, &g_ptx_sim_mode,
      "Select between Performance (default) or Functional simulation (1)",
      "0");
  option_parser_register(opp, "-gpgpu_clock_domains", OPT_CSTR, &gpgpu_clock_domains,
      "Clock Domain Frequencies in MhZ {<Core Clock>:<ICNT Clock>:<L2 Clock>:<DRAM Clock>}",
      "500.0:2000.0:2000.0:2000.0");
  option_parser_register(opp, "-gpgpu_max_concurrent_kernel", OPT_INT32, &max_concurrent_kernel,
      "maximum kernels that can run concurrently on GPU", "8");
  option_parser_register(opp, "-gpgpu_cflog_interval", OPT_INT32, &gpgpu_cflog_interval,
      "Interval between each snapshot in control flow logger",
      "0");
  option_parser_register(opp, "-visualizer_enabled", OPT_BOOL,
      &g_visualizer_enabled, "Turn on visualizer output (1=On, 0=Off)",
      "1");
  option_parser_register(opp, "-visualizer_outputfile", OPT_CSTR,
      &g_visualizer_filename, "Specifies the output log file for visualizer",
      NULL);
  option_parser_register(opp, "-visualizer_zlevel", OPT_INT32,
      &g_visualizer_zlevel, "Compression level of the visualizer output log (0=no comp, 9=highest)",
      "6");
  option_parser_register(opp, "-trace_enabled", OPT_BOOL,
      &Trace::enabled, "Turn on traces",
      "0");
  option_parser_register(opp, "-trace_components", OPT_CSTR,
      &Trace::config_str, "comma seperated list of traces to enable. "
      "Complete list found in trace_streams.tup. "
      "Default none",
      "none");
  option_parser_register(opp, "-trace_sampling_core", OPT_INT32,
      &Trace::sampling_core, "The core which is printed using CORE_DPRINTF. Default 0",
      "0");
  option_parser_register(opp, "-trace_sampling_memory_partition", OPT_INT32,
      &Trace::sampling_memory_partition, "The memory partition which is printed using MEMPART_DPRINTF. Default -1 (i.e. all)",
      "-1");
  ptx_file_line_stats_options(opp);
}

/////////////////////////////////////////////////////////////////////////////

void increment_x_then_y_then_z(dim3 &i, const dim3 &bound)
{
  i.x++;
  if (i.x >= bound.x) {
    i.x = 0;
    i.y++;
    if (i.y >= bound.y) {
      i.y = 0;
      if (i.z < bound.z)
        i.z++;
    }
  }
}

void gpgpu_sim::launch(kernel_info_t *kinfo)
{
  unsigned cta_size = kinfo->threads_per_cta();
  if (cta_size > m_shader_config->n_thread_per_shader) {
    printf("Execution error: Shader kernel CTA (block) size is too large for microarch config.\n");
    printf("                 CTA size (x*y*z) = %u, max supported = %u\n", cta_size,
        m_shader_config->n_thread_per_shader);
    printf("                 => either change -gpgpu_shader argument in gpgpusim.config file or\n");
    printf("                 modify the CUDA source to decrease the kernel block size.\n");
    abort();
  }
  unsigned n = 0;
  for (n = 0; n < m_running_kernels.size(); n++) {
    printf("GPGPU-sim: Launching Kernel %s : n = %u. Running kernel size = %d", kinfo->name().c_str(), n, m_running_kernels.size());
    if ((NULL == m_running_kernels[n]) || m_running_kernels[n]->done()) {
      m_running_kernels[n] = kinfo;
      break;
    }
  }
  assert(n < m_running_kernels.size());
}

bool gpgpu_sim::can_start_kernel()
{
  for (unsigned n = 0; n < m_running_kernels.size(); n++) {
    if ((NULL == m_running_kernels[n]) || m_running_kernels[n]->done())
      return true;
  }
  return false;
}

bool gpgpu_sim::get_more_cta_left() const
{
  if (m_config.gpu_max_cta_opt != 0) {
    if (m_total_cta_launched >= m_config.gpu_max_cta_opt)
      return false;
  }
  for (unsigned n = 0; n < m_running_kernels.size(); n++) {
    if (m_running_kernels[n] && !m_running_kernels[n]->no_more_ctas_to_run())
      return true;
  }
  return false;
}

kernel_info_t *gpgpu_sim::select_kernel(unsigned sid)
{
  for (unsigned n = 0; n < m_running_kernels.size(); n++) {
    unsigned idx = (n + m_last_issued_kernel + 1) % m_config.max_concurrent_kernel;
    if (m_running_kernels[idx] && !m_running_kernels[idx]->no_more_ctas_to_run()) {
      if(App::get_app_id(m_running_kernels[idx]->get_stream_id()) == App::get_app_id_from_sm(sid))
      {
        m_last_issued_kernel = idx;
        // record this kernel for stat print if it is the first time this kernel is selected for execution
        unsigned launch_uid = m_running_kernels[idx]->get_uid();
        if (std::find(m_executed_kernel_uids.begin(), m_executed_kernel_uids.end(), launch_uid) == m_executed_kernel_uids.end()) {
          m_executed_kernel_uids.push_back(launch_uid);
          m_executed_kernel_names.push_back(m_running_kernels[idx]->name());
        }

        /* If all the apps have ready kernels, only then issue the kernel */
        if(app_ready_sem == false)
        {
          app_ready[App::get_app_id_from_sm(sid)] = true;
          for(int i = 0; i < ConfigOptions::n_apps; i++)
          {
            if(app_ready[App::get_app_id(i)] == false || gpu_sim_cycle < 1000)
              /* if(app_ready[App::get_app_id(i)] == false) */
              return NULL;
          }
          app_ready_sem = true;
          return m_running_kernels[idx];;
        }
        return m_running_kernels[idx];
      }
    }
  }
  return NULL;
}

unsigned gpgpu_sim::finished_kernel()
{
  if (m_finished_kernel.empty())
    return 0;
  unsigned result = m_finished_kernel.front();
  m_finished_kernel.pop_front();
  return result;
}

void gpgpu_sim::set_kernel_done(kernel_info_t *kernel)
{
  App* app = App::get_app(App::get_app_id(kernel->get_stream_id()));
  app->mem_flag = true;
  unsigned uid = kernel->get_uid();
  m_finished_kernel.push_back(uid);
  std::vector<kernel_info_t*>::iterator k;
  for (k = m_running_kernels.begin(); k != m_running_kernels.end(); k++) {
    if (*k == kernel) {
      *k = NULL;
      break;
    }
  }
  assert(k != m_running_kernels.end());
}

void set_ptx_warp_size(const struct core_config * warp_size);

gpgpu_sim::gpgpu_sim(const gpgpu_sim_config &config)
  : gpgpu_t(config), m_config(config), m_memory_config(&config.m_memory_config),
    m_shader_config(&config.m_shader_config)
{
  // set groups based on n_apps
  set_ptx_warp_size(m_shader_config);
  ptx_file_line_stats_create_exposed_latency_tracker(m_config.num_shader());

  printf("Initializing shader stats\n");
  m_shader_stats = new shader_core_stats(m_shader_config);
  printf("Initializing memory stats\n");
  m_memory_stats = new memory_stats_t(m_config.num_shader(), m_shader_config, m_memory_config);

  printf("Initializing MMU\n");
  m_page_manager = g_mmu;

  //FIXME: Check this
  if (m_page_manager->need_init)
    m_page_manager->init2(m_memory_config);

  m_page_manager->set_stat(m_memory_stats);
  // call init next
}

int gpgpu_sim::shared_mem_size() const
{
  return m_shader_config->gpgpu_shmem_size;
}

int gpgpu_sim::num_registers_per_core() const
{
  return m_shader_config->gpgpu_shader_registers;
}

int gpgpu_sim::wrp_size() const
{
  return m_shader_config->warp_size;
}

int gpgpu_sim::shader_clock() const
{
  return m_config.core_freq / 1000;
}

void gpgpu_sim::set_prop(cudaDeviceProp *prop)
{
  m_cuda_properties = prop;
}

const struct cudaDeviceProp *gpgpu_sim::get_prop() const
{
  return m_cuda_properties;
}

enum divergence_support_t gpgpu_sim::simd_model() const
{
  return m_shader_config->model;
}

void gpgpu_sim_config::init_clock_domains(void)
{
  sscanf(gpgpu_clock_domains, "%lf:%lf:%lf:%lf",
      &core_freq, &icnt_freq, &l2_freq, &dram_freq);
  core_freq = core_freq MhZ;
  icnt_freq = icnt_freq MhZ;
  l2_freq = l2_freq MhZ;
  dram_freq = dram_freq MhZ;
  core_period = 1 / core_freq;
  icnt_period = 1 / icnt_freq;
  dram_period = 1 / dram_freq;
  l2_period = 1 / l2_freq;
  printf("GPGPU-Sim uArch: clock freqs: %lf:%lf:%lf:%lf\n", core_freq, icnt_freq, l2_freq, dram_freq);
  printf("GPGPU-Sim uArch: clock periods: %.20lf:%.20lf:%.20lf:%.20lf\n", core_period, icnt_period, l2_period, dram_period);
}

void gpgpu_sim::reinit_clock_domains(void)
{
  core_time = 0;
  dram_time = 0;
  icnt_time = 0;
  l2_time = 0;
}

bool gpgpu_sim::active()
{

  if (m_config.gpu_max_cycle_opt && (gpu_tot_sim_cycle + gpu_sim_cycle) >= m_config.gpu_max_cycle_opt)
    return false;
  //if (m_config.gpu_max_insn_opt && (gpu_tot_sim_insn + gpu_sim_insn) >= m_config.gpu_max_insn_opt)
  //return false;
  if (m_config.gpu_max_cta_opt && (gpu_tot_issued_cta >= m_config.gpu_max_cta_opt))
    return false;
  if (m_config.gpu_deadlock_detect && gpu_deadlock)
    return false;
  for (unsigned i = 0; i < m_shader_config->n_simt_clusters; i++)
    if (m_cluster[i]->get_not_completed() > 0)
      return true;;
  for (unsigned i = 0; i < m_memory_config->m_n_mem; i++)
    if (m_memory_partition_unit[i]->busy() > 0)
      return true;;
  if (icnt_busy())
    return true;
  if (get_more_cta_left())
    return true;
  return false;
}

void gpgpu_sim::init()
{

  static bool init_done = false;
  if(init_done)
  {
    return;
  }
  else
  {
    init_done = true;
  }

  //clear the essential stats file
  std::stringstream output_file;
  output_file << "essential.txt";
  std::string ostr = output_file.str();
  FILE* fout = fopen(ostr.c_str(), "w+");
  fclose(fout);

  printf("Initializing GPGPU-sim\n");
  for (unsigned i = 0; i < ConfigOptions::n_apps; i++) {
    std::stringstream fname;
    fname << "stream" << i << ".txt";
    if(!App::is_registered(i)){
        printf("GPGPU-sim INIT routine: Registering app %d", i);
        App::register_app(i); //This is needed for streams that are > 0. Stream 0 causes gpgpu-sim to initize itself. 
    }
    printf("GPGPU-sim INIT routine: Creating app with ID = %d, app name = %s\n", i, fname.str().c_str());
    appid_t appid = App::create_app(App::get_app_id(i), fopen(fname.str().c_str(), "w"), m_shader_config->warp_size);
    //appid_t appid = App::create_app(i, fopen(fname.str().c_str(), "w"), m_shader_config->warp_size);
    // static partitioning of sms
    unsigned sms_per_app = m_config.num_shader() / ConfigOptions::n_apps;
    std::vector<int> sms;
    if (i == ConfigOptions::n_apps - 1) { // give the last app all the remaining sms
      for (unsigned j = sms_per_app * i; j < m_config.num_shader(); j++) {
        sms.push_back(j);
      }
    } else {
      for (unsigned j = sms_per_app * i; j < sms_per_app * (i + 1); j++) {
        sms.push_back(j);
      }
    }
    App::set_app_sms(appid, sms);
  }

   //Pratheek: try start apps on the same cycle
   for(int i = 0; i < ConfigOptions::n_apps; i++)
   {
     app_ready[App::get_app_id(i)] = false;
   }
   app_ready_sem = false;

  m_memory_stats->init();
  printf("Done initializing memory stats in GPGPU-sim. Initializing shader stats\n");
  m_shader_stats->init();

  average_pipeline_duty_cycle = (float *)malloc(sizeof(float));
  active_sms = (float *)malloc(sizeof(float));
  m_power_stats = new power_stat_t(m_shader_config, average_pipeline_duty_cycle, active_sms, m_shader_stats, m_memory_config, m_memory_stats);

  gpu_sim_insn = 0;
  gpu_tot_sim_insn = 0;
  gpu_tot_issued_cta = 0;
  gpu_deadlock = false;



  max_insn_struck = false; // important
  for (unsigned i = 0; i < m_config.num_shader(); i++)  {
    gpu_sim_insn_per_core[i] = 0;
    gpu_sim_last_cycle_per_core[i] = 0;
    gpu_sim_first_cycle_per_core[i] = 0;
  }

  //m_page_manager = new mmu(m_memory_config);
  printf("Done initializing shader stat. Initializing shared TLB\n");
  // Add a pointer to memory_partition unit so that tlb can insert DRAM copy/zero commands
  m_shared_tlb = new tlb_tag_array(m_memory_config, m_shader_stats, m_page_manager, true, m_memory_stats, m_memory_partition_unit);
  //m_shared_tlb = new tlb_tag_array(m_memory_config, m_shader_stats, m_page_manager, true);
  printf("Done initializing shared TLB\n");




  m_cluster = new simt_core_cluster*[m_shader_config->n_simt_clusters];
  for (unsigned i = 0; i < m_shader_config->n_simt_clusters; i++)
    m_cluster[i] = new simt_core_cluster(this, i, m_shader_config, m_memory_config, m_shader_stats, m_memory_stats, m_page_manager, m_shared_tlb);

  m_memory_partition_unit = new memory_partition_unit*[m_memory_config->m_n_mem];
  m_memory_sub_partition = new memory_sub_partition*[m_memory_config->m_n_mem_sub_partition];
  for (unsigned i = 0; i < m_memory_config->m_n_mem; i++) {
    m_memory_partition_unit[i] = new memory_partition_unit(i, m_memory_config, m_memory_stats, m_page_manager, m_shared_tlb);
    for (unsigned p = 0; p < m_memory_config->m_n_sub_partition_per_memory_channel; p++) {
      unsigned submpid = i * m_memory_config->m_n_sub_partition_per_memory_channel + p;
      m_memory_sub_partition[submpid] = m_memory_partition_unit[i]->get_sub_partition(p);
    }
  }

  icnt_wrapper_init();
  icnt_create(m_shader_config->n_simt_clusters, m_memory_config->m_n_mem_sub_partition);


  //FIXME: Check this
  if (m_page_manager->need_init)
    m_page_manager->set_ready();



  time_vector_create(NUM_MEM_REQ_STAT);
  fprintf(stdout, "GPGPU-Sim uArch: performance model initialization complete.\n");

  m_running_kernels.resize(m_config.max_concurrent_kernel, NULL);
  m_last_issued_kernel = -1; //0
  m_last_cluster_issue = -1; //0
  *average_pipeline_duty_cycle = 0;
  *active_sms = 0;

  last_liveness_message_time = 0;
  // run a CUDA grid on the GPU microarchitecture simulator
  gpu_sim_cycle = 0;
  gpu_sim_insn = 0;
  last_gpu_sim_insn = 0;
  m_total_cta_launched = 0;
  count_tlp = 0;
  gpu_sms = m_config.num_shader();

  reinit_clock_domains();
  set_param_gpgpu_num_shaders(m_config.num_shader());
  for (unsigned i = 0; i < m_shader_config->n_simt_clusters; i++)
    m_cluster[i]->reinit();
  m_shader_stats->new_grid();
  // initialize the control-flow, memory access, memory latency logger
  if (m_config.g_visualizer_enabled) {
    create_thread_CFlogger(m_config.num_shader(), m_shader_config->n_thread_per_shader, 0, m_config.gpgpu_cflog_interval);
  }
  shader_CTA_count_create(m_config.num_shader(), m_config.gpgpu_cflog_interval);
  if (m_config.gpgpu_cflog_interval != 0) {
    insn_warp_occ_create(m_config.num_shader(), m_shader_config->warp_size);
    shader_warp_occ_create(m_config.num_shader(), m_shader_config->warp_size, m_config.gpgpu_cflog_interval);
    shader_mem_acc_create(m_config.num_shader(), m_memory_config->m_n_mem, 4, m_config.gpgpu_cflog_interval);
    shader_mem_lat_create(m_config.num_shader(), m_config.gpgpu_cflog_interval);
    shader_cache_access_create(m_config.num_shader(), 3, m_config.gpgpu_cflog_interval);
    set_spill_interval(m_config.gpgpu_cflog_interval * 40);
  }

  if (g_network_mode)
    icnt_init();

  // McPAT initialization function. Called on first launch of GPU
#ifdef GPGPUSIM_POWER_MODEL
  if (m_config.g_power_simulation_enabled) {
    //init_mcpat(m_config, m_gpgpusim_wrapper, m_config.gpu_stat_sample_freq,  gpu_tot_sim_insn, gpu_sim_insn);
  }
#endif
}

void gpgpu_sim::update_stats() {
  m_memory_stats->memlatstat_lat_pw();
  gpu_tot_sim_cycle += gpu_sim_cycle;
  gpu_tot_sim_insn += gpu_sim_insn;
}

void gpgpu_sim::update_stats_lite() {
  m_memory_stats->memlatstat_lat_pw();
}

void gpgpu_sim::print_stats()
{
  ptx_file_line_stats_write_file();
  gpu_print_stat_file(stdout);

  /* //pratheek */
  /* //print only essential stats into essential.txt */
  /* print_essential(); */

  if (g_network_mode) {
    printf("----------------------------Interconnect-DETAILS--------------------------------\n");
    icnt_display_stats();
    icnt_display_overall_stats();
    printf("----------------------------END-of-Interconnect-DETAILS-------------------------\n");
  }
}

void gpgpu_sim::deadlock_check()
{

  if (m_config.gpu_deadlock_detect && gpu_deadlock) {
    fflush(stdout);
    printf("\n\nGPGPU-Sim uArch: ERROR ** deadlock detected: last writeback core %u @ gpu_sim_cycle %u (+ gpu_tot_sim_cycle %u) (%u cycles ago)\n",
        gpu_sim_insn_last_update_sid,
        (unsigned) gpu_sim_insn_last_update, (unsigned)(gpu_tot_sim_cycle - gpu_sim_cycle),
        (unsigned)(gpu_sim_cycle - gpu_sim_insn_last_update));
    unsigned num_cores = 0;
    for (unsigned i = 0; i < m_shader_config->n_simt_clusters; i++) {
      unsigned not_completed = m_cluster[i]->get_not_completed();
      if (not_completed) {
        if (!num_cores)  {
          printf("GPGPU-Sim uArch: DEADLOCK  shader cores no longer committing instructions [core(# threads)]:\n");
          printf("GPGPU-Sim uArch: DEADLOCK  ");
          m_cluster[i]->print_not_completed(stdout);
        } else if (num_cores < 8) {
          m_cluster[i]->print_not_completed(stdout);
        } else if (num_cores >= 8) {
          printf(" + others ... ");
        }
        num_cores += m_shader_config->n_simt_cores_per_cluster;
      }
    }
    printf("\n");
    for (unsigned i = 0; i < m_memory_config->m_n_mem; i++) {
      bool busy = m_memory_partition_unit[i]->busy();
      if (busy)
        printf("GPGPU-Sim uArch DEADLOCK:  memory partition %u busy\n", i);
    }
    if (icnt_busy()) {
      printf("GPGPU-Sim uArch DEADLOCK:  iterconnect contains traffic\n");
      icnt_display_state(stdout);
    }
    printf("\nRe-run the simulator in gdb and use debug routines in .gdbinit to debug this\n");
    fflush(stdout);
    abort();
  }
  if (m_config.gpu_max_insn_opt && max_insn_struck) {
    print_stats();
    printf("GPGPU-Sim uArch MAX INSTRUCTIONS STRUCK\n");
    abort();
  }
}

/**
 * Flush the cache of app i.
 * This appears to index the apps starting at 1.
 */
void gpgpu_sim::app_cache_flush(int i) {
  unsigned cluster_size = m_memory_config->gpu_sms / ConfigOptions::n_apps;
  for (unsigned j = (i - 1) * cluster_size; j < i * cluster_size && j < gpu_sms; j++) {
    m_cluster[j]->cache_flush();
  }
}

unsigned long long gpgpu_sim::get_gpu_insn_max() {
  return m_config.gpu_max_insn_opt;
}

/// printing the names and uids of a set of executed kernels (usually there is only one)
std::string gpgpu_sim::executed_kernel_info_string()
{
  std::stringstream statout;

  statout << "kernel_name = ";
  for (unsigned int k = 0; k < m_executed_kernel_names.size(); k++) {
    statout << m_executed_kernel_names[k] << " ";
  }
  statout << std::endl;
  statout << "kernel_launch_uid = ";
  for (unsigned int k = 0; k < m_executed_kernel_uids.size(); k++) {
    statout << m_executed_kernel_uids[k] << " ";
  }
  statout << std::endl;

  return statout.str();
}

void gpgpu_sim::set_cache_config(std::string kernel_name,  FuncCache cacheConfig)
{
  m_special_cache_config[kernel_name] = cacheConfig ;
}

FuncCache gpgpu_sim::get_cache_config(std::string kernel_name)
{
  for (std::map<std::string, FuncCache>::iterator iter = m_special_cache_config.begin(); iter != m_special_cache_config.end(); iter++) {
    std::string kernel = iter->first;
    if (kernel_name.compare(kernel) == 0) {
      return iter->second;
    }
  }
  return (FuncCache)0;
}

bool gpgpu_sim::has_special_cache_config(std::string kernel_name)
{
  for (std::map<std::string, FuncCache>::iterator iter = m_special_cache_config.begin(); iter != m_special_cache_config.end(); iter++) {
    std::string kernel = iter->first;
    if (kernel_name.compare(kernel) == 0) {
      return true;
    }
  }
  return false;
}

void gpgpu_sim::set_cache_config(std::string kernel_name)
{
  if (has_special_cache_config(kernel_name)) {
    change_cache_config(get_cache_config(kernel_name));
  } else {
    change_cache_config(FuncCachePreferNone);
  }
}

void gpgpu_sim::change_cache_config(FuncCache cache_config)
{
  if (cache_config != m_shader_config->m_L1D_config.get_cache_status()) {
    printf("FLUSH L1 Cache at configuration change between kernels\n");
    for (unsigned i = 0; i < m_shader_config->n_simt_clusters; i++) {
      m_cluster[i]->cache_flush();
    }
  }

  switch (cache_config) {
    case FuncCachePreferNone:
      m_shader_config->m_L1D_config.init(m_shader_config->m_L1D_config.m_config_string, FuncCachePreferNone);
      m_shader_config->gpgpu_shmem_size = m_shader_config->gpgpu_shmem_sizeDefault;
      break;
    case FuncCachePreferL1:
      if ((m_shader_config->m_L1D_config.m_config_stringPrefL1 == NULL) || (m_shader_config->gpgpu_shmem_sizePrefL1 == (unsigned) - 1))
      {
        printf("WARNING: missing Preferred L1 configuration\n");
        m_shader_config->m_L1D_config.init(m_shader_config->m_L1D_config.m_config_string, FuncCachePreferNone);
        m_shader_config->gpgpu_shmem_size = m_shader_config->gpgpu_shmem_sizeDefault;

      } else {
        m_shader_config->m_L1D_config.init(m_shader_config->m_L1D_config.m_config_stringPrefL1, FuncCachePreferL1);
        m_shader_config->gpgpu_shmem_size = m_shader_config->gpgpu_shmem_sizePrefL1;
      }
      break;
    case FuncCachePreferShared:
      if ((m_shader_config->m_L1D_config.m_config_stringPrefShared == NULL) || (m_shader_config->gpgpu_shmem_sizePrefShared == (unsigned) - 1))
      {
        printf("WARNING: missing Preferred L1 configuration\n");
        m_shader_config->m_L1D_config.init(m_shader_config->m_L1D_config.m_config_string, FuncCachePreferNone);
        m_shader_config->gpgpu_shmem_size = m_shader_config->gpgpu_shmem_sizeDefault;
      } else {
        m_shader_config->m_L1D_config.init(m_shader_config->m_L1D_config.m_config_stringPrefShared, FuncCachePreferShared);
        m_shader_config->gpgpu_shmem_size = m_shader_config->gpgpu_shmem_sizePrefShared;
      }
      break;
    default:
      break;
  }
}

void gpgpu_sim::clear_executed_kernel_info()
{
  m_executed_kernel_names.clear();
  m_executed_kernel_uids.clear();
}

void gpgpu_sim::gpu_print_stat_file(FILE* outputfile)
{
  //FILE *statfout = stdout;

  //std::string kernel_info_str = executed_kernel_info_string();
  //fprintf(statfout, "%s", kernel_info_str.c_str());

  for (unsigned i = 0; i < ConfigOptions::n_apps; i++) {
    fprintf(outputfile, "app_id_to_name %d = %s\n", App::get_app_id(i), App::get_app_name(i).c_str());
  }
  for (unsigned i = 0; i < ConfigOptions::n_apps; i++) {
    App* app = App::get_app(App::get_app_id(i));
    fprintf(outputfile, "gpu_ipc_%u = %12.4f\n", App::get_app_id(i), (float) app->gpu_sim_instruction_count /
        app->gpu_total_simulator_cycles_stream);
  }
  for (unsigned i = 0; i < ConfigOptions::n_apps; i++) {
    App* app = App::get_app(App::get_app_id(i));
    fprintf(outputfile, "gpu_tot_sim_cycle_stream_%u = %lld\n", App::get_app_id(i),
        app->gpu_total_simulator_cycles_stream);
  }
  for (unsigned i = 0; i < ConfigOptions::n_apps; i++) {
    App* app = App::get_app(App::get_app_id(i));
    fprintf(outputfile, "gpu_sim_insn_%u = %lld\n", App::get_app_id(i), app->gpu_sim_instruction_count);
  }

  fprintf(outputfile, "gpu_sim_cycle = %lld\n", gpu_sim_cycle);
  fprintf(outputfile, "gpu_sim_insn = %lld\n", gpu_sim_insn);
  fprintf(outputfile, "gpu_ipc = %12.4f\n", (float)gpu_sim_insn / gpu_sim_cycle);
  fprintf(outputfile, "gpu_tot_sim_cycle = %lld\n", gpu_tot_sim_cycle + gpu_sim_cycle);
  fprintf(outputfile, "gpu_tot_sim_insn = %lld\n", gpu_tot_sim_insn + gpu_sim_insn);
  fprintf(outputfile, "gpu_tot_ipc = %12.4f\n", (float)(gpu_tot_sim_insn + gpu_sim_insn) /
      (gpu_tot_sim_cycle + gpu_sim_cycle));
	for (int j = 0; j < gpu_sms; j++) {
		fprintf(outputfile, "instructions per core: COREID : %d\t Instructions %d\n", j, gpu_sim_insn_per_core[j]);
	}
	for (int j = 0; j < gpu_sms; j++) {
		fprintf(outputfile, "first cycle per core: COREID : %d\t first cycle %d\n", j, gpu_sim_first_cycle_per_core[j]);
	}
	for (int j = 0; j < gpu_sms; j++) {
		fprintf(outputfile, "last cycle per core: COREID : %d\t last cycle %d\n", j, gpu_sim_last_cycle_per_core[j]);
	}
	for (int j = 0; j < gpu_sms; j++) {
		fprintf(outputfile, "ICP per core: COREID : %d\t IPC %f\n", j, (float)gpu_sim_insn_per_core[j] / 
        ((float)(gpu_sim_last_cycle_per_core[j] - gpu_sim_first_cycle_per_core [j])));
	}
  fprintf(outputfile, "gpu_tot_issued_cta = %lld\n", gpu_tot_issued_cta);

  // performance counter for stalls due to congestion.
  fprintf(outputfile, "gpu_stall_dramfull = %d\n", gpu_stall_dramfull);
  fprintf(outputfile, "gpu_stall_icnt2sh    = %d\n", gpu_stall_icnt2sh);

  time_t curr_time;
  time(&curr_time);
  unsigned long long elapsed_time = MAX(curr_time - g_simulation_starttime, 1);
  fprintf(outputfile, "gpu_total_sim_rate=%u\n", (unsigned)((gpu_tot_sim_insn + gpu_sim_insn) / elapsed_time));

  //shader_print_l1_miss_stat( stdout );
  shader_print_cache_stats(outputfile);

  cache_stats core_cache_stats;
  core_cache_stats.clear();
  for (unsigned i = 0; i < m_config.num_cluster(); i++) {
    m_cluster[i]->get_cache_stats(core_cache_stats);
  }
  fprintf(outputfile, "\nTotal_core_cache_stats:\n");
  core_cache_stats.print_stats(outputfile, "Total_core_cache_stats_breakdown");
  shader_print_scheduler_stat(outputfile, false);

  m_shader_stats->print(outputfile);
#ifdef GPGPUSIM_POWER_MODEL
  if (m_config.g_power_simulation_enabled) {
    //m_gpgpusim_wrapper->print_power_kernel_stats(gpu_sim_cycle, gpu_tot_sim_cycle, gpu_tot_sim_insn + gpu_sim_insn, kernel_info_str, true );
    //mcpat_reset_perf_count(m_gpgpusim_wrapper);
  }
#endif

  // performance counter that are not local to one shader
  m_memory_stats->memlatstat_print_file(m_memory_config->m_n_mem, m_memory_config->nbk, outputfile);
  for (unsigned i = 0; i < m_memory_config->m_n_mem; i++)
    m_memory_partition_unit[i]->print(outputfile);

  // L2 cache stats
  if (!m_memory_config->m_L2_config.disabled()) {
    cache_stats l2_stats;
    struct cache_sub_stats l2_css;
    struct cache_sub_stats total_l2_css;
    l2_stats.clear();
    l2_css.clear();
    total_l2_css.clear();

    fprintf(outputfile, "\n========= L2 cache stats =========\n");
    for (unsigned i = 0; i < m_memory_config->m_n_mem_sub_partition; i++) {
      m_memory_sub_partition[i]->accumulate_L2cache_stats(l2_stats);
      m_memory_sub_partition[i]->get_L2cache_sub_stats(l2_css);

      fprintf(outputfile, "L2_cache_bank[%d]: Access = %u, Miss = %u, Miss_rate = %.3lf, Pending_hits = %u, Reservation_fails = %u\n",
          i, l2_css.accesses, l2_css.misses, (double)l2_css.misses / (double)l2_css.accesses, l2_css.pending_hits, l2_css.res_fails);

      total_l2_css += l2_css;
    }
    if (!m_memory_config->m_L2_config.disabled() && m_memory_config->m_L2_config.get_num_lines()) {
      L2c_print_cache_stat(outputfile);
      fprintf(outputfile, "L2_total_cache_accesses = %u\n", total_l2_css.accesses);
      fprintf(outputfile, "L2_total_cache_misses = %u\n", total_l2_css.misses);
      if (total_l2_css.accesses > 0)
        fprintf(outputfile, "L2_total_cache_miss_rate = %.4lf\n", (double)total_l2_css.misses / (double)total_l2_css.accesses);
      fprintf(outputfile, "L2_total_cache_pending_hits = %u\n", total_l2_css.pending_hits);
      fprintf(outputfile, "L2_total_cache_reservation_fails = %u\n", total_l2_css.res_fails);
      fprintf(outputfile, "L2_total_cache_breakdown:\n");
      l2_stats.print_stats(outputfile, "L2_cache_stats_breakdown");
      total_l2_css.print_port_stats(outputfile, "L2_cache");
    }
  }

  if (m_config.gpgpu_cflog_interval != 0) {
    spill_log_to_file(outputfile, 1, gpu_sim_cycle);
    insn_warp_occ_print(outputfile);
  }
  if (gpgpu_ptx_instruction_classification) {
    StatDisp(g_inst_classification_stat[g_ptx_kernel_count]);
    StatDisp(g_inst_op_classification_stat[g_ptx_kernel_count]);
  }

#ifdef GPGPUSIM_POWER_MODEL
  if (m_config.g_power_simulation_enabled) {
    //m_gpgpusim_wrapper->detect_print_steady_state(1,gpu_tot_sim_insn+gpu_sim_insn);
  }
#endif
  // Interconnect power stat print
  long total_simt_to_mem = 0;
  long total_mem_to_simt = 0;
  long temp_stm = 0;
  long temp_mts = 0;
  for (unsigned i = 0; i < m_config.num_cluster(); i++) {
    m_cluster[i]->get_icnt_stats(temp_stm, temp_mts);
    total_simt_to_mem += temp_stm;
    total_mem_to_simt += temp_mts;
  }
  fprintf(outputfile, "\nicnt_total_pkts_mem_to_simt=%ld\n", total_mem_to_simt);
  fprintf(outputfile, "icnt_total_pkts_simt_to_mem=%ld\n", total_simt_to_mem);

  //time_vector_print();
  //fflush(stdout);

  //clear_executed_kernel_info();


  fprintf(outputfile, "\n\nAdditional Stats\n");
        fprintf(outputfile, "total number of l1 accesses = %d\n", l1_cache_access);
        fprintf(outputfile, "total number of l1 accesses after tlb hit = %d\n",
            l1_cache_access_tlb_hit);
        fprintf(outputfile, "total number of coalesced accesses = %d\n", total_access_after_coalesce);
        fprintf(outputfile, "number of repeat accesses = %d\n", repeat_access);
        fprintf(outputfile, "number of cache reservation fails =%d\n", reservation_fail_count);
        fprintf(outputfile, "number of read mshr cache fails =%d\n", r_mshr_miss);
        fprintf(outputfile, "number of write mshr cache fails =%d\n", w_mshr_miss);
        fprintf(outputfile, "number of read other cache fails =%d\n", r_other_fail);
        fprintf(outputfile, "number of write other cache fails =%d\n", w_other_fail);
        for(int i = 0; i < 5; i++)
          fprintf(outputfile, "number of page walks depth %d returning =%d\n", i, page_walks_returning_depth[i]);
        fprintf(outputfile, "total number of tlb requests generated =%d\n", tlb_requests_generated);
        fprintf(outputfile, "total number of page walk mem_fetches generated =%d\n", page_walks_generated);
        fprintf(outputfile, "total number of page walk cache hits = %d\n", pw_cache_hits);
        fprintf(outputfile, "total number of page walk enquees= %d\n", pw_cache_latency_queue_pushes);
        fprintf(outputfile, "total tlb requests sent to memory = %d\n", tlb_requests_to_memory);
        fprintf(outputfile, "total tlb misses = %d\n", tlb_misses_counter);
        fprintf(outputfile, "last cycle page walk = %d\n", last_page_walk_return);

        fprintf(outputfile, "\n");
        fprintf(outputfile, "l2 pagetable hits = %d\n", tlb_related_l2_hit);
        fprintf(outputfile, "l2 pagetable miss = %d\n", tlb_related_l2_miss);
        fprintf(outputfile, "l2 pagetable mshr = %d\n", tlb_related_l2_mshr);
        fprintf(outputfile, "pte requests queued into l2 in total= %d\n", tlb_requests_into_l2);
        fprintf(outputfile, "pte requests queued into l2 from ROP= %d\n", tlb_requests_into_l2_rop);
        fprintf(outputfile, "\n");

        m_shader_stats->print_essential(outputfile);
        m_memory_stats->print_essential(outputfile);
}

//pratheek: 
void gpgpu_sim::print_essential(int streamid)
{

  /* std::stringstream output_file; */
  /* output_file << "essential.txt"; */
  /* std::string ostr = output_file.str(); */
  /* FILE* fout = fopen(ostr.c_str(), "a+"); */

  printf("\n\nEssential Stats : %d\n", streamid);
  printf("for_parser_app_id_to_name %d = %s\n", App::get_app_id(streamid), App::get_app_name(streamid).c_str());

  printf("for_parser_gpu_sim_cycle = %lld\n", gpu_sim_cycle);
  printf("for_parser_gpu_sim_insn = %lld\n", gpu_sim_insn);
  printf("for_parser_gpu_ipc = %12.4f\n", (float)gpu_sim_insn / gpu_sim_cycle);

  for (unsigned i = 0; i < ConfigOptions::n_apps; i++) {
    App* app = App::get_app(App::get_app_id(i));
    printf("for_parser_gpu_ipc_%u = %12.4f\n", App::get_app_id(i), (float) app->gpu_sim_instruction_count /
        app->gpu_total_simulator_cycles_stream);
  }

  m_memory_stats->print_essential(stdout);

}


// performance counter that are not local to one shader
unsigned gpgpu_sim::threads_per_core() const
{
  return m_shader_config->n_thread_per_shader;
}

void shader_core_ctx::mem_instruction_stats(const warp_inst_t &inst)
{
  unsigned active_count = inst.active_count();
  //this breaks some encapsulation: the is_[space] functions, if you change those, change this.
  switch (inst.space.get_type()) {
    case undefined_space:
    case reg_space:
      break;
    case shared_space:
      m_stats->gpgpu_n_shmem_insn += active_count;
      break;
    case const_space:
      m_stats->gpgpu_n_const_insn += active_count;
      break;
    case param_space_kernel:
    case param_space_local:
      m_stats->gpgpu_n_param_insn += active_count;
      break;
    case tex_space:
      m_stats->gpgpu_n_tex_insn += active_count;
      break;
    case global_space:
    case local_space:
      if (inst.is_store())
        m_stats->gpgpu_n_store_insn += active_count;
      else
        m_stats->gpgpu_n_load_insn += active_count;
      break;
    default:
      abort();
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Launches a cooperative thread array (CTA).
 *
 * @param kernel
 *    object that tells us which kernel to ask for a CTA from
 */

void shader_core_ctx::issue_block2core(kernel_info_t &kernel)
{
  set_max_cta(kernel);

  unsigned kernel_id = kernel.get_uid(); //new
  unsigned stream_id = kernel.get_stream_id();

  // find a free CTA context
  unsigned free_cta_hw_id = (unsigned) - 1;
  for (unsigned i = 0; i < kernel_max_cta_per_shader; i++) {
    if (m_cta_status[i] == 0) {
      free_cta_hw_id = i;
      break;
    }
  }
  assert(free_cta_hw_id != (unsigned) - 1);

  // determine hardware threads and warps that will be used for this CTA
  int cta_size = kernel.threads_per_cta();

  // hw warp id = hw thread id mod warp size, so we need to find a range
  // of hardware thread ids corresponding to an integral number of hardware
  // thread ids
  int padded_cta_size = cta_size;
  if (cta_size % m_config->warp_size)
    padded_cta_size = ((cta_size / m_config->warp_size) + 1) * (m_config->warp_size);
  unsigned start_thread = free_cta_hw_id * padded_cta_size;
  unsigned end_thread  = start_thread +  cta_size;

  // reset the microarchitecture state of the selected hardware thread and warp contexts
  reinit(start_thread, end_thread, false);

  // initalize scalar threads and determine which hardware warps they are allocated to
  // bind functional simulation state of threads to hardware resources (simulation)
  warp_set_t warps;
  unsigned nthreads_in_block = 0;
  for (unsigned i = start_thread; i < end_thread; i++) {
    m_threadState[i].m_cta_id = free_cta_hw_id;
    unsigned warp_id = i / m_config->warp_size;
    nthreads_in_block += ptx_sim_init_thread(kernel, &m_thread[i], m_sid, i, cta_size - (i - start_thread), m_config->n_thread_per_shader, this, free_cta_hw_id, warp_id, m_cluster->get_gpu());
    m_threadState[i].m_active = true;
    warps.set(warp_id);
  }
  assert(nthreads_in_block > 0 && nthreads_in_block <= m_config->n_thread_per_shader);  // should be at least one, but less than max
  m_cta_status[free_cta_hw_id] = nthreads_in_block;

  // now that we know which warps are used in this CTA, we can allocate
  // resources for use in CTA-wide barrier operations
  m_barriers.allocate_barrier(free_cta_hw_id, warps);

  // initialize the SIMT stacks and fetch hardware
  init_warps(free_cta_hw_id, start_thread, end_thread);
  m_n_active_cta++;

  shader_CTA_count_log(m_sid, 1);
  //printf("GPGPU-Sim uArch: core:%3d, cta:%2u initialized @(%lld,%lld)\n", m_sid, free_cta_hw_id, gpu_sim_cycle, gpu_tot_sim_cycle );
  printf("GPGPU-Sim uArch: Shader:%3d, cta:%2u initialized @(%lld,%lld), ACTIVE=%d, KERNEL=%d, STREAM=%d\n", m_sid, free_cta_hw_id, gpu_sim_cycle, gpu_tot_sim_cycle, m_n_active_cta, kernel_id, stream_id);
}

///////////////////////////////////////////////////////////////////////////////////////////

void dram_t::dram_log(int task)
{
  if (task == SAMPLELOG) {
    StatAddSample(mrqq_Dist, que_length());
  } else if (task == DUMPLOG) {
    printf("Queue Length DRAM[%d] ", id); StatDisp(mrqq_Dist);
  }
}

//Find next clock domain and increment its time
int gpgpu_sim::next_clock_domain(void)
{
  double smallest = min3(core_time, icnt_time, dram_time);
  int mask = 0x00;
  if (l2_time <= smallest) {
    smallest = l2_time;
    mask |= L2 ;
    l2_time += m_config.l2_period;
  }
  if (icnt_time <= smallest) {
    mask |= ICNT;
    icnt_time += m_config.icnt_period;
  }
  if (dram_time <= smallest) {
    mask |= DRAM;
    dram_time += m_config.dram_period;
  }
  if (core_time <= smallest) {
    mask |= CORE;
    core_time += m_config.core_period;
  }
  return mask;
}

void gpgpu_sim::issue_block2core() //new
{
  unsigned last_issued = m_last_cluster_issue;
  unsigned clusters_issued;
  if (m_memory_config->gpu_char == 0) {
    clusters_issued = m_shader_config->n_simt_clusters;
  }
  else {
    clusters_issued = m_shader_config->n_simt_clusters / 2;
  }
  for (unsigned i = 0; i < clusters_issued; i++) {
    unsigned idx = (i + last_issued + 1) % clusters_issued;;
    unsigned num = m_cluster[idx]->issue_block2core();
    if (num) {
      m_last_cluster_issue = idx;
      m_total_cta_launched += num;
    }
  }
}

unsigned long long g_single_step = 0; // set this in gdb to single step the pipeline

void gpgpu_sim::cycle()
{
  // if any memory operation is outstanding, skip this cycle?
  if (std::any_of(App::get_apps().cbegin(), App::get_apps().cend(),
      [&](std::pair<appid_t, App*> app) {
        return app.second->mem_flag;
      })) {
    return;
  }
  int clock_mask = next_clock_domain();

  if (clock_mask & CORE) {
    // shader core loading (pop from ICNT into core) follows CORE clock
    for (unsigned i = 0; i < m_shader_config->n_simt_clusters; i++)
      m_cluster[i]->icnt_cycle();
  }
  if (clock_mask & ICNT) {
    // pop from memory controller to interconnect
    for (unsigned i = 0; i < m_memory_config->m_n_mem_sub_partition; i++) {
      mem_fetch* mf = m_memory_sub_partition[i]->top();
      if (mf) {
        unsigned response_size = mf->get_is_write() ? mf->get_ctrl_size() : mf->size();
        if (::icnt_has_buffer(m_shader_config->mem2device(i), response_size)) {
          if (!mf->get_is_write())
            mf->set_return_timestamp(gpu_sim_cycle + gpu_tot_sim_cycle);
          mf->set_status(IN_ICNT_TO_SHADER, gpu_sim_cycle + gpu_tot_sim_cycle);
          ::icnt_push(m_shader_config->mem2device(i), mf->get_tpc(), mf, response_size);
          m_memory_sub_partition[i]->pop();
        } else {
          gpu_stall_icnt2sh++;
        }
      } else {
        m_memory_sub_partition[i]->pop();
      }
    }
  }

  if (clock_mask & DRAM) {
    for (unsigned i = 0; i < m_memory_config->m_n_mem; i++) {
      m_memory_partition_unit[i]->dram_cycle(); // Issue the dram command (scheduler + delay model)
      // Update performance counters for DRAM
      m_memory_partition_unit[i]->set_dram_power_stats(m_power_stats->pwr_mem_stat->n_cmd[CURRENT_STAT_IDX][i], m_power_stats->pwr_mem_stat->n_activity[CURRENT_STAT_IDX][i],
          m_power_stats->pwr_mem_stat->n_nop[CURRENT_STAT_IDX][i], m_power_stats->pwr_mem_stat->n_act[CURRENT_STAT_IDX][i], m_power_stats->pwr_mem_stat->n_pre[CURRENT_STAT_IDX][i],
          m_power_stats->pwr_mem_stat->n_rd[CURRENT_STAT_IDX][i], m_power_stats->pwr_mem_stat->n_wr[CURRENT_STAT_IDX][i], m_power_stats->pwr_mem_stat->n_req[CURRENT_STAT_IDX][i]);
    }
  }

  // L2 operations follow L2 clock domain
  if (clock_mask & L2) {
    m_power_stats->pwr_mem_stat->l2_cache_stats[CURRENT_STAT_IDX].clear();
    for (unsigned i = 0; i < m_memory_config->m_n_mem_sub_partition; i++) {
      //move memory request from interconnect into memory partition (if not backed up)
      //Note:This needs to be called in DRAM clock domain if there is no L2 cache in the system
      if (m_memory_sub_partition[i]->full()) {
        gpu_stall_dramfull++;
      } else {
        mem_fetch* mf = (mem_fetch*) icnt_pop(m_shader_config->mem2device(i));
        m_memory_sub_partition[i]->push(mf, gpu_sim_cycle + gpu_tot_sim_cycle);
      }
      m_memory_sub_partition[i]->cache_cycle(gpu_sim_cycle + gpu_tot_sim_cycle);
      m_memory_sub_partition[i]->accumulate_L2cache_stats(m_power_stats->pwr_mem_stat->l2_cache_stats[CURRENT_STAT_IDX]);
    }
    //pratheek
    //cycle the shared TLB
    m_shared_tlb->cycle();
  }

  if (clock_mask & ICNT) {
    icnt_transfer();
  }

  if (clock_mask & CORE) {
    // L1 cache + shader core pipeline stages
    m_power_stats->pwr_mem_stat->core_cache_stats[CURRENT_STAT_IDX].clear();
    for (unsigned i = 0; i < m_shader_config->n_simt_clusters; i++) {
      if (m_cluster[i]->get_not_completed() || get_more_cta_left()) {
        m_cluster[i]->core_cycle();
        *active_sms += m_cluster[i]->get_n_active_sms();
      }
      // Update core icnt/cache stats for GPUWattch
      m_cluster[i]->get_icnt_stats(m_power_stats->pwr_mem_stat->n_simt_to_mem[CURRENT_STAT_IDX][i], m_power_stats->pwr_mem_stat->n_mem_to_simt[CURRENT_STAT_IDX][i]);
      m_cluster[i]->get_cache_stats(m_power_stats->pwr_mem_stat->core_cache_stats[CURRENT_STAT_IDX]);
    }
    float temp = 0;


    for (unsigned i = 0; i < m_shader_config->num_shader(); i++) {
      temp += m_shader_stats->m_pipeline_duty_cycle[i];
    }
    temp = temp / m_shader_config->num_shader();
    *average_pipeline_duty_cycle = ((*average_pipeline_duty_cycle) + temp);
    //cout<<"Average pipeline duty cycle: "<<*average_pipeline_duty_cycle<<endl;


    if (g_single_step && ((gpu_sim_cycle + gpu_tot_sim_cycle) >= g_single_step)) {
      asm("int $03");
    }
    gpu_sim_cycle++;

    unsigned cluster_size = gpu_sms / ConfigOptions::n_apps;

    my_active_sms = 0;
    for (int i = 0; i < gpu_sms; i++) {
      if (m_cluster[i]->get_n_active_sms()) {
        my_active_sms++;
      }
    }
    for (unsigned j = 0; j < ConfigOptions::n_apps; j++) {
      App* app = App::get_app(App::get_app_id(j));
      for (unsigned i = j * cluster_size; i < (j + 1) * cluster_size; i++) {
        if (m_cluster[i]->get_n_active_sms() > 0) {
          app->gpu_total_simulator_cycles_stream++;
          break;
        }
      }
    }

    if (g_interactive_debugger_enabled)
      gpgpu_debug();

    // McPAT main cycle (interface with McPAT)
#ifdef GPGPUSIM_POWER_MODEL
    if (m_config.g_power_simulation_enabled) {
      //mcpat_cycle(m_config, getShaderCoreConfig(), m_gpgpusim_wrapper, m_power_stats, m_config.gpu_stat_sample_freq, gpu_tot_sim_cycle, gpu_sim_cycle, gpu_tot_sim_insn, gpu_sim_insn);
    }
#endif

    issue_block2core();

    // Depending on configuration, flush the caches once all of threads are completed.
    int all_threads_complete = 1;
    if (m_config.gpgpu_flush_l1_cache) {
      for (unsigned i = 0; i < m_shader_config->n_simt_clusters; i++) {
        if (m_cluster[i]->get_not_completed() == 0)
          m_cluster[i]->cache_flush();
        else
          all_threads_complete = 0 ;
      }
    }

    if (m_config.gpgpu_flush_l2_cache) {
      if (!m_config.gpgpu_flush_l1_cache) {
        for (unsigned i = 0; i < m_shader_config->n_simt_clusters; i++) {
          if (m_cluster[i]->get_not_completed() != 0) {
            all_threads_complete = 0 ;
            break;
          }
        }
      }

      if (all_threads_complete && !m_memory_config->m_L2_config.disabled()) {
        printf("Flushed L2 caches...\n");
        if (m_memory_config->m_L2_config.get_num_lines()) {
          int dlc = 0;
          for (unsigned i = 0; i < m_memory_config->m_n_mem; i++) {
            dlc = m_memory_sub_partition[i]->flushL2();
            assert(dlc == 0);  // need to model actual writes to DRAM here
            printf("Dirty lines flushed from L2 %d is %d\n", i, dlc);
          }
        }
      }
    }

    /* //print essential satis every x000 cycles */
    /* if(gpu_sim_cycle % 3000 == 0) */
    /* { */
    /*   print_essential(); */
    /* } */



    if (!(gpu_sim_cycle % m_config.gpu_stat_sample_freq)) {
      time_t days, hrs, minutes, sec;
      time_t curr_time;
      time(&curr_time);
      unsigned long long  elapsed_time = MAX(curr_time - g_simulation_starttime, 1);
      if ((elapsed_time - last_liveness_message_time) >= m_config.liveness_message_freq) {
        days    = elapsed_time / (3600 * 24);
        hrs     = elapsed_time / 3600 - 24 * days;
        minutes = elapsed_time / 60 - 60 * (hrs + 24 * days);
        sec = elapsed_time - 60 * (minutes + 60 * (hrs + 24 * days));
        printf("GPGPU-Sim uArch: cycles simulated: %lld  inst.: %lld (ipc=%4.1f) sim_rate=%u (inst/sec) elapsed = %u:%u:%02u:%02u / %s",
            gpu_tot_sim_cycle + gpu_sim_cycle, gpu_tot_sim_insn + gpu_sim_insn,
            (double)gpu_sim_insn / (double)gpu_sim_cycle,
            (unsigned)((gpu_tot_sim_insn + gpu_sim_insn) / elapsed_time),
            (unsigned)days, (unsigned)hrs, (unsigned)minutes, (unsigned)sec,
            ctime(&curr_time));

        for (unsigned i = 0; i < ConfigOptions::n_apps; i++) {
          App* app = App::get_app(App::get_app_id(i));
          printf("gpu_sim_insn_%u = %lld (ipc=%4.1f, lat=) \n", i, app->gpu_sim_instruction_count,
              (double) app->gpu_sim_instruction_count / app->gpu_total_simulator_cycles_stream);
        }
        fflush(stdout);
        last_liveness_message_time = elapsed_time;
      }
      visualizer_printstat();
      m_memory_stats->memlatstat_lat_pw();
      if (m_config.gpgpu_runtime_stat && (m_config.gpu_runtime_stat_flag != 0)) {
        if (m_config.gpu_runtime_stat_flag & GPU_RSTAT_BW_STAT) {
          for (unsigned i = 0; i < m_memory_config->m_n_mem; i++)
            m_memory_partition_unit[i]->print_stat(stdout);
          printf("maxmrqlatency = %d \n", m_memory_stats->max_mrq_latency);
          printf("maxmflatency = %d \n", m_memory_stats->max_mf_latency);
          printf("high_prio_queue_drain_reset = %d \n", m_memory_stats->drain_reset);
          printf("average_combo_count = %d \n", m_memory_stats->total_combo / m_memory_config->max_DRAM_high_prio_combo);
          printf("sched_from_high_prio = %d, DRAM_high_prio = %d\n",  m_memory_stats->sched_from_high_prio,  m_memory_stats->DRAM_high_prio);
        }
        if (m_config.gpu_runtime_stat_flag & GPU_RSTAT_SHD_INFO)
          shader_print_runtime_stat(stdout);
        if (m_config.gpu_runtime_stat_flag & GPU_RSTAT_L1MISS)
          shader_print_l1_miss_stat(stdout);
        if (m_config.gpu_runtime_stat_flag & GPU_RSTAT_SCHED)
          shader_print_scheduler_stat(stdout, false);
      }
    }

    if (!(gpu_sim_cycle % 200000)) {
      // deadlock detection
      if (m_config.gpu_deadlock_detect && gpu_sim_insn == last_gpu_sim_insn) {
        gpu_deadlock = true;
      } else {
        last_gpu_sim_insn = gpu_sim_insn;
      }
    }

    if (m_config.gpu_max_insn_opt
        && std::all_of(App::get_apps().cbegin(), App::get_apps().cend(),
            [&](std::pair<appid_t, App*> app) {
              return app.second->gpu_sim_instruction_count >= this->m_config.gpu_max_insn_opt;
            })) {
      max_insn_struck = true;
      print_stats();

      fprintf(output, "statistics when three apps completed MAX instructions\n");
      fprintf(output, "-------------------------------------------------\n");
      for (unsigned i = 0; i < ConfigOptions::n_apps; i++) {
        App* app = App::get_app(App::get_app_id(i));
        int f = feof(app->output);
        if (!f) {
          std::stringstream filename;
          filename << "stream" << i << ".txt";
          output = freopen(filename.str().c_str(), "w+", app->output);
          gpu_print_stat_file(output);
          print_essential(i);
          fflush(output);
          fclose(output);
        }
      }
      abort();
    }
    try_snap_shot(gpu_sim_cycle);
    spill_log_to_file(stdout, 0, gpu_sim_cycle);
  }
}

  void shader_core_ctx::dump_warp_state(FILE *fout) const
  {
    fprintf(fout, "\n");
    fprintf(fout, "per warp functional simulation status:\n");
    for (unsigned w = 0; w < m_config->max_warps_per_shader; w++)
      m_warp[w].print(fout);
  }

  void gpgpu_sim::dump_pipeline(int mask, int s, int m) const
  {
    /*
       You may want to use this function while running GPGPU-Sim in gdb.
       One way to do that is add the following to your .gdbinit file:

       define dp
       call g_the_gpu.dump_pipeline_impl((0x40|0x4|0x1),$arg0,0)
       end

       Then, typing "dp 3" will show the contents of the pipeline for shader core 3.
     */

    printf("Dumping pipeline state...\n");
    if (!mask) mask = 0xFFFFFFFF;
    for (unsigned i = 0; i < m_shader_config->n_simt_clusters; i++) {
      if (s != -1) {
        i = s;
      }
      if (mask & 1) m_cluster[m_shader_config->sid_to_cluster(i)]->display_pipeline(i, stdout, 1, mask & 0x2E);
      if (s != -1) {
        break;

      }
    }
    if (mask & 0x10000) {
      for (unsigned i = 0; i < m_memory_config->m_n_mem; i++) {
        if (m != -1) {
          i = m;
        }
        printf("DRAM / memory controller %u:\n", i);
        if (mask & 0x100000) m_memory_partition_unit[i]->print_stat(stdout);
        if (mask & 0x1000000)   m_memory_partition_unit[i]->visualize();
        if (mask & 0x10000000)   m_memory_partition_unit[i]->print(stdout);
        if (m != -1) {
          break;
        }
      }
    }
    fflush(stdout);
  }

  const struct shader_core_config * gpgpu_sim::getShaderCoreConfig()
  {
    return m_shader_config;
  }

  const struct memory_config * gpgpu_sim::getMemoryConfig()
  {
    return m_memory_config;
  }

  simt_core_cluster * gpgpu_sim::getSIMTCluster()
  {
    return *m_cluster;
  }

void gpgpu_sim::flushL2(unsigned appid)
{
  for (unsigned i = 0; i < m_memory_config->m_n_mem; i++) {
            m_memory_sub_partition[i]->flushL2(appid);
          }
}