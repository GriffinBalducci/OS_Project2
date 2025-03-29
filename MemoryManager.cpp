#include "MemoryManager.h"
#include <math.h>
#include <iostream>
#include <fcntl.h>

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
    delete[] holeList;

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
    delete[] holeList;

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
    // Initialize the bitmap
    std::vector<bool> bitmap;

    // Check for remainder
    size_t remainder = sizeInWords % 8;

    // Check for needing extra blank byte
    if (remainder != 0)
    {
        size_t extraBits = 8 - remainder;
        
        // Resize the bitmap to the size in words + extra bits and fill with ones
        bitmap.resize(sizeInWords + extraBits, 1);
    }
    else
    {
        // Resize the bitmap to the size in words and fill with ones
        bitmap.resize(sizeInWords, 1);
    }

    // Search for holes
    for (auto it = holes.begin(); it != holes.end(); ++it)
    {
        // Go to the given hole offset and mark the corresponding bit with 0
        // Then, continue to the end of the hole and mark all bits with 0
        for (size_t i = it->offset; i < (it->offset + it->size); i++)
        {
            bitmap[i] = 0;
        }
    }

    // Mirror the bitmap until the remainder byte, if one exists
    std::vector<bool> bitmapMirrored(bitmap.size(), 1);
    for (size_t i = 0; i < (bitmap.size() / 8); i++) 
    {
        // Mirror the bits in the current byte
        for (size_t j = 0; j < 8; j++)
        {
            // (i * 8) Gives the starting index of the byte in either bitmap
            // (+ j) Gives the index of the bit in the mirrored byte
            // (7 - j) Gives the index of the bit in the original byte
            bitmapMirrored[i * 8 + j] = bitmap[i * 8 + (7 - j)];
        }
    }
    
    // Fetch the size of the bitmap in two bytes
    uint16_t bitmapSizeInTwoBytes = (bitmapMirrored.size() / 8);

    // Flip the bitmapSizeInTwoBytes to little endian
    uint8_t leftBits = static_cast<uint8_t>(bitmapSizeInTwoBytes); // Just keep the right byte with conversion
    uint8_t rightBits = static_cast<uint8_t>(bitmapSizeInTwoBytes >> 8); // Shift the left byte to the right and keep only it with conversion

    // Create a new vector to hold the bitmap with the size in two bytes
    std::vector<uint8_t> bitmapWithSize(2 + bitmapMirrored.size() / 8, 0);
    bitmapWithSize[0] = leftBits;
    bitmapWithSize[1] = rightBits;

    // Fill the bitmapWithSize with the mirrored bits (per byte)
    for (size_t byteIndex = 0; byteIndex < bitmapSizeInTwoBytes; ++byteIndex)
    {
        uint8_t byte = 0;

        // For each bit in the byte, set the corresponding bit in the byte
        for (size_t bit = 0; bit < 8; ++bit)
        {
            // Check if the bit is true (1) at this bit
            if (bitmapMirrored[byteIndex * 8 + bit])
            {
                byte |= (1 << (7 - bit)); // Shift bit to run from left to right
            }
        }

        // Append the byte to the bitmapWithSize vector
        // The +2 accounts for the two size bytes at the beginning of the vector
        bitmapWithSize[2 + byteIndex] = byte;
    }

    // Convert the bitmapWithSize vector to the final required array and pointer
    uint8_t *finalArray = new uint8_t[bitmapWithSize.size()];
    std::copy(bitmapWithSize.begin(), bitmapWithSize.end(), finalArray);

    return  finalArray;
}
