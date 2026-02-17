#define _GNU_SOURCE
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <glib.h>

// DPLL

typedef struct
{
    int var;
    bool neg;
} Literal;

typedef struct
{
    int size;
    Literal *literals;
    bool satisfied;
} Clause;

typedef struct
{
    int numVars;
    int numClauses;
    Clause *clauses;
} Formula;

typedef struct
{
    int numVars;
    GArray **watch_lists;
} WatchTable;

typedef enum
{
    ASSIGNMENT,
    CLAUSE_SATISFY,
    WATCHLIST_ADD,
    WATCHLIST_REMOVE
} UndoType;

typedef struct
{
    UndoType type;
    int var;
    int index;
} UndoEntry;

typedef struct
{
    GSList *head;
} UndoStack;

typedef enum
{
    SAT,
    UNSAT,
    TIMEOUT
} DPLLReturnType;

static int *var_sort = NULL;
static clock_t start_time;
static double timeout_seconds = 3600.0; // 1 hour cutoff timer

// DPLL

DPLLReturnType dpll(Formula *formula, int *assignments, UndoStack *stack, WatchTable *wtable, int depth);
bool unit_propagate_dpll(Formula *formula, int *assignments, UndoStack *stack);
bool unit_propagate_2watchlit(Formula *formula, int *assignments, UndoStack *stack, WatchTable *wtable);
bool pure_literal_elimination(Formula *formula, int *assignments, UndoStack *stack);
int pick_unassigned_variable(Formula *formula, int *assignments);

void push_assignment(UndoStack *stack, int var);
void push_clause_satisfy(UndoStack *stack, int index);
void undo_to_checkpoint(UndoStack *stack, GSList *checkpoint, Formula *formula, int *assignments, WatchTable *wtable);

WatchTable *init_empty_watch_table(Formula *formula);
void watchtable_remove(WatchTable *wtable, int index, int value, UndoStack *stack);
void watchtable_add(WatchTable *wtable, int index, int value, UndoStack *stack);
void free_watchtable(WatchTable *wtable);
void satisfy_clauses_after_assignment(Formula *formula, int *assignments, UndoStack *stack);

Formula *parse_formula(const char *filename);
void free_formula(Formula *formula);

gint lit_key(Literal *lit);
gboolean clause_subset(Clause *a, Clause *b);
void remove_supersets(Formula *formula);

// Method to check if the timeout is triggered
bool timeout_exceeded()
{
    clock_t now = clock();
    double elapsed = (double)(now - start_time) / CLOCKS_PER_SEC;
    return elapsed >= timeout_seconds;
}

// Method to find the index of a lit in the watchlist
int watchlist_index(const Literal lit, const int numVars)
{
    return lit.var + lit.neg * numVars;
}

// Comparison for descending order
int compare_desc(const void *a, const void *b, void *counter)
{
    int idx1 = *(const int *)a;
    int idx2 = *(const int *)b;
    int *cnt = (int *)counter;

    return cnt[idx2] - cnt[idx1];
}

// List to create an array of sorted indexes for variables
int *get_sorted_indices(int *counter, int numVars)
{
    int *indices = calloc((numVars + 1), sizeof(int));
    if (!indices)
        return NULL;

    for (int i = 0; i <= numVars; i++)
    {
        indices[i] = i;
    }

    qsort_r(indices, numVars + 1, sizeof(int), compare_desc, counter);
    return indices;
}

