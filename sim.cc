#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "sim.h"
#include <algorithm>


/////////////////////////////////////////////////////////
///////////////////CACHE IMPLEMENTATION//////////////////
/////////////////////////////////////////////////////////

// log2 for powers of two (BLOCKSIZE and #sets are guaranteed powers of two).
static uint32_t log2u(uint32_t x) {
   uint32_t n = 0;
   while (x > 1) { x = x>>1; n++; }  //count number of divisions by 2
   return n;
}

// Implement Cache Constructor uppon declaration 
Cache::Cache(uint32_t size, uint32_t assoc, uint32_t blocksize, Cache *next,
uint32_t pref_n, uint32_t pref_m){
   this->assoc = assoc;
   this->next = next; 
   num_sets = size / (blocksize * assoc);
   offset_bits = log2u(blocksize);
   index_bits = log2u(num_sets);
   sets.assign(num_sets, std::vector<block_t>(assoc));

   this->pref_m = pref_m;
   sbs.assign(pref_n, stream_buffer_t());
   for (uint32_t s = 0; s < pref_n; s++) sbs[s].lru = s; //N stream buffers ranked 0..N-1
}

// Address    |      tag      |    index    |  offset |
void Cache::request(uint32_t addr, char rw){
   bool is_write = (rw == 'w');
   if (is_write) writes++; else reads++; // increment number of writes/reads for this cache

   uint32_t block_address = addr >> offset_bits; //remove offset bits at the end, only need block addr
   uint32_t index = block_address & (num_sets - 1); // mask only works b/c sets is a power of 2
   u_int32_t tag = block_address >> index_bits; 
   std::vector<block_t>&set = sets[index]; // create a reference, used to update conents

   // CACHE SEARCH: Search INDIVIDUAL set for a hit, search each way
   int way = -1;
   for (uint32_t w = 0; w < assoc; w++){
      if (set[w].valid  && set[w].tag == tag){
         way = (int)w; 
         break;
      }
   }
   // Did we hit in the Cache? 
   bool cache_hit = (way >= 0);

   // BUFFER SEARCH
   int sb = find_sb_hit(block_address);

   // CACHE MISS - even if hits in stream buffer, still has to be installed
   if(!cache_hit){
      // A "miss" means the block is in neither the cache nor a stream buffer
      if(sb < 0){
         if(is_write) write_misses++; else read_misses++;
      }
      //Step 1: Make room for new block - choose who to evict(LRU)
      way = (int)find_victim(index);
      block_t &victim = set[way]; 

      // Is this victim a valid dirty? 
      if(victim.dirty && victim.valid){
         writebacks++; //issue a writeback 
         // is there another cache level or just main memory
         if(next){
            uint32_t victim_addr = ((victim.tag << index_bits) | index) << offset_bits; //rebuild victim addr
            next->request(victim_addr, 'w'); // Writeback to next cache level 
         }
      }

      //Step 2: Bring in new block, update LRU
      //only go to next level cache/mem if SB misses too
      if(sb < 0 && next) next->request(block_address << offset_bits, 'r'); //Does next level cache have it? or main memory
      
      victim.valid = true;
      victim.dirty = false;
      victim.tag = tag; // Replacing the victim's contents with the right tag. Simulating pulling conents 
                        // from the other cache level.  
      victim.LRU = assoc; 
   }

   // UPDATE LRU AND DIRTY (IF WRITE)
   if(is_write) set[way].dirty = true;
   update_lru(index, uint32_t(way));

   //Stream Buffer Management 
   if(sb >= 0)
      sb_advance((uint32_t)sb, block_address); // scenarios #2 and #4: continue the stream
   else if(!cache_hit && !sbs.empty()) // check - do we have buffers enabled?
      sb_new_stream(block_address); // scenario #1
}



u_int32_t Cache::find_victim(uint32_t index){
   std::vector<block_t>&set = sets[index];
   uint32_t victim = 0;
   for(u_int32_t w = 0; w < assoc; w++){
      if(!set[w].valid) return w; // any invalid blocks starting from way 0 
      if(set[w].LRU > set[victim].LRU) victim = w; // running maximum, keep track of oldest block seen
   }
   return victim; 
}

void Cache::update_lru(u_int32_t index, uint32_t way){
   std::vector<block_t>&set = sets[index];
   uint32_t old = set[way].LRU; // save rank before changing
   for (uint32_t w = 0; w < assoc; w++) {
      if (w != way && set[w].valid && set[w].LRU < old) set[w].LRU++;
   }
   set[way].LRU = 0;
}

