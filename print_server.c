#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include "dbllinklist.h"
#include "makeargv.h"
#include "printer_management.h"
#include "UICI/restart.h"
#include "UICI/uici.h"



////////////////////////////
// TYPEDEFS and GLOBALS


/*
   A list element with associated data.
   Will be used for job lists as there are two different
   orders on jobs (clientwise and printerwise)
*/
typedef struct {
    list_head_t     list_elem;  // For hooking into a list
    void*           data;       // For the data
} list_elem_t;

/* Represents a connection to a client */
typedef struct {
    pthread_t   tid;                    // Client-Worker-Thread id
    int         com_fd;                 // File descriptor of communication channel
    char        client_name[MAX_CANON]; // Name of the client
} connection_t;

/* Represents a client connected to the server */
typedef struct {
    list_head_t     list_elem;  // Pointers to next and previos client (-> list clients)
    list_elem_t     jobs;       // Anchor to the list of jobs started by this client
    pthread_rwlock_t joblist_rw; // RW lock for manipulating the joblist
    int             job_counter;// For assigning new job ids
    int             id;         // Id of the client
    connection_t*   connection; // Connection to the client
    int             quit;       // quit != 0 -> close connection
} client_t;

/* Enum for job stati */
typedef enum {
    WAITING,
    IN_PROGRESS,
    CANCELED,
    FINISHED,
    PRINTER_ERROR,
    FILE_ERROR
} status_e;

/* Represents a printer */
typedef struct {
    list_head_t     list_elem;  // Pointers to next and previous printer (-> list printer_list)
    int             id;         // Id of the printer
    list_elem_t     jobs;       // Anchor to the list of jobs assigned to this printer
    int             fd;         // File descriptor to print to
    pthread_rwlock_t joblist_rw; // RW lock for manipulating the joblist
    pthread_mutex_t job_mutex;  // Mutex for the conditional
    pthread_cond_t  job_cond;   // Conditional to broadcast when this printer goes to idle
    status_e        status;     // Status of this printer
} printer_t;

/*
   Represents a print job.
   Has no list_head_t member as job lists are managed via list_elem_t
   with pointers to jobs.
*/
typedef struct {
    list_elem_t     client_list_elem; // Job list element in the client's list
    list_elem_t     printer_list_elem; // Job list element in the printer's list
    printer_t*      printer;    // Pointer to the printer that will have to execute this job
    client_t*       client;     // Pointer to the client that started this job
    char*           filename;   // Name of the file to read from
    FILE*           fd;         // File to read from
    int             page_count; // How many pages have been printed
    pthread_t       tid;        // Job worker thread id
    int             id;         // Client job id
    pthread_rwlock_t attr_rw; // Lock for accessing the job status
    status_e        status;     // Status of this job
} job_t;

/* Represents a command to be received by clients */
typedef struct {
    list_head_t     list_elem;       // Pointers to next and previous command (-> list commands)
    char            cmd[MAX_CANON];  // Name of the command
    void          (*functionPtr)(client_t*, int, char**, char*);  // Pointer to the function this command should call (client, args, retval)
} command_t;

/* Global list of commands a client can send */
list_head_t command_list;

/* Counter for assigning new client ids */
int client_count = 0;

/* Global list of available printers */
list_head_t printer_list;

/* Global list of connected clients */
list_head_t client_list;

/* RW lock to synchronize printer list access */
pthread_rwlock_t printer_list_rw;

/* RW lock to synchronize client list access */
pthread_rwlock_t client_list_rw;

/* Job Worker prototype */
void* job_worker(void* args);

/* get status prototype */
char* get_status(status_e status);

/* Max lines per page */
const int lines_per_page = 5;

/* Cost per page */
const double page_price = 0.05;




/*
 * Print a list of clients (prints their file descriptor number)
 */
