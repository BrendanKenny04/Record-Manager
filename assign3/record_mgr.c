#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "record_mgr.h"
#include "storage_mgr.c"
#include "buffer_mgr.c"

head* tableList;
void main(){ //Testing
    initRecordManager(NULL);
    Schema *s = malloc(sizeof(Schema));
    char *names[] = {"Name","ID","Time"};
    DataType types[] = {DT_STRING,DT_INT,DT_FLOAT};
    int keys[] = {0};
    int lens[] = {12,0,0};
    s->attrNames = names;
    s->dataTypes = types;
    s->keyAttrs = keys;
    s->keySize = 1;
    s->numAttr = 3;
    s->typeLength = lens;
    createTable("Joes",s);
    RM_TableData *rel = malloc(sizeof(RM_TableData));
    openTable(rel,"Joes");
    //Tests
    
    Record *r = malloc(sizeof(Record));
    createRecord(&r,s);
    Value *v1,*v2,*v3,*v4;
    MAKE_STRING_VALUE(v1,"daniel");
    MAKE_VALUE(v2,DT_INT,20);
    MAKE_VALUE(v3,DT_FLOAT,14.5);
    setAttr(r,s,0,v1);
    setAttr(r,s,1,v2);
    setAttr(r,s,2,v3);
    for(int i=0;i<5;i++){
        insertRecord(rel,r);
    }
    
    getAttr(r,s,0,&v4);
    printf("%s\n",v4->v.stringV);
    closeTable(rel);
    deleteTable("Joes");
}
// table and manager
RC initRecordManager (void *mgmtData){
    initStorageManager();
    tableList = malloc(sizeof(head*));
    link *td = malloc(sizeof(link*));
    td->next = NULL;
    td->data = mgmtData;
    tableList->first = td;
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
    RID lt = {0,0};
    bool gaps[256]; //arbitrary max amount of pages per table set to 256
    for(int i=0;i<256;i++){
        gaps[i]=false;
    }
    meta->latest=lt;
    meta->gaps=gaps;
    meta->maxRecords = PAGE_SIZE/getRecordSize(schema);
    newTable->mgmtData=meta;
    
    //Create the page file and a buffer manager for the table, and put in the table's metadata
    createPageFile(name);
    SM_FileHandle* tableFile = malloc(sizeof(SM_FileHandle));
    openPageFile(name,tableFile);
    BM_BufferPool* bm = malloc(sizeof(BM_BufferPool));
    BM_PageHandle* page = MAKE_PAGE_HANDLE();
    initBufferPool(bm,name,5,RS_LRU,NULL);
    meta->bm = bm;


    //Finally, put the table on the global table list
    link *tableEntry = malloc(sizeof(link*));
    tableEntry->next = NULL;
    tableEntry->data = newTable;
    append(tableList,tableEntry);
    return RC_OK;
}
RC openTable (RM_TableData *relo, char *name){
    link *t = tableList->first->next;
    for (t;t;t=t->next){
        RM_TableData *rel = t->data;
        if (strcmp(rel->name,name)==0){
            relo->name=rel->name;
            relo->mgmtData=rel->mgmtData;
            relo->schema=rel->schema;
            return RC_OK;
        }
    }
    return RC_FILE_NOT_FOUND;
}
RC closeTable (RM_TableData *rel){
    free(rel);
}
RC deleteTable (char *name){
    for (link *t = tableList->first->next;t;t=t->next){
        RM_TableData *rel = t->data;
        if (strcmp(rel->name,name)==0){
            destroyPageFile(rel->name);
            shutdownBufferPool(rel->mgmtData->bm);
            free(rel->mgmtData);
            free(rel->schema);
            return delete(tableList,t);
        }
    }
    return RC_FILE_NOT_FOUND;
}
int getNumTuples (RM_TableData *rel){ //unfinished!
    tData *td = rel->mgmtData;

    //calculate the maximum amount based on the td->latest available RID slot
    int pnum = td->latest.page-1; //must subtract one to account for the metadata page
    int snum = td->latest.slot-1;
    int maxcount = pnum * td->maxRecords+snum;

    //now that we have a theoretical max, find the number of deleted tuples, and subtract that off of the max
    int deadcount=0;
    /* something */
    return maxcount-deadcount;
}

