#include "MemoryManager.h"
#include <math.h>
#include <iostream>
#include <fcntl.h>
#include <unistd.h>


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

    // Save the size in words for later use
    this->sizeInWords = sizeInWords;
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
    
    // Fetch the hole list
    void *holeList = getList();
    
    // Call the allocator function to get the offset in words
    size_t offsetInWords = allocator(sizeInWords, holeList);

    // Deallocate the hole list (Dynamically allocated in getList)
    delete[] static_cast<uint16_t*>(holeList);

    // Convert the offset in words to an offset in bytes
    uint8_t offsetInBytes = offsetInWords * wordSize;
    
    // Update the chosen hole
    for (auto it = holes.begin(); it != holes.end(); ++it)
    {
        // Matching hole found
        if (it->offset == offsetInWords)
        {
            // Update the hole offset and size
            it->offset += sizeInWords;
            it->size -= sizeInWords;

            // Remove the hole if it has no size
            if (it->size == 0) { holes.erase(it); }

            // Done: exit the loop
            break;
        }
    }

    uint8_t *allocationAddress = memoryBlock + offsetInBytes;

    // Add the allocation to the allocations map
    allocations[allocationAddress] = sizeInWords;

    // Return a pointer to the newly allocated memory
    return (memoryBlock + offsetInBytes);
}

void MemoryManager::free(void *address)
{
    // Check if the address is allocated
    auto it = allocations.find((uint8_t*)address);
    if (it == allocations.end()) { return; } // Address not found
    size_t sizeInWords = it->second; // Address found: get the size in words

    // Remove the allocation from the allocations map
    allocations.erase(it);

    // Determine the offset in bytes (difference between the address and the memory block)
    size_t offsetInBytes = (uint8_t *)address - memoryBlock;

    // Convert the offset in bytes to an offset in words
    size_t offsetInWords = offsetInBytes / wordSize;

    for (auto it = holes.begin(); it != holes.end(); ++it)
    {
        // Check if a hole is adjacent to the left of the deallocated memory
        if (it->offset + it->size == offsetInWords)
        {
            // Extend the hole to the right
            it->size += sizeInWords;

            // Check if a hole is adjacent to the right of the deallocated memory (double adjacent)
            auto itNext = std::next(it);
            if ((itNext != holes.end()) && (itNext->offset == offsetInWords + sizeInWords))
            {
                // Extend the first hole further right
                it->size += (itNext)->size; 

                // Remove the 2nd hole (left adjacent)
                holes.erase(itNext);

                return;
            }

            return;
        }

        // Check if a hole is only adjacent to the right of the deallocated memory
        if (it->offset == offsetInWords + sizeInWords)
        {
            // Extend the hole to the left
            it->offset -= sizeInWords; 
            it->size += sizeInWords;
            return;
        }
        
        // Check if the deallocated memory is left of the current hole but not adjacent to any
        if (offsetInWords < it->offset)
        {
            Hole newHole { offsetInWords, sizeInWords };
            holes.insert(it, newHole);
            return;
        }
    }

    // Deallocated memory is at the very end and non-adjacent to any hole
    Hole newHole { offsetInWords, sizeInWords };
    holes.push_back(newHole);
    return;
}

void MemoryManager::setAllocator(std::function<int(int, void *)> allocator) { this->allocator = allocator; }

int MemoryManager::dumpMemoryMap(char *filename)
{
    // Open/create the file for writing
    int openedFile = open(filename, O_TRUNC | O_CREAT | O_WRONLY, 0644);

    // Check if the openedFile was opened/created successfully
    if (openedFile == -1) { return -1; } 

    // Fetch the hole list
    void *holeList = getList();

    // Ensure the holeList is valid
    if (holeList == nullptr) 
    { 
        // Cleanup and return error
        close(openedFile);
        return -1; 
    }

    // Fetch the hole count
    size_t holeCount = ((uint16_t *)holeList)[0];
    
    // Text vector
    std::vector<std::string> textVector;

    for (size_t i = 1; i <= holeCount * 2; i++)
    {
        if (i % 2 != 0)
        {
            // i is odd, push offset
            textVector.push_back("[" + std::to_string(((uint16_t *)holeList)[i]) + ", ");
        }
        else
        {
            // i is even, push size
            textVector.push_back(std::to_string(((uint16_t *)holeList)[i]) + "]");

            if (i != holeCount * 2)
            {
                // Add " - " if it's not the last element
                textVector.push_back(" - ");
            }
        }
    }

    // Deallocate the hole list (Dynamically allocated in getList)
    delete[] static_cast<uint16_t*>(holeList);

    // Write the text vector to the openedFile
    for (auto text : textVector)
    {
        write(openedFile, text.c_str(), text.length());
    }

    // Close the file
    close(openedFile);

    // Success
    return 0;
}