void print_job_list(list_elem_t* job_list) {
    printf("\nJob List:\n---------------\n");
    // Print second to last element, as first element is the anchor
    for(list_head_t *ptr = job_list->list_elem.next; ptr != &job_list->list_elem; ptr = ptr->next) {
        list_elem_t* elem = (list_elem_t*)ptr;
        job_t* job = (job_t*)elem->data;
        char* status = get_status(job->status);
        printf("  Client %d, job %d, file '%s', status '%s'\n", job->client->id, job->id, job->filename, status);
        free(status);
    }
    printf("\n");
}




//////////////////
// COMMANDS


int invalid_arg_count(int req, int argc, char* retval) {
    argc = argc - 1;
    if(argc != req) {
        sprintf(retval, "  This command takes %d arguments. Instead received %d.\n", req, argc);
        return 1;
    }
    return 0;
}

/*
   Initializes a printer.
   The given pointer must be initialized.
   There must exist a printer with the given id
   that can be opened with open_printer(id).
*/
void init_printer(printer_t* printer, int printer_id) {
    list_init(&printer->list_elem);
    list_init(&printer->jobs.list_elem);        
    printer->id = printer_id;
    pthread_rwlock_init(&printer->joblist_rw, NULL);
    pthread_mutex_init(&printer->job_mutex, NULL);
    pthread_cond_init(&printer->job_cond, NULL);
    printer->status = WAITING;
    printer->fd = open_printer(printer->id);
}

/*
   Creates a print job.
   Prints the file with the given name on the printer with the given id.
   Usage: print printer_id filename
*/
void print_cmd_fct(client_t* client, int argc, char** args, char* retval) {
    printer_t* printer = NULL;

    // Check parameter count
    if(invalid_arg_count(2, argc, retval))
        return;    

    // Create new job
    job_t* job = malloc(sizeof(job_t));
    job->status = WAITING;
    
    // Check whether given id is valid and printer exists
    int printer_id = atoi(args[1]);
    if(printer_id == 0 || !printer_exists(printer_id)) {
        printf("Error: Printer does not exist or given argument is not a number.\n");
        job->status = PRINTER_ERROR;
    } else {
        // Check whether the printer is already in the list
        int printer_found = 0;
        if(pthread_rwlock_rdlock(&printer_list_rw) == 0) {
            for(list_head_t *ptr = printer_list.next; ptr != &printer_list; ptr = ptr->next) {
                printer = (printer_t*)ptr;
                if(printer_id == printer->id) {
                    printer_found = 1;
                    printf("Printer found in list.\n");
                    break;
                }
            }
        } else {
            printf("Error read-locking printer list lock! Could not start print job. \n");
            job->status = PRINTER_ERROR;
        }
        pthread_rwlock_unlock(&printer_list_rw);

        // If not: put it in the list
        if(!printer_found && job->status != PRINTER_ERROR) {
            printer = malloc(sizeof(printer_t));
            init_printer(printer, printer_id);

            pthread_rwlock_wrlock(&printer_list_rw);
            list_add_tail(&printer->list_elem, &printer_list);
            pthread_rwlock_unlock(&printer_list_rw);
        
            printf("Added new printer to list.\n");
        }
    }

    // Init job
    job->printer = printer;
    job->client = client;
    list_init(&job->client_list_elem.list_elem);
    list_init(&job->printer_list_elem.list_elem);
    job->filename = malloc(strlen(args[2]) * sizeof(char));
    strcpy(job->filename, args[2]);
    job->page_count = 0;
    pthread_rwlock_init(&job->attr_rw, NULL);
    
    // Put job in client's job list
    client->job_counter++;
    job->id = client->job_counter;
    job->client_list_elem.data = (void*)job;
    pthread_rwlock_wrlock(&client->joblist_rw);
    list_add_tail(&job->client_list_elem.list_elem, &client->jobs.list_elem);
    pthread_rwlock_unlock(&client->joblist_rw);

    // Put job in printer's job list (in case it exists)
    job->printer_list_elem.data = (void*)job;
    if(printer) {
        pthread_rwlock_wrlock(&printer->joblist_rw);
        list_add_tail(&job->printer_list_elem.list_elem, &printer->jobs.list_elem);
        pthread_rwlock_unlock(&printer->joblist_rw);
    }

    // Create job worker thread
    int error = pthread_create(&(job->tid), NULL, job_worker, job);
    if (error) {
        sprintf(retval, "  Failed to create job worker thread: %s\n", strerror(error));
        
        pthread_rwlock_wrlock(&client->joblist_rw);
        list_del(&job->client_list_elem.list_elem);
        pthread_rwlock_unlock(&client->joblist_rw);

        if(printer) {
            pthread_rwlock_wrlock(&printer->joblist_rw);
            list_del(&job->printer_list_elem.list_elem);
            pthread_rwlock_unlock(&printer->joblist_rw);
        }

        free(job->filename);
        free(job);
        return;
    }
 
    sprintf(retval, "  Created job no. %d\n", job->id);
    //print_job_list(&job->printer->jobs);
}

