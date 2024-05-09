#include <sys/wait.h>
#include "buffermngr.h"
#include "parse.h"
#include "util.h"
#include <string.h>

/* Constants */
#define DEBUG 0

/*
static const char *instructions[] = { "quit", "help", "list", "new", "open", "write", "close", "print", "active", "pause", "resume", "cancel", "exec", NULL};
static const char *textproc_path[] = { "./", "/usr/bin/", NULL };
*/
typedef struct buffer {
    int id; // the id of the buffer
    char *cmd; // the cmd of the node
    char *content;
    int state; // the state of the node (ready, working, pause)
    int type; // type of the node(active, background)
    int pid; // pid (assigned after fork)
    int in_p[2];
    int out_p[2];
    struct buffer *next; // pointer to the next node
} buffer_process;


typedef struct queue {
    int count; // number of nodes in the queue
    buffer_process *head; // head of the queue
    buffer_process *tail; // tail of the queue
    buffer_process *active; // keep track of which node is active
} buffer_queue;


buffer_queue *queue = NULL;


buffer_process *remove_helper(buffer_queue **queue, int id);

buffer_process *node_getter(buffer_queue *queue, int id);

void add_helper(buffer_queue **queue, buffer_process *node);

void assigning_type(buffer_queue **queue, int id);

void listing(buffer_queue **queue);

void active(buffer_queue **queue, int id);

void close_helper_wo_argument(buffer_queue **queue, buffer_process *removed);

void close_helper_w_argument(buffer_queue **queue, buffer_process *removed);

void close_helper(buffer_queue **queue, int id);

void cancel_helper(buffer_queue **queue, int id);

void pause_helper(buffer_queue **queue, int id);

void key_c_handle(int signal);

void key_z_handle(int signal);

void exec_helper(buffer_queue **queue, char *cmdLine, char *argv[], int id, char *cmd);

void resume_helper(buffer_queue **queue, int id);

void handle_sigint(int sig);

void signal_handler(int signal);

void handle_sigtstp(int sig);

void initializing_sigint();

void initializing_sigtstp();

void initializing_sigchild();

void call_pipe(buffer_process **node);

void call_dup2(buffer_process **node);

buffer_process *check_for_id(buffer_queue **queue, int id);

void handle_exit_status(buffer_process **node);

void handle_stopped_status(buffer_process **node);

void handle_continued_status(buffer_process **node);

void reset_cmd_pid(buffer_process **node);

void process_helper(buffer_queue **queue, int id, int signal_type, int log_type, int log_cmd_type);

#define WAIT_OPTIONS (WUNTRACED | WCONTINUED | WNOHANG)


