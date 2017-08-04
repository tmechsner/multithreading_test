#include <stdlib.h>
#include <stdio.h>
#include "dbllinklist.h"

// @return 1 for empty, 0 for not empty
int list_empty(list_head_t* list) {
    if(list)
        return (list->next == list) && (list->prev == list);
    return 0;
}

/* initialize "shortcut links" for empty list */
void list_init(list_head_t *head) {
    if(head) {
        head->next = head;
        head->prev = head;
    }
}

/* insert new entry after the specified head */
void list_add(list_head_t *new, list_head_t *head) {
    if(new && head) {
        new->next = head->next;
        new->prev = head;
        head->next->prev = new;
        head->next = new;
    }
}

/* insert new entry before the specified head */
void list_add_tail(list_head_t *new, list_head_t *head) {
    if(new && head) {
        new->next = head;
        new->prev = head->prev;
        head->prev->next = new;
        head->prev = new;
    }
}

/* deletes entry from list, reinitializes it (next = prev = 0),
and returns pointer to entry */
list_head_t* list_del(list_head_t *entry) {
    entry->next->prev = entry->prev;
    entry->prev->next = entry->next;
    entry->next = entry;
    entry->prev = entry;
    return entry;
}

/* delete entry from one list and insert after the specified head */
void list_move(list_head_t *entry, list_head_t *head) {
    list_add(list_del(entry), head);
}

/* delete entry from one list and insert before the specified head */
void list_move_tail(list_head_t *entry, list_head_t *head) {
    list_add_tail(list_del(entry), head);
}

// Print the whole list
/*
void print_list(struct process *process) {
    // Print second to last element, as first element is the anchor
    for(list_head_t *ptr = process->elem.next; ptr != &process->elem; ptr = ptr->next) {
        struct process* elem = ptr;
        printf("%d\n", elem->pid);
    }
}

// Create a process instance,
// assign it's pid to the given pid,
// initialize it's list_head pointer
// and return a pointer to it.
struct process* create_process(int pid) {
    struct process* proc_ptr = malloc(sizeof(struct process));
    list_init(&(proc_ptr->elem));
    proc_ptr->pid = pid;
    return proc_ptr;
}


int main(int argc, char** argv) {
    // Create a list anchor
    list_head_t* list = malloc(sizeof(list_head_t*));
    list_init(list);
    
    printf("\nList empty? 1 = yes, 0 = no\n");
    printf("%d\n\n", list_empty(list));
    
    // Create process instances and append them to the list
    for(int i = 0; i < 5; i++) {
        list_add_tail(&create_process(i)->elem, list);
    }
    
    printf("Initial state after creating five processes\n");
    print_list(list);
    
    printf("\nCreate a new element and insert it at the front\n");
    struct process* new_process = create_process(5);
    list_add(&new_process->elem, list);
    print_list(list);
    
    printf("\nCreate a new element and insert it at the back\n");
    struct process* new_process_tail = create_process(6);
    list_add_tail(&new_process_tail->elem, list);
    print_list(list);
    
    printf("\nRemove the first element previously created\n");
    list_del(&new_process->elem);
    print_list(list);
    
    printf("\nMove the last element previously created to the front\n");
    list_move(&new_process_tail->elem, list);
    print_list(list);
    
    printf("\nMove it to the back again\n");
    list_move_tail(&new_process_tail->elem, list);
    print_list(list);
    
    printf("\nList empty? 1 = yes, 0 = no\n");
    printf("%d\n", list_empty(list));
    
    
    return 0;
}
*/