char* get_status(status_e status) {
    char* result = malloc(strlen("printer error!")*sizeof(char));
    switch(status) {
        case WAITING:       sprintf(result, "waiting"); break;
        case IN_PROGRESS:   sprintf(result, "printing"); break;
        case CANCELED:      sprintf(result, "cancelled"); break;
        case FINISHED:      sprintf(result, "finished"); break;
        case PRINTER_ERROR: sprintf(result, "printer error"); break;
        case FILE_ERROR:    sprintf(result, "file error"); break;
        default:            sprintf(result, "invalid state"); break;
    }
    return result;
}

/*
   Queries the status of a job.
   Usage: status job_id
*/
void status_cmd_fct(client_t* client, int argc, char** args, char* retval) {
    // Check parameter count
    if(invalid_arg_count(1, argc, retval))
        return;

    int job_id = atoi(args[1]);
    job_t* job;
    int job_found = 0;
    for(list_head_t *ptr = client->jobs.list_elem.next; ptr != &client->jobs.list_elem; ptr = ptr->next) {
        list_elem_t* list_elem = (list_elem_t*)ptr;
        job = (job_t*)list_elem->data;
        if(job->id == job_id) {
            job_found = 1;
            printf("Job found in list.\n");
            break;
        }
    }

    if(job_found) {
        pthread_rwlock_rdlock(&job->attr_rw);

        char* status = get_status(job->status);
        sprintf(retval, "  Job %d has status '%s'.\n", job_id, status);
        free(status);
        
        pthread_rwlock_unlock(&job->attr_rw);
    } else {
        sprintf(retval, "  Job %s could not be found. \n", args[1]);
    }

    return;
}

