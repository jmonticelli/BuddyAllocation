#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <math.h>

#define MAX_MEM 1<<30

void memory_error();
void * my_buddy_malloc(int malloc_size);
void my_free(void * ptr);
void dump_heap();
void coalesce(void * ptr);

struct Block {
    char flags; // 1 bit for free/not free, 7 size bits, up to 2^127 in representation
    struct Block *prev; // prev block
    struct Block *next; // next block
};

static struct Block * head[26]; // head of memory lists
static void * base;
int init = 0;

void * my_buddy_malloc(int malloc_size) {
    if(!init) {
		base = mmap(NULL, MAX_MEM, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, 0, 0); // mmap 1GB :)
		brk(base); // sets program brk to base
		sbrk(1<<30); // set brk to end of that
		head[25] = (struct Block *) base; // max block size, 1<<30
		head[25]->flags = (char)(30); // just size of block - no marking of occupied because it's not occupied
        init++; // initialized :)
    }
	void * ptr; // initialize a void pointer
    int alloc_size = malloc_size + 1; // malloc size + 1 byte header for flags
	int pow2 = ceil(log2(alloc_size)); // the power of two of your alloc_size (the ceiling of the base 2 log of the alloc_size) i.e. 16 bit malloc + 17 = 2^5, i.e. 32bytes)
	if(pow2 < 5) // less than 32-byte?
		pow2 = 5; // make it 32-byte - our min alloc is 32 byte
	if(head[pow2-5] == NULL) { // no free block?!?!?!?!
		int i = pow2-4; // - 5 + 1 = -4... we want 4 because we're checking to see if higher a larger size chunk of memory has a free space
		if (i > 25) { // array bounds checking
			memory_error();
		}
		while(head[i] == NULL && i < 26) { // find a larger free block
			i++;
		}
		if(head[i] == NULL) { // if there is no block at the max size (if not max size, then this will fail)
			memory_error();
		}
		while(pow2-5 < i) {
			ptr = (void *)head[i];
			if(head[i]->next == NULL) {
				head[i--] = NULL; // set head of i to 0 and post-decrement
			}
			else {
				head[i] = head[i]->next; // new head is old head->next
				head[i]->prev = NULL; // set new head's prev to NULL
				i--;
			}
			int buddy_size = i + 5; // because size of current block = 2 ^ (index + 5)
			
			// head block
			head[i] = (struct Block *) ptr;
			head[i]->flags = (char)buddy_size;
			head[i]->prev = NULL;
			// buddy block
			int base_offset = ((char *) ptr) - ((char *) base); // pointer arithmetic via char * casts
			int buddy_offset = base_offset ^ (1<<buddy_size); // offset = buddy XOR size
			void * buddy_ptr = (void *)(((char *) ptr) + buddy_offset); // buddy_ptr is offset with original pointer 
			// establish head->next attributes (buddy attributes)
			head[i]->next = (struct Block *) buddy_ptr;
			//printf("head[i] = %x, head[i]->next = %x \n", head[i], head[i]->next);
			head[i]->next->prev = head[i];
			head[i]->next->next = NULL;
			head[i]->next->flags = (char)buddy_size;
		}
		// now we have 2 buddies of size that we need
		ptr = head[pow2-5]; // get return address that we want
		struct Block * blockptr = (struct Block *) ptr;
		blockptr->flags |= 0x80; // flag occ bit
		//blockptr->prev = NULL; // clear memory
		//blockptr->next = NULL; // clear memory
		char * incrementptr = (char *) ptr;
		incrementptr++; // since block size is 1 byte, increment by sizeof char
		ptr = (void *) incrementptr;
		head[pow2-5] = head[pow2-5]->next;
		head[pow2-5]->prev = NULL;
		return ptr;
	} else { // we have free block!!!!!!
		ptr = head[pow2-5];
		if(head[pow2-5]->next == NULL)
			head[pow2-5] = NULL;
		else {
			head[pow2-5] = head[pow2-5]->next; // new head is old head->next
			head[pow2-5]->prev = NULL; // head->prev = NULL
		}
		struct Block * blockptr = (struct Block *) ptr;
		blockptr->flags |= 0x80; // flag occ bit
		//blockptr->prev = NULL; // clear memory
		//blockptr->next = NULL; // clear memory
		char * incrementptr = (char *) ptr;
		incrementptr = incrementptr + 1; // since block size is 1 byte, increment by sizeof char
		ptr = (void *) incrementptr;
		return ptr;
	}
}

