typedef struct list_head {
    struct list_head* next;
    struct list_head* prev;
} list_head_t;

/* initialize "shortcut links" for empty list */
void list_init(list_head_t *head);

/* insert new entry after the specified head */
void list_add(list_head_t *new, list_head_t *head);

/* insert new entry before the specified head */
void list_add_tail(list_head_t *new, list_head_t *head);

/* deletes entry from list, reinitializes it (next = prev = 0),
and returns pointer to entry */
list_head_t* list_del(list_head_t *entry);

/* delete entry from one list and insert after the specified head */
void list_move(list_head_t *entry, list_head_t *head);

/* delete entry from one list and insert before the specified head */
void list_move_tail(list_head_t *entry, list_head_t *head);

/* tests whether a list is empty */
int list_empty(list_head_t *head);