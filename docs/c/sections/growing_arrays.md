## [:rewind: C Reference](../c.md)

# Growing Arrays

Growing arrays are a custom type designed to handle dynamically scaling arrays in C. It allocates on the heap and only allocates what it needs, with no limit to how much it can allocate if necessary.

This won't go over every function, but it will go over the most important ones. For a complete view on all the functions, head to `src/game/memory.c`, and head over to the growing array section (starts at line 193).

## Setting Up

Define a pointer to a `GrowingArray` struct:

```c
struct GrowingArray *gGrowingArrayExample = NULL;
```

You then need to find a spot to initialize this growing array. Look for an initialization function that works for the code you are modifying and throw the initialization for the array there.

You can initialize a growing array using the function

```c
struct GrowingArray *growing_array_init(struct GrowingArray *array, u32 capacity, GrowingArrayAllocFunc alloc, GrowingArrayFreeFunc free);
```

The first param is the array it points to. If you ever need to reinitialize a growing array, you'd insert your `gGrowingArrayExample`. Technically, we don't need to do that for a first time initialization, so we could pass in `NULL`, however it's recommended to pass in your growing array for clarity.

The `capacity` argument is the initial capacity. You generally want to avoid reallocations as reallocations are slow, so keep this number as a good average for how much capacity your array would typically need.

Then there's an `alloc` function and a `free` function. In 99.999% of cases, you will pass in `malloc` for the allocation function and `free` for the free function. Note that while `malloc` usually doesn't zero-initialize the block of memory allocated, the growing array will zero-initialize the block, so all allocated memory is zero-initialized.

As an example initialization:

```c
gGrowingArrayExample = growing_array_init(gGrowingArrayExample, 16, malloc, free);
```

## Allocating to the Growing Array

Each growing array has a `count` and the `buffer`. The `count` is the number of active elements in the `buffer`. The `count` does **not** represent the `capacity` of the array.

To allocate data to the growing array, use the `growing_array_alloc` function.

```c
void *growing_array_alloc(struct GrowingArray *array, u32 size)
```

This function takes in your Growing Array, and requests a size. The size is the amount of bytes to allocate to the array element, typically the size of the struct being used.

The function returns a pointer to the data allocated. It always pushes to the end of the array, so if you need to store the index, the index will always be `gGrowingArrayExample->count` before allocation, or `gGrowingArrayExample->count - 1` after allocation.

## Removing Elements from a Growing Array

These are the functions for it:

```c
bool growing_array_swap_and_pop_index(struct GrowingArray *array, u32 index);
bool growing_array_swap_and_pop(struct GrowingArray *array, void *ptr);
```

One takes in a pointer to the block of data you want to remove, and another takes the index.

The `growing_array_swap_and_pop` finds the pointer in the array and then calls `growing_array_swap_and_pop_index` internally.

What "swap" and "pop" means is quite simple. What's happening under the hood is it is taking the index provided, moving it to the end of the array, and decrementing the `count`. The element is still there and allocated, but can't be read unless you explicitly try to. On the next allocation using `growing_array_alloc`, the element will be zeroed out and the `count` will be incremented, acting like the popped element never existed.

This behavior is effectively equivalent to removing an element, we just don't bother with the processing required to actually free the element, especially since it will probably be used again anyways. It would be quite inefficent to free the memory and then use it again shortly after, requiring unecessary allocation.

Do note this shifts the indexes for all the elements in an array! Account for that when using this function.

## Cleanup

If you ever need to cleanup a growing array and remove it entirely, use the `growing_array_free` function:

```c
void growing_array_free(struct GrowingArray **array);
```

This function is quite simple, it just takes in your growing array and frees the entire thing, including making the array passed in `NULL`.