/*
   Queries the invoice of a job.
   Waits for that job to finish, if it has not finished yet.
   Usage: invoice job_id
*/
void invoice_cmd_fct(client_t* client, int argc, char** args, char* retval) {
    // Check parameter count
    if(invalid_arg_count(1, argc, retval))
        return;

    int job_id = atoi(args[1]);
    job_t* job;
    int job_found = 0;
    for(list_head_t *ptr = client->jobs.list_elem.next; ptr != &client->jobs.list_elem; ptr = ptr->next) {
        list_elem_t* list_elem = (list_elem_t*)ptr;
        job = (job_t*)list_elem->data;
        if(job->id == job_id) {
            job_found = 1;
            printf("Job found in list.\n");
            break;
        }
    }
    
    if(job_found) {
        void* thread_retval;
        
        pthread_rwlock_rdlock(&job->attr_rw);
        if(job->status == WAITING || job->status == CANCELED) { // Maybe the job hasn't noticed yet that it was cancelled because it is sleeping
            printf("Cancel thread as it might be sleeping\n");
            pthread_rwlock_unlock(&job->attr_rw);
            // We dont want to wait until the job awakens when some print job finishes
            pthread_cancel(job->tid);
            // Remove this job from the printer's joblist as it cannot do that itself anymore
            printf("Removing it from printer's joblist as it cannot do that itself anymore...\n");
            pthread_rwlock_wrlock(&job->printer->joblist_rw);
            list_del(&job->printer_list_elem.list_elem);
            pthread_rwlock_unlock(&job->printer->joblist_rw);
        } else {
            printf("Waiting for job %d to finish...\n", job->id);
            pthread_rwlock_unlock(&job->attr_rw);
            pthread_join(job->tid, &thread_retval);
        }
        printf("Thread finished.\n");
        
        double total = 0.0;
        pthread_rwlock_rdlock(&job->attr_rw);
        if(job->status != FILE_ERROR && job->status != PRINTER_ERROR) {
            total = page_price * job->page_count;
        }
        char* status = get_status(job->status);
        if(job->status == PRINTER_ERROR) {
            sprintf(retval, "  Job %d: status '%s', printed %d pages. %.2f total.\n", job->id, status, job->page_count, total);
        } else {
            sprintf(retval, "  Job %d, printer %d: status '%s', printed %d pages. %.2f total.\n", job->id, job->printer->id, status, job->page_count, total);
        }
        free(status);
        pthread_rwlock_unlock(&job->attr_rw);

        pthread_rwlock_wrlock(&client->joblist_rw);
        list_del(&job->client_list_elem.list_elem);
        pthread_rwlock_unlock(&client->joblist_rw);

        free(job->filename);
        free(job);
        printf("Removed job from client %d's job list.\n", client->id);
    } else {
        sprintf(retval, "  Job %s could not be found. \n", args[1]);
    }

    return;
}

/*
   Cancels a job if it hasn't finished yet or is in erroneous state.
*/
void cancel_job(int job_id, client_t* client, char* retval) {
    job_t* job;
    int job_found = 0;
    pthread_rwlock_rdlock(&client->joblist_rw);
    printf("  cancel_job: Looking for job...\n");
    for(list_head_t *ptr = client->jobs.list_elem.next; ptr != &client->jobs.list_elem; ptr = ptr->next) {
        list_elem_t* list_elem = (list_elem_t*)ptr;
        job = (job_t*)list_elem->data;
        if(job->id == job_id) {
            job_found = 1;
            break;
        }
    }
    pthread_rwlock_unlock(&client->joblist_rw);
    
    if(job_found) {
        printf("  cancel_job: Job found. Setting state to cancelled...\n");
        pthread_rwlock_wrlock(&job->attr_rw);
        if(job->status == IN_PROGRESS) {
            job->status = CANCELED;
            sprintf(retval, "  Job %d was cancelled.\n", job->id);
            // Don't remove it from printer list: job worker thread does that itself
        } else if(job->status == WAITING || job->status == CANCELED) {
            job->status = CANCELED;
            printf("  cancel_job: Sending thread cancel signal as it might be sleeping...\n");
            pthread_cancel(job->tid);
            printf("  cancel_job: Job was cancelled.  Removing it from printer's joblist as it cannot do that itself anymore...\n");
            // Remove job from printer list as it cannot remove itself as it has been cancelled
            pthread_rwlock_wrlock(&job->printer->joblist_rw);
            list_del(&job->printer_list_elem.list_elem);
            pthread_rwlock_unlock(&job->printer->joblist_rw);
            sprintf(retval, "  Job %d was cancelled.\n", job->id);
        } else {
            sprintf(retval, "  Job %d has already finished or is in error state.\n", job->id);
        }
        pthread_rwlock_unlock(&job->attr_rw);
    } else {
        sprintf(retval, "  Job %d could not be found. \n", job_id);
    }
    printf("  cancel_job: Ready.\n");    
    return;
}

/*
   Cancels a job if it hasn't finished yet or is in erroneous state.
   Usage: cancel job_id
*/
void cancel_cmd_fct(client_t* client, int argc, char** args, char* retval) {
    // Check parameter count
    if(invalid_arg_count(1, argc, retval))
        return;

    int job_id = atoi(args[1]);
    cancel_job(job_id, client, retval);
}

