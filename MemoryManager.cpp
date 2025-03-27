#include "MemoryManager.h"
#include <math.h>

MemoryManager::MemoryManager(unsigned wordSize, std::function<int(int, void *)> allocator)
{
    // Store the word size and allocator function
    this->wordSize = wordSize;
    this->allocator = allocator;
}

MemoryManager::~MemoryManager()
{
    shutdown();
}

void MemoryManager::initialize(size_t sizeInWords)
{
    // Check if the size in words or the word size is 0
    if (sizeInWords == 0 || wordSize == 0) { return; }

    // Check if the words are no larger than 65536
    if (sizeInWords > 65536) { return; }

    // Check if the memory block has already been allocated
    if (memoryBlock != nullptr) { shutdown(); }
    
    // Allocate the memory block
    memoryBlock = new uint8_t[sizeInWords * wordSize];

    // Build the big hole
    holes.push_back(Hole { 0, sizeInWords });
}

void MemoryManager::shutdown()
{
    // Deallocate the memory block created by the initialize function
    delete[] memoryBlock;

    // Reset the word size, allocator, memory block, and holes
    wordSize = 0;
    allocator = nullptr;
    memoryBlock = nullptr;
    holes = {};
}

void *MemoryManager::getList()
{
    // Count holes
    int holeCount = holes.size();

    // Create a list of holes
    uint16_t *holeList = new uint16_t[1 + (holeCount * 2)];

    // Set the first element to the hole count
    holeList[0] = holeCount;

    int index = 1;

    // Loop through and grab the offset and size of each hole
    for (auto it = holes.begin(); it != holes.end(); ++it)
    {
        holeList[index] = it->offset;
        index++;
        holeList[index] = it->size;
        index++;
    }

    return holeList;
}

// INCOMPLETE
void *MemoryManager::allocate(size_t sizeInBytes)
{
    // Check if the size in bytes is 0
    if (sizeInBytes == 0) { return nullptr; }

    // Check if the memory block is null
    if (memoryBlock == nullptr) { return nullptr; }

    // Calculate the size in words
    size_t sizeInWords = sizeInBytes / wordSize;
    size_t remainder = sizeInBytes % wordSize; // Check for a remainder
    if (remainder > 0) { sizeInWords++; } // If there is a remainder, bump up by one word
    
    // Search for the first hole that fits the sizeInWords
    for (auto it = holes.begin(); it != holes.end(); ++it)
    {
        // First hole that fits
        if (it->size >= sizeInWords)
        {
            // Allocate the memory
            int offset = it->offset;
            allocator(sizeInWords, memoryBlock + offset * wordSize);

            // Change the hole:
            it->offset += sizeInWords; // Increment the offset by the size in words
            it->size -= sizeInWords; // Decrement the size by the size in words

            // Leave no empty holes
            if (it->size == 0)
            {
                holes.erase(it); // Remove the hole at the iterator
            }

            // Return the memory
            return memoryBlock + (offset * wordSize);
        }
    }

    // No man's land
    return nullptr;
}

// INCOMPLETE
void MemoryManager::free(void *address)
{

}

