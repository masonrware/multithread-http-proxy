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
};

// Function prototypes
void swap(struct HeapNode *a, struct HeapNode *b);
void maxHeapify(struct PriorityQueue *pq, int i);
void insert(struct PriorityQueue *pq, int data, int priority);
struct HeapNode extractMax(struct PriorityQueue *pq);

#endif  // PRIORITY_QUEUE_H
