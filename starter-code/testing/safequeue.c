#include <stdio.h>
#include <stdlib.h>

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
void insert(struct PriorityQueue *pq, int data, int priority) {
    if (pq->size == pq->max_size) {
        printf("Priority Queue is full. Cannot insert.\n");
        return;
    }

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
struct HeapNode extractMax(struct PriorityQueue *pq) {
    if (pq->size == 0) {
        printf("Priority Queue is empty.\n");
        exit(1); // You may choose to handle this differently based on your requirements
    }

    struct HeapNode maxNode = pq->heap[0];
    pq->size--;

    if (pq->size > 0) {
        pq->heap[0] = pq->heap[pq->size];
        maxHeapify(pq, 0);
    }

    return maxNode;
}

// Example usage
// int main() {
//     struct PriorityQueue pq;
//     pq.size = 0;

//     insert(&pq, 3, 2);
//     insert(&pq, 5, 1);
//     insert(&pq, 8, 3);

//     struct HeapNode maxNode = extractMax(&pq);
//     printf("Extracted element: %d with priority %d\n", maxNode.data, maxNode.priority);

//     return 0;
//}