/* The entry of your text processor program */
int main() {
    char cmdline[MAXLINE];        /* Command line */
    char *cmd = NULL;


    /* Intial Prompt and Welcome */
    log_help();
    queue = malloc(sizeof(buffer_queue));
    queue->count = 0;
    queue->head = NULL;
    queue->tail = NULL;
    queue->active = NULL;
    /* Shell looping here to accept user command and execute */
    initializing_sigint();
    initializing_sigchild();
    initializing_sigtstp();
    while (1) {
        char *argv[MAXARGS];        /* Argument list */
        Instruction inst;           /* Instruction structure: check parse.h */


        /* Print prompt */
        log_prompt();


        /* Read a line */
        // note: fgets will keep the ending '\n'
        errno = 0;
        if (fgets(cmdline, MAXLINE, stdin) == NULL) {
            if (errno == EINTR) {
                continue;
            }
            exit(-1);
        }


        if (feof(stdin)) {  /* ctrl-d will exit text processor */
            exit(0);
        }


        /* Parse command line */
        if (strlen(cmdline) == 1)   /* empty cmd line will be ignored */
            continue;


        cmdline[strlen(cmdline) - 1] = '\0';        /* remove trailing '\n' */


        cmd = malloc(strlen(cmdline) + 1);
        snprintf(cmd, strlen(cmdline) + 1, "%s", cmdline);


        /* Bail if command is only whitespace */
        if (!is_whitespace(cmd)) {
            initialize_command(&inst, argv);    /* initialize arg lists and instruction */
            parse(cmd, &inst, argv);            /* call provided parse() */


            if (DEBUG) {
                debug_print_parse(cmd, &inst, argv, "main (after parse)");
            }


            /* After parsing: your code to continue from here */
            /*================================================*/


            if (strcmp(inst.instruct, "help") == 0) {
                log_help();


            } else if (strcmp(inst.instruct, "quit") == 0) {
                log_quit();
                exit(0);


            } else if (strcmp(inst.instruct, "new") == 0) {
                buffer_process *node = malloc(sizeof(buffer_process));//allocating memory for new node
                node->cmd = malloc(MAXLINE);// allocating memory for cmd of node
                strcpy(node->cmd, cmd); // copy the cmd to the node cmd
                if (queue->count != 0) { // if the queue is not empty
                    node->id = queue->tail->id + 1; // the id of the new node will be the id of the tail + 1
                } else {
                    node->id = queue->count + 1; // if not then the id of the new node will be the id of current count
                }
                node->content = malloc(500);
                node->state = LOG_STATE_READY; // set the state to ready
                node->next = NULL; // set the next pointer to null
                node->type = LOG_ACTIVE; //set the type to LOG_ACTIVE
                log_open(node->id); // print out the new open node
                log_activate(node->id); // print out activating the newly created node
                add_helper(&queue, node); // add the node into the queue


                //updating the type, only set the active to buffer type to active, otherwise it's in the background
                assigning_type(&queue, queue->active->id);


                //running the exec in the updated new command
                if (argv[1] != NULL) {
                    exec_helper(&queue, cmdline, argv, inst.id, cmd);
                }
            } else if (strcmp(inst.instruct, "list") == 0) {
                listing(&queue); // print out the list of the nodes
            } else if (strcmp(inst.instruct, "active") == 0) {
                active(&queue, inst.id); // print out the active node
            } else if (strcmp(inst.instruct, "close") == 0) {
                close_helper(&queue, inst.id); // close the node
            } else if (strcmp(inst.instruct, "open") == 0) {
                buffer_process *new_buffer = malloc(sizeof(buffer_process)); //create a new buffer
                new_buffer->cmd = malloc(MAXLINE); // allocate memory for cmd
                new_buffer->content = malloc(500); // allocate memory for cmd
                if (queue->count == 0) { // check if the queue is empty, set the id appropriately
                    new_buffer->id = queue->count + 1; // set the id to the count + 1
                } else { // if the queue is not empty, set the id appropriately
                    new_buffer->id = queue->tail->id + 1; // set the id to the tail + 1
                }
                int file_descriptor = open(inst.file, O_RDONLY); // open the file
                char *char_txt = fd_to_text(file_descriptor); // convert the file to text
                if (file_descriptor != -1) { // if the file is not empty
                    strcpy(new_buffer->content, char_txt); // copy the content to the buffer
                    log_open(new_buffer->id); // print out the open message
                    log_activate(new_buffer->id); // print out the activate message
                    queue->active = new_buffer; // set the active node to the new buffer
                    add_helper(&queue, new_buffer); // add the new buffer to the queue
                    log_read(new_buffer->id, inst.file); // print out the read message


                } else { // if the file is empty
                    log_file_error(LOG_FILE_OPEN_READ, inst.file); // print out the error message
                    close(file_descriptor); // close the file
                }
                free(char_txt); // free the char_txt
                close(file_descriptor); // close the file
            } else if (strcmp(inst.instruct, "write") == 0) { // write the content to the file
                int file_descriptor = open(inst.file, O_WRONLY | O_CREAT | O_TRUNC, 0600); // open the file
                if (file_descriptor != -1) { // if the file is not empty


                    if (inst.id == 0) {  // if there is no argument
                        inst.id = queue->active->id; // set the id to the active node
                    }

                    buffer_process *node = node_getter(queue, inst.id); // get the node with the id
                    if (node != NULL) { // if the node is not null
                        text_to_fd(node->content, file_descriptor); // write the content to the file
                        log_write(node->id, inst.file); // print out the write message
                        close(file_descriptor); // close the file
                    } else {
                        log_buf_id_error(inst.id); // print out the error message
                    }
                } else { // if the file is empty
                    log_file_error(LOG_FILE_OPEN_WRITE, inst.file); // print out the error message
                    close(file_descriptor); // close the file
                }
            } else if (strcmp(inst.instruct, "print") == 0) { // print the content of the node
                if(queue->count == 0){
                    log_buf_id_error(inst.id); // print out the error message
                    continue;
                }
                if (inst.id != 0) { // if there is an argument
                    buffer_process *node_with_id = node_getter(queue, inst.id); // get the node with the id

                    if (node_with_id == NULL) { // if the node is null
                        log_buf_id_error(inst.id); // print out the error message
                        continue;
                    } else { // if the node is not null
                        log_print(node_with_id->id, node_with_id->content); // print out the content of the node
                    }
                } else { // if there is no argument
                    log_print(queue->active->id, queue->active->content); // print out the content of the active node
                }
            } else if (strcmp(inst.instruct, "exec") == 0) { // execute the command
                if(queue->count == 0){
                    log_buf_id_error(inst.id); // print out the error message
                    continue;
                }else{
                    exec_helper(&queue, cmdline, argv, inst.id, cmd); // call the exec helper
                }
            } else if (strcmp(inst.instruct, "cancel") == 0) { // cancel the command
                if(queue->count == 0){
                    log_buf_id_error(inst.id); // print out the error message
                }else{
                    cancel_helper(&queue, inst.id); // call the cancel helper
                }
            } else if (strcmp(inst.instruct, "pause") == 0) { // pause the command
                if(queue->count == 0){
                    log_buf_id_error(inst.id); // print out the error message
                }else{
                    pause_helper(&queue, inst.id); // call the pause helper
                }
            } else if (strcmp(inst.instruct, "resume") == 0) { // resume the command
                if(queue->count == 0){
                    log_buf_id_error(inst.id); // print out the error message
                }else{
                    resume_helper(&queue, inst.id); // call the resume helper
                }
            }
        }
        free(cmd);
        cmd = NULL;
        free_command(&inst, argv);
    }
    return 0;
}


