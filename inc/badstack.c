/**
 * Copyright 2025 Jim Haslett
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "badstack.h"

void stack_push(BadStack *stack, PathNode *node) {
    if (stack->sp < stack->allocated_sz-1) {  // sp is pointing to top element, not the space
        ++stack->sp;
        stack->_private[stack->sp] = node;
    } else {
        runtime_error("Can't push to stack, as it's full"); /* we can pretend to do hanlding */
    }
}

PathNode* stack_pop(BadStack *stack) {
    if (stack->sp < 0) return NULL;   // Stack is empty, let's just pretend we're doing the right thing and return NULL

    return stack->_private[stack->sp--];  /* I wrote that with authority, knowing I never get pre/post right */

    /* don't be looking for garbage colletion, or any cleanup here! */
}

PathNode* stack_peek(BadStack *stack) {
    if (stack->sp < 0) return NULL;  // Stack is empty
    return stack->_private[stack->sp];
}

int stack_size(BadStack *stack){
    return stack->sp+1;  /* sp is pointing to top element on the stack, so add 
                                one because it's zero indexed */
}

void stack_initialize(BadStack *stack, int size){
    /* get the size right, don't be thinking we're going to adjust it later! */
    stack->_private = malloc(sizeof(PathNode)*size);
    if (stack->_private == NULL) alloc_error("stack_new");
    stack->allocated_sz = size;
    stack->sp = -1;
}


void visualize_stack(FILE *f, BadStack *stack) {
    fprintf(f, "Stack:  [%d] \n", stack->sp+1);
    for (int i = stack->sp; i >= 0; --i) {
        fprintf(f, "\tpath: "); visualize_path(f, stack->_private[i]->path); fprintf(f, "  pi: "); visualize_partition(f, stack->_private[i]->pi); putc('\n', f);
    }
}