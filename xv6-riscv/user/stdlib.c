// user/stdlib.c

#include "kernel/types.h"
#include "user/user.h"      // For malloc() and free()
#include "stdlib.h"         // Include your new header

// --- Helper Functions ---

/*
 * Helper for atof: Calculates 10^n using floating-point math
 */
double
simple_pow10(int n)
{
  double res = 1.0;
  if(n < 0) {
    for(int i = 0; i < -n; i++) res /= 10.0;
  } else {
    for(int i = 0; i < n; i++) res *= 10.0;
  }
  return res;
}

/*
 * Helper for qsort: Swaps two blocks of generic memory
 */
void
swap_generic(void *a, void *b, uint size)
{
  char *p = (char*)a;
  char *q = (char*)b;
  for(uint i = 0; i < size; i++) {
    char temp = p[i];
    p[i] = q[i];
    q[i] = temp;
  }
}

// --- Standard Library Functions ---

/*
 * 1. calloc (Allocate and Zero) - Robust implementation
 */
void*
calloc(uint n, uint size)
{
    uint total_size;
    void *ptr;

    // 1. Calculate total required size in bytes.
    // This value can be 0 if n or size is 0.
    total_size = n * size;

    // 2. Critical Safety Check: Integer Overflow
    // If n > 0 and (n * size) wrapped around, total_size / n won't equal size.
    if (n > 0 && total_size / n != size) {
        return 0; // Return NULL on overflow
    }

    // 3. Allocate memory using xv6's malloc.
    // Use ternary operator to call malloc(1) if total_size is 0,
    // guaranteeing a unique, non-NULL pointer for free().
    ptr = malloc(total_size > 0 ? total_size : 1);

    // 4. Check for malloc failure
    if(ptr == 0) {
        return 0;
    }
    
    // 5. Zero the memory ONLY if memory was actually requested (total_size > 0).
    if (total_size > 0) {
        char *byte_ptr = (char*)ptr;
        for(uint i = 0; i < total_size; i++) {
            byte_ptr[i] = 0;
        }
    }

    return ptr;
}


/*
 * 2. atoi (ASCII to Integer)
 */
// int
// atoi(const char *s)
// {
//   int n = 0;
//   int sign = 1;

//   // 1. Skip leading whitespace
//   while(*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\v' || *s == '\f') {
//     s++;
//   }

//   // 2. Handle optional sign
//   if(*s == '-') {
//     sign = -1;
//     s++;
//   } else if(*s == '+') {
//     s++;
//   }

//   // 3. Convert digits
//   while(*s >= '0' && *s <= '9') {
//     n = n * 10 + (*s - '0');
//     s++;
//   }

//   return n * sign;
// }


/*
 * 3. atof (ASCII to Double/Float)
 */
double
atof(const char *s)
{
  double val = 0.0;
  int sign = 1;
  double power = 1.0;
  int exp_val = 0;
  int exp_sign = 1;

  // 1. Skip whitespace
  while(*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\v' || *s == '\f') {
    s++;
  }

  // 2. Handle sign
  if(*s == '-') {
    sign = -1;
    s++;
  } else if(*s == '+') {
    s++;
  }

  // 3. Handle integer part
  while(*s >= '0' && *s <= '9') {
    val = val * 10.0 + (*s - '0');
    s++;
  }

  // 4. Handle fractional part (decimal point)
  if(*s == '.') {
    s++;
    while(*s >= '0' && *s <= '9') {
      val = val * 10.0 + (*s - '0');
      power *= 10.0;
      s++;
    }
  }
  
  val = val / power;

  // 5. Handle exponent part (scientific notation)
  if(*s == 'e' || *s == 'E') {
    s++;
    // Get exponent sign
    if(*s == '-') {
      exp_sign = -1;
      s++;
    } else if(*s == '+') {
      s++;
    }
    
    // Get exponent value
    while(*s >= '0' && *s <= '9') {
      exp_val = exp_val * 10 + (*s - '0');
      s++;
    }
    
    val = val * simple_pow10(exp_val * exp_sign);
  }

  return val * sign;
}