//remove function that remove a node from the queue using its id
buffer_process *remove_helper(buffer_queue **queue, int id) {
    if ((*queue)->count == 0) { // check if the queue is empty
        return NULL; // return null if the queue is empty
    }


    buffer_process *temp1 = (*queue)->head; // temp variable point at the head of the queue
    buffer_process *previous = (*queue)->head; // previous variable point at the head of the queue


    if ((*queue)->head->id == id) { // if removing the head of the queue


        (*queue)->head = temp1->next; // move the head to the next node
        temp1->next = NULL; // set that node next pointer to null


    } else if ((*queue)->tail->id == id) { // if removing the tail of the queue


        temp1 = (*queue)->tail; // have the temp variable point at the tail


        while (previous->next->id !=
               (*queue)->tail->id) { // keep looping until the previous variable is at the node before the tail
            previous = previous->next;
        }
        previous->next = NULL; // set next pointer of that node to null, cutting the tail
        (*queue)->tail = previous; // setting the new tail as the previous node


    } else { //  if removing the buffer is in the middle of the queue
        while (temp1 != NULL) {
            if (temp1->id ==
                id) { // keep looping until the id of that node matches with the id of the node you want to remove
                previous->next = temp1->next; // set the pointer of the previous node of the current node to the next node of the current node, cutting the back
                temp1->next = NULL; // cutting the front
                break;
            }
            previous = temp1; // setting the previous node to the current node
            temp1 = temp1->next; // move the current node to the next node
        }
    }

    (*queue)->count--; //  lower the count
    return temp1; // return the removed node
}


//getting the node with the input id
buffer_process *node_getter(buffer_queue *queue, int id) {
    if(queue->count == 0){
        return NULL;
    }
    buffer_process *temp = queue->head; //  set the temp variable at the head
    while (temp != NULL) {// keep looping until temp is at the node you want
        if (temp->id == id) {
            break;
        }
        temp = temp->next;
    }
    return temp; // return the node with the input id
}


