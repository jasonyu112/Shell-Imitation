#include "icssh.h"
#include "helpers.h"
#include "linkedlist.h"
#include <readline/readline.h>
#include <signal.h>
#include <errno.h>

 int reapChildrenFlag = 0;
 list_t * list;

// should also set exitstatus when reaped
void sigchild_handler(int sig){
	int olderrno = errno;
	pid_t pid;
	sigset_t mask_all, prev_all;
	sigfillset(&mask_all);
	sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
	reapChildrenFlag = 1;
	sigprocmask(SIG_SETMASK, &prev_all, NULL);
	errno = olderrno;
}

void siguser_handler(int sig){
	fprintf(stderr, "Num of Background processes: %d\n", list->length);
}


int main(int argc, char* argv[]) {
    int max_bgprocs = -1;
	int exec_result;
	int exit_status;
	int firstChildFlag=0;
	pid_t pid;
	pid_t wait_result;
	char* line;
	list = CreateList(&procComparator, &procPrinter, &procDeleter);
	list_t* historyList = CreateList(NULL, &historyPrinter, &historyDeleter);
	sigset_t mask_all, mask_child, prev_one;
	sigfillset(&mask_all);
	sigemptyset(&mask_child);
	sigaddset(&mask_child, SIGCHLD);
	signal(SIGCHLD, sigchild_handler);
	signal(SIGUSR2, siguser_handler);

#ifdef GS
    rl_outstream = fopen("/dev/null", "w");
#endif

    // check command line arg
    if(argc > 1) {
        int check = atoi(argv[1]);
        if(check != 0)
            max_bgprocs = check;
        else {
            printf("Invalid command line argument value\n");
            exit(EXIT_FAILURE);
        }
    }

	// Setup segmentation fault handler
	if (signal(SIGSEGV, sigsegv_handler) == SIG_ERR) {
		perror("Failed to set signal handler");
		exit(EXIT_FAILURE);
	}

    	// print the prompt & wait for the user to enter commands string
	while ((line = readline(SHELL_PROMPT)) != NULL) {
        // MAGIC HAPPENS! Command string is parsed into a job struct
        // Will print out error message if command string is invalid
		char * temp = NULL;
		
		if(*(line) == '!'){				//if command is !
			temp = line;
			char *letter = (line)+1;
			if (isdigit(*letter)){
				int t = atoi(letter);
				if(t<=5 && t>=1){
					int start = 1;
					node_t* head = historyList->head;
					while(start!=t){
						head = head->next;
						start++;
					}
					line = (char*)head->data;
					printf("%s\n", line);
				}
			}else{
				if(*letter == ' ' || *letter == '\0' || *letter == '\n'){
					line = (char*)(historyList->head->data);
					printf("%s\n", line);
				}
			}
		}
		
		
		if(strcmp(line, "history")!=0 && line[0]!= '!' && strcmp(line, "")!=0){					// if cmd is not history, put into historyList
			char* tempHistory = malloc(strlen(line)+1);
			strcpy(tempHistory, line);
			InsertAtHead(historyList, tempHistory);
		}
		

		job_info* job = validate_input(line);
		if (temp!= NULL){		// if ! executed line is replaced with original line
			line = temp;
		}
        if (job == NULL) { // Command was empty string or invalid
			free(line);
			continue;
		}

        //Prints out the job linked list struture for debugging
        #ifdef DEBUG   // If DEBUG flag removed in makefile, this will not longer print
     		debug_print_job(job);
        #endif
		// example built-in: exit
		if (strcmp(job->procs->cmd, "exit") == 0) {
			// Terminating the shell
			free(line);
			free_job(job);   // calling validate_input with NULL will free the memory it has allocated
			validate_input(NULL);
			killAllChildren(list);
			DestroyLinkedList(&list);
			DestroyHistoryLinkedList(&historyList);
            return 0;
		}
		else if(strcmp(job->procs->cmd, "cd") == 0){
			char s[100];
			if(strcmp(job->line, "cd") == 0){
				chdir(getenv("HOME"));
				printf("%s\n", getcwd(s,100));
			}
			else{
				if(chdir((job->procs->argv)[1])!=0){
					fprintf(stderr,DIR_ERR);
				}
				else{
					printf("%s\n", getcwd(s,100));
				}
			}
			free(line);
			free_job(job);
		}
		else if(strcmp(job->procs->cmd, "estatus") == 0){
			if(firstChildFlag == 0){
				printf("-100\n");
			}
			else{
				printf("%d\n", WEXITSTATUS(exit_status));
			}
			free(line);
			free_job(job);
		}
		
		else if(strcmp(job->procs->cmd, "bglist") == 0){
			node_t* head = (node_t*)(list->head);
			while(head != NULL){
				list->printer(head->data,stdout);
				head = head->next;
			}
			free(line);
			free_job(job);
		}
		
		else if(strcmp(job->procs->cmd, "history") == 0){
			node_t* head = (node_t*)(historyList->head);
			int tempC = 1;
			while(head != NULL){
				printf("%d: ", tempC);
				historyList->printer(head->data, stdout);
				head = head->next;
				tempC++;
			}
			free(line);
			free_job(job);
		}
		
		// example of good error handling!
        // create the child proccess
		else{
			if(job->nproc == 1){
				sigprocmask(SIG_BLOCK, &mask_child, &prev_one);
				//block sigchild here before forking							********************
				if ((pid = fork()) < 0) {
					perror("fork error");
					exit(EXIT_FAILURE);
				}
				if (pid == 0) {  //If zero, then it's the child process
					//get the first command in the job list to execute
					//unblock sigchild here in child			********************
					sigprocmask(SIG_SETMASK, &prev_one, NULL);
					proc_info* proc = job->procs;
					int out_fd = 1;
					int in_fd = 0;
					int err_fd = 2;
					if(job->out_file!=NULL){
						out_fd = open(job->out_file, O_CREAT|O_WRONLY);
					}
					if(job->in_file!=NULL){
						in_fd = open(job->in_file, O_RDONLY);
					}
					if(proc->err_file!=NULL){
						err_fd = open(proc->err_file, O_CREAT|O_WRONLY);
					}
					if(in_fd ==-1){
						fprintf(stderr, RD_ERR);
						exit(0);
					}
					dup2(out_fd, 1);
					dup2(in_fd, 0);
					dup2(err_fd, 2);
					exec_result = execvp(proc->cmd, proc->argv);
					if (exec_result < 0) {  //Error checking
						printf(EXEC_ERR, proc->cmd);
						
						// Cleaning up to make Valgrind happy 
						// (not necessary because child will exit. Resources will be reaped by parent)
						free_job(job);  
						free(line);
						validate_input(NULL);  // calling validate_input with NULL will free the memory it has allocated

						exit(EXIT_FAILURE);
					}
				} 
				else if(job->bg ==0 && pid > 0){
					// As the parent, wait for the foreground job to finish
					sigprocmask(SIG_BLOCK, &mask_all, NULL);
					wait_result = waitpid(pid, &exit_status, 0);
					firstChildFlag = 1;
					if (wait_result < 0) {
						printf(WAIT_ERR);
						exit(EXIT_FAILURE);
					}
					free_job(job);  // if a foreground job, we no longer need the data
					free(line);
					sigprocmask(SIG_SETMASK, &prev_one, NULL);
				}
				else if(job->bg == 1 && pid > 0 ){
					sigprocmask(SIG_BLOCK, &mask_all, NULL); //block all signals
					bgentry_t* bgProc = malloc(sizeof(bgentry_t));
					bgProc->job = job;
					bgProc->pid = pid;
					bgProc->seconds = 0;
					bgProc->seconds = time(&(bgProc->seconds));
					InsertInOrder(list,bgProc);
					free(line);
					sigprocmask(SIG_SETMASK, &prev_one, NULL); //unblock all signals
				}
			}
			else if (job->nproc == 2){
				int p[2] = {0,0};
				pipe(p);
				
				sigprocmask(SIG_BLOCK, &mask_child, &prev_one);
				if ((pid = fork()) < 0) {
					perror("fork error");
					exit(EXIT_FAILURE);
				}
				if (pid == 0) {  //If zero, then it's the child process
					//get the first command in the job list to execute
					//unblock sigchild here in child			********************
					sigprocmask(SIG_SETMASK, &prev_one, NULL);
					proc_info* proc = job->procs;
					int out_fd = 1;
					int in_fd = 0;
					int err_fd = 2;
					if(job->out_file!=NULL){
						out_fd = open(job->out_file, O_CREAT|O_WRONLY);
					}
					if(job->in_file!=NULL){
						in_fd = open(job->in_file, O_RDONLY);
					}
					if(proc->err_file!=NULL){
						err_fd = open(proc->err_file, O_CREAT|O_WRONLY);
					}
					if(in_fd ==-1){
						fprintf(stderr, RD_ERR);
						exit(0);
					}
					close(p[0]);
					dup2(p[1], STDOUT_FILENO);
					dup2(in_fd, 0);
					dup2(err_fd, 2);
					close(p[1]);

					exec_result = execvp(proc->cmd, proc->argv);
					if (exec_result < 0) {  //Error checking
						printf(EXEC_ERR, proc->cmd);
						
						// Cleaning up to make Valgrind happy 
						// (not necessary because child will exit. Resources will be reaped by parent)
						free_job(job);  
						free(line);
						validate_input(NULL);  // calling validate_input with NULL will free the memory it has allocated

						exit(EXIT_FAILURE);
					}
				}
				//
				if ((pid = fork()) < 0) {
					perror("fork error");
					exit(EXIT_FAILURE);
				}
				if (pid == 0) {  //If zero, then it's the child process
					//get the first command in the job list to execute
					//unblock sigchild here in child			********************
					sigprocmask(SIG_SETMASK, &prev_one, NULL);
					proc_info* proc = job->procs->next_proc;
					int out_fd = 1;
					int in_fd = 0;
					int err_fd = 2;
					if(job->out_file!=NULL){
						out_fd = open(job->out_file, O_CREAT|O_WRONLY);
					}
					if(job->in_file!=NULL){
						in_fd = open(job->in_file, O_RDONLY);
					}
					if(proc->err_file!=NULL){
						err_fd = open(proc->err_file, O_CREAT|O_WRONLY);
					}
					if(in_fd ==-1){
						fprintf(stderr, RD_ERR);
						exit(0);
					}
					close(p[1]);
					dup2(out_fd, 1);
					dup2(p[0], STDIN_FILENO);
					dup2(err_fd, 2);
					close(p[0]);

					exec_result = execvp(proc->cmd, proc->argv);
					if (exec_result < 0) {  //Error checking
						printf(EXEC_ERR, proc->cmd);
						
						// Cleaning up to make Valgrind happy 
						// (not necessary because child will exit. Resources will be reaped by parent)
						free_job(job);  
						free(line);
						validate_input(NULL);  // calling validate_input with NULL will free the memory it has allocated

						exit(EXIT_FAILURE);
					}
				}
				/////////////
				if(job->bg ==0 && pid > 0){
					// As the parent, wait for the foreground job to finish
					sigprocmask(SIG_BLOCK, &mask_all, NULL);
					close(p[0]);
					close(p[1]);
					wait_result = waitpid(-1, &exit_status, 0);
					wait_result = waitpid(-1, &exit_status, 0);
					firstChildFlag = 1;
					if (wait_result < 0) {
						printf(WAIT_ERR);
						exit(EXIT_FAILURE);
					}
					free_job(job);  // if a foreground job, we no longer need the data
					free(line);
					sigprocmask(SIG_SETMASK, &prev_one, NULL);
				}
				else if(job->bg == 1 && pid >0){
					sigprocmask(SIG_BLOCK, &mask_all, NULL); //block all signals
					close(p[0]);
					close(p[1]);
					bgentry_t* bgProc = malloc(sizeof(bgentry_t));
					bgProc->job = job;
					bgProc->pid = pid;
					bgProc->seconds = 0;
					bgProc->seconds = time(&(bgProc->seconds));
					InsertInOrder(list,bgProc);
					free_job(job);
					free(line);
					sigprocmask(SIG_SETMASK, &prev_one, NULL); //unblock all signals
				}
			}




			else if (job->nproc == 3){
				int p[2] = {0,0};
				pipe(p);
				
				sigprocmask(SIG_BLOCK, &mask_child, &prev_one);
				if ((pid = fork()) < 0) {
					perror("fork error");
					exit(EXIT_FAILURE);
				}
				if (pid == 0) {  //If zero, then it's the child process
					//get the first command in the job list to execute
					//unblock sigchild here in child			********************
					sigprocmask(SIG_SETMASK, &prev_one, NULL);
					proc_info* proc = job->procs;
					int out_fd = 1;
					int in_fd = 0;
					int err_fd = 2;
					if(job->out_file!=NULL){
						out_fd = open(job->out_file, O_CREAT|O_WRONLY);
					}
					if(job->in_file!=NULL){
						in_fd = open(job->in_file, O_RDONLY);
					}
					if(proc->err_file!=NULL){
						err_fd = open(proc->err_file, O_CREAT|O_WRONLY);
					}
					if(in_fd ==-1){
						fprintf(stderr, RD_ERR);
						exit(0);
					}
					close(p[0]);
					dup2(p[1], STDOUT_FILENO);
					dup2(in_fd, 0);
					dup2(err_fd, 2);
					close(p[1]);

					exec_result = execvp(proc->cmd, proc->argv);
					if (exec_result < 0) {  //Error checking
						printf(EXEC_ERR, proc->cmd);
						
						// Cleaning up to make Valgrind happy 
						// (not necessary because child will exit. Resources will be reaped by parent)
						free_job(job);  
						free(line);
						validate_input(NULL);  // calling validate_input with NULL will free the memory it has allocated

						exit(EXIT_FAILURE);
					}
				}
				//
				int fd[2] = {0,0};
				pipe(fd);
				if ((pid = fork()) < 0) {
					perror("fork error");
					exit(EXIT_FAILURE);
				}
				if (pid == 0) {  //If zero, then it's the child process
					//get the first command in the job list to execute
					//unblock sigchild here in child			********************
					sigprocmask(SIG_SETMASK, &prev_one, NULL);
					proc_info* proc = job->procs->next_proc;
					int out_fd = 1;
					int in_fd = 0;
					int err_fd = 2;
					if(job->out_file!=NULL){
						out_fd = open(job->out_file, O_CREAT|O_WRONLY);
					}
					if(job->in_file!=NULL){
						in_fd = open(job->in_file, O_RDONLY);
					}
					if(proc->err_file!=NULL){
						err_fd = open(proc->err_file, O_CREAT|O_WRONLY);
					}
					if(in_fd ==-1){
						fprintf(stderr, RD_ERR);
						exit(0);
					}
					close(p[1]);
					close(fd[0]);
					dup2(fd[1], STDOUT_FILENO);
					dup2(p[0], STDIN_FILENO);
					dup2(err_fd, 2);
					close(p[0]);
					close(fd[1]);

					exec_result = execvp(proc->cmd, proc->argv);
					if (exec_result < 0) {  //Error checking
						printf(EXEC_ERR, proc->cmd);
						
						// Cleaning up to make Valgrind happy 
						// (not necessary because child will exit. Resources will be reaped by parent)
						free_job(job);  
						free(line);
						validate_input(NULL);  // calling validate_input with NULL will free the memory it has allocated

						exit(EXIT_FAILURE);
					}
				}
				close(p[0]);
				close(p[1]);
				if ((pid = fork()) < 0) {
					perror("fork error");
					exit(EXIT_FAILURE);
				}
				if (pid == 0) {  //If zero, then it's the child process
					//get the first command in the job list to execute
					//unblock sigchild here in child			********************
					sigprocmask(SIG_SETMASK, &prev_one, NULL);
					proc_info* proc = job->procs->next_proc->next_proc;
					int out_fd = 1;
					int in_fd = 0;
					int err_fd = 2;
					if(job->out_file!=NULL){
						out_fd = open(job->out_file, O_CREAT|O_WRONLY);
					}
					if(job->in_file!=NULL){
						in_fd = open(job->in_file, O_RDONLY);
					}
					if(proc->err_file!=NULL){
						err_fd = open(proc->err_file, O_CREAT|O_WRONLY);
					}
					if(in_fd ==-1){
						fprintf(stderr, RD_ERR);
						exit(0);
					}
					close(fd[1]);
					dup2(out_fd, STDOUT_FILENO);
					dup2(fd[0], STDIN_FILENO);
					dup2(err_fd, 2);
					close(fd[0]);

					exec_result = execvp(proc->cmd, proc->argv);
					if (exec_result < 0) {  //Error checking
						printf(EXEC_ERR, proc->cmd);
						
						// Cleaning up to make Valgrind happy 
						// (not necessary because child will exit. Resources will be reaped by parent)
						free_job(job);  
						free(line);
						validate_input(NULL);  // calling validate_input with NULL will free the memory it has allocated

						exit(EXIT_FAILURE);
					}
				}
				if(job->bg ==0 && pid > 0){
					// As the parent, wait for the foreground job to finish
					sigprocmask(SIG_BLOCK, &mask_all, NULL);
					close(fd[0]);
					close(fd[1]);
					wait_result = waitpid(-1, &exit_status, 0);
					wait_result = waitpid(-1, &exit_status, 0);
					wait_result = waitpid(-1, &exit_status, 0);
					firstChildFlag = 1;
					if (wait_result < 0) {
						printf(WAIT_ERR);
						exit(EXIT_FAILURE);
					}
					free_job(job);  // if a foreground job, we no longer need the data
					free(line);
					sigprocmask(SIG_SETMASK, &prev_one, NULL);
				}
				else if(job->bg == 1 && pid >0){
					sigprocmask(SIG_BLOCK, &mask_all, NULL); //block all signals
					close(fd[0]);
					close(fd[1]);
					bgentry_t* bgProc = malloc(sizeof(bgentry_t));
					bgProc->job = job;
					bgProc->pid = pid;
					bgProc->seconds = 0;
					bgProc->seconds = time(&(bgProc->seconds));
					InsertInOrder(list,bgProc);
					free_job(job);
					free(line);
					sigprocmask(SIG_SETMASK, &prev_one, NULL); //unblock all signals
				}
			}
		}
		
		if((historyList->length)>5){							//if historyList too long, pop old and insert new
			char * d = RemoveFromTail(historyList);
			free(d);
		}
		
		if(reapChildrenFlag){
			reapChildren(list, &reapChildrenFlag);
		}
	}

    	// calling validate_input with NULL will free the memory it has allocated
    validate_input(NULL);
	killAllChildren(list);
	DestroyLinkedList(&list);
	DestroyHistoryLinkedList(&historyList);

#ifndef GS
	fclose(rl_outstream);
#endif
	return 0;
}