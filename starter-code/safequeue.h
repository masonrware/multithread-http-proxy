// priority_queue.h

#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H

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

// Function prototypes
void create_queue(struct PriorityQueue *pq, int msize, int size);
void swap(struct HeapNode *a, struct HeapNode *b);
void maxHeapify(struct PriorityQueue *pq, int i);
void add_work(struct PriorityQueue *pq, int data, int priority);
struct HeapNode get_work(struct PriorityQueue *pq);
struct HeapNode get_work_nonblocking(struct PriorityQueue *pq);

#endif  // PRIORITY_QUEUE_H