// adding the node to the queue
void add_helper(buffer_queue **queue, buffer_process *node) {
    if ((*queue)->head != NULL) { // add the newly created node to the end of the queue
        (*queue)->tail->next = node; // set the next pointer of tail to that node
        (*queue)->tail = node; // set the new tail
        (*queue)->active = (*queue)->tail; // set the active node to the tail
    } else { // if head is null
        (*queue)->head = node; // set head as the newly created node
        (*queue)->tail = node; // set tail as the newly created node
        (*queue)->active = (*queue)->tail; // set the active node as the newly created node
    }
    (*queue)->count++; // increment the count
}


// assigning the type of all the node as either active or background
void assigning_type(buffer_queue **queue, int id) {
    buffer_process *temp = (*queue)->head; // set the pointer at head
    while (temp != NULL) {
        if (temp->id == id) { // if the node does not match the input id
            temp->type = 0; //then that node is in the background
        }else{
            temp->type = 1;
        }
        temp = temp->next;
    }
}


//function to show all the nodes in the queue
void listing(buffer_queue **queue) {
    buffer_process *temp = (*queue)->head; // create a temp at head
    log_buf_count((*queue)->count); // print out the count
    if ((*queue)->count == 0) { // if count is 0 then
        log_show_active(0); // print out there is no active buffer
    } else {
        while (temp != NULL) { // loop through each node and print out the detail of the node
            log_buf_details(temp->id, temp->state, temp->pid, temp->cmd);
            temp = temp->next;
        }
        log_show_active((*queue)->active->id); // print out the current active node
    }
}


//
void active(buffer_queue **queue, int id) {
    if((*queue)->count == 0){
        log_buf_id_error(id);
        return;
    }
    buffer_process *temp = (*queue)->head; // create a temp at head
    if ((*queue)->count > 0) { // if the queue is not empty
        int check = 0; // flag for checking
        if (id != 0) { // if there is an argument
            while (temp != NULL) { // loop through the queue
                if (temp->id == id) { // find the node with the same id
                    (*queue)->active = temp; // set that node activate
                    log_activate((*queue)->active->id); // print out the activate message
                    assigning_type(queue, (*queue)->active->id); // update the type of the nodes
                    check = 1; // turn the flag to true
                    break; // break;
                }
                temp = temp->next;
            }
            if (check == 0) { // if the flag is 0, no node with that id
                log_buf_id_error(id); // print out the error with that argument
            }
        } else { // if there is no argument then print out the current active node
            log_activate((*queue)->active->id);
        }
    } else {
        log_buf_id_error(id); // if the queue is empty then print out the error message
    }
}


void close_helper_wo_argument(buffer_queue **queue, buffer_process *removed) {
    log_close(removed->id); // close that id
    if ((*queue)->count == 0) { // if queue is empty
        return;
    } else { // queue is not empty
        (*queue)->active = (*queue)->tail; // find node with highest id
        log_activate((*queue)->active->id); // print message
    }
}


void close_helper_w_argument(buffer_queue **queue, buffer_process *removed) {
    buffer_process *temp1 = (*queue)->head;
    log_close(removed->id); // close that removed node


    if ((*queue)->count == 0) { // check if the queue is empty
        return; // do nothing
    } else { // queue is not empty
        int check = 0; // flag for checking
        while (temp1 != NULL) { // loop through the queue
            if (temp1->id == (*queue)->tail->id) { // find node with id
                if (removed->id == (*queue)->active->id) {
                    (*queue)->active = temp1; // set that node to active
                    log_activate((*queue)->active->id); // print message
                } else {
                    (*queue)->active = temp1; // set that node to active
                }
                check = 1;
                break;
            }
            temp1 = temp1->next;
        }
        if (check == 0) { // if node could not be found
            log_buf_id_error(removed->id); // print error
        }
    }
}