void Cache::print_contents(const char* name){
   printf("===== %s contents =====\n", name);
   for (uint32_t i = 0; i < num_sets; i++) {
      std::vector<const block_t *> order;
      for (uint32_t w = 0; w < assoc; w++)
         if (sets[i][w].valid) order.push_back(&sets[i][w]);
      if (order.empty()) continue;
      //lambda algorithm for sorting, if block a's lru is lower than b's, it goes first
      std::sort(order.begin(), order.end(),
                [](const block_t *a, const block_t *b) { return a->LRU < b->LRU; });
      printf("set %6u: ", i);
      for (const block_t *b : order)
         printf("%8x %c", b->tag, b->dirty ? 'D' : ' ');
      printf("\n");
   }
}



/////////////////////////////////////////////////////////
///////////////////SB IMPLEMENTATION//////////////////
/////////////////////////////////////////////////////////


// Return the MRU stream buffer that contains block_addr, or -1 if none does.
int Cache::find_sb_hit(uint32_t block_addr){
   int best = -1;
   for (uint32_t s = 0; s < sbs.size(); s++){
      const stream_buffer_t& b = sbs[s];
      if (b.valid && block_addr >= b.head && block_addr < b.head + pref_m){
         if(best < 0 || b.lru < sbs[best].lru) best = (int) s; // lowest LRU counter -> MRU
      }
   }
   return best;
}

// Scenario 1 - replace LRU stream buffer with X+1...X+M
void Cache::sb_new_stream(uint32_t block_addr){
   uint32_t s = 0; 
   for (uint32_t i = 1; i < sbs.size(); i++){
      if(sbs[i].lru > sbs[s].lru) s = i; //find LRU buffer rank
   }
   sbs[s].valid = true; 
   sbs[s].head = block_addr + 1;
   prefetches += pref_m; 

   sb_make_mru(s);
}

void Cache::sb_advance(uint32_t s, uint32_t block_addr){
   uint32_t new_head = block_addr + 1;
   prefetches += new_head - sbs[s].head;
   sbs[s].head = new_head;
   sb_make_mru(s);
}

void Cache::sb_make_mru(uint32_t s){
   uint32_t old = sbs[s].lru;
   for (uint32_t i = 0; i < sbs.size(); i++){
      if (i != s && sbs[i].lru < old) sbs[i].lru++;
   }
   sbs[s].lru = 0; // Make MRU, set counter to 0
}


// Print valid stream buffers MRU -> LRU, each as M block addresses.
void Cache::print_stream_buffers() {
   printf("===== Stream Buffer(s) contents =====\n");
   std::vector<const stream_buffer_t *> order;
   for (const stream_buffer_t &b : sbs)
      if (b.valid) order.push_back(&b);
   std::sort(order.begin(), order.end(),
             [](const stream_buffer_t *a, const stream_buffer_t *b) { return a->lru < b->lru; });
   for (const stream_buffer_t *b : order) {
      for (uint32_t k = 0; k < pref_m; k++)
         printf("%8x ", b->head + k);
      printf("\n");
   }
}




















































