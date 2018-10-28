#include "oryx.h"

#define ALIGN_BYTES (offsetof (struct { char x; uint64_t y; }, y) - 1)
// Each pre-allocated memory block is 10M
#define MEM_BLOCK_SIZE 	10*1024*1024
#define MAX_MEM_BLOCKS	256

atomic64_t mem_access_times = ATOMIC_INIT(0);

typedef struct memory_handle_s {
	/* 
	 * to speedup aggregation/record statistics, the internal hash tables use their own memory management.
	 * memory is allocated with malloc in chunks of MemBlockSize. All memory blocks are kept in the
	 * memblock array. Memory blocks are allocated on request up to the number of MaxMemBlocks. If more 
	 * blocks are requested, the memblock array is automatically extended.
	 * Memory allocated from a memblock is aligned accoording ALIGN
	 */

	uint32_t	block_size;		/* max size of each pre-allocated memblock */

	/* memory blocks - containing the flow records and keys */
	void		**mem_block;		/* array holding all NumBlocks allocated memory blocks */
	uint32_t 	max_blocks;		/* Size of memblock array */
	uint32_t 	num_blocks;		/* number of allocated flow blocks in memblock array */
	uint32_t	current_block;	/* Index of current memblock to allocate memory from */
	uint32_t 	allocted;		/* Number of bytes already allocated in memblock */

	pthread_mutex_t mem_mutex;
} memory_handle_t;

typedef struct mem_debug_info_s {
	uint32_t 	max_blocks;
	uint32_t 	num_blocks;
	uint32_t	current_block;
	uint32_t 	allocted;
}mem_debug_info_t;

int MEM_InitMemory(void **mem_handle) 
{
	memory_handle_t *handle = (memory_handle_t *)malloc(sizeof(memory_handle_t));
	
	handle->mem_block = (void **)calloc(MAX_MEM_BLOCKS, sizeof(void *));
	if (unlikely(!handle->mem_block))
		oryx_panic(-1,
			"calloc: %s", oryx_safe_strerror(errno));
	
	handle->block_size	  = MEM_BLOCK_SIZE;

	handle->mem_block[0]  = malloc(MEM_BLOCK_SIZE);
	handle->max_blocks    = MAX_MEM_BLOCKS;
	handle->num_blocks	  = 1;
	handle->current_block = 0;
	handle->allocted	  = 0;
	
	pthread_mutex_init(&handle->mem_mutex, NULL);

	*mem_handle = (void *)handle;
	return 0;

} // End of  MemoryHandle_init

void *MEM_GetMemory(void *mem_handle, uint32_t size)
{
	void 		*p;
	uint32_t	aligned_size;
	memory_handle_t *handle = (memory_handle_t *)mem_handle;

	do_mutex_lock(&handle->mem_mutex);
	
	// make sure size of memory is aligned
	aligned_size = (((uint32_t)(size) + ALIGN_BYTES) &~ ALIGN_BYTES);

	if ((handle->allocted+aligned_size) > MEM_BLOCK_SIZE) 
	{
		// not enough space - allocate a new memblock
		handle->current_block++;
		if (handle->current_block >= handle->max_blocks)
		{
			// we run out in memblock array - re-allocate memblock array
			handle->max_blocks += MAX_MEM_BLOCKS;
			handle->mem_block   = (void **)realloc(handle->mem_block, handle->max_blocks * sizeof(void *));
			if (unlikely(!handle->mem_block))
				oryx_panic(-1,
					"realloc: %s", oryx_safe_strerror(errno));
		} 

		// allocate new memblock
		p = malloc(MEM_BLOCK_SIZE);
		if (unlikely (!p))
			oryx_panic(-1,
				"malloc: %s", oryx_safe_strerror(errno));

		handle->mem_block[handle->current_block] = p;
		// reset counter for new memblock
		handle->allocted = 0;
		handle->num_blocks++;
	} 

	// enough space available in current memblock
	p = (char *)handle->mem_block[handle->current_block] + handle->allocted;
	handle->allocted += aligned_size;

	do_mutex_unlock(&handle->mem_mutex);
	return p;
}

int MEM_GetMemSize(void *mem_handle ,uint64_t* size)
{
	memory_handle_t *handle = (memory_handle_t *)mem_handle;
	if(handle == NULL || size ==  NULL)
		return -1;

	*size = (uint64_t)handle->current_block * MEM_BLOCK_SIZE + handle->allocted;
	
	return 0;
}

void MEM_UninitMemory(void *mem_handle) 
{
	int i;
	memory_handle_t *handle = (memory_handle_t *)mem_handle;

	for (i = 0; i < (int)handle->num_blocks; i++) 
		free(handle->mem_block[i]);
	
	handle->num_blocks	= 0;
	handle->current_block = 0;
	handle->allocted 	 = 0;

	free((void *)handle->mem_block);
	handle->mem_block	= NULL;
	handle->max_blocks	= 0;

	pthread_mutex_destroy(&handle->mem_mutex);

	free(mem_handle);
}

void * oryx_shm_get(key_t key , int size)
{	
	void	*mem = NULL;
	int		shm_id;

	/* There is a shared memory with the KEY. */
	shm_id = shmget(key, size, 0640); 	
	if (shm_id != -1) {
		/* map this share memory to current address space of current progress. */
		mem = (void*)shmat(shm_id, NULL, 0);
		if (mem != (void *)-1)
			return mem;
	}

	/* Create shared memory with the given KEY. */
	shm_id = shmget(key, size, (0640|IPC_CREAT|IPC_EXCL)); 
	if (shm_id == -1) {
		oryx_panic(-1,
			"shmget: %s", oryx_safe_strerror(errno));
	} else {
		mem =(void *)shmat(shm_id, NULL, 0);
		if (mem != (void *)-1)
			memset(mem , 0 , size);
	}
	return mem;
}

int oryx_shm_destroy(void *mem)
{
	if (shmdt(mem) == -1) {
		oryx_loge(-1,
			"shmdt: %s", oryx_safe_strerror(errno));
		return -1;
	}
	return 0;
}