int main(int argc, char *argv[])
{
    start_time = clock();

    // Invalid argument case
    if (argc != 2)
    {
        printf("Usage: %s <filename.cnf>\n", argv[0]);
        return 1;
    }

    char *filename = argv[1];
    printf("Filename provided: %s\n", filename);

    Formula *formula = parse_formula(filename);
    if (formula == NULL)
    {
        printf("File failed to parse!\n");
        return 1;
    }

    // Remove superset clauses
    remove_supersets(formula);

    // Initialise WatchTable
    WatchTable *wtable = init_empty_watch_table(formula);
    for (int i = 0; i < formula->numClauses; i++)
    {
        Clause clause = formula->clauses[i];
        if (clause.size == 0)
        {
            continue;
        }
        else if (clause.size == 1)
        {
            int index = watchlist_index(clause.literals[0], formula->numVars);
            GArray *arr = wtable->watch_lists[index];
            g_array_append_val(arr, i);
        }
        else
        {
            int index1 = watchlist_index(clause.literals[0], formula->numVars);
            int index2 = watchlist_index(clause.literals[1], formula->numVars);
            GArray *arr1 = wtable->watch_lists[index1];
            GArray *arr2 = wtable->watch_lists[index2];
            g_array_append_val(arr1, i);
            g_array_append_val(arr2, i);
        }
    }

    // Create sorted list of variable occurances for use in heuristic
    int *counter = calloc(formula->numVars + 1, sizeof(int));
    for (int i = 0; i < formula->numClauses; i++)
    {
        Clause clause = formula->clauses[i];
        for (int j = 0; j < clause.size; j++)
        {
            Literal lit = clause.literals[j];
            counter[lit.var]++;
        }
    }
    var_sort = get_sorted_indices(counter, formula->numVars);
    free(counter);

    // Init assignments array to all unassigned
    int *assignments = (int *)malloc(sizeof(int) * (formula->numVars + 1));
    for (int i = 1; i < formula->numVars + 1; i++)
    {
        assignments[i] = -1;
    }

    // Init UndoStack
    UndoStack *undo_stack = malloc(sizeof(UndoStack));
    undo_stack->head = NULL;

    // Run SAT solver
    DPLLReturnType sat = dpll(formula, assignments, undo_stack, wtable, 0);

    // Free Memory
    free(var_sort);
    free(assignments);
    g_slist_free_full(undo_stack->head, free);
    free(undo_stack);
    free_watchtable(wtable);
    free_formula(formula);

    clock_t end_ticks = clock();

    if (sat == SAT)
    {
        printf("Result: SAT\n");
    }
    else if (sat == UNSAT)
    {
        printf("Result: UNSAT\n");
    }
    else if (sat == TIMEOUT)
    {
        printf("Result: TIMEOUT\n");
    }

    double elapsed_time = (double)(end_ticks - start_time) / CLOCKS_PER_SEC;
    printf("CPU time used: %.5f seconds\n", elapsed_time);
    return 0;
}

DPLLReturnType dpll(Formula *formula, int *assignments, UndoStack *undo_stack, WatchTable *wtable, int depth)
{
    // Timeout Case
    if (timeout_exceeded())
    {
        return TIMEOUT;
    }

    // Undo Checkpoint
    GSList *checkpoint = undo_stack->head;

    if (!unit_propagate_2watchlit(formula, assignments, undo_stack, wtable))
    {
        undo_to_checkpoint(undo_stack, checkpoint, formula, assignments, wtable);
        return UNSAT;
    }

    if (!pure_literal_elimination(formula, assignments, undo_stack))
    {
        undo_to_checkpoint(undo_stack, checkpoint, formula, assignments, wtable);
        return UNSAT;
    }

    // Check if all clauses are satisfied.
    // Extra check for any clauses that might have been satisified, without a flag
    bool all_satisfied = true;
    for (int i = 0; i < formula->numClauses; i++)
    {
        if (!formula->clauses[i].satisfied)
        {
            bool missing_flag = false;
            for (int j = 0; j < formula->clauses[i].size; j++)
            {
                Literal lit = formula->clauses[i].literals[j];
                int as = assignments[lit.var];
                if ((as == 0 && lit.neg) || (as == 1 && !lit.neg))
                {
                    missing_flag = true;
                    break;
                }
            }

            if (missing_flag)
            {
                formula->clauses[i].satisfied = true;
                push_clause_satisfy(undo_stack, i);
            }
            else
            {
                all_satisfied = false;
                break;
            }
        }
    }

    if (all_satisfied)
    {
        return SAT;
    }

    int x = pick_unassigned_variable(formula, assignments);
    if (x == -1)
    {
        return UNSAT;
    }
    else
    {
        // Create a second checkpoint to undo the assignment + clause satisfy actions if needed.
        GSList *checkpoint2 = undo_stack->head;
        assignments[x] = 0;
        push_assignment(undo_stack, x);
        satisfy_clauses_after_assignment(formula, assignments, undo_stack);

        DPLLReturnType result1 = dpll(formula, assignments, undo_stack, wtable, depth + 1);
        if (result1 == UNSAT)
        {
            // Second assignment case
            undo_to_checkpoint(undo_stack, checkpoint2, formula, assignments, wtable);
            assignments[x] = 1;
            push_assignment(undo_stack, x);
            satisfy_clauses_after_assignment(formula, assignments, undo_stack);

            DPLLReturnType result2 = dpll(formula, assignments, undo_stack, wtable, depth + 1);
            if (result2 == UNSAT)
            {
                undo_to_checkpoint(undo_stack, checkpoint, formula, assignments, wtable);
            }

            return result2;
        }
        else
        {
            // Returns SAT or TIMEOUT
            return result1;
        }
    }
}