// handling records in a table
RC insertRecord (RM_TableData *rel, Record *record){
    record->deleted=false;
    tData *td = rel->mgmtData;
    for(int i=0;i<=td->latest.page;i++){            //Before the end of the table
        if(td->gaps[i]){                        //If there is a page with an open slot
            /*look for a spot on that page, insert*/
            return RC_OK;
        }
    }                                           //No empty spaces before last record
    if(td->latest.slot>=(td->maxRecords-1)){         //Page would overflow on next insert
        record->id=td->latest;                        //Set the record's slot to the next slot
        td->latest.slot=0;                            //Update the next slot
        td->latest.page++;                            //And move to the next page
    }else{                                       //Normal insert
        record->id=td->latest;                        //Set the record's slot to the next slot
        td->latest.slot++;                            //Update the next slot
    }
    BM_PageHandle *ph = MAKE_PAGE_HANDLE();                                             //Make a page
    pinPage(td->bm,ph,record->id.page);                                                 //Pin the page we are inserting into
    int size = getRecordSize(rel->schema);                                              //Get the size of the record
    memcpy((record->data)+size-sizeof(RID)-sizeof(bool),&record->id.page,sizeof(int));  //Fill out the record with required metadata 
    memcpy((record->data)+size-sizeof(int)-sizeof(bool),&record->id.slot,sizeof(int));  //(RID)
    memcpy((record->data)+size-sizeof(RID),&record->deleted,sizeof(bool));              //(Deletion status)
    memcpy((ph->data)+(size*record->id.slot),record->data,size);                        //Copy the record into the table
    markDirty(td->bm,ph);                                                               //Write to 'disk'
    unpinPage(td->bm,ph);
    return RC_OK;
}
RC deleteRecord (RM_TableData *rel, RID id){}
RC updateRecord (RM_TableData *rel, Record *record){}
RC getRecord (RM_TableData *rel, RID id, Record *record){
    int size = getRecordSize(rel->schema);
    BM_PageHandle *ph = MAKE_PAGE_HANDLE();
    pinPage(rel->mgmtData->bm,ph,id.page);
    int offset = id.slot*size;
    
}

// scans
RC startScan (RM_TableData *rel, RM_ScanHandle *scan, Expr *cond){}
RC next (RM_ScanHandle *scan, Record *record){}
RC closeScan (RM_ScanHandle *scan){}

// dealing with schemas
int getRecordSize (Schema *schema){
    size_t recordSize = 0;
    for (int i=0;i<schema->numAttr;i++){
        switch (schema->dataTypes[i]){
            case DT_INT: //Integer
                recordSize+=sizeof(int);
                break;
            case DT_STRING: //String
                recordSize+=schema->typeLength[i];
                break;
            case DT_FLOAT: //Float
                recordSize+=sizeof(float);
                break;
            case DT_BOOL: //Boolean
                recordSize+=sizeof(bool);
                break;
        }
    }
    return recordSize+sizeof(RID)+sizeof(bool);
}
Schema *createSchema (int numAttr, char **attrNames, DataType *dataTypes, int *typeLength, int keySize, int *keys){
    Schema *s = malloc(sizeof(Schema)); //Simply allocate and then fill out the schema
    s->attrNames=attrNames;
    s->dataTypes=dataTypes;
    s->keyAttrs=keys;
    s->keySize=keySize;
    s->numAttr=numAttr;
    s->typeLength=typeLength;
    return s;
}
RC freeSchema (Schema *schema){
    //everything is statically allocated, only need to free the schema itself
    free(schema);
}

// dealing with records and attribute values
RC createRecord (Record **record, Schema *schema){
    Record *r = malloc(sizeof(Record));
    *record = r;
    r->data = malloc(getRecordSize(schema));
}
RC freeRecord (Record *record){
    free(record);//None of the record's metadata is dynamically allocated, so this is quite simple.
}
RC getAttr (Record *record, Schema *schema, int attrNum, Value **value){
    //Find the offset to the desired attribute
    size_t offset = 0;
    for (int i=0;i<attrNum;i++){
        switch (schema->dataTypes[i]){
            case DT_INT: //Integer
                offset+=sizeof(int);
                break;
            case DT_STRING: //String
                offset+=schema->typeLength[i];
                break;
            case DT_FLOAT: //Float
                offset+=sizeof(float);
                break;
            case DT_BOOL: //Boolean
                offset+=sizeof(bool);
                break;
        }
    }
    Value *val;
    switch (schema->dataTypes[attrNum]){
        case DT_INT:
            MAKE_VALUE(val,DT_INT,*(int*)((record->data)+offset));
            break;
        case DT_STRING:
            MAKE_STRING_VALUE(val,(record->data)+offset);
            break;
        case DT_FLOAT:
            MAKE_VALUE(val,DT_FLOAT,*(float*)((record->data)+offset));
            break;
        case DT_BOOL:
            MAKE_VALUE(val,DT_BOOL,*(bool*)((record->data)+offset));
            break;
    }
    *value = val;
    return RC_OK;
}
RC setAttr (Record *record, Schema *schema, int attrNum, Value *value){
    DataType dt = schema->dataTypes[attrNum];
    if (dt!=value->dt)return RC_RM_COMPARE_VALUE_OF_DIFFERENT_DATATYPE;
    
    size_t offset = 0;
    for (int i=0;i<attrNum;i++){
        switch (schema->dataTypes[i]){
            case DT_INT: //Integer
                offset+=sizeof(int);
                break;
            case DT_STRING: //String
                offset+=schema->typeLength[i];
                break;
            case DT_FLOAT: //Float
                offset+=sizeof(float);
                break;
            case DT_BOOL: //Boolean
                offset+=sizeof(bool);
                break;
        }
    }
    //get the value and convert it into binary, then put it in the right slot
    switch (value->dt){
        case DT_INT:
            memcpy((record->data)+offset,&(value->v.intV),sizeof(int));
            break;
        case DT_STRING:
            memcpy((record->data)+offset,(value->v.stringV),schema->typeLength[attrNum]);
            break;
        case DT_FLOAT:
            memcpy((record->data)+offset,&(value->v.floatV),sizeof(float));
            break;
        case DT_BOOL:
            memcpy((record->data)+offset,&(value->v.boolV),sizeof(bool));
            break;
    } return RC_OK;
}