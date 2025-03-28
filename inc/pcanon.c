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

#include "pcanon.h"
#include "badstack.h"
#include "path.h"

#define __DEBUG__ FALSE

#define __DEBUG_C__ TRUE  /* debug node comparison events */
#define __DEBUG_P__ TRUE  /* debgu node process events */


static void _partition_by_scoped_degree(graph *g, partition *pi, int cell, int cell_sz, partition *alpha, int scope_idx, int scope_sz, int m, int n);
static int _target_cell(partition *pi);
static void _first_node(graph *g, int m, int n, BadStack *stack);
static void _process_leaf(Path *path, partition *pi, Status *status);
static void _process_next(graph *g, int m, int n, BadStack *stack, Status *status);

void run(graph *g, int m, int n){
    BadStack *stack = malloc(sizeof(BadStack));    // Yep, it's bad
    stack_initialize(stack, n);

    Status *status = (Status*)malloc(sizeof(Status));
    status->g = g;
    status->m = m;
    status->n = n;
    status->cl = NULL;
    status->cl_pi = NULL;
    status->best_invar = NULL;
    status->best_invar_path = NULL;

    /* build theta and mcr */
    status->theta = generate_unit_partition(n);
    for (int i = 0; i < status->theta->sz; ++i) status->theta->ptn[i] = 0; /* theta starts off discrete */
    status->mcr = (int*)malloc(sizeof(int)*n);
    status->mcr_sz = 0; 
    
    DYNALLOCAUTOGROUP(status->autogrp, n, n, "run_dyn_autogrp");

    status->flag_new_cl = FALSE;
    status->flag_new_auto = FALSE;
    
    DYNALLOCPART(status->base_pi, n, "run_status_malloc");
    for(int i = 0; i < n; ++i) {
        status->base_pi->lab[i] = i;
        status->base_pi->ptn[i] = 0;
    }

    int nodes_processed = 0;

    _first_node(g, m, n, stack);

    while(stack_size(stack) > 0) {
        status->flag_new_cl = FALSE;
        status->flag_new_auto = FALSE;
        _process_next(g, m, n, stack, status);
        if (status->flag_new_cl) {
            printf("MPI: Communicate New Best CL  "); visualize_partition_with_char_offset(DEBUGFILE, status->cl, 'a'); ENDL();
            /* stuff goes here */
        } else if (status->flag_new_auto) {
            partition *aut = status->autogrp->automorphisms[status->autogrp->sz-1];
            printf("MPI: Communicate New Auto  "); visualize_partition_with_char_offset(DEBUGFILE, aut, 'a'); ENDL();
            automorphisms_merge_perm_into_oribit(aut, status->theta);
            automorphisms_calculate_mcr(status->theta, status->mcr, &status->mcr_sz);

            printf("Theta: "); visualize_partition_with_char_offset(DEBUGFILE, status->theta, 'a'); printf("  MCR (%d): ", status->mcr_sz);
            for (int i = 0; i < status->mcr_sz; ++i) {
                printf(" %c", (char)('a'+status->mcr[i]));
            }
            ENDL();
            /* stuff goes here too! */
        }
        ++nodes_processed;
    }


    /*  Bridges  at Pitsburgh super computer center PSC */
    /**
     * IPDPS  - October 2025 submission deadline
     */

    printf("\nNodes Processed : %d\n", nodes_processed);

    printf("Canonical Label: "); visualize_partition_with_char_offset(DEBUGFILE, status->cl, 'a'); ENDL();
    
    for (int i = 0; i < status->autogrp->sz; ++i) {
        printf("Automorphism: "); visualize_partition_with_char_offset(DEBUGFILE, status->autogrp->automorphisms[i], 'a'); ENDL();
    }

    free(stack);
    FREES(status->theta);
    if (status->mcr) free(status->mcr);
    FREES(status->base_pi);
    FREEAUTOGROUP(status->autogrp);
    if(status->best_invar) free(status->best_invar);
    if(status->best_invar_path) free(status->best_invar_path);
    free(status);

}