bool unit_propagate_dpll(Formula *formula, int *assignments, UndoStack *stack)
{
    bool progress = true;

    while (progress)
    {
        progress = false;

        for (int i = 0; i < formula->numClauses; i++)
        {
            if (formula->clauses[i].satisfied)
            {
                continue;
            }

            Clause clause = formula->clauses[i];

            int unassigned_count = 0;
            Literal unassigned_lit;
            bool clause_satisfied = false;

            for (int j = 0; j < clause.size; j++)
            {
                Literal lit = clause.literals[j];
                int value = assignments[lit.var];

                if (value == -1)
                {
                    unassigned_count++;
                    unassigned_lit = lit;
                }
                else
                {
                    if ((value == 1 && !lit.neg) || (value == 0 && lit.neg))
                    {
                        clause_satisfied = true;
                        break;
                    }
                }
            }

            if (clause_satisfied)
            {
                continue;
            }

            if (unassigned_count == 0)
            {
                return false;
            }
            else if (unassigned_count == 1)
            {
                assignments[unassigned_lit.var] = (int)!unassigned_lit.neg;
                push_assignment(stack, unassigned_lit.var);

                progress = true;
            }
        }
    }

    return true;
}

bool unit_propagate_2watchlit(Formula *formula, int *assignments, UndoStack *stack, WatchTable *wtable)
{
    // Init a new queue for storing unit literals
    GQueue *queue = g_queue_new();

    // Here we add the literals from any unit clauses into the queue
    for (int i = 0; i < formula->numClauses; i++)
    {
        Clause *clause = &formula->clauses[i];
        if (clause->satisfied)
        {
            continue;
        }

        int unassigned_counter = 0;
        Literal *lit_copy = malloc(sizeof(Literal));
        for (int j = 0; j < clause->size; j++)
        {
            Literal lit = clause->literals[j];
            if (assignments[lit.var] == -1)
            {
                if (unassigned_counter == 0)
                {
                    lit_copy->var = lit.var;
                    lit_copy->neg = lit.neg;
                }
                unassigned_counter++;
            }
        }

        if (unassigned_counter == 1)
        {
            g_queue_push_tail(queue, lit_copy);
        }
        else
        {
            free(lit_copy);
        }
    }

    while (!g_queue_is_empty(queue))
    {
        // Get the next literal. If it is unassigned, give it an assignment that satisfies it.
        Literal *lit = g_queue_pop_head(queue);

        if (assignments[lit->var] == -1)
        {
            // If literal is negated, set to false, else to true
            assignments[lit->var] = lit->neg ? 0 : 1;
            push_assignment(stack, lit->var);

            int index = watchlist_index(*lit, formula->numVars);
            GArray *wlist = wtable->watch_lists[index];
            for (int i = 0; i < wlist->len; i++)
            {
                int indexc = g_array_index(wlist, int, i);
                Clause *clause = &formula->clauses[indexc];
                if (!clause->satisfied)
                {
                    clause->satisfied = true;
                    push_clause_satisfy(stack, indexc);
                }
            }
        }

        // Find the opposite literal.
        Literal oplit = {lit->var, !lit->neg};
        int index = watchlist_index(oplit, formula->numVars);
        GArray *wlist = wtable->watch_lists[index];
        free(lit);

        // For each clause that watches the opposite literal
        for (int i = 0; i < wlist->len; i++)
        {
            int indexi = g_array_index(wlist, int, i);
            Clause *clause = &formula->clauses[indexi];

            if (clause->satisfied)
            {
                continue;
            }

            // Find the other literal watched by this clause
            Literal other = {0, false}; // 0 cannot be an actual literal.
            for (int j = 0; j < clause->size; j++)
            {
                Literal l = clause->literals[j];
                int indexj = watchlist_index(l, formula->numVars);

                // Is the literal we are looking at the opposite literal?
                if (l.var == oplit.var && l.neg == oplit.neg)
                {
                    continue;
                }

                // In the watch list of the next literal, check if this clause is watching it.
                GArray *twlist = wtable->watch_lists[indexj];
                for (int k = 0; k < twlist->len; k++)
                {
                    if (g_array_index(twlist, int, k) == indexi)
                    {
                        other = l;
                        break;
                    }
                }

                // If we found the other watch, exit loop
                if (other.var != 0)
                {
                    break;
                }
            }

            // If there is no other literal being watched, eg. unit clause
            if (other.var == 0)
            {
                // Skip if already satisfied
                if (clause->satisfied)
                {
                    continue;
                }

                // Otherwise, check if it is an unsatisfiable clause based on current assignments
                bool all_false = true;
                for (int x = 0; x < clause->size; x++)
                {
                    Literal tlit = clause->literals[x];
                    int assign = assignments[tlit.var];
                    all_false = all_false && ((assign == 0 && !tlit.neg) || (assign == 1 && tlit.neg));
                }

                if (all_false)
                {
                    g_queue_free_full(queue, free);
                    return false;
                }
                // If it isnt, then add it to the queue. (This may be a duplicate of the original step, but this shouldnt matter too much to performance.)
                else
                {
                    Literal *lit_copy = malloc(sizeof(Literal));
                    lit_copy->var = oplit.var;
                    lit_copy->neg = oplit.neg;
                    g_queue_push_tail(queue, lit_copy);
                    continue;
                }
            }

            // If the other watched literal is already satisfied, skip the clause.
            int other_a = assignments[other.var];
            if ((other_a == 1 && !other.neg) || (other_a == 0 && other.neg))
            {
                continue;
            }

            // Now we try to replace the watched literal with a new literal.
            bool found = false;
            for (int j = 0; j < clause->size; j++)
            {
                Literal nlit = clause->literals[j];
                int indexn = watchlist_index(nlit, formula->numVars);

                // If it matches one of the literals the clause is already looking at, skip it.
                if (indexn == index || (nlit.var == other.var && nlit.neg == other.neg))
                {
                    continue;
                }

                // If the next literal is unassigned, or satisfies the clause, then we watch it.
                int assign_nlit = assignments[nlit.var];
                if (assign_nlit == -1 || (assign_nlit == 0 && nlit.neg) || (assign_nlit == 1 && !nlit.neg))
                {
                    watchtable_remove(wtable, index, indexi, stack);
                    watchtable_add(wtable, indexn, indexi, stack);
                    found = true;
                    break;
                }
            }

            // If we could find no new literals to watch because of a unit clause or conflicts
            if (!found)
            {
                if (assignments[other.var] == -1)
                {
                    assignments[other.var] = other.neg ? 0 : 1;
                    push_assignment(stack, other.var);

                    int index = watchlist_index(other, formula->numVars);
                    GArray *wlist = wtable->watch_lists[index];
                    for (int i = 0; i < wlist->len; i++)
                    {
                        int indexc = g_array_index(wlist, int, i);
                        Clause *clause = &formula->clauses[indexc];
                        if (!clause->satisfied)
                        {
                            clause->satisfied = true;
                            push_clause_satisfy(stack, indexc);
                        }
                    }

                    Literal *lit_copy = malloc(sizeof(Literal));
                    lit_copy->var = other.var;
                    lit_copy->neg = other.neg;
                    g_queue_push_tail(queue, lit_copy);
                }
                else
                {
                    g_queue_free_full(queue, free);
                    return false;
                }
            }
        }
    }

    g_queue_free_full(queue, free);
    return true;
}

