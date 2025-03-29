# Compiler and flags
Compiler = g++
Flags = -std=c++17 -Wall -Wextra

# Library and Object file names
Library = libMemoryManager.a
Object = MemoryManager.o

# Build the Library
$(Library): $(Object)
	ar rcs $(Library) $(Object)

# Compile the object file
$(Object): MemoryManager.cpp MemoryManager.h
	$(Compiler) $(Flags) -c MemoryManager.cpp -o $(Object)

# Clean up the build
clean:
	rm -f $(Object) $(Library)