void close_helper(buffer_queue **queue, int id) {
    buffer_process *removed = NULL;
    if ((*queue)->count == 0) { // if the queue is empty
        log_buf_id_error(id);
    } else { // if the queue is not empty
        if (id != 0) { // if there is an argument
            removed = remove_helper(queue, id); // remove the node with the id
            if (removed != NULL) { // if the removed node is not null
                if (removed->state != LOG_STATE_READY) { // if the removed node is not ready
                    log_close_error(id); // print error message
                } else { // if the removed node is ready
                    close_helper_w_argument(queue, removed); // call helper to close the node
                }
            } else { // if the removed node is null
                log_buf_id_error(id); // print error message
            }
        } else { // if there is no argument
            removed = remove_helper(queue, (*queue)->active->id); // Call helper to remove active id
            if (removed != NULL) { // if the removed node is not null
                if (removed->state != LOG_STATE_READY) { // if the removed node is not ready
                    log_close_error(id); // print error message
                } else { // if the removed node is ready
                    close_helper_wo_argument(queue, removed); // call helper to close the node
                }
            } else { // if the removed node is null
                log_buf_id_error(id); // print error message
            }
            assigning_type(queue, (*queue)->active->id); // update the type of the nodes
        }
    }
}


buffer_process *get_node_w_pid(buffer_queue *queue, pid_t pid) { // getting node with the input pid

    buffer_process *temp = queue->head; // setting temp at head
    while (temp != NULL) {  // loop through  queue
        if (temp->pid == pid) {
            break;
        }
        temp = temp->next;
    }
    return temp; // return the correct one
}


void signal_handler(int signal) { // sigchild
    pid_t pid; // creating pid
    int status; // creating status
    while ((pid = waitpid(-1, &status, WAIT_OPTIONS)) > 0) { // wait for the child process to finish
        buffer_process *temp1 = get_node_w_pid(queue, pid); // get the node with the pid
        if (temp1 == NULL) { // if the node is null
            return; // do nothing
        }
        if (WIFEXITED(status)) { // if the child process exited
            if (WEXITSTATUS(status) == 0) { // if the exit status is 0
                handle_exit_status(&temp1); // log the exit status
            }
            temp1->state = LOG_STATE_READY; // set to ready
            log_cmd_state(temp1->pid, temp1->type, temp1->cmd, LOG_CANCEL); // log the cancel status
            reset_cmd_pid(&temp1); // reset the pid and cmd
        } else if (WIFSTOPPED(status)) { // if the child process is stopped
            handle_stopped_status(&temp1); // log the stopped status. set to paused
        } else if (WIFCONTINUED(status)) { // if the child process is continued
            handle_continued_status(&temp1); // log the continued status, set to working

            sigset_t mask;
            sigemptyset(&mask);
            sigaddset(&mask, SIGCHLD);
            sigprocmask(SIG_BLOCK, &mask, NULL);

            if (temp1->id == queue->active->id) { // if the node is the active node
                if(waitpid(temp1->pid, &status, 0) > 0){
                    if (WIFEXITED(status)) { // if the process exited
                        if (WEXITSTATUS(status) == 0) { // if the exit status is 0
                            handle_exit_status(&temp1); // log the exit status
                        }
                        log_cmd_state(temp1->pid, temp1->type, temp1->cmd, LOG_CANCEL); // log the cancel status
                    }
                    temp1->state = LOG_STATE_READY;
                    reset_cmd_pid(&temp1); // reset the pid and cmd
                }
            }
            sigprocmask(SIG_UNBLOCK, &mask, NULL);
        } else if (WIFSIGNALED(status)) { // if the child process is signaled
            temp1->state = LOG_STATE_READY; // set the state to ready
            log_cmd_state(temp1->pid, temp1->type, temp1->cmd, LOG_CANCEL_SIG); // log the cancel signal
            reset_cmd_pid(&temp1); // reset the pid and cmd
        }
    }
}