/*
   Queries a list of all jobs that have been created for the given printer.
   Usage: jobs printer_id
*/
void jobs_cmd_fct(client_t* calling_lient, int argc, char** args, char* retval) {
    // Check parameter count
    if(invalid_arg_count(1, argc, retval))
        return;

    int printer_id = atoi(args[1]);
    int jobs_found = 0;
    if(pthread_rwlock_rdlock(&client_list_rw) == 0) {
        char* extension = malloc(126*sizeof(char));
        char* text      = malloc(1*sizeof(char));
        text[0] = 0;
        // Traverse all clients...
        for(list_head_t *ptr = client_list.next; ptr != &client_list; ptr = ptr->next) {
            client_t* client = (client_t*)ptr;
            pthread_rwlock_rdlock(&client->joblist_rw);
            job_t* job = NULL;
            // ...and all their jobs
            for(list_head_t *ptr = client->jobs.list_elem.next; ptr != &client->jobs.list_elem; ptr = ptr->next) {
                list_elem_t* list_elem = (list_elem_t*)ptr;
                job = (job_t*)list_elem->data;
                if(job->printer != NULL && job->printer->id == printer_id) {
                    char* status = get_status(job->status);
                    sprintf(extension, "  Client %d, job %d, file '%s', status '%s'\n", job->client->id, job->id, job->filename, status);
                    free(status);
                    text = string_append(text, extension);
                    jobs_found++;
                }
            }
            pthread_rwlock_unlock(&client->joblist_rw);
        }
        sprintf(retval, "%s", text);
        free(text);
        free(extension);
        pthread_rwlock_unlock(&client_list_rw);

        if(!jobs_found) {
            sprintf(retval, "  Currently there are no jobs for printer %s.\n", args[1]);
        }
    } else {
        sprintf(retval, "  Error accessing client list.\n");
    }

    return;
}

/*
   Closes the connection.
   Cancels all jobs that have been started by the calling client.
   Usage: quit
*/
void quit_cmd_fct(client_t* client, int argc, char** args, char* retval) {
    // Check parameter count
    if(invalid_arg_count(0, argc, retval))
        return;
    
    char* text = malloc(5*sizeof(char));
    char* extension = malloc(200*sizeof(char));
    text[0] = 0;
    
    pthread_rwlock_rdlock(&client->joblist_rw);
    job_t* job = NULL;
    // Traverse all jobs of this client
    for(list_head_t *ptr = client->jobs.list_elem.next; !list_empty(&client->jobs.list_elem); ptr = client->jobs.list_elem.next) {
        list_elem_t* list_elem = (list_elem_t*)ptr;
        job = (job_t*)list_elem->data;
            
        cancel_job(job->id, client, extension);

        pthread_rwlock_rdlock(&job->attr_rw);
        if(job->status != WAITING && job->status != CANCELED) {
            pthread_rwlock_unlock(&job->attr_rw);
            void* thread_retval;
            pthread_join(job->tid, &thread_retval);
        } else {
            pthread_rwlock_unlock(&job->attr_rw);
        }
        text = string_append(text, extension);
        printf("quit_cmd: Job finished.\n");
        
        //pthread_rwlock_rdlock(&job->attr_rw);
        //if(job->status == IN_PROGRESS) {
        //    pthread_cond_broadcast(&job->printer->job_cond);
        //    pthread_mutex_unlock(&job->printer->job_mutex);
        //}
        //pthread_rwlock_unlock(&job->attr_rw);
        //pthread_mutex_lock(&printer->job_mutex);
        //pthread_cond_broadcast(&printer->job_cond);
        //pthread_mutex_unlock(&printer->job_mutex);
        
        //void* thread_retval;
        //pthread_join(job->tid, &thread_retval);

        printf("quit_cmd: Free job and delete from list...\n");
        list_del(&job->client_list_elem.list_elem);

        free(job->filename);
        free(job);
        printf("quit_cmd: Ready, next one...\n");
    }
    pthread_rwlock_unlock(&client->joblist_rw);

    sprintf(retval, "%s", text);
    
    printf("quit_cmd: Free extension string...\n");

    free(extension);
    free(text);
    printf("quit_cmd: Setting quit signal\n");
    client->quit = 1;
    
    printf("quit_cmd: Quit finished.\n");
    return;
}

