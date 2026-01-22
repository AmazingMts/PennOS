/* CS5480 Project 0
 * Author: Ajax Li
 * Purpose: Implements a vector of pointers
 */

#include "./Vec.h"
#include <stdlib.h>
#include "./panic.h"

/*
 * This function constructs a new vector with the indicated capacity.
 * It also takes in an optional destructor function which can be NULL.
 */
Vec vec_new(size_t initial_capacity, ptr_dtor_fn ele_dtor_fn) {
  Vec new_v;
  new_v.length = 0;
  new_v.capacity = initial_capacity;
  new_v.data = malloc(initial_capacity * sizeof(ptr_t));
  if (new_v.data == NULL) {
    panic("vec_new: malloc failed!\n");
  }
  new_v.ele_dtor_fn = ele_dtor_fn;

  return new_v;
}

/*
 * This function returns the pointer at the given index of the vector.
 */
ptr_t vec_get(Vec* self, size_t index) {
  if (index >= self->length) {
    panic("vec_get: index out of range!\n");
  }

  return self->data[index];
}

/*
 * This function wipes the value of the pointer at the given index
 * and replaces it with the given new element.
 */
void vec_set(Vec* self, size_t index, ptr_t new_ele) {
  if (index >= self->length) {
    panic("vec_set: index out of range!\n");
  }
  if (self->ele_dtor_fn) {  // Makes sure to clear the old element.
    self->ele_dtor_fn(self->data[index]);
  }
  self->data[index] = new_ele;
}

/*
 * This function pushes a new element to the back of the vector.
 */
void vec_push_back(Vec* self, ptr_t new_ele) {
  // Pushing to the back is equivalent to inserting at index = length.
  vec_insert(self, self->length, new_ele);
}

/*
 * This function wipes the last element in the vector.
 */
bool vec_pop_back(Vec* self) {
  if (self->length == 0) {
    return false;
  }
  // Similar to above, popping the back is equivalent to
  // erasing at index = length - 1.
  vec_erase(self, self->length - 1);
  return true;
}

/*
 * This helper function doubles the vector's capacity. If the capacity
 * is initially 0, it will make it 1.
 */
static void double_size(Vec* self) {
  if (self->capacity == 0) {
    self->capacity = 1;
  } else {
    self->capacity *= 2;
  }

  // If the reallocation function failed, it will return NULL but not clear
  // the original vector. Therefore a temporary pointer is needed to prevent
  // memory leak.
  ptr_t* new_data = realloc(self->data, self->capacity * sizeof(ptr_t));
  if (new_data == NULL) {
    panic("double_size: reallocation failed!\n");
  }
  self->data = new_data;
}

/*
 * This function inserts the given new element at a specific index
 * of the vector.
 */
void vec_insert(Vec* self, size_t index, ptr_t new_ele) {
  if (index > self->length) {
    panic("vec_insert: index out of range!\n");
  }
  if (self->length == self->capacity) {
    double_size(self);
  }
  if (index == self->length) {
    self->data[self->length] = new_ele;
  } else {
    // This for loop shift elements from index to the end one position to the
    // right to make space for the new element.
    for (size_t i = self->length; i > index; i--) {
      self->data[i] = self->data[i - 1];
    }
    self->data[index] = new_ele;
  }
  self->length++;
}

/*
 * This function wipes the element at the given index of the vector.
 */
void vec_erase(Vec* self, size_t index) {
  if (index >= self->length) {
    panic("vec_erase: index out of range!\n");
  }
  if (self->ele_dtor_fn) {
    self->ele_dtor_fn(self->data[index]);
  }
  // This for loop shifts elements from index + 1 to the end one position left.
  for (size_t i = index; i < self->length - 1; i++) {
    self->data[i] = self->data[i + 1];
  }
  self->length--;
}

/*
 * This function grows the vector to the given capacity.
 * It does nothing if the specified capacity is less than current.
 */
void vec_resize(Vec* self, size_t new_capacity) {
  if (new_capacity <= self->length) {
    return;
  }

  // For the same reason as mentioned in double_size, utilizes a temporary
  // pointer.
  ptr_t* new_data = realloc(self->data, new_capacity * sizeof(ptr_t));
  if (new_data == NULL) {
    panic("vec_resize: reallocation failed!\n");
  }
  self->data = new_data;
  self->capacity = new_capacity;
}

/*
 * This function wipes out all elements inside the vector. Note that it will
 * keep the vector itself.
 */
void vec_clear(Vec* self) {
  if (self->ele_dtor_fn) {
    for (size_t i = 0; i < self->length; i++) {
      self->ele_dtor_fn(self->data[i]);
    }
  }
  self->length = 0;
}

/*
 * This function destroies the vector along with every element within it.
 */
void vec_destroy(Vec* self) {
  vec_clear(self);
  free(self->data);
  self->data = NULL;
  self->capacity = 0;
}

bool vec_remove(Vec* self, ptr_t ele) {
  for (int i = 0; i < self->length; i++) {
    if (vec_get(self, i) == ele) {
      vec_erase(self, i);
      return true;
    }
  }
  return false;
}