void my_free(void * ptr) {
    char * bytePointer = (char *) ptr; // get memory address
    bytePointer -= 1; // decrement one byte to get to header
    struct Block * block = (struct Block *) bytePointer;
    block->flags &= 0x7F; // set highest order bit to be 0 - anding with 0x7F is anding with 0111 1111
    // block->flags is NOW just the size of the block
    struct Block * curr = head[block->flags - 5];
	if(curr == NULL) {
		head[block->flags - 5] = block;
		block->next = NULL;
		block->prev = NULL;
	} else {
		block->next = curr;
		block->prev = NULL;
		curr->prev = block;
		head[block->flags - 5] = block;
	}
	// check for coalescing
	coalesce((void *)block);
}

void coalesce(void * ptr) { // "recursive function" calls my_free, eventually recalls coalesce function
	struct Block * block = (struct Block *) ptr;
	int free_offset = (char *)block - (char *)base; // get this free'd element offset
	struct Block * curr = (struct Block *) head[block->flags - 5]->next;
	while(curr != NULL) {
		int curr_offset = ((((char *)curr)-(char *)base));
		if(free_offset == curr_offset ^ (1<<block->flags)) { // if free_offset is curr_offset XOR size
			if(free_offset > curr_offset) { // if chunk of memory you are freeing is on the RIGHT
				if(block == head[block->flags-5]) { // if block is head of list
					head[block->flags-5] = block->next; // make next element head of list
					head[block->flags-5]->prev = NULL;
				}
				//added
				if(curr == head[block->flags-5]) { // if block is head of list
					head[block->flags-5] = curr->next; // make next element head of list
					if(head[block->flags -5] != NULL) head[block->flags-5]->prev = NULL;
				}
				//added
				curr->flags += 1; // left side doubles size
				if(curr->prev != NULL) { // previous node?
					curr->prev->next = curr->next; // link previous node to next node
				}
				if(curr->next != NULL) { // next node?
					curr->next->prev = curr->prev; // link next node to previous node
				}
				if(block->prev != NULL) {
					block->prev->next = block->next;
				}
				if(block->next != NULL) {
					block->next->prev = block->prev;
				}
				char * free_val = (char *)curr + 1;
				my_free(free_val); // call free so that bin gets updated
			} else { // if chunk of memory you are freeing is on the LEFT
				if(block == head[block->flags-5]) { // if block is head of list
					head[block->flags-5] = block->next; // set head to next
					head[block->flags-5]->prev = NULL;
				}
				//added
				if(curr == head[block->flags-5]) { // if block is head of list
					head[block->flags-5] = curr->next; // make next element head of list
					if(head[block->flags -5] != NULL) head[block->flags-5]->prev = NULL;
				}
				//added
				block->flags += 1; // left side doubles size
				if(block->prev != NULL) {
					block->prev->next = block->next;
				}
				if(block->next != NULL) {
					block->next->prev = block->prev;
				}
				if(curr->prev != NULL) { // previous node?
					curr->prev->next = curr->next; // link previous node to next node
				}
				if(curr->next != NULL) { // next node?
					curr->next->prev = curr->prev; // link next node to previous node
				}
				char * free_val = (char *)block + 1;
				my_free(free_val); // call free so that bin gets updated
			}
			return;
		}
		curr = curr->next;
	}
}

void dump_heap() {
    struct Block *cur;
    int i;
    for(i = 0; i < 26; i++) {
        printf("%d->", i+5);
        for(cur = head[i]; cur != NULL; cur = cur->next) {
            printf("[%d:%d:%d]->", (cur->flags & 0x80), (char *)cur - (char *)base, (cur->flags & 0x7F));
            assert((char *)cur >= (char *)base); // assert statement 1
			//printf("%x + %x <= %x?\n", (char *)cur, (cur->flags & 0x7F), (char *)sbrk(0));
			//assert((char *)cur + (cur->flags & 0x7F) <= (char *)sbrk(0)); // if this results in false, terminate program b/c something went wrong
            if(cur->next != NULL) assert(cur->next->prev == cur); // make SURE going forward and back results in the same Block
        }
        printf("NULL\n");
    }
}

void memory_error() {
	printf("You do not have enough memory to allocate this request. Program gracefully exiting.");
	exit(-1);
}
/*
int main() {
    dump_heap();
    return 0;
}
*/