bool pure_literal_elimination(Formula *formula, int *assignments, UndoStack *stack)
{
    bool *positive_units = calloc(formula->numVars + 1, sizeof(bool));
    bool *negative_units = calloc(formula->numVars + 1, sizeof(bool));

    for (int i = 0; i < formula->numClauses; i++)
    {
        if (formula->clauses[i].satisfied)
        {
            continue;
        }

        for (int j = 0; j < formula->clauses[i].size; j++)
        {
            if (assignments[formula->clauses[i].literals[j].var] != -1)
            {
                continue; // Skipping already assigned variables
            }

            if (formula->clauses[i].literals[j].neg)
            {
                negative_units[formula->clauses[i].literals[j].var] = true;
            }
            else
            {
                positive_units[formula->clauses[i].literals[j].var] = true;
            }
        }
    }

    bool *lit_purity = calloc(formula->numVars + 1, sizeof(bool));
    for (int var = 1; var <= formula->numVars; var++)
    {
        if (assignments[var] != -1)
        {
            continue; // Skipping already assigned variables
        }

        if (positive_units[var] && !negative_units[var])
        {
            lit_purity[var] = true;
            assignments[var] = 1;
            push_assignment(stack, var);
        }
        else if (!positive_units[var] && negative_units[var])
        {
            lit_purity[var] = true;
            assignments[var] = 0;
            push_assignment(stack, var);
        }
    }

    for (int i = 0; i < formula->numClauses; i++)
    {
        bool satisfied = false;

        for (int j = 0; j < formula->clauses[i].size; j++)
        {
            if (lit_purity[formula->clauses[i].literals[j].var])
            {
                satisfied = true;
                break;
            }
        }

        if (satisfied)
        {
            formula->clauses[i].satisfied = true;
            push_clause_satisfy(stack, i);
        }
    }

    free(positive_units);
    free(negative_units);
    free(lit_purity);

    return true;
}

