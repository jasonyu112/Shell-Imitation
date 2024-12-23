// A header file for helpers.c
// Declare any additional functions in this file
#include "linkedlist.h"
#include "icssh.h"

int procComparator(const void* proc1, const void* proc2);
void procPrinter(void* data,  void* fp);
void procDeleter(void* data);
void DestroyLinkedList(list_t** list);
void historyPrinter(void* data,  void* fp);
void historyDeleter(void* data);
void DestroyHistoryLinkedList(list_t** list);
int getBgIndex(int pid, list_t* list);
char * getBgLine(int pid,list_t * list);
void reapChildren(list_t * list, int * reapChildrenFlag);
void killAllChildren(list_t* list);