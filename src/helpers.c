// Your helper functions need to be here.
#include "helpers.h"

int procComparator(const void* proc1, const void* proc2){
    int p1 = ((bgentry_t*)(proc1))->seconds;
    int p2 = ((bgentry_t*)(proc2))->seconds;
    if(p1<p2){
        return -1;
    }
    if(p1>p2){
        return 1;
    }
    return 0;
}
void procPrinter(void* data,  void* fp){
    bgentry_t* temp = (bgentry_t*)data;
    print_bgentry(temp);
}
void procDeleter(void* data){
    bgentry_t* proc = (bgentry_t*)(data);
    job_info* job = proc->job;
    char* line = job->line;
    free_job(job);
	free(proc);
    return;
}

void DestroyLinkedList(list_t** list){
    if(list == NULL){
		return;
	}
	if(*list == NULL){
		return;
	}
	node_t * iter = (*list) -> head;
	node_t * prev = (*list) -> head;
	while(iter!=NULL){
		iter = iter->next;
        (*list)->deleter(prev->data);
		//free(prev->data);
		free(prev);
		prev = iter;
	}
	free(*list);
    return;
}
void DestroyHistoryLinkedList(list_t** list){
    if(list == NULL){
		return;
	}
	if(*list == NULL){
		return;
	}
	node_t * iter = (*list) -> head;
	node_t * prev = (*list) -> head;
	while(iter!=NULL){
		iter = iter->next;
		free(prev->data);
		free(prev);
		prev = iter;
	}
	free(*list);
    return;
}

void historyPrinter(void* data,  void* fp){
    char * d = (char*)data;
    fprintf(fp, "%s\n", d);
}

void historyDeleter(void* data){
    char * d = (char*)data;
    free(d);
}

int getBgIndex(int pid, list_t* list){
	int index = 0;
	node_t* iter = list->head;
	while(iter!=NULL){
		bgentry_t* bgEntry = (bgentry_t*)(iter->data);
		if(bgEntry->pid == pid){
			return index;
		}
		iter = iter->next;
		index++;
	}
	return -1;
}

char * getBgLine(int pid,list_t * list){
	node_t* iter = list->head;

	while(iter != NULL){
		bgentry_t* bgEntry = (bgentry_t*)(iter->data);
		if(bgEntry->pid == pid){
			return bgEntry->job->line;
		}
		iter = iter->next;
	}
	return NULL;
}

void reapChildren(list_t * list, int* reapChildrenFlag){
	pid_t pid;
	sigset_t mask_all, prev_all;
	sigfillset(&mask_all);
	while((pid = waitpid(-1,NULL,WNOHANG)) > 0){
		sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
		char * temp = getBgLine(pid,list);
		printf(BG_TERM, pid, temp);
		int index = getBgIndex(pid, list);
		bgentry_t* bgProc = RemoveByIndex(list, index);
		free_job(bgProc->job);
		free(bgProc);
		*reapChildrenFlag = 0;
		sigprocmask(SIG_SETMASK, &prev_all, NULL);
	}
}

void killAllChildren(list_t* list){
	node_t* head = list->head;
	sigset_t mask_all, prev_all;
	sigfillset(&mask_all);
	sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
	while (head!=NULL){
		bgentry_t* bgProc = RemoveFromHead(list);
		pid_t pid = bgProc->pid;
		char* temp = bgProc->job->line;
		printf(BG_TERM, pid, temp);
		kill(pid, SIGKILL);
		free_job(bgProc->job);
		free(bgProc);
		head = list->head;
	}
	
	sigprocmask(SIG_SETMASK, &prev_all, NULL);
}