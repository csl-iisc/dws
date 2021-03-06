/*
 * App.h
 *
 *  Created on: Mar 3, 2017
 *      Author: vance
 */

#ifndef SRC_GPGPU_SIM_APP_H_
#define SRC_GPGPU_SIM_APP_H_

#include <stdint.h>
#include <map>
#include <vector>
#include <list>
#include <set>
#include "../abstract_hardware_model.h" // For new_addr_type...

typedef uint32_t appid_t;

class App {
public:
  static App* get_app(appid_t);
  static appid_t get_app_id(int);
  static appid_t get_app_id_from_sm(int);
  static appid_t create_app(appid_t, FILE*, unsigned);
  static appid_t register_app(int);
  static std::map<appid_t, App*>& get_apps(void);
  static const std::vector<int> get_app_sms(appid_t);
  static void set_app_sms(appid_t appid, std::vector<int>& sms);
  static bool is_registered(int);
  static void set_app_name(int index, std::string name);
  static std::string get_app_name(int index);

private:
  static std::map<appid_t, App*> apps;
  static std::map<int, appid_t> sm_to_app;
  static std::map<int, appid_t> creation_index_to_app;
  static appid_t next_app_id;
  static std::map<int, std::string> creation_index_to_name;

private:
  App(appid_t, FILE*, unsigned);

public:
  virtual ~App();
  const appid_t appid;

  uint64_t gpu_sim_instruction_count;
  uint64_t gpu_total_simulator_cycles_stream;

  float periodic_l2mpki;
  float periodic_miss_rate;
  float mflatency;
  float tlb_mflatency;

  // From stream_manager
  bool mem_flag;
  bool stat_flag;
  uint64_t app_insn;
  FILE* output;
  uint64_t countevent;

  // From dram
  uint64_t n_req;
  uint64_t bwutil;
  uint64_t bwutil_data;
  uint64_t bwutil_tlb;
  uint64_t bwutil_periodic;
  uint64_t bwutil_periodic_data;
  uint64_t bwutil_periodic_tlb;
  uint64_t n_cmd_blp;
  uint64_t mem_state_blp;
  uint64_t dram_cycles_active;
  uint64_t blp;
  uint64_t k_app;
  float miss_rate_d;

  // From dram_sched
  uint64_t epoch_app_concurrent;

  // From tlb
  float tokens; // tlb bypass tokens
  float total_tokens;
  bool wid_tokens[4000];
  unsigned epoch_accesses;
  unsigned epoch_hit;
  unsigned epoch_bypass_hit;
  unsigned epoch_miss;
  float epoch_previous_miss_rate;
  float epoch_previous2_miss_rate;
  unsigned flush_count;
  unsigned total_access_cache; // To count how many cache accesses are there. For flushing
  uint64_t concurrent_tracker;
  unsigned wid_epoch_accesses[4000];
  unsigned wid_epoch_hit[4000];
  unsigned wid_epoch_miss[4000];
  float wid_epoch_previous_miss_rate[4000];
  float wid_epoch_previous2_miss_rate[4000];
  std::list<new_addr_type> * miss_tracker;
  std::map<new_addr_type, unsigned> * miss_tracker_count;
  std::list<unsigned long long> * miss_tracker_timestamp;
  unsigned long long tlb_occupancy;

  // From tlb_tag_array
  std::set<new_addr_type> addr_mapping;
  bool evicted;

  // From memory_stats_t

