#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "record_mgr.h"
#include "storage_mgr.c"
#include "buffer_mgr.c"

head* tableList;
void main(){ //Testing
    initStorageManager();
    initRecordManager(NULL);
    Schema *s = malloc(sizeof(Schema));
    char *names[] = {"Name","ID","Time"};
    DataType types[] = {DT_STRING,DT_INT,DT_STRING};
    int keys[] = {0};
    int lens[] = {12,23};
    s->attrNames = names;
    s->dataTypes = types;
    s->keyAttrs = keys;
    s->keySize = 1;
    s->numAttr = 3;
    s->typeLength = lens;
    
}
// table and manager
RC initRecordManager (void *mgmtData){
    tableList = malloc(sizeof(head*));
    link *md = malloc(sizeof(link*));
    md->next = NULL;
    md->data = mgmtData;
    tableList->first = md;
    tableList->length = 0;
}
RC shutdownRecordManager (){
    free(tableList->first);
    free(tableList);
}
RC createTable (char *name, Schema *schema){
    //Initialize the table's data structures
    RM_TableData *newTable = malloc(sizeof(RM_TableData));

    newTable->name=name;
    newTable->schema=schema;
    tData *meta = malloc(sizeof(tData));
    RID lt = {1,0};
    bool *fs = malloc(sizeof(bool)*255);
    meta->latest=lt;
    meta->freeSpaces=fs;
    newTable->mgmtData=meta;
    
    //Parse the schema to calculate the length of each record
    size_t recordSize = 0;
    int j=0;
    for (int i=0;i<schema->numAttr;i++){
        switch (schema->dataTypes[i]){
            case DT_INT: //Integer
                recordSize+=sizeof(int);
                break;
            case DT_STRING: //String
                recordSize+=schema->typeLength[j++];
                break;
            case DT_FLOAT: //Float
                recordSize+=sizeof(float);
                break;
            case DT_BOOL: //Boolean
                recordSize+=sizeof(bool);
                break;
        }
    }
    meta->size=recordSize+sizeof(RID);

    //Create the page file and a buffer manager for the table, and put in the table's metadata
    createPageFile(name);
    SM_FileHandle* tableFile = malloc(sizeof(SM_FileHandle));
    openPageFile(name,tableFile);
    BM_BufferPool* bm = malloc(sizeof(BM_BufferPool));
    BM_PageHandle* page = MAKE_PAGE_HANDLE();
    initBufferPool(bm,name,3,RS_LRU,NULL);
    meta->bm = bm;
    pinPage(bm,page,0);
    markDirty(bm,page);
    sprintf(page->data,"Schema: %p\nOther Metadata: %p",(void*)schema,(void*)meta);
    forceFlushPool(bm);

    //Finally, put the table on the global table list
    link *tableEntry = malloc(sizeof(link*));
    tableEntry->next = NULL;
    tableEntry->data = newTable;
    append(tableList,tableEntry);
    return RC_OK;
}
RC openTable (RM_TableData *rel, char *name){}
RC closeTable (RM_TableData *rel){}
RC deleteTable (char *name){}
int getNumTuples (RM_TableData *rel){}

// handling records in a table
RC insertRecord (RM_TableData *rel, Record *record){}
RC deleteRecord (RM_TableData *rel, RID id){}
RC updateRecord (RM_TableData *rel, Record *record){}
RC getRecord (RM_TableData *rel, RID id, Record *record){}

// scans
RC startScan (RM_TableData *rel, RM_ScanHandle *scan, Expr *cond){}
RC next (RM_ScanHandle *scan, Record *record){}
RC closeScan (RM_ScanHandle *scan){}

// dealing with schemas
int getRecordSize (Schema *schema){}
Schema *createSchema (int numAttr, char **attrNames, DataType *dataTypes, int *typeLength, int keySize, int *keys){}
RC freeSchema (Schema *schema){}

// dealing with records and attribute values
RC createRecord (Record **record, Schema *schema){}
RC freeRecord (Record *record){}
RC getAttr (Record *record, Schema *schema, int attrNum, Value **value){}
RC setAttr (Record *record, Schema *schema, int attrNum, Value *value){}