/*
   Create a command object, assign it a name and a function and
   save it to the commands list.
*/
void add_command(char* name, void (*functionPtr)(client_t*, int, char**, char*)) {
    command_t* cmd = malloc(sizeof(command_t));
    strncpy(cmd->cmd, name, MAX_CANON);
    cmd->functionPtr = functionPtr;
    list_init(&cmd->list_elem);
    list_add(&cmd->list_elem, &command_list);
}

/*
   Create commands:
   Associate names with functions and save them to the command list.
*/
void init_commands() {
    list_init(&command_list);
    add_command("print", &print_cmd_fct); 
    add_command("status", &status_cmd_fct);
    add_command("invoice", &invoice_cmd_fct);
    add_command("cancel", &cancel_cmd_fct);
    add_command("jobs", &jobs_cmd_fct);
    add_command("quit", &quit_cmd_fct);
}



/////////////////
// THREADS


/*
 * Job-Worker Thread
 * Executes the attached job.
 */
void* job_worker(void *arg) {
    job_t* job = (job_t*)arg;
    printer_t* printer = job->printer;

    // For proper cancel handling
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    if(!printer) {
        return NULL;
    }

    // Sleep until this job is at top of the printer's job list
    pthread_mutex_lock(&printer->job_mutex);
    while(1) {
        pthread_rwlock_rdlock(&printer->joblist_rw);
        if(printer->jobs.list_elem.next != &job->printer_list_elem.list_elem) {
            pthread_rwlock_unlock(&printer->joblist_rw);
            // This is NOT the next job
            // Check if the job has been canceled
            printf("    jobworker: Job %d here, but it's not my turn. Check if I have been canceled...\n", job->id);
            pthread_rwlock_rdlock(&job->attr_rw);
            if(job->status == CANCELED) {
                pthread_rwlock_unlock(&job->attr_rw);
                break;
            }
            pthread_rwlock_unlock(&job->attr_rw);

            pthread_cond_wait(&printer->job_cond, &printer->job_mutex);
        } else {
            // This IS the next job
            pthread_rwlock_unlock(&printer->joblist_rw);
            break;
        }
    }
    
    // This job is at top of the printer's job list, check file to read and start printing!
    char* line = NULL;
    size_t len = 0;
    ssize_t read;
    int aborted = 0;
    int line_count = 0;

    job->fd = fopen(job->filename, "r");
    if (job->fd == NULL) {
        pthread_rwlock_wrlock(&job->attr_rw);
        job->status = FILE_ERROR;
        pthread_rwlock_unlock(&job->attr_rw);
        aborted = 1;
        printf("    jobworker: Could not read file %s.\n", job->filename);
    } else {
        pthread_rwlock_rdlock(&job->attr_rw);
        if(job->status == CANCELED) {
            printf("    jobworker: Job canceled: Client %d, job %d, printer %d\n", job->client->id, job->id, job->printer->id);
            aborted = 1;
        } else {
            printf("    jobworker: Start printing: Client %d, job %d, printer %d\n", job->client->id, job->id, job->printer->id);
            pthread_rwlock_unlock(&job->attr_rw);
            pthread_rwlock_wrlock(&job->attr_rw);
            job->status = IN_PROGRESS;
            job->page_count = 1;
        }
        pthread_rwlock_unlock(&job->attr_rw);

        while(!aborted && (read = getline(&line, &len, job->fd)) != -1) {
            line_count++;
            for(int i = 0; i < strlen(line) && !aborted; i++) {       
                // Check whether the printer is available
                if(!printer_exists(printer->id)) {
                    pthread_rwlock_wrlock(&job->attr_rw);
                    job->status = PRINTER_ERROR;
                    pthread_rwlock_unlock(&job->attr_rw);
                    aborted = 1;
                    printf("    jobworker: Job error: Printer %d became unavailable.\n", job->printer->id);
                    break;
                }
               
                // Page full? 
                if(line_count > lines_per_page) {
                    print_char(printer->fd, '\n');
                    pthread_rwlock_wrlock(&job->attr_rw);
                    job->page_count++;
                    pthread_rwlock_unlock(&job->attr_rw);
                    line_count = 1;
                }

                //printf("Next char is: %c\n", job->text[i]); 
                print_char(printer->fd, line[i]);

                // Check whether the job has been canceled 
                pthread_rwlock_rdlock(&job->attr_rw);
                if(job->status == CANCELED) {
                    pthread_rwlock_unlock(&job->attr_rw);
                    aborted = 1;
                    printf("    jobworker: Job canceled: Client %d, job %d, printer %d\n", job->client->id, job->id, job->printer->id);
                    break;
                }
                pthread_rwlock_unlock(&job->attr_rw);
            }
        }
        fclose(job->fd);
        if (line) {
            free(line);
        }
    }

    // Remove this job from the printer's joblist
    printf("    jobworker: Removing myself from printer's joblist\n");
    pthread_rwlock_wrlock(&printer->joblist_rw);
    list_del(&job->printer_list_elem.list_elem);
    pthread_rwlock_unlock(&printer->joblist_rw);

    
    //print_job_list(&job->printer->jobs);


    printf("    jobworker: Broadcasting signal\n");
    pthread_cond_broadcast(&printer->job_cond);
    pthread_mutex_unlock(&printer->job_mutex);

    // Set this job's status to finished if there was no error / cancellation
    if(!aborted) {
        printf("    jobworker: Finished printing: Client %d, job %d, printer %d, printed pages %d\n", job->client->id, job->id, job->printer->id, job->page_count);
        pthread_rwlock_wrlock(&job->attr_rw);
        job->status = FINISHED;
        pthread_rwlock_unlock(&job->attr_rw);
    } else {
        printf("    jobworker: Cancellation complete.\n");
    }

    if(list_empty(&job->printer->jobs.list_elem)) {
        printf("    jobworker: Joblist of printer %d now empty.\n", job->printer->id);
    } else {
        printf("    jobworker: Joblist of printer %d is not empty.\n", job->printer->id);
    }

    return NULL;
}

