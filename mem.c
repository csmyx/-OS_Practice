#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "mem.h"


typedef struct {
	int size;
	long long magic;
} header_t;

typedef struct _node_t {
	int size;
	struct _node_t *next;
} node_t;

node_t *head = NULL;
int m_error = 0;

//node_t *best_fit(node_t *node, int size);
//node_t *worst_fit(node_t *node, int size);
//node_t *first_fit(node_t *node, int size);

int main(void)
{
	int size = 1024*12;
	if (mem_init(size) != 0) {
		return -1;
	}
	printf("maclloc size: %d\n", head->size);
	mem_dump();
	void *p1 = mem_alloc(1024*2, M_BESTFIT);
	printf("best_fit allocated 2k Bytes as p1!\n");
	mem_dump();
	void *p2 = mem_alloc(1024*4, M_WORSTFIT);
	printf("worst_fit allocated 4k Bytes as p2!\n");
	mem_dump();
	void *p3 = mem_alloc(1024*3, M_FIRSTFIT);
	printf("first_fit allocated 3k Bytes as p3!\n");
	mem_dump();
	void *p4 = mem_alloc(1024*1, M_WORSTFIT);
	printf("best_fit allocated 1k Bytes as p4!\n");
	mem_dump();
	mem_free(p1);
	printf("freed p1!\n");
	mem_free(p3);
	printf("freed p3!\n");
	mem_dump();
	void *p5 = mem_alloc(1, M_FIRSTFIT);
	printf("first_fit allocated 1 Byte!\n");
	mem_dump();
	void *p6 = mem_alloc(1, M_BESTFIT);
	printf("best_fit allocated 1 Byte!\n");
	mem_dump();
	void *p7 = mem_alloc(1, M_WORSTFIT);
	printf("worst_fit allocated 1 Byte!\n");
	mem_dump();

	return 0;
}

int mem_init(int size_of_region)
{
	static int call_time = 0; // record the time of being called
	
	if (call_time > 0) {
		perror("size_of_region must be positive!\n");
		return -1;
	}

	if (size_of_region <= 0) {
		m_error = E_BAD_ARGS;
		perror("size_of_region must be positive!\n");
		return -1;

	}

	int page_size = getpagesize();
	int page_num = size_of_region / page_size;

	if (size_of_region % page_size != 0) {
		page_num++;
	}
	
	int fd = open("/dev/zero", O_RDWR);
	head = mmap(NULL, page_num*page_size, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0); // head node	

	if (head == MAP_FAILED) {
		perror("mmap fiailed!\n");
		return -1;
	} 
 	head->size = page_num * page_size - sizeof(node_t);
	head->next = NULL;
	call_time++;

	close(fd);
	return 0;
}

void *mem_alloc(int size, int style)
{
	if (size % 8 != 0) { // 8-byte alignment
		size += 8 - size % 8;
	}

	if (size <= 0) {
		m_error = E_BAD_ARGS;
		perror("malloc size must be positive!\n");
		return 0;
	}

	/* Choosing allcate strategy */
	node_t *pnode = head;
	node_t *ptr = NULL, *prev = NULL, *ptmp = NULL;
	// The smallest size of all free chunk sizes greater than requested size
	int best_size = pnode->size+1; 	
	// The biggest size of all free chunk sizes greater than requested size
	int worst_size = 0; 
	// The first free chunk with size bigger than request
	int first_size = 0;

	switch (style) {
	case M_BESTFIT:
		while (1) {
			if (pnode->size >= size && pnode->size < best_size) {
				best_size = pnode->size;
				prev = ptmp;
				ptr = pnode;
			}
			if (!pnode->next) {
				break;
			}
			ptmp = pnode;
			pnode = pnode->next;
		}	
		break;
	case M_WORSTFIT:
		while (1) {
			if (pnode->size >= size && pnode->size > worst_size) {
				worst_size = pnode->size;
				prev = ptmp;
				ptr = pnode;
			}
			if (!pnode->next) {
				break;
			}
			ptmp = pnode;
			pnode = pnode->next;
		}
		break;
	case M_FIRSTFIT:
		while(pnode) {
			if (pnode->size >= size) {
				first_size = pnode->size;
				ptr	= pnode;
				break;
			}
			prev = pnode;
			pnode = pnode->next;
		}
		break;
	default:
		perror("No such style!\n");
		break;
	}

	if (!ptr) {
		m_error = E_NO_SPACE;
		return NULL;
	} 

	/* Allocating memory */
	if (ptr == head) {
		node_t *tmp = head;
		head = (node_t*)((void*)(head+1) + size);
		head->size = tmp->size - size - sizeof(node_t);
		head->next = tmp->next;
	} else {
		prev->next = (node_t*)((void*)(ptr+1) + size);
		(prev->next)->size = ptr->size - size - sizeof(node_t);
		(prev->next)->next = ptr->next;
	}

	/* Creating header */
	header_t *pheader = (header_t*)ptr;
	pheader->size = size;
	pheader->magic = MAGIC;
	return pheader+1;

}



int mem_free(void *ptr) {
	header_t *pheader = (header_t*)ptr-1;
	if (pheader->magic != MAGIC) {
		m_error = E_CORRUPT_FREESPACE;
		return -1;
	}

	/* Inserting new free chunk in ascending order according to the address in order to coalesce neighboring freed blocks conveniently */
	node_t *pcurrent = (node_t*)pheader; // Current free chunk
	node_t *prev = NULL, *pnext = NULL; // Point to free chunks with smaller and bigger address than current node to be free respectively
	node_t *pnode = head;
	while (pnode != NULL) {
		if (pnode > pcurrent) {
			pnext = pnode;
			break;
		}
		prev = pnode;
		pnode = pnode->next;
	}

	if (prev == NULL) { // Current node is in front of the head node
		node_t *tmp = head;
		head = pcurrent;
		head->size = pheader->size;
		head->next = tmp;
	} else {
		prev->next = pcurrent;
		pcurrent->next = pnext;
	}


	/* Coalescing rejoins neighboring freed blocks into one bigger free chunk */
	void *p1 = NULL, *p2 = NULL;
	if (pnext != NULL) {
		p2 = (void*)(pcurrent+1)+pcurrent->size;
		if (p2 == (void*)pnext) {
			pcurrent->next = pnext->next;
			pcurrent->size += pnext->size + sizeof(node_t);
		}
	}
	if (prev != NULL) {
		p1 = (void*)(prev+1)+prev->size;
		if (p1 == (void*)pcurrent) {
			prev->next = pcurrent->next;
			prev->size += pcurrent->size + sizeof(node_t);
		}
	}
	return 0;
}

void mem_dump() {
	static int n = 1;
	printf("--------\ndump%d:\n", n);
	node_t *pnode = head;
	int i = 1;
	while (pnode) {
		printf("Chunk%3d -- begin:%p, end:%p, Size:%d\n", i, pnode, (void*)pnode+pnode->size, pnode->size);
		pnode = pnode->next;
		i++;
	}
	printf("--------\n");
	n++;
}
