// Copyright (c) 2009-2011, Tor M. Aamodt
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

#ifndef MC_PARTITION_INCLUDED
#define MC_PARTITION_INCLUDED

#include "dram.h"
#include "../abstract_hardware_model.h"

#include "gpu-cache.h"
#include "gpu-sim.h"

#include <list>
#include <queue>

extern int tlb_related_l2_mshr;
extern int tlb_related_l2_miss;
extern int tlb_related_l2_hit;
extern int tlb_requests_into_l2;
extern int tlb_requests_into_l2_rop;

class mem_fetch;

class mmu;

class dram_cmd;

class partition_mf_allocator : public mem_fetch_allocator {
public:
    partition_mf_allocator( const memory_config *config )
    {
        m_memory_config = config;
    }
    virtual mem_fetch * alloc(const class warp_inst_t &inst, const mem_access_t &access) const 
    {
        abort();
        return NULL;
    }
    virtual mem_fetch * alloc(new_addr_type addr, mem_access_type type, unsigned size, bool wr, unsigned sid) const;
private:
    const memory_config *m_memory_config;
};

// Memory partition unit contains all the units assolcated with a single DRAM channel. 
// - It arbitrates the DRAM channel among multiple sub partitions.  
// - It does not connect directly with the interconnection network. 
class memory_partition_unit
{
public: 
   memory_partition_unit( unsigned partition_id, const struct memory_config *config, class memory_stats_t *stats, mmu * page_manager, tlb_tag_array * shared_tlb);
   ~memory_partition_unit(); 

   bool busy() const;

   tlb_tag_array * m_shared_tlb;

   mmu * m_page_manager;

   void cache_cycle( unsigned cycle );
   void dram_cycle();

   void set_done( mem_fetch *mf );

   void visualizer_print( gzFile visualizer_file ) const;

   void print_stat( FILE *fp ) { m_dram->print_stat(fp); }
   
   
   void visualize() const { m_dram->visualize(); }
   void print( FILE *fp ) const;
   
   unsigned dram_bwutil();

   void insert_dram_command(dram_cmd * cmd);

   class memory_sub_partition * get_sub_partition(int sub_partition_id) 
   {
      return m_sub_partition[sub_partition_id]; 
   }
   


   // Power model
   void set_dram_power_stats(unsigned &n_cmd,
                             unsigned &n_activity,
                             unsigned &n_nop,
                             unsigned &n_act,
                             unsigned &n_pre,
                             unsigned &n_rd,
                             unsigned &n_wr,
                             unsigned &n_req) const;

   int global_sub_partition_id_to_local_id(int global_sub_partition_id) const; 

   unsigned get_mpid() const { return m_id; }

   std::list<mem_fetch*> * get_l2_tlb_queue(){return m_tlb_request_queue;}

private: 

   unsigned m_id;
   const struct memory_config *m_config;
   class memory_stats_t *m_stats;
   class memory_sub_partition **m_sub_partition; 
   class dram_t *m_dram;

   std::list<mem_fetch*> * m_tlb_request_queue;

   class arbitration_metadata
   {
   public: 
      arbitration_metadata(const struct memory_config *config); 

      // check if a subpartition still has credit 
      bool has_credits(int inner_sub_partition_id) const; 
      // borrow a credit for a subpartition 
      void borrow_credit(int inner_sub_partition_id); 
      // return a credit from a subpartition 
      void return_credit(int inner_sub_partition_id); 

      // return the last subpartition that borrowed credit 
      int last_borrower() const { return m_last_borrower; } 

      void print( FILE *fp ) const; 
   private: 
      // id of the last subpartition that borrowed credit 
      int m_last_borrower; 

      int m_shared_credit_limit; 
      int m_private_credit_limit; 

      // credits borrowed by the subpartitions
      std::vector<int> m_private_credit; 
      int m_shared_credit; 
   }; 
   arbitration_metadata m_arbitration_metadata; 

   // determine wheither a given subpartition can issue to DRAM 
   bool can_issue_to_dram(int inner_sub_partition_id); 

   // model DRAM access scheduler latency (fixed latency between L2 and DRAM)
   struct dram_delay_t
   {
      unsigned long long ready_cycle;
      class mem_fetch* req;
   };
   std::list<dram_delay_t> m_dram_latency_queue;
};

class memory_sub_partition
{
public:
   memory_sub_partition( unsigned sub_partition_id, const struct memory_config *config, class memory_stats_t *stats );
   ~memory_sub_partition(); 

   unsigned get_id() const { return m_id; } 

   bool busy() const;

   void cache_cycle( unsigned cycle );