  uint64_t mrqs_latency;
  uint64_t mrq_num;
  uint64_t mf_num_lat_pw;
  uint64_t tlb_mf_num_lat_pw;
  uint64_t mf_tot_lat_pw; //total latency summed up per window.
                          //divide by mf_num_lat_pw to obtain average latency Per Window
  uint64_t tlb_mf_tot_lat_pw; //total latency summed up per window.
                              //divide by mf_num_lat_pw to obtain average latency Per Window
  uint64_t mf_total_lat;
  uint64_t tlb_mf_total_lat;
  uint64_t high_prio_queue_count_app;
  uint64_t coalesced_tried_app;
  uint64_t coalesced_succeed_app;
  uint64_t coalesced_noinval_succeed_app;
  uint64_t coalesced_fail_app;
  uint64_t tlb_bypassed_app;
  uint64_t l2_cache_accesses_app;
  uint64_t l2_cache_hits_app;
  uint64_t l2_cache_misses_app;
  float tlb_occupancy_end;
  float tlb_occupancy_peak;
  float tlb_occupancy_avg;
  uint64_t dram_prioritized_cycles_app; //How many cycles a certain app is prioritized in DRAM
  uint64_t num_mfs;
  uint64_t tlb_num_mfs;
  float rbl;
  uint64_t lat;
  uint64_t ** num_activates_;
  uint64_t ** row_access_;
  uint64_t ** num_activates_w_;
  uint64_t ** row_access_w_;

  // From shader
  uint64_t pw_cache_hit_app;
  uint64_t pw_cache_miss_app;
  uint64_t tlb_hit_app;
  uint64_t large_tlb_hit_app;
  uint64_t small_tlb_hit_app;
  uint64_t tlb2_hit_app;
  uint64_t large_tlb2_hit_app;
  uint64_t small_tlb2_hit_app;
  uint64_t tlb_fault_app;
  uint64_t tlb2_fault_app;
  uint64_t tlb_miss_app;
  uint64_t large_tlb_miss;
  uint64_t small_tlb_miss;
  uint64_t tlb2_miss_app;
  uint64_t large_tlb2_miss_app;
  uint64_t small_tlb2_miss_app;
  uint64_t tlb_access_app;
  uint64_t tlb2_access_app;
  uint64_t tlb_prefetch_hit_app;
  uint64_t tlb_hit_l1cache_res_fail;
  //uint64_t tlb_bypassed_app;
  uint64_t large_tlb_bypassed_app;
  uint64_t small_tlb_bypassed_app;
  uint64_t tlb_concurrent_serviced_app;
  uint64_t* tlb_concurrent_total_time_app;
  uint64_t tlb_current_concurrent_serviced_app;
  uint64_t avail_warp_app;
  uint64_t tlb_hit_app_epoch;
  uint64_t tlb2_hit_app_epoch;
  uint64_t tlb_miss_epoch;
  uint64_t tlb2_miss_app_epoch;
  uint64_t tlb_access_epoch;
  uint64_t tlb2_access_app_epoch;
  uint64_t tlb_concurrent_max_app;
  uint64_t l1cache_hit_app_epoch;
  uint64_t l2cache_hit_app_epoch;
  uint64_t l1cache_miss_app_epoch;
  uint64_t l2cache_miss_app_epoch;
  uint64_t l1cache_access_app_epoch;
  uint64_t l2cache_access_app_epoch;
  float available_warp_per_tlb_app;
  uint64_t* shader_cycle_distro;

  uint64_t tlb_mshr_hit_app;
  uint64_t tlb_mshr_fail_app;
  uint64_t tlb_bkpres_fail_app;
  uint64_t l1cache_reservation_fail_app;

  uint64_t bypass_tlb_hit;
  uint64_t bypass_tlb_miss;

  // from gpu-cache
  uint64_t m_access_s;
  uint64_t m_miss_s;
  uint64_t m_access_s_previous;
  uint64_t m_miss_s_previous;

  //pratheek
   uint64_t tlb_level_accesses[10];
   uint64_t tlb_level_hits[10];
   uint64_t tlb_level_misses[10];
   uint64_t tlb_level_fails[10];

   uint64_t debug_tlb_hits;
   uint64_t debug_tlb_misses;
   uint64_t debug_tlb_mshrs;
   uint64_t debug_tlb_accesses;

   uint64_t page_walk_total_latency;
   uint64_t page_walk_total_num;

   uint64_t data_total_latency;
   uint64_t data_total_num;

};

#endif /* SRC_GPGPU_SIM_APP_H_ */
