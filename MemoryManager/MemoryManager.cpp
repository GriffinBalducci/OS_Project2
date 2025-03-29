#include "MemoryManager.h"
#include <math.h>
#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>


MemoryManager::MemoryManager(unsigned wordSize, std::function<int(int, void *)> allocator)
{
    this->wordSize = wordSize;
    this->allocator = allocator;
}

MemoryManager::~MemoryManager() { shutdown(); }

void MemoryManager::initialize(size_t sizeInWords)
{
    if (sizeInWords == 0 || wordSize == 0) { return; }
    if (sizeInWords > 65536) { return; }
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

    // Reset the memory block and holes
    memoryBlock = nullptr;
    holes.clear();
    allocations.clear();
}

void *MemoryManager::getList()
{
    // Count holes
    int holeCount = holes.size();

    // Create a list of holes
    uint16_t *holeList = new uint16_t[1 + (holeCount * 2)];

    // Set the first element to the hole count
    holeList[0] = holeCount;

    // Loop through and grab the offset and size of each hole
    int index = 1;
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
    if (sizeInBytes == 0) { return nullptr; }
    if (!memoryBlock) { return nullptr; }

    // Calculate the size in words needed for the allocation
    size_t sizeInWords = sizeInBytes / wordSize;
    size_t remainder = sizeInBytes % wordSize; // Check for a remainder
    if (remainder > 0) { sizeInWords++; } // If there is a remainder, bump up by one word
    
    // Ensure the size in words does not exceed memory size
    if (sizeInWords > this->sizeInWords) { return nullptr; }
    
    // Fetch the hole list
    void *holeList = getList();
    
    // Call the allocator function to get the offset in words
    int offset = allocator(sizeInWords, holeList);

    delete[] static_cast<uint16_t*>(holeList);
    
    // Ensure allocation worked
    if (offset == -1) { return nullptr; }

    // Convert the offset in words to a size_t
    size_t offsetInWords = static_cast<size_t>(offset);

    // Convert the offset in words to an offset in bytes
    size_t offsetInBytes = (offsetInWords * wordSize);
    
    // Update the fitting hole
    // Search through hole list
    for (auto it = holes.begin(); it != holes.end(); ++it)
    {
        // If offset of hole matches, hole is matched
        if (it->offset == offsetInWords)
        {
            it->offset += sizeInWords;
            it->size -= sizeInWords;

            // Leave no empty hole
            if (it->size == 0) { holes.erase(it); }

            break;
        }
    }

    // Calculate the allocation address for the allocation map
    uint8_t *allocationAddress = (memoryBlock + offsetInBytes);

    allocations[allocationAddress] = sizeInWords;

    // Return a pointer to the newly allocated memory
    return (memoryBlock + offsetInBytes);
}

void MemoryManager::free(void *address)
{
    if (!memoryBlock) { return; }

    // If the given address in before or after the memory block, return null
    if ((address < memoryBlock) || (address >= memoryBlock + (sizeInWords * wordSize))) { return; }
    
    // Ensure the address is allocated
    auto it = allocations.find((uint8_t*)address); // Look for address
    if (it == allocations.end()) { return; } // Address not found
    size_t sizeInWords = it->second; // Address found: get the size in words

    allocations.erase(it);

    // Determine the offset in bytes (difference between the address and the memory block)
    size_t offsetInBytes = ((uint8_t *)address - memoryBlock);

    // Convert the offset in bytes to an offset in words
    size_t offsetInWords = (offsetInBytes / wordSize);

    // Perform hole updating
    for (auto it = holes.begin(); it != holes.end(); ++it)
    {
        // Check if a hole is adjacent to the left of the deallocated memory
        if ((it->offset + it->size) == offsetInWords)
        {
            // Extend the hole to the right
            it->size += sizeInWords;

            // Check if a hole is adjacent to the right of the deallocated memory (double adjacent)
            auto itNext = std::next(it);
            if ((itNext != holes.end()) && (itNext->offset == (offsetInWords + sizeInWords)))
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
        if (it->offset == (offsetInWords + sizeInWords))
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
    if (openedFile == -1) { return -1; } 

    // Fetch the hole list
    void *holeList = getList();

    // Ensure the holeList is valid
    if (!holeList) 
    { 
        close(openedFile);
        return -1; 
    }

    // Fetch the hole count
    size_t holeCount = ((uint16_t *)holeList)[0];
    
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

    delete[] static_cast<uint16_t*>(holeList);

    // Write the text vector to the openedFile
    for (auto text : textVector) { write(openedFile, text.c_str(), text.length());}

    // Close the file
    close(openedFile);

    // Success
    return 0;
}

void *MemoryManager::getBitmap()
{
    if (!memoryBlock) { return nullptr; }
    if (sizeInWords == 0) { return nullptr; }
    
    // Determine the size (bytes) needed for the bitmap
    size_t bitmapSize = (sizeInWords / 8);

    // If there is a remainder, one more byte is required
    if (sizeInWords % 8 != 0) { bitmapSize++; }

    // Declare the bitmap
    uint8_t *bitmap = new uint8_t[bitmapSize];
    
    // Initialize the bitmap to 0s (all holes)
    memset(bitmap, 0, bitmapSize);
    
    // Create a buffer bitmap of all 1s
    uint8_t *bufferBitmap = new uint8_t[bitmapSize]; 
    memset(bufferBitmap, 0xFF, bitmapSize);
    
    // Modify the buffer bitmap with 0s where holes exist
    // Iterate through each hole
    for (auto it = holes.begin(); it != holes.end(); ++it) 
    {
        // Iterate through the length (size) of the current hole (it)
        for (size_t i = 0; i < it->size; ++i) 
        {
            size_t bitIndex = (it->offset + i);
            
            // If bits exist beyond memory, ignore them
            if (bitIndex >= sizeInWords) { continue; }
            
            size_t byteIndex = (bitIndex / 8);
            size_t bitOffset = (bitIndex % 8);
            
            // Set the bit to 0 in the buffer bitmap
            bufferBitmap[byteIndex] &= ~(1 << bitOffset);
        }
    }
    
    // Now copy the buffer bitmap to the final bitmap
    memcpy(bitmap, bufferBitmap, bitmapSize);
    
    // Ensure the last byte is correct if sizeInWords had a remainder byte
    if (sizeInWords % 8 != 0) 
    {
        size_t lastByteBits = sizeInWords % 8;
        size_t lastByteIndex = bitmapSize - 1;
        
        // Create a mask with 1s for bits to keep
        uint8_t mask = 0xFF >> (8 - lastByteBits);
        
        // Apply the mask to the last byte
        bitmap[lastByteIndex] &= mask;
    }
    
    delete[] bufferBitmap;
    
    // Create the final bitmap with size bytes
    uint8_t *finalBitmap = new uint8_t[bitmapSize + 2];
    
    // Set the first two bytes (size bytes) in little-endian
    finalBitmap[0] = bitmapSize & 0xFF;
    finalBitmap[1] = (bitmapSize >> 8) & 0xFF;
    
    // Copy the bitmap data to final
    for (size_t i = 0; i < bitmapSize; i++) { finalBitmap[i + 2] = bitmap[i]; }

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
        if (holeSize >= static_cast<size_t>(sizeInWords))
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
        if (holeSize >= static_cast<size_t>(sizeInWords))
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