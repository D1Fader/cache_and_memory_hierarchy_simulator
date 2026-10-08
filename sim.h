#ifndef SIM_CACHE_H
#define SIM_CACHE_H
#include <inttypes.h>
#include <vector>

typedef 
struct {
   uint32_t BLOCKSIZE;
   uint32_t L1_SIZE;
   uint32_t L1_ASSOC;
   uint32_t L2_SIZE;
   uint32_t L2_ASSOC;
   uint32_t PREF_N;
   uint32_t PREF_M;
} cache_params_t;

// Put additional data structures here as per your requirement.

// Memory Block Structure to Hold necessary info (LRU count, Valid, Dirty etc.)
struct block_t{
   uint32_t tag = 0;
   bool valid = false;
   bool dirty = false;
   uint32_t LRU = 0;
};

// Stream Buffer
struct stream_buffer_t{
   bool valid = false;
   uint32_t head = 0;
   uint32_t lru = 0; //replace the LRU buffer when creating a new stream

};

// Generic Cache Class
class Cache{
   public:
   // Constructor -- updated to inlcude stream buffer paramaters 
   Cache(uint32_t size, uint32_t assoc, u_int32_t blocksize, Cache* next, 
      uint32_t pref_n = 0, uint32_t pref_m = 0);

   // interface for generic requests 
   void request(uint32_t addr, char rw);

   // function to print cache contents
   void print_contents(const char* name);

   // added print function for stream buffer functionality 
   void print_stream_buffers();

   // Measurements for this level.
   uint32_t reads        = 0;
   uint32_t read_misses  = 0;
   uint32_t writes       = 0;
   uint32_t write_misses = 0;
   uint32_t writebacks   = 0;
   uint32_t prefetches   = 0;   // prefetch requests issued to the next level

   private:
   // Other important details about the cache that other objects
   // don't need to access, keep private. 
   uint32_t num_sets;
   uint32_t assoc;
   uint32_t offset_bits;    // log2(BLOCKSIZE)
   uint32_t index_bits;     // log2(num_sets)
   Cache   *next;           // next level down; NULL = main memory

   std::vector<std::vector<block_t>> sets;   // sets[index][way]
   // private functions for replacement or hits/misses

   uint32_t pref_m;   // M blocks per stream buffer
   std::vector<stream_buffer_t> sbs; // N stream buffers (empty vector = prefetcher disabled)
   uint32_t find_victim(uint32_t index);
   void     update_lru(uint32_t index, uint32_t way);
   // STREAM BUFFER HELPER FUNCTIONS
   int      find_sb_hit(uint32_t block_addr); //Which SB holds X? Picks the MRU one if several do
   void     sb_new_stream(uint32_t block_addr); //Scenario #1: load X+1 … X+M into the LRU SB
   void     sb_advance(uint32_t s, uint32_t block_addr); //Scenarios #2/#4: drop entries up to X, refill to X+M
   void     sb_make_mru(uint32_t s); //LRU update across the SBs
};




#endif