/*  "argc" holds the number of command-line arguments.
    "argv[]" holds the arguments themselves.

    Example:
    ./sim 32 8192 4 262144 8 3 10 gcc_trace.txt
    argc = 9
    argv[0] = "./sim"
    argv[1] = "32"
    argv[2] = "8192"
    ... and so on
*/
int main (int argc, char *argv[]) {
   FILE *fp;			// File pointer.
   char *trace_file;		// This variable holds the trace file name.
   cache_params_t params;	// Look at the sim.h header file for the definition of struct cache_params_t.
   char rw;			// This variable holds the request's type (read or write) obtained from the trace.
   uint32_t addr;		// This variable holds the request's address obtained from the trace.
				// The header file <inttypes.h> above defines signed and unsigned integers of various sizes in a machine-agnostic way.  "uint32_t" is an unsigned integer of 32 bits.

   // Exit with an error if the number of command-line arguments is incorrect.
   if (argc != 9) {
      printf("Error: Expected 8 command-line arguments but was provided %d.\n", (argc - 1));
      exit(EXIT_FAILURE);
   }
    
   // "atoi()" (included by <stdlib.h>) converts a string (char *) to an integer (int).
   params.BLOCKSIZE = (uint32_t) atoi(argv[1]);
   params.L1_SIZE   = (uint32_t) atoi(argv[2]);
   params.L1_ASSOC  = (uint32_t) atoi(argv[3]);
   params.L2_SIZE   = (uint32_t) atoi(argv[4]);
   params.L2_ASSOC  = (uint32_t) atoi(argv[5]);
   params.PREF_N    = (uint32_t) atoi(argv[6]);
   params.PREF_M    = (uint32_t) atoi(argv[7]);
   trace_file       = argv[8];

   // Open the trace file for reading.
   fp = fopen(trace_file, "r");
   if (fp == (FILE *) NULL) {
      // Exit with an error if file open failed.
      printf("Error: Unable to open file %s\n", trace_file);
      exit(EXIT_FAILURE);
   }
    
   // Print simulator configuration.
   printf("===== Simulator configuration =====\n");
   printf("BLOCKSIZE:  %u\n", params.BLOCKSIZE);
   printf("L1_SIZE:    %u\n", params.L1_SIZE);
   printf("L1_ASSOC:   %u\n", params.L1_ASSOC);
   printf("L2_SIZE:    %u\n", params.L2_SIZE);
   printf("L2_ASSOC:   %u\n", params.L2_ASSOC);
   printf("PREF_N:     %u\n", params.PREF_N);
   printf("PREF_M:     %u\n", params.PREF_M);
   printf("trace_file: %s\n", trace_file);
   printf("\n");


   // BUILD MEMORY HIERARCHY 
   Cache* L2 = NULL;
   if(params.L2_SIZE > 0){
      L2 = new Cache(params.L2_SIZE, params.L2_ASSOC, params.BLOCKSIZE, NULL, params.PREF_N, params.PREF_M);
   }
   Cache L1(params.L1_SIZE, params.L1_ASSOC, params.BLOCKSIZE, L2, L2 ? 0 : params.PREF_N, L2 ? 0 : params.PREF_M);

   // HANLDE REQUESTS
   while (fscanf(fp, "%c %x\n", &rw, &addr) == 2) {	// Stay in the loop if fscanf() successfully parsed two tokens as specified.
      if (rw != 'r' && rw != 'w') {
         printf("Error: Unknown request type %c.\n", rw);
	   exit(EXIT_FAILURE);
      }

      
      ///////////////////////////////////////////////////////
      // Issue the request to the L1 cache instance here.
      L1.request(addr, rw);
      ///////////////////////////////////////////////////////
    }

    //////////////////////////////////////////////////////////
    /////////////OUTPUT CONTENTS//////////////////////////////
    //////////////////////////////////////////////////////////
    L1.print_contents("L1");
    printf("\n");
    if(L2){
      L2->print_contents("L2");
      printf("\n");
    }
    if (params.PREF_N > 0){
      Cache *last = L2 ? L2 : &L1;
      last->print_stream_buffers();
      printf("\n");
    }

    // Phase 3: L2 + prefetching optioins  
    double   l1_miss_rate = (double)(L1.read_misses + L1.write_misses) / (L1.reads + L1.writes);
    // L2 measurements (all 0 when there is no L2).
    uint32_t l2_reads = 0, l2_read_misses = 0, l2_writes = 0, l2_write_misses = 0, l2_writebacks = 0;
    uint32_t l2_prefetches = 0;
    double   l2_miss_rate = 0.0;
    if (L2) {
       l2_reads        = L2->reads;
       l2_read_misses  = L2->read_misses;
       l2_writes       = L2->writes;
       l2_write_misses = L2->write_misses;
       l2_writebacks   = L2->writebacks;
       l2_prefetches   = L2->prefetches;
       l2_miss_rate    = (double) l2_read_misses / l2_reads;     // n = i / h
    }

    // Memory traffic = blocks moved to/from main memory, counted at the last level.
    uint32_t traffic;
    if (L2) traffic = l2_read_misses + l2_write_misses + l2_writebacks + l2_prefetches;      // i + k + m + o + p
    else    traffic = L1.read_misses + L1.write_misses + L1.writebacks + L1.prefetches;      // b + d + f + g

    printf("===== Measurements =====\n");
    printf("a. L1 reads:                   %u\n", L1.reads);
    printf("b. L1 read misses:             %u\n", L1.read_misses);
    printf("c. L1 writes:                  %u\n", L1.writes);
    printf("d. L1 write misses:            %u\n", L1.write_misses);
    printf("e. L1 miss rate:               %.4f\n", l1_miss_rate);
    printf("f. L1 writebacks:              %u\n", L1.writebacks);
    printf("g. L1 prefetches:              %u\n", L1.prefetches);
    printf("h. L2 reads (demand):          %u\n", l2_reads);
    printf("i. L2 read misses (demand):    %u\n", l2_read_misses);
    printf("j. L2 reads (prefetch):        %u\n", 0);
    printf("k. L2 read misses (prefetch):  %u\n", 0);
    printf("l. L2 writes:                  %u\n", l2_writes);
    printf("m. L2 write misses:            %u\n", l2_write_misses);
    printf("n. L2 miss rate:               %.4f\n", l2_miss_rate);
    printf("o. L2 writebacks:              %u\n", l2_writebacks);
    printf("p. L2 prefetches:              %u\n", l2_prefetches);
    printf("q. memory traffic:             %u\n", traffic);

    delete L2;

    return(0);
}