static void _first_node(graph *g, int m, int n, BadStack *stack) {

    partition *pi = generate_unit_partition(n);
    partition *active = generate_unit_partition(n);
    partition *new_pi = refine(g, pi, active, m, n);

    if (!is_partition_discrete(new_pi)) {
        /* if it is not discrete, add the child nodes, in proper order to the stack */
        int cell, cell_sz;
        get_partition_cell_by_index(new_pi, &cell, &cell_sz, _target_cell(new_pi));
        for (int i = cell+cell_sz-1; i >= cell; --i) {
            PathNode *next;
            DYNALLOCPATHNODE(next, "process");
            DYNALLOCPATH(next->path, 1, "process");
            next->path->data[0] = new_pi->lab[i];
            next->pi = new_pi;
            new_pi->_ref_count++;  /* Doing this so we know how many references to this one partition exist, to keep cleanup safe */  /* Sure this'll really work */
            stack_push(stack, next);
        }
    }
    
    FREEPART(new_pi);
    FREEPART(active);
    FREEPART(pi);
}

static void _process_leaf(Path *path, partition *pi, Status *status) {
    int cmp = 0;  /* used to compare new node with best invariant <1 is better (new CL), 0 is equiv (auto if leaf), >1 worse (throw away)*/

    partition *perm = generate_permutation(status->base_pi, pi);

    graph *invar = calculate_invariant(status->g, status->m, status->n, perm);
    if (status->best_invar == NULL) {
        cmp = -1;
    } else {
        cmp = compare_invariants(status->best_invar, invar, status->m, status->n);
    }
   
    if (__DEBUG_C__) printf("C "); visualize_path(DEBUGFILE, path); printf("  Partition:  ");  visualize_partition(DEBUGFILE, pi); printf("  cmp: %d\n\n", cmp);

    if (cmp < 0) {
        /* New best invariant found! */
        status->flag_new_cl = TRUE;
        status->cl = perm;
        status->cl_pi = pi;
        status->best_invar = invar;
        status->best_invar_path = path;
    } else if (cmp == 0) {
        /* automorphism found */
        status->flag_new_auto = TRUE;
        partition *aut = generate_permutation(status->cl_pi, pi);
        automorphisms_append(status->autogrp, aut);

        FREEPART(perm);
        FREES(invar);       
    } else {
        FREEPART(perm);
        FREES(invar);
    }
}

static void _process_next(graph *g, int m, int n, BadStack *stack, Status *status) {
    PathNode *node = stack_pop(stack);

    /**
     * Creating the active set of cells to refine against
     */
    partition *active;
    DYNALLOCPART(active, n, "process");  //certainly not right, no need to make it this big for reals!!
    active->lab[0] = node->path->data[node->path->sz-1];
    active->ptn[0] = 0;
    active->sz = 1;


    /**
     * 
     * Refinement to create new partition is being done here.
     * 
     */
    partition *new_pi = refine(g, node->pi, active, m, n);

    if (__DEBUG_P__) printf("P "); visualize_path(DEBUGFILE, node->path);  printf("  pi: ");  visualize_partition(DEBUGFILE, new_pi);  printf("  active: ");  visualize_partition(DEBUGFILE, active); ENDL();


    /**
     * New partion means new work list, if it is not discrete
     */
    if (is_partition_discrete(new_pi)) {
        _process_leaf(node->path, new_pi, status);
    } else {
        /* if it is not discrete, add the child nodes, in proper order to the stack */
        int cell, cell_sz;
        get_partition_cell_by_index(new_pi, &cell, &cell_sz, _target_cell(new_pi));
        for (int i = cell+cell_sz-1; i >= cell; --i) {
            PathNode *next;
            DYNALLOCPATHNODE(next, "process");
            DYNALLOCPATH(next->path, node->path->sz+1, "process");
            for (int j = 0; j < node->path->sz; ++j) {
                next->path->data[j] = node->path->data[j];
            }
            next->path->data[next->path->sz-1] = new_pi->lab[i];
            next->pi = new_pi;
            new_pi->_ref_count++;  /* Doing this so we know how many references to this one partition exist, to keep cleanup safe */  /* Sure this'll really work */
            stack_push(stack, next);
        }
    }

    FREEPART(active);
    // FREEPATHNODE(node);
}