int pick_unassigned_variable(Formula *formula, int *assignments)
{
    // Pick first unassigned variable
    // for (int i = 1; i < formula->numVars; i++)
    // {
    //     if (assignments[i] == -1)
    //     {
    //         return i;
    //     }
    // }

    // Pick most often occuring variable
    for (int i = 0; i < formula->numVars; i++)
    {
        if (var_sort[i] == 0)
        {
            return -1;
        }

        if (assignments[var_sort[i]] == -1)
        {
            return var_sort[i];
        }
    }

    return -1;
}

Formula *parse_formula(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        return NULL;
    }

    int numVars = 0, numClauses = 0;
    char *line = NULL;
    size_t len = 0;

    while (getline(&line, &len, file) != -1)
    {
        if (line[0] == 'p')
        {
            sscanf(line, "p cnf %d %d", &numVars, &numClauses);
            break;
        }
    }

    Formula *formula = (Formula *)malloc(sizeof(Formula));
    formula->numVars = numVars;
    formula->numClauses = numClauses;
    formula->clauses = (Clause *)malloc(sizeof(Clause) * numClauses);

    printf("| Vars: %d | Clauses: %d |\n", numVars, numClauses);

    int clauseIndex = 0;
    while (getline(&line, &len, file) != -1 && clauseIndex < numClauses)
    {
        if (line[0] == 'c' || line[0] == 'p' || strlen(line) < 2)
            continue;

        int lit;
        int clauseSize = 0;
        int capacity = 4; // Starting capacity that can be doubled if more space is needed
        Literal *literals = (Literal *)malloc(sizeof(Literal) * capacity);

        char *token = strtok(line, " \t\n");
        while (token != NULL)
        {
            lit = atoi(token);
            if (lit == 0)
                break;

            if (clauseSize >= capacity)
            {
                capacity *= 2;
                literals = (Literal *)realloc(literals, sizeof(Literal) * capacity);
            }

            literals[clauseSize].var = abs(lit);
            literals[clauseSize].neg = lit < 0;
            clauseSize++;

            token = strtok(NULL, " \t\n");
        }

        formula->clauses[clauseIndex].size = clauseSize;
        formula->clauses[clauseIndex].literals = literals;
        formula->clauses[clauseIndex].satisfied = false;
        clauseIndex++;
    }

    // Resize if the file does not contain up the the correct amount of clauses.
    if (clauseIndex < numClauses)
    {
        Clause *resize_clauses = realloc(formula->clauses, sizeof(Clause) * clauseIndex);
        formula->numClauses = clauseIndex;
    }

    free(line);
    fclose(file);

    return formula;
}

