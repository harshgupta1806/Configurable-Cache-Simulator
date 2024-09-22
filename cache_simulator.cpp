#include <iostream>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <list>

using namespace std;

// Cache block structure
struct CacheBlock {
    unsigned long tag;
    bool valid;
    bool dirty;

    CacheBlock() : tag(0), valid(false), dirty(false) {}
};

// Cache set structure (for associativity)
struct CacheSet {
    list<CacheBlock> blocks;
    unordered_map<unsigned long, list<CacheBlock>::iterator> blockMap;

    CacheSet(int assoc) {
        for (int i = 0; i < assoc; ++i) {
            blocks.emplace_back();
        }
    }

    CacheBlock* findBlock(unsigned long tag) {
        if (blockMap.find(tag) != blockMap.end()) {
            return &(*blockMap[tag]);
        }
        return nullptr;
    }

    void moveToMRU(CacheBlock* block) {
        blocks.erase(blockMap[block->tag]);
        blocks.push_front(*block);
        blockMap[block->tag] = blocks.begin();
    }

    CacheBlock* evictLRU() {
        CacheBlock* lruBlock = &blocks.back();
        blockMap.erase(lruBlock->tag);
        blocks.pop_back();
        return lruBlock;
    }

    void insertBlock(CacheBlock block) {
        blocks.push_front(block);
        blockMap[block.tag] = blocks.begin();
    }

    bool isFull() {
        return blockMap.size() == blocks.size();
    }
};

// Cache structure
class Cache {
public:
    int size;
    int assoc;
    int blockSize;
    int numSets;
    vector<CacheSet> sets;

    // Statistics
    int numReads = 0;
    int numReadMisses = 0;
    int numWrites = 0;
    int numWriteMisses = 0;
    int numWriteBacks = 0;

    Cache(int size, int assoc, int blockSize)
        : size(size), assoc(assoc), blockSize(blockSize) {
        numSets = size / (assoc * blockSize);
        sets = vector<CacheSet>(numSets, CacheSet(assoc));
    }

    CacheBlock* accessCache(unsigned long address) {
        unsigned long setIndex = (address / blockSize) % numSets;
        unsigned long tag = address / (blockSize * numSets);
        CacheSet& set = sets[setIndex];
        CacheBlock* block = set.findBlock(tag);
        if (block) {
            set.moveToMRU(block);  // Update LRU position
            return block;          // Cache hit
        }
        return nullptr;  // Cache miss
    }

    void writeToCache(unsigned long address) {
        unsigned long setIndex = (address / blockSize) % numSets;
        unsigned long tag = address / (blockSize * numSets);
        CacheSet& set = sets[setIndex];
        CacheBlock* block = set.findBlock(tag);

        if (block) {
            set.moveToMRU(block);
            block->dirty = true;  // Mark block as dirty on write
        } else {
            // Cache miss
            CacheBlock newBlock;
            newBlock.tag = tag;
            newBlock.valid = true;
            newBlock.dirty = true;

            if (set.isFull()) {
                CacheBlock* evicted = set.evictLRU();
                if (evicted->dirty) {
                    numWriteBacks++;
                    cout << "Writing back dirty block to next level cache\n";
                }
            }

            set.insertBlock(newBlock);
        }
    }

    void printCacheStats() {
        cout << "Number of reads: " << numReads << endl;
        cout << "Number of read misses: " << numReadMisses << endl;
        cout << "Number of writes: " << numWrites << endl;
        cout << "Number of write misses: " << numWriteMisses << endl;
        cout << "Number of writebacks: " << numWriteBacks << endl;
    }
};

class VictimCache {
public:
    int numBlocks;
    list<CacheBlock> blocks;
    unordered_map<unsigned long, list<CacheBlock>::iterator> blockMap;

    VictimCache(int numBlocks) : numBlocks(numBlocks) {
        for (int i = 0; i < numBlocks; ++i) {
            blocks.emplace_back();
        }
    }

    CacheBlock* findBlock(unsigned long tag) {
        if (blockMap.find(tag) != blockMap.end()) {
            return &(*blockMap[tag]);
        }
        return nullptr;
    }

    void moveToMRU(CacheBlock* block) {
        blocks.erase(blockMap[block->tag]);
        blocks.push_front(*block);
        blockMap[block->tag] = blocks.begin();
    }

    CacheBlock* evictLRU() {
        CacheBlock* lruBlock = &blocks.back();
        blockMap.erase(lruBlock->tag);
        blocks.pop_back();
        return lruBlock;
    }

    void insertBlock(CacheBlock block) {
        blocks.push_front(block);
        blockMap[block.tag] = blocks.begin();
    }

    bool isFull() {
        return blockMap.size() == blocks.size();
    }
};

// Simulation function to handle cache and victim cache interaction
void simulateCacheHierarchy(Cache& L1, Cache* L2, VictimCache* VC, string traceFile) {
    ifstream trace(traceFile);
    if (!trace.is_open()) {
        cerr << "Error opening trace file.\n";
        return;
    }

    string line;
    while (getline(trace, line)) {
        char op;
        unsigned long address;
        sscanf(line.c_str(), "%c %lx", &op, &address);

        if (op == 'r') {
            L1.numReads++;
            CacheBlock* block = L1.accessCache(address);
            if (!block) {
                L1.numReadMisses++;
                if (VC) {
                    CacheBlock* victimBlock = VC->findBlock(address);
                    if (victimBlock) {
                        VC->moveToMRU(victimBlock);
                        // Swap with L1 (not fully implemented here for simplicity)
                    } else {
                        if (L2) {
                            block = L2->accessCache(address);
                        }
                        // Fetch from memory if L2 also misses
                    }
                }
            }
        } else if (op == 'w') {
            L1.numWrites++;
            CacheBlock* block = L1.accessCache(address);
            if (!block) {
                L1.numWriteMisses++;
            }
            L1.writeToCache(address);
        }
    }

    trace.close();
}

int main(int argc, char* argv[]) {
    if (argc < 7) {
        cerr << "Usage: ./cache_sim <L1_SIZE> <L1_ASSOC> <L1_BLOCKSIZE> <VC_NUM_BLOCKS> <L2_SIZE> <L2_ASSOC> <trace_file>\n";
        return 1;
    }

    // Parse input arguments
    int L1_SIZE = stoi(argv[1]);
    int L1_ASSOC = stoi(argv[2]);
    int L1_BLOCKSIZE = stoi(argv[3]);
    int VC_NUM_BLOCKS = stoi(argv[4]);
    int L2_SIZE = stoi(argv[5]);
    int L2_ASSOC = stoi(argv[6]);
    string traceFile = argv[7];

    // Create L1 cache
    Cache L1(L1_SIZE, L1_ASSOC, L1_BLOCKSIZE);

    // Create L2 cache if applicable
    Cache* L2 = nullptr;
    if (L2_SIZE > 0) {
        L2 = new Cache(L2_SIZE, L2_ASSOC, L1_BLOCKSIZE);
    }

    // Create Victim Cache if applicable
    VictimCache* VC = nullptr;
    if (VC_NUM_BLOCKS > 0) {
        VC = new VictimCache(VC_NUM_BLOCKS);
    }

    // Run the simulation
    simulateCacheHierarchy(L1, L2, VC, traceFile);

    // Print cache statistics
    cout << "L1 Cache Stats:" << endl;
    L1.printCacheStats();

    if (L2) {
        cout << "L2 Cache Stats:" << endl;
        L2->printCacheStats();
    }

    // Clean up dynamic memory if needed
    if (L2) delete L2;
    if (VC) delete VC;

    return 0;
}