/*
 * Initialize a client: create job list, client-list-element and save connection
 */
void init_client(client_t* client, connection_t* con) {
    client->connection = con;
    client->job_counter = 0;
    client->quit = 0;
    client_count++;
    client->id = client_count;
    list_init(&client->jobs.list_elem);
    list_init(&client->list_elem);
}

/*
 * Print a list of clients (prints their file descriptor number)
 */
void print_client_list(list_head_t* client_list) {
    printf("\nClient List:\n---------------\n");
    // Print second to last element, as first element is the anchor
    for(list_head_t *ptr = client_list->next; ptr != client_list; ptr = ptr->next) {
        client_t* elem = (client_t*)ptr;
        printf("%d\n", elem->id);
    }
    printf("\n");
}

/*
 * Client-Worker Thread
 * Communicates with the attached client and creates Job-Worker Threads.
 */
void* client_worker(void *arg) {
    client_t* client = (client_t*) arg;
    connection_t* con = client->connection;
    int bytesread;
    char buf[MAX_CANON];
    char* reply = malloc(10000*sizeof(char));

    fprintf(stderr, "fd=%d: connected to %s\n", con->com_fd, con->client_name);
  
    // read data from client until client quits
    while (client->quit == 0) {
        // read data from client until client quits
        bytesread = read(con->com_fd, buf, MAX_CANON-1);
        if (bytesread == -1) {
            fprintf(stderr, "fd=%d: communication error with client %s\n", 
                con->com_fd, con->client_name);
            break;
        }
        
        // zero bytes indicates eof (client has closed connection)
        if (bytesread == 0) {
            fprintf(stderr, "fd=%d: connection closed by client %s\n", 
                con->com_fd, con->client_name);
            break;
        }
        
        // if we got to this point, we have data
        fprintf(stderr, "\nIncoming Message from fd %d\n", con->com_fd);
        buf[bytesread] = '\0';
        if (bytesread > 0) {
            
            // Remove trailing control characters from received string
            char** clean_input;
            if(makeargv(buf, "\r\n", &clean_input) == -1) {
                printf("Could not parse received message! Could not remove control characters.\n");
                continue;
            }
            
            fprintf(stderr, "Message: %s\n", *clean_input);
            
            // Tokenize received string to seperate cmd and params
            char** args;
            int argc = makeargv(*clean_input, " ", &args);
            if(argc == -1) {
                printf("Could not parse received message! Could not tokenize the string.\n");
                continue;
            }
            
            // Traverse commands, compare to first element in args (should be the command name)
            int command_found = 0;
            for(list_head_t *ptr = command_list.next; ptr != &command_list; ptr = ptr->next) {
                command_t* elem = (command_t*)ptr;
                if(!strcmp(args[0], elem->cmd)) {
                    command_found = 1;
                    printf("Calling function '%s'\n", elem->cmd);
                    (*elem->functionPtr)(client, argc, args, reply);
                }
            }
            if(!command_found) {
                sprintf(reply, "  '%s' is not a valid command.\n", args[0]);
            }
            printf("Function returned: %s\n", reply);
            freemakeargv(clean_input);
            freemakeargv(args);
            
            // reply
            write(con->com_fd, reply, strlen(reply));
        }
    }
    
    // closing connection
    fprintf(stderr, "fd=%d: closing connection to client %s\n", 
            con->com_fd, con->client_name);
    if (close(con->com_fd) == -1)
        perror("failed to close com_fd\n");
   
    pthread_rwlock_wrlock(&client_list_rw);
    list_del(&client->list_elem);
    pthread_rwlock_unlock(&client_list_rw);
 
    free(client);
    
    return NULL;
}