void exec_helper(buffer_queue **queue, char *cmdLine, char *argv[], int id, char *cmd) { // exec helper
    buffer_process *node = check_for_id(queue, id); // check for the node with the id

    if (node == NULL) { // if the active buffer is null
        log_buf_id_error(node->id); // print out the error message
        return; // do nothing
    } else if (node->state != LOG_STATE_READY) { // if the active buffer is not ready
        log_cmd_state_conflict(node->id, node->state); // print out the error message
        return; // do nothing
    } else {
        //creating 2 pipes
        call_pipe(&node);

        sigset_t mask; // creating a mask
        sigemptyset(&mask); // empty the mask
        sigaddset(&mask, SIGCHLD); // add the signal to the mask
        sigprocmask(SIG_BLOCK, &mask, NULL); // block the signal
        //call fork
        pid_t child_pid = fork();
        if (child_pid == 0) { // child process
            setpgid(0, 0); // set the process group id
            close(node->in_p[1]); // close in pipe
            close(node->out_p[0]); // close out pipe
            // Redirecting stdin to read from the input pipe
            call_dup2(&node);
            close(node->in_p[0]); // close the input pipe
            close(node->out_p[1]); // close the output pipe
            char *line1 = malloc(MAXLINE); // allocate memory for line1
            char *line2 = malloc(MAXLINE); // allocate memory for line2
            strcpy(line1, "/usr/bin/"); // copy the string to line1
            strcpy(line2, "./"); // copy the string to line2


            sigprocmask(SIG_UNBLOCK, &mask, NULL); // unblock the signal


            execv(strcat(line1, argv[0]), argv); // execute the command
            execv(strcat(line2, argv[0]), argv); // execute the command

            log_command_error(cmd); // log the command error
            exit(0); // exit the process


        } else { // parent process

            node->pid = child_pid; // set the pid of the node
            strcpy(node->cmd, cmd); //copy the cmd to the node cmd

            log_start(node->id, child_pid, node->type, cmdLine); // log the start of the process

            close(node->in_p[0]); // Close read end of input pipe
            close(node->out_p[1]); // Close write end of output pipe

            write(node->in_p[1], node->content, strlen(node->content)); // write the content to the input pipe
            close(node->in_p[1]); // Close write end of input pipe

            sigprocmask(SIG_UNBLOCK, &mask, NULL); // unblock the signal

            node->state = LOG_STATE_WORKING; // set the state to working
            int status = 0; // set the status to 0

            sigset_t mask;
            sigemptyset(&mask);
            sigaddset(&mask, SIGCHLD);
            sigprocmask(SIG_BLOCK, &mask, NULL);

            if (node->id == (*queue)->active->id) { // if the node is the active buffer is selected then wait for the process to finish before moving on
                if (waitpid(child_pid, &status, 0) > 0) { // wait for the process to finish
                    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) { // if the process exited and the exit status is 0
                        handle_exit_status(&node); // log the exit status
                        log_cmd_state(node->pid, node->type, node->cmd, LOG_CANCEL); // log the cancel status
                    } else { // if the process did not exit
                        log_cmd_state(node->pid, node->type, node->cmd, LOG_CANCEL); // log the cancel status
                    }
                    node->state = LOG_STATE_READY; // set the state to ready
                    reset_cmd_pid(&node);
                }
            }
            sigprocmask(SIG_UNBLOCK, &mask, NULL); // unblock the signal
        }
    }
}


void handle_sigint(int sig) { // handle the sigint signal
    if(queue->active != NULL){
        if (queue->active->pid > 0 && queue->active->state == LOG_STATE_WORKING) { // if the active buffer is working
            queue->active->state = LOG_STATE_READY; // set the state to ready
            kill(queue->active->pid, SIGINT); // send the signal to the process
        }
    }
    log_ctrl_c(); // Log the SIGINT signal reception
}


void handle_sigtstp(int sig) { // handle the sigtstp signal
    if(queue->active != NULL){
        if (queue->active->pid > 0 && queue->active->state == LOG_STATE_WORKING) { // if the active buffer is working
            queue->active->state = LOG_STATE_PAUSED; // set the state to paused
            kill(queue->active->pid, SIGTSTP); // send the signal to the process
        }
    }
    log_ctrl_z(); // Log the SIGTSTP signal reception
}

void cancel_helper(buffer_queue **queue, int id) { // cancel helper
    process_helper(queue, id, SIGINT, LOG_CANCEL_SIG, LOG_CMD_CANCEL); // call the process helper
}


void pause_helper(buffer_queue **queue, int id) { // pause helper
    process_helper(queue, id, SIGTSTP, LOG_PAUSE, LOG_CMD_PAUSE); // call the process helper
}


void resume_helper(buffer_queue **queue, int id) { // resume helper
    process_helper(queue, id, SIGCONT, LOG_RESUME, LOG_CMD_RESUME); // call the process helper
}