   bool full() const;
   void push( class mem_fetch* mf, unsigned long long clock_cycle );
   class mem_fetch* pop(); 
   class mem_fetch* top();
   void set_done( mem_fetch *mf );

   unsigned flushL2();
   void flushL2(unsigned appid)
   {
     m_L2cache->flush(appid);
   }
   
   
   //new
   void print_cache_stat(unsigned &accesses, unsigned &misses,
       std::vector<uint64_t>& accesses_s, std::vector<uint64_t>& misses_s, FILE *fp) const;

   float periodic_miss_rate(unsigned long long);
   float periodic_miss_rate_stream(unsigned long long, unsigned);
   float periodic_mpki_core( unsigned long long insn_data[], unsigned which_core);
   
   unsigned get_misses_core(unsigned which_core);
   unsigned get_misses_core_tlb(unsigned which_core);
   unsigned get_misses_core_data(unsigned which_core);
   unsigned get_accesses_core(unsigned which_core);
   unsigned get_accesses_core_tlb(unsigned which_core);
   unsigned get_accesses_core_data(unsigned which_core);

   float miss_rate;
   std::vector<float> miss_rate_stream;
   float miss_rate_core[64];

   // interface to L2_dram_queue
   bool L2_dram_queue_empty() const; 
   class mem_fetch* L2_dram_queue_top() const; 
   void L2_dram_queue_pop(); 

   // interface to dram_L2_queue
   bool dram_L2_queue_full() const; 
   void dram_L2_queue_push( class mem_fetch* mf ); 

   void visualizer_print( gzFile visualizer_file );
   void print_cache_stat(unsigned &accesses, unsigned &misses) const;
   void print( FILE *fp ) const;

   void accumulate_L2cache_stats(class cache_stats &l2_stats) const;
   void get_L2cache_sub_stats(struct cache_sub_stats &css ) const;
   


private:
// data
   unsigned m_id;  //< the global sub partition ID
   const struct memory_config *m_config;
   class l2_cache *m_L2cache;
   class dram_t *m_dram; //not sure new
   class L2interface *m_L2interface;
   partition_mf_allocator *m_mf_allocator;


   std::list<mem_fetch*> * m_tlb_bypassed_dram;

   // model delay of ROP units with a fixed latency
   struct rop_delay_t
   {
    	unsigned long long ready_cycle;
    	class mem_fetch* req;
   };
   std::queue<rop_delay_t> m_rop;

   // these are various FIFOs between units within a memory partition
   fifo_pipeline<mem_fetch> *m_icnt_L2_queue;
   fifo_pipeline<mem_fetch> *m_L2_dram_queue;
   fifo_pipeline<mem_fetch> *m_dram_L2_queue;
   fifo_pipeline<mem_fetch> *m_L2_icnt_queue; // L2 cache hit response queue

   /* fifo_pipeline<mem_fetch> *m_L2_dram_queue_fl; */
   std::queue<mem_fetch*> *m_L2_dram_queue_fl;
   fifo_pipeline<mem_fetch> *m_dram_L2_queue_fl;

   class mem_fetch *L2dramout; 
   unsigned long long int wb_addr;

   class memory_stats_t *m_stats;

   std::set<mem_fetch*> m_request_tracker;

   friend class L2interface;
};

class L2interface : public mem_fetch_interface {
public:
    L2interface( memory_sub_partition *unit ) { m_unit=unit; }
    virtual ~L2interface() {}
    virtual bool full( unsigned size, bool write) const 
    {
        // assume read and write packets all same size
        //pratheek
      if (m_unit->m_config->fixed_latency_enabled)
      {
        //tmp;
        /* mf->set_reply(); */
        /* mf->ready_cycle = gpu_tot_sim_cycle + gpu_sim_cycle + 100; */
        return false;
        /* return m_unit->m_L2_dram_queue_fl->full(); */
      }
      else
      {
        return m_unit->m_L2_dram_queue->full();
      }

        /* return m_unit->m_L2_dram_queue->full(); */
    }
    virtual void push(mem_fetch *mf) 
    {
        //This is called by m_memport->push in baseline cache cycle for L2
        mf->set_status(IN_PARTITION_L2_TO_DRAM_QUEUE,0/*FIXME*/);

        //pratheek
      if (m_unit->m_config->fixed_latency_enabled)
      {
        //tmp;
        mf->set_reply();
        mf->ready_cycle = gpu_tot_sim_cycle + gpu_sim_cycle + 100;
        m_unit->m_L2_dram_queue_fl->push(mf);
      }
      else
      {
        m_unit->m_L2_dram_queue->push(mf);
      }
        /* m_unit->m_L2_dram_queue->push(mf); */
    }
private:
    memory_sub_partition *m_unit;
};

#endif