// Undo Stack Push Functions

void push_assignment(UndoStack *stack, int var)
{
    UndoEntry *e = malloc(sizeof(UndoEntry));
    e->type = ASSIGNMENT;
    e->var = var;
    stack->head = g_slist_prepend(stack->head, e);
}

void push_clause_satisfy(UndoStack *stack, int index)
{
    UndoEntry *e = malloc(sizeof(UndoEntry));
    e->type = CLAUSE_SATISFY;
    e->index = index;
    stack->head = g_slist_prepend(stack->head, e);
}

void watchtable_add(WatchTable *wtable, int index, int value, UndoStack *stack)
{
    GArray *arr = wtable->watch_lists[index];
    g_array_append_val(arr, value);

    UndoEntry *e = malloc(sizeof(UndoEntry));
    e->type = WATCHLIST_ADD;
    e->index = index;
    e->var = value;
    stack->head = g_slist_prepend(stack->head, e);
}

void watchtable_remove(WatchTable *wtable, int index, int value, UndoStack *stack)
{
    GArray *arr = wtable->watch_lists[index];
    for (int i = 0; i < arr->len; i++)
    {
        if (g_array_index(arr, int, i) == value)
        {
            g_array_remove_index(arr, i);
            break;
        }
    }

    UndoEntry *e = malloc(sizeof(UndoEntry));
    e->type = WATCHLIST_REMOVE;
    e->index = index;
    e->var = value;
    stack->head = g_slist_prepend(stack->head, e);
}

void undo_to_checkpoint(UndoStack *stack, GSList *checkpoint, Formula *formula, int *assignments, WatchTable *wtable)
{
    while (stack->head != checkpoint)
    {
        UndoEntry *e = stack->head->data;
        if (e->type == ASSIGNMENT)
        {
            assignments[e->var] = -1;
        }
        else if (e->type == WATCHLIST_ADD)
        {
            GArray *arr = wtable->watch_lists[e->index];
            for (int i = 0; i < arr->len; i++)
            {
                if (g_array_index(arr, int, i) == e->var)
                {
                    g_array_remove_index(arr, i);
                    break;
                }
            }
        }
        else if (e->type == WATCHLIST_REMOVE)
        {
            GArray *arr = wtable->watch_lists[e->index];
            g_array_append_val(arr, e->var);
        }
        else if (e->type == CLAUSE_SATISFY)
        {
            formula->clauses[e->index].satisfied = false;
        }

        GSList *next = stack->head->next;
        free(e);
        g_slist_free_1(stack->head);
        stack->head = next;
    }
}