/*
 * 4. qsort (Quicksort) with Tail Call Elimination for O(log N) stack space.
 */
void
qsort(void *base, uint n_elem, uint size, int (*compare)(const void *, const void *))
{
  // Outer while loop for Tail Call Elimination
  while (n_elem > 1) {
    
    // --- MEDIAN-OF-THREE PIVOT SELECTION ---
    
    uint low = 0;
    uint high = n_elem - 1;
    // Overflow-safe midpoint calculation
    uint mid = low + (high - low) / 2;

    // Pointers to the three candidate elements
    void *ptr_low = (char*)base + low * size;
    void *ptr_mid = (char*)base + mid * size;
    void *ptr_high = (char*)base + high * size;

    // Sort the three elements (low, mid, high) such that the median ends up at high.
    // This is more efficient than a helper function for just three items.
    
    // 1. Ensure mid <= low (swap if mid > low)
    if (compare(ptr_mid, ptr_low) < 0)
        swap_generic(ptr_mid, ptr_low, size);
    
    // 2. Ensure high <= low (swap if high > low)
    // The smallest is now guaranteed to be at ptr_low.
    if (compare(ptr_high, ptr_low) < 0)
        swap_generic(ptr_high, ptr_low, size);
        
    // 3. Ensure high <= mid (swap if high > mid)
    // The median is now guaranteed to be at ptr_high (the pivot position).
    if (compare(ptr_high, ptr_mid) < 0)
        swap_generic(ptr_high, ptr_mid, size);
    
    // --- PARTITIONING (Lomuto) ---

    // The median is now in the pivot position (arr[high])
    void *pivot = ptr_high; 
    uint i = 0; // Index for the 'less than' region boundary
    
    for(uint j = 0; j < high; j++) { // Loop only runs up to 'high' (pivot position)
      void *elem_j = (char*)base + j * size;
      
      if(compare(elem_j, pivot) <= 0) {
        void *elem_i = (char*)base + i * size;
        swap_generic(elem_i, elem_j, size);
        i++;
      }
    }
    
    // Place pivot in its final sorted position (at index i). 
    // This completes the partition step.
    void *pivot_dest = (char*)base + i * size;
    swap_generic(pivot_dest, pivot, size);
    
    uint pivot_index = i;

    // --- TAIL CALL ELIMINATION ---
    
    uint size_left = pivot_index;
    uint size_right = n_elem - pivot_index - 1;
    
    if (size_left < size_right) {
      // 1. Recurse on the smaller (Left) partition
      qsort(base, size_left, size, compare);
      
      // 2. Iterate on the larger (Right) partition
      base = (char*)base + (pivot_index + 1) * size; 
      n_elem = size_right;             
      // Outer 'while' loop continues
    } else {
      // 1. Recurse on the smaller (Right) partition
      qsort((char*)base + (pivot_index + 1) * size, size_right, size, compare);
      
      // 2. Iterate on the larger (Left) partition
      // 'base' stays the same
      n_elem = size_left;                         
      // Outer 'while' loop continues
    }
  }
}

/*
 * 5. bsearch (Binary Search)
 */
void*
bsearch(const void *key, const void *base, uint n_elem, uint size, int (*compare)(const void *, const void *))
{
  int low = 0;
  int high = n_elem - 1;

  while(low <= high) {
    // Overflow-safe midpoint calculation
    int mid = low + (high - low) / 2; 
    
    void *mid_ptr = (char*)base + mid * size;
    
    int cmp = compare(key, mid_ptr);

    if(cmp < 0) {
      high = mid - 1;
    } else if(cmp > 0) {
      low = mid + 1;
    } else {
      return mid_ptr; // Found
    }
  }

  return 0; // Not found
}