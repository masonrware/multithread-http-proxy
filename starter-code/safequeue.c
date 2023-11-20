#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>


// change this to use the -q cla from proxyserver
#define MAX_SIZE 100

// Structure to represent a node in the heap
struct HeapNode {
    int data;
    int priority;
};

// Structure to represent the priority queue
struct PriorityQueue {
    struct HeapNode heap[MAX_SIZE];
    int size;
    int max_size;
};

// change queue creation in pserver main
void create_queue(struct PriorityQueue *pq, int msize, int size){
  pq->size = size;
  pq->max_size = msize; 
}

// Function to swap two nodes in the heap
void swap(struct HeapNode *a, struct HeapNode *b) {
    struct HeapNode temp = *a;
    *a = *b;
    *b = temp;
}

// Function to heapify a subtree rooted with node i
void maxHeapify(struct PriorityQueue *pq, int i) {
    int largest = i;
    int left = 2 * i + 1;
    int right = 2 * i + 2;

    if (left < pq->size && pq->heap[left].priority > pq->heap[largest].priority)
        largest = left;

    if (right < pq->size && pq->heap[right].priority > pq->heap[largest].priority)
        largest = right;

    if (largest != i) {
        swap(&pq->heap[i], &pq->heap[largest]);
        maxHeapify(pq, largest);
    }
}

// Function to insert a new element with a given priority into the priority queue
void add_work(struct PriorityQueue *pq, int data, int priority) {
    int i = pq->size;
    pq->size++;
    pq->heap[i].data = data;
    pq->heap[i].priority = priority;

    // Fix the max-heap property
    while (i > 0 && pq->heap[i].priority > pq->heap[(i - 1) / 2].priority) {
        swap(&pq->heap[i], &pq->heap[(i - 1) / 2]);
        i = (i - 1) / 2;
    }
}

// Function to extract the element with the maximum priority from the priority queue
struct HeapNode get_work(struct PriorityQueue *pq) {
    struct HeapNode maxNode = pq->heap[0];
    pq->size--;

    if (pq->size > 0) {
        pq->heap[0] = pq->heap[pq->size];
        maxHeapify(pq, 0);
    }

    return maxNode;
}


struct HeapNode get_work_nonblocking(struct PriorityQueue *pq) {
    if (pq->size == 0) {
        printf("Priority Queue is empty.\n");
        exit(1); 
    }

    struct HeapNode maxNode = pq->heap[0];
    pq->size--;

    if (pq->size > 0) {
        pq->heap[0] = pq->heap[pq->size];
        maxHeapify(pq, 0);
    }

    return maxNode;
}