/* 
 * Dispatcher Thread
 * Waits for new connections and creates client-worker-threads for these.
 */
int main(int argc, char *argv[]) {
    u_port_t port;
    int listenfd;
    connection_t *con;

    init_commands();
        
    list_init(&client_list);
    list_init(&printer_list);

    pthread_rwlock_init(&printer_list_rw, NULL);
    pthread_rwlock_init(&client_list_rw, NULL);

    if (argc != 2) {
        fprintf(stderr, "Usage: %s port\n", argv[0]);
        return 1;   
    }
    
    // create listening endpoint
    port = (u_port_t) atoi(argv[1]);
    if ((listenfd = u_open(port)) == -1) {
        perror("Failed to create listening endpoint");
        return 1;
    }
  
    // endless loop: look for client, spawn client-worker-thread
    while (1) {
        
        con = malloc(sizeof(connection_t));
        
        // wait for client to connect
        // free connection in error case
        fprintf(stderr, "waiting for connection on port %d\n", (int)port);
        if ((con->com_fd = u_accept(listenfd, con->client_name, MAX_CANON)) == -1) {
            perror("failed to accept connection");
            free(con);
            continue;
        }
        
        // create and save a client from the connection
        client_t* client = malloc(sizeof(client_t));
        init_client(client, con);
        pthread_rwlock_wrlock(&client_list_rw);
        list_add_tail(&client->list_elem, &client_list);
        pthread_rwlock_unlock(&client_list_rw);
        
        // start a thread and detach it
        // free connection and client in error case
        int error;
        error = pthread_create(&(con->tid), NULL, client_worker, client);
        if (error) {
            fprintf(stderr, "failed to create thread %s\n", strerror(error));
            free(con);
            free(client);
            continue;
        } else {
            pthread_detach(con->tid);
        }
        
        print_client_list(&client_list);
    }
}
 