partition* refine(graph *g, partition *pi, partition *active, int m, int n){
    partition *pi_hat = copy_partition(pi);
    partition *alpha = copy_partition(active);
    int a = 0;

    if (__DEBUG__) {printf("pi: "); visualize_partition(DEBUGFILE, pi_hat); printf(" alpha: "); visualize_partition(DEBUGFILE, alpha); ENDL();}
    while (a < partition_cell_count(alpha) && !is_partition_discrete(pi_hat)) {
        int scope_idx, scope_sz;  // refinement scope cell index in alpha, and size of cell
        get_partition_cell_by_index(alpha, &scope_idx, &scope_sz, a);
        if (__DEBUG__) {printf("\n\n\nStarting Outer Loop:  pi_hat: "); visualize_partition(DEBUGFILE, pi_hat); printf("  alpha "); visualize_partition(DEBUGFILE, alpha); printf(", a %d, refine_cell %d   refine_cell_sz %d\n", a, scope_idx, scope_sz);}
        int p = 0;
        while (p < partition_cell_count(pi_hat)){

            int alpha_idx = get_index_of_cell_from_another_partition(alpha, pi_hat, p);  // index in alpha or -1 if not there, of the cell we are partitioning (used lower)

            int cell, cell_sz;  // current cell we are partitioning, was V/V_k in the paper
            get_partition_cell_by_index(pi_hat, &cell, &cell_sz, p);

            if (__DEBUG__) {printf("\n\tPre Partitioning by Scoped Degree\n");}
            if (__DEBUG__) {printf("\tpi_hat: "); visualize_partition(DEBUGFILE, pi_hat); printf("  p: %d  (cell, cell_sz): (%d, %d)\n",p, cell, cell_sz);}
            if (__DEBUG__) {printf("\talpha: "); visualize_partition(DEBUGFILE, alpha); printf("  a: %d,  (scope_idx, scope_sz): (%d, %d)\n", a, scope_idx, scope_sz );}
            
            _partition_by_scoped_degree(g, pi_hat, cell, cell_sz, alpha, scope_idx, scope_sz, m, n);
            
            if (__DEBUG__) {printf("\tPost Partitioning by Scoped Degree\n");}
            if (__DEBUG__) {printf("\tpi_hat: "); visualize_partition(DEBUGFILE, pi_hat); printf("  p: %d  (cell, cell_sz): (%d, %d)\n",p, cell, cell_sz);}
            
            int new_cell_size = partial_partition_cell_count(pi_hat, cell, cell_sz);  // new size of cell that was partitioned
            if (new_cell_size ==1) ++p;
            else {
                int t = first_index_of_max_cell_size_of_partition(pi_hat, p, p+new_cell_size);
                if (__DEBUG__) {printf("\tt: %d\n", t);}
                
                if (alpha_idx > -1){
                    if (__DEBUG__) printf("\tNeed to copy: \n");
                    if (__DEBUG__) {printf("\t\tSRC: pi_hat: "); visualize_partition(DEBUGFILE, pi_hat); printf("  index t: %d  \n", t);}
                    if (__DEBUG__) {printf("\t\tDST: alpha: "); visualize_partition(DEBUGFILE, alpha); printf("  index alpha_idx: %d\n", alpha_idx );}
                    overwrite_partion_cell_with_cell_from_another_partition(pi_hat, t, alpha, alpha_idx);
                    if (__DEBUG__) printf("\tPOST copy: \n");
                    if (__DEBUG__) {printf("\t\tSRC: pi_hat: "); visualize_partition(DEBUGFILE, pi_hat); ENDL();}
                    if (__DEBUG__) {printf("\t\tDST: alpha: "); visualize_partition(DEBUGFILE, alpha); ENDL();}

                    for (int i = p; i < p + new_cell_size; ++i){
                        if (i != t){
                            if (__DEBUG__) printf("\n\tNeed to append: \n");
                            if (__DEBUG__) {printf("\t\tSRC: pi_hat: "); visualize_partition(DEBUGFILE, pi_hat); printf("  index i: %d  \n", i);}
                            if (__DEBUG__) {printf("\t\tDST: alpha: "); visualize_partition(DEBUGFILE, alpha); ENDL();}
                            append_cell_to_partition_from_another_partition(pi_hat, i, alpha);
                            if (__DEBUG__) printf("\tPOST append: \n");
                            if (__DEBUG__) {printf("\t\tSRC: pi_hat: "); visualize_partition(DEBUGFILE, pi_hat); ENDL();}
                            if (__DEBUG__) {printf("\t\tDST: alpha: "); visualize_partition(DEBUGFILE, alpha); ENDL();}

                        }
                    }
                } else {
                    for (int i = p; i < p + new_cell_size; ++i){
                            if (__DEBUG__) printf("\n\tNeed to append: \n");
                            if (__DEBUG__) {printf("\t\tSRC: pi_hat: "); visualize_partition(DEBUGFILE, pi_hat); printf("  index i: %d  \n", i);}
                            if (__DEBUG__) {printf("\t\tDST: alpha: "); visualize_partition(DEBUGFILE, alpha); ENDL();}
                            append_cell_to_partition_from_another_partition(pi_hat, i, alpha);
                            if (__DEBUG__) printf("\tPOST append: \n");
                            if (__DEBUG__) {printf("\t\tSRC: pi_hat: "); visualize_partition(DEBUGFILE, pi_hat); ENDL();}
                            if (__DEBUG__) {printf("\t\tDST: alpha: "); visualize_partition(DEBUGFILE, alpha); ENDL();}
                    }                    
                }
            }
        }
        ++a;

    }
    if (__DEBUG__) {printf("\n\nFinal pi_hat: "); visualize_partition(DEBUGFILE, pi_hat); ENDL();}
    return pi_hat;
}