void *MemoryManager::getBitmap()
{
    // Determine the size (bytes) needed for the bitmap
    size_t bitmapSize = sizeInWords / 8;

    // If there is a remainder, one more byte is required
    if (sizeInWords % 8 != 0) { bitmapSize++; }

    // Begin the bitmap with all 1s (all holes)
    uint8_t *bitmap = new uint8_t[bitmapSize];
    for (size_t i = 0; i < bitmapSize; i++) { bitmap[i] = 0xFF; }

    // Iterate through the holes and mark 0s in the bitmap when a hole is found
    for (auto it = holes.begin(); it != holes.end(); ++it)
    {
        // Set the hole bits to 0 in the bitmap
        for (size_t i = 0; i < it->size; ++i) 
        {
            size_t bitIndex = it->offset + i; // Hole offset + i
            size_t bit = bitIndex % 8; // Get the bit index within the byte
            size_t byte = bitIndex / 8; // Get the byte index in the bitmap
            bitmap[byte] &= ~(1 << bit); // Use a mask of 1 that targets the bit, then use ~ to flip it to 0
        }
    }

    // Reverse the order of the bits in each byte of the bitmap
    uint8_t original = 0;
    uint8_t reversed = 0;
    for (size_t byteIndex = 0; byteIndex < bitmapSize; ++byteIndex)
    {
        original = bitmap[byteIndex];
        reversed = 0;

        for (int bit = 0; bit < 8; ++bit)
        {
            // Check each bit in the original byte for a 1 from left to right
            if (original & (1 << bit))
            {
                // If original bit is 1, shift it left in the reversed byte. Use |= to keep previous bits
                reversed |= (1 << (7 - bit));
            }
        }

        bitmap[byteIndex] = reversed;
    }

    // Fetch the size of the bitmap in bytes
    uint16_t bitmapSizeInBytes = bitmapSize;

    // Create a new bitmap to hold the final result
    uint8_t *finalBitmap = new uint8_t[bitmapSize + 2];

    finalBitmap[0] = bitmapSizeInBytes & 0xFF; // Keep the rightmost 8 bits via masking and put it in the first byte
    finalBitmap[1] = (bitmapSizeInBytes >> 8) & 0xFF; // Same thing, but shift right first, then mask, and put it in the second byte

    // Fetch the bitmap data and copy it to the final bitmap
    for (size_t i = 0; i < bitmapSize; i++) { finalBitmap[i + 2] = bitmap[i]; }

    // Prevent memory leaks
    delete[] bitmap;

    return finalBitmap;
}

unsigned MemoryManager::getWordSize() { return wordSize; }

void *MemoryManager::getMemoryStart() { return memoryBlock; }

unsigned MemoryManager::getMemoryLimit() { return sizeInWords * wordSize; }

int bestFit(int sizeInWords, void *list)
{
    // Cast to original type
    uint16_t *holeList = (uint16_t *)list;
    size_t holeCount = holeList[0];

    // Create variables to keep track of the best fit
    size_t bestFitOffset = 10000;
    size_t bestFitSize = 10000;

    // Initialize hole size
    size_t holeSize = 0;

    // Loop through the list of holes
    for (size_t i = 1; i < holeCount * 2; i += 2)
    {
        // Determine hole size
        holeSize = holeList[i + 1];

        // Check if the hole is large enough
        if (holeSize >= sizeInWords)
        {
            // See if new bestFitSize
            if (holeSize < bestFitSize)
            {
                // Update the best fit size and offset
                bestFitSize = holeSize;
                bestFitOffset = holeList[i];
            }
        }
    }
    
    if (bestFitOffset == 10000)
    {
        // No fit found
        return -1;
    }
    else
    {
        return bestFitOffset;
    }
}

int worstFit(int sizeInWords, void *list)
{
    // Cast to original type
    uint16_t *holeList = (uint16_t *)list;
    size_t holeCount = holeList[0];

    // Create variables to keep track of the best fit
    size_t worstFitOffset = 10000;
    size_t worstFitSize = 0;

    // Initialize hole size
    size_t holeSize = 0;

    // Loop through the list of holes
    for (size_t i = 1; i < holeCount * 2; i += 2)
    {
        // Determine hole size
        holeSize = holeList[i + 1];

        // Check if the hole is large enough
        if (holeSize >= sizeInWords)
        {
            // See if new worstFitSize
            if (holeSize > worstFitSize)
            {
                // Update the best fit size and offset
                worstFitSize = holeSize;
                worstFitOffset = holeList[i];
            }
        }
    }
    
    if (worstFitOffset == 10000)
    {
        // No fit found
        return -1;
    }
    else
    {
        return worstFitOffset;
    }
}