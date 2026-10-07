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
Cache::Cache(uint32_t size, uint32_t assoc, uint32_t blocksize, Cache *next){
   this->assoc = assoc;
   this->next = next; 
   num_sets = size / (blocksize * assoc);
   offset_bits = log2u(blocksize);
   index_bits = log2u(num_sets);
   sets.assign(num_sets, std::vector<block_t>(assoc));
}

// Address    |      tag      |    index    |  offset |
void Cache::request(uint32_t addr, char rw){
   bool is_write = (rw == 'w');
   if (is_write) writes++; else reads++; // increment number of writes/reads for this cache

   uint32_t block_address = addr >> offset_bits; //remove offset bits at the end, only need block addr
   uint32_t index = block_address & (num_sets - 1); // mask only works b/c sets is a power of 2
   u_int32_t tag = block_address >> index_bits; 
   std::vector<block_t>&set = sets[index]; // create a reference, used to update conents

   // Search INDIVIDUAL set for a hit, search each way
   int way = -1;
   for (uint32_t w = 0; w < assoc; w++){
      if (set[w].valid  && set[w].tag == tag){
         way = (int)w; 
         break;
      }
   }

   // MISS 
   if(way < 0){
      if(is_write) write_misses++; else read_misses++;

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
      if(next) next->request(block_address << index_bits, 'r'); //Does next level cache have it? or main memory
      
      victim.valid = true;
      victim.dirty = false;
      victim.tag = tag; // Replacing the victim's contents with the right tag. Simulating pulling conents 
                        // from the other cache level.  
      victim.LRU = assoc; 
   }

   // UPDATE LRU AND DIRTY (IF WRITE)
   if(is_write) set[way].dirty = true;
   update_lru(index, uint32_t(way));
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
   Cache L1(params.L1_SIZE, params.L1_ASSOC, params.BLOCKSIZE, NULL);

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

    // Phase 1: no L2, no prefetcher -> those measurements are 0.
    uint32_t traffic = L1.read_misses + L1.write_misses + L1.writebacks;   // b + d + f + g
    double   l1_miss_rate = (double)(L1.read_misses + L1.write_misses) / (L1.reads + L1.writes);

    printf("===== Measurements =====\n");
    printf("a. L1 reads:                   %u\n", L1.reads);
    printf("b. L1 read misses:             %u\n", L1.read_misses);
    printf("c. L1 writes:                  %u\n", L1.writes);
    printf("d. L1 write misses:            %u\n", L1.write_misses);
    printf("e. L1 miss rate:               %.4f\n", l1_miss_rate);
    printf("f. L1 writebacks:              %u\n", L1.writebacks);
    printf("g. L1 prefetches:              %u\n", 0);
    printf("h. L2 reads (demand):          %u\n", 0);
    printf("i. L2 read misses (demand):    %u\n", 0);
    printf("j. L2 reads (prefetch):        %u\n", 0);
    printf("k. L2 read misses (prefetch):  %u\n", 0);
    printf("l. L2 writes:                  %u\n", 0);
    printf("m. L2 write misses:            %u\n", 0);
    printf("n. L2 miss rate:               %.4f\n", 0.0);
    printf("o. L2 writebacks:              %u\n", 0);
    printf("p. L2 prefetches:              %u\n", 0);
    printf("q. memory traffic:             %u\n", traffic);

    return(0);
}