// def first_index_of_non_fixed_cell_of_smallest_size(partition):
//     '''
//     Finds the smallest cell size > 1, and returns the index of the first instance
//     of a cell of that size in partition
//     '''
//     smallest = -1
//     idx = -1
//     for i, cell in enumerate(partition):
//         if len(cell) == 2:
//             return i
//         if len(cell) > smallest:
//             smallest = len(cell)
//             idx = i
//     return idx

/**
 * This is our target cell function.  It picks the cell that is the smallest
 * index of the smallest non-fixed cell size in the partition.abort
 * 
 * Finds the smallest cell size > 1, and returns the index of the first instance
 * of a cell of that size in partition
 */

static int _target_cell(partition *pi){
    int smallest_sz = pi->sz+1;
    int smallest_idx = -1; 
    int current_sz = 1;
    int current_idx = 0;

    for (int i = 0; i < pi->sz; ++i){
        // printf("i: %d    pi->ptn[%d]: %d smallest_sz: %d   smallest_idx: %d   current_sz: %d   current_idx: %d\n", 
        //             i, i, pi->ptn[i], smallest_sz, smallest_idx, current_sz, current_idx);
        if (pi->ptn[i] == 0){
            /* end of current cell */
            if (current_sz < smallest_sz) {
                if (current_sz > 1) {
                    if (current_sz == 2) return current_idx;  // the first time we find a cell of size 2 we can return
                    smallest_sz = current_sz;
                    smallest_idx = current_idx;
                }
                current_sz = 0;
                ++current_idx;
            }
        }
        ++current_sz;
    }
    return smallest_idx;
 }

/**
 * Returns the degree of vertex v with respect to the verticies in scope
 * parameters:
 *      G       the graph's adjacency matrix
 *      scope   the scope of vertices with which to calculate the degree
 *      v       the vertex on which we are calculating the degree
 *      m       the number of setwords per row in the graphwi
 *      n       the number of vertices
 * 
 * returns an integer value of the degree of v with respect to scope over graph G
 */
static int _scoped_degree(graph *g, partition *alpha, int scope_idx, int scope_sz, int v, int m, int n){
    set *gi = (set*)g + (m*v);
    int degree = 0;
    for (int i = scope_idx; i < scope_idx+scope_sz; ++i){
        if (ISELEMENT(gi, alpha->lab[i])) ++degree;
    }
    return degree;
}


/**
 * Needed for the parallel sort (not parallel processing, permutating two arrays based on sorting of the first array)
 */
#define SORT_TYPE1 int
#define SORT_TYPE2 int
#define SORT_OF_SORT 2
#define SORT_NAME sortparallel
#include "mckay_sorttemplates.c"
/** */

/**
 * splits (partitions) cell into a smaller cells, grouped by their scoped degree
 * with respect to scope, and numerically sorted by the value of their degree
 * 
 * parameters:
 *      G       the graph's adjacency matrix
 *      scope   the scope of vertices with which to calculate the degree
 *      cell    the cell we are splitting
 * 
 * returns a list containing the new cells
 */
static void _partition_by_scoped_degree(graph *g, partition *pi, int cell, int cell_sz, partition *alpha, int scope_idx, int scope_sz, int m, int n){
    partition *cell_sort;
    DYNALLOCPART(cell_sort, cell_sz, "partition_by_scoped_degre");

    for (int i = 0; i < cell_sz; ++i){
        cell_sort->lab[i] = pi->lab[cell+i];
        cell_sort->ptn[i] = _scoped_degree(g, alpha, scope_idx, scope_sz, cell_sort->lab[i], m, n);
    }

    sortparallel(cell_sort->ptn, cell_sort->lab, cell_sort->sz);

    for (int i = 0; i < cell_sz; ++i){
        pi->lab[cell+i] = cell_sort->lab[i];
        if (i < cell_sz-1 && cell_sort->ptn[i] == cell_sort->ptn[i+1]){
            pi->ptn[cell+i] = 1;
        } else {
            pi->ptn[cell+i] = 0;
        }
    }

    FREEPART(cell_sort);
}