// WatchTable Init and Free functions

WatchTable *init_empty_watch_table(Formula *formula)
{
    WatchTable *wtable = (WatchTable *)malloc(sizeof(WatchTable));
    wtable->numVars = formula->numVars * 2 + 1;
    wtable->watch_lists = (GArray **)calloc(wtable->numVars, sizeof(GArray *));

    for (int i = 0; i < wtable->numVars; i++)
    {
        wtable->watch_lists[i] = g_array_new(FALSE, FALSE, sizeof(int));
    }

    return wtable;
}

void free_watchtable(WatchTable *wtable)
{
    if (!wtable)
        return;

    for (int i = 0; i < wtable->numVars; i++)
    {
        if (wtable->watch_lists[i])
        {
            g_array_free(wtable->watch_lists[i], TRUE);
        }
    }

    g_free(wtable->watch_lists);

    g_free(wtable);
}

// Formula Free function

void free_formula(Formula *formula)
{
    for (int i = 0; i < formula->numClauses; i++)
    {
        free(formula->clauses[i].literals);
    }
    free(formula->clauses);
    free(formula);
}

// Function to satisfy clauses after an assignment occurs

void satisfy_clauses_after_assignment(Formula *formula, int *assignments, UndoStack *stack)
{
    for (int i = 0; i < formula->numClauses; i++)
    {
        Clause *clause = &formula->clauses[i];

        if (clause->satisfied)
        {
            continue;
        }

        bool satisfied = false;
        for (int j = 0; j < clause->size; j++)
        {
            Literal lit = clause->literals[j];
            int a = assignments[lit.var];
            if ((a == 0 && lit.neg) || (a == 1 && !lit.neg))
            {
                clause->satisfied = true;
                push_clause_satisfy(stack, i);
                break;
            }
        }
    }
}

// Superset helper methods

gint lit_key(Literal *lit)
{
    return lit->neg ? -lit->var : lit->var;
}

gboolean clause_subset(Clause *a, Clause *b)
{
    GHashTable *setB = g_hash_table_new(g_int_hash, g_int_equal);
    for (int i = 0; i < b->size; i++)
    {
        gint *k = g_new(gint, 1);
        *k = lit_key(&b->literals[i]);
        g_hash_table_add(setB, k);
    }

    gboolean is_subset = true;
    for (int i = 0; i < a->size; i++)
    {
        gint key = lit_key(&a->literals[i]);
        if (!g_hash_table_contains(setB, &key))
        {
            is_subset = false;
            break;
        }
    }

    g_hash_table_destroy(setB);
    return is_subset;
}

void remove_supersets(Formula *formula)
{
    gboolean *remove_marked = g_new0(gboolean, formula->numClauses);

    for (int i = 0; i < formula->numClauses; i++)
    {
        if (remove_marked[i])
        {
            continue;
        }

        for (int j = 0; j < formula->numClauses; j++)
        {
            if (i == j || remove_marked[j])
            {
                continue;
            }

            Clause *clausei = &formula->clauses[i];
            Clause *clausej = &formula->clauses[j];

            if (clausei->size >= clausej->size && clause_subset(clausej, clausei))
            {
                remove_marked[i] = true;
                break;
            }
        }
    }

    int counter = 0;
    for (int i = 0; i < formula->numClauses; i++)
    {
        if (!remove_marked[i])
        {
            formula->clauses[counter] = formula->clauses[i];
            counter++;
        }
        else
        {
            free(formula->clauses[i].literals);
        }
    }

    formula->numClauses = counter;
    formula->clauses = realloc(formula->clauses, sizeof(Clause) * counter);
    g_free(remove_marked);
}
