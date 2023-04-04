#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include <stdbool.h>

// attach a shared memory block
// associate with filename
// create it if it doesn't exist
int* attach_memory_block(char* filename, int size);
bool detach_memory_block(int* block);
bool destroy_memory_block(char* filename);

#endif // !SHARED_MEMORY_H