void initializing_sigint() { // initializing the sigint signal
    struct sigaction sa_int = {0}; // creating a sigaction
    sa_int.sa_handler = handle_sigint; // setting the handler
    sigemptyset(&sa_int.sa_mask); // empty the mask
    sigaction(SIGINT, &sa_int, NULL); // set the action
}

void initializing_sigtstp() { // initializing the sigtstp signal
    struct sigaction sa_tstp = {0}; // creating a sigaction
    sa_tstp.sa_handler = handle_sigtstp; // setting the handler
    sigemptyset(&sa_tstp.sa_mask); // empty the mask
    sigaction(SIGTSTP, &sa_tstp, NULL); // set the action
}


void initializing_sigchild() { // initializing the sigchild signal
    struct sigaction sa = {0}; // creating a sigaction
    sa.sa_handler = signal_handler; // setting the handler
    sigemptyset(&sa.sa_mask); // empty the mask
    sigaction(SIGCHLD, &sa, NULL); // set the action
}


void call_pipe(buffer_process **node) { // call pipe
    pipe((*node)->out_p); // create a pipe
    pipe((*node)->in_p); // create a pipe
}


void call_dup2(buffer_process **node) { // call dup2
    dup2((*node)->in_p[0], STDIN_FILENO); // Redirecting stdin to read from the input pipe
    dup2((*node)->out_p[1], STDOUT_FILENO);// Redirecting stdout to write to the output pipe
}

buffer_process *check_for_id(buffer_queue **queue, int id) { // check for the id
    buffer_process *node = NULL; // create a node
    if (id != 0) { // if there is an argument
        node = node_getter((*queue), id); // get the node with the id
    } else { // if there is no argument
        node = (*queue)->active; // set the node as the active node
    }
    return node; // return the node
}

void handle_exit_status(buffer_process **node) { // handle the exit status
    char *output = fd_to_text((*node)->out_p[0]); // convert the output to text
    close((*node)->out_p[0]); // close the output pipe
    strcpy((*node)->content, output); // Store output in active buffer
    free(output); // free the output
}


void handle_stopped_status(buffer_process **node) { // handle the stopped status
    (*node)->state = LOG_STATE_PAUSED; // set the state to paused
    log_cmd_state((*node)->pid, (*node)->type, (*node)->cmd, LOG_PAUSE); // log the pause status
}

void handle_continued_status(buffer_process **node) { // handle the continued status
    (*node)->state = LOG_STATE_WORKING; // set the state to working
    log_cmd_state((*node)->pid, (*node)->type, (*node)->cmd, LOG_RESUME); // log the resume status
}

void reset_cmd_pid(buffer_process **node) { // reset the cmd and pid
    strcpy((*node)->cmd, ""); // reset the cmd
    (*node)->pid = 0; // reset the pid
}

void process_helper(buffer_queue **queue, int id, int signal_type, int log_type, int log_cmd_type) { // process helper
    if ((*queue)->count == 0) { // if the queue is empty
        return; // do nothing
    }


    buffer_process *node = check_for_id(queue, id); // check for the node with the id

    if (node == NULL) { // if the node is null
        log_buf_id_error(node->id); // print out the error message
        return; // do nothing
    }


    if (node->state == LOG_STATE_READY) {   // if the node is ready
        log_cmd_state_conflict(node->id, node->state); // print out the error message
        return; // do nothing
    }

    if (node->id == (*queue)->active->id) { // if the node is the active node
        log_cmd_signal(log_cmd_type, (*queue)->active->id); // log the signal
    } else { // if the node is not the active node
        log_cmd_signal(log_cmd_type, node->id); // log the signal
    }

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    if(log_cmd_type == LOG_CMD_CANCEL){
        if (node->state == LOG_STATE_PAUSED) { // if the node is paused
            sigprocmask(SIG_BLOCK, &mask, NULL); // Block SIGCHLD
            kill(node->pid, SIGCONT); // send the signal to the process
        }
    }

    if (log_cmd_type == LOG_CMD_RESUME){
        sigprocmask(SIG_UNBLOCK, &mask, NULL); // Block SIGCHLD
    }

    kill(node->pid, signal_type);
}



