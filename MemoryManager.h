#pragma once
#include <functional>
#include <cstdint>
#include "Hole.h"

class MemoryManager
{
    public:
    MemoryManager(unsigned wordSize, std::function<int(int, void *)> allocator);
    ~MemoryManager();
    void initialize(size_t sizeInWords);
    void shutdown();
    void *getList();
    void *allocate(size_t sizeInBytes);
    void free(void *address);

    private:
    unsigned wordSize = 0;
    std::function<int(int, void *)> allocator = nullptr;
    uint8_t* memoryBlock = nullptr;
    std::vector<Hole> holes = {};
};