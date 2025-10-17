#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "record_mgr.h"
#include "storage_mgr.c"
#include "buffer_mgr.c"
#include "expr.c"
#include "dberror.c"

head* tableList;

void main(){ //Testing
    initRecordManager(NULL);
    char *names[] = {"Name","ID","Time"};
    DataType types[] = {DT_STRING,DT_INT,DT_FLOAT};
    int keys[] = {0};
    int lens[] = {12,0,0};
    Schema *s = createSchema(3,names,types,lens,1,keys);
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
    freeVal(v1);
    freeVal(v2);
    freeVal(v3);
    for(int i=0;i<5;i++){
        insertRecord(rel,r);
    }

    Record *r2 = malloc(sizeof(Record));
    createRecord(&r2,s);
    getRecord(rel,r->id,r2);

    for(int i=0;i<5;i++){
        insertRecord(rel,r);
    }
    MAKE_STRING_VALUE(v1,"josephus");
    setAttr(r,s,0,v1);
    r->id.slot--;
    updateRecord(rel,r);

    getAttr(r,s,0,&v4);
    printf("Name: %s\n",v4->v.stringV);

    RID deletthis = {0,6};
    deleteRecord(rel,deletthis);

    insertRecord(rel,r);
    closeTable(rel);
    deleteTable("Joes");
    shutdownRecordManager();
/**/}

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
    tData *td= malloc(sizeof(tData));
    RID lt = {0,0};
    int *gaps = malloc(sizeof(int)*256); //arbitrary max amount of pages per table set to 256
    for(int i=0;i<256;i++){
        gaps[i]=0;
    }
    td->latest=lt;
    td->gaps=gaps;
    td->maxRecords = PAGE_SIZE/(sizeof(RID)+sizeof(bool)+getRecordSize(schema));
    newTable->mgmtData=td;
    
    //Create the page file and a buffer manager for the table, and put in the table's metadata
    createPageFile(name);
    SM_FileHandle* tableFile = malloc(sizeof(SM_FileHandle));
    openPageFile(name,tableFile);
    BM_BufferPool* bm = malloc(sizeof(BM_BufferPool));
    BM_PageHandle* page = MAKE_PAGE_HANDLE();
    initBufferPool(bm,name,5,RS_LRU,NULL);
    td->bm = bm;


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
            freeSchema(rel->schema);
            return delete(tableList,t);
        }
    }
    return RC_FILE_NOT_FOUND;
}
int getNumTuples (RM_TableData *rel){
    tData *td = rel->mgmtData;

    //calculate the maximum amount based on the td->latest available RID slot
    int pnum = td->latest.page;
    int snum = td->latest.slot-1; //must subtract one because the latest slot is not yet filled
    int maxcount = pnum * td->maxRecords+snum;

    //now that we have a theoretical max, find the number of deleted tuples, and subtract that off of the max
    int deadcount=0;
    for (int i=0;i<256;i++){
        deadcount+=td->gaps[i];
    }
    return maxcount-deadcount;
}

// handling records in a table
RC insertRecord (RM_TableData *rel, Record *record){ //(deletion) unfinished!
    record->deleted=FALSE;
    tData *td = rel->mgmtData;
    for(int i=0;i<=td->latest.page;i++){                                            //Before the end of the table
        if(td->gaps[i]){                                                             //If there is a page with an open slot
            for(int j=0;j<td->maxRecords;j++){                                         //Look through that page for an opening
                int size = getRecordSize(rel->schema);                                 //This block appears in many functions.
                int fullsize = size+sizeof(RID)+sizeof(bool);                          //It gets the data size and full size of the record,
                BM_PageHandle *ph = MAKE_PAGE_HANDLE();                                //accesses the buffer page with the relevant record on it,
                pinPage(td->bm,ph,i);                                                  //and calculates the offset on that page for the relevant record.
                int offset = j*fullsize;                                               //This block can vary depending on what defines the relevant record
                bool deleted;
                memcpy(&deleted,(ph->data)+offset+fullsize-sizeof(bool),sizeof(bool)); //Get the deleted flag
                if (deleted){                                                          //Check if the tuple has been deleted (is open for replacement)
                    record->id.page = i;                                                //Set the record's page
                    record->id.slot = j;                                                //and slot to the deleted record's
                    updateRecord(rel,record);                                           //pretend to update that record, with the new one's data
                    bool d = FALSE;                                                     
                    memcpy((ph->data)+offset+fullsize-sizeof(bool),&d,sizeof(bool));    //make the record no longer deleted
                    markDirty(td->bm,ph);                                               //write to the disk
                    td->gaps[i]--;                                                      //subtract one of the gaps
                    return RC_OK;
                }
                unpinPage(td->bm,ph);
            }
            return RC_RM_NO_MORE_TUPLES;    //somehow the gaps list was wrong!
        }
    }                                           //No empty spaces before last record
    if(td->latest.slot>=(td->maxRecords-1)){     //Page would overflow on next insert
        record->id=td->latest;                    //Set the record's slot to the next slot
        td->latest.slot=0;                        //Update the next slot
        td->latest.page++;                        //And move to the next page
    }else{                                       //Normal insert
        record->id=td->latest;                    //Set the record's slot to the next slot
        td->latest.slot++;                        //Update the next slot
    }
    BM_PageHandle *ph = MAKE_PAGE_HANDLE();                     //Make a page
    pinPage(td->bm,ph,record->id.page);                         //Pin the page we are inserting into
    int size = getRecordSize(rel->schema);                      //Get the size of the record
    int fullsize = size+sizeof(RID)+sizeof(bool);               //Get the size of a full entry, including metadata
    char *tmp = malloc(fullsize);                               //Create a buffer to put the padded record in4
    memcpy(tmp,record->data,size);                              //Fill out the record with required metadata
    memcpy(tmp+size,&record->id,sizeof(RID));                   //(RID)
    memcpy(tmp+size+sizeof(RID),&record->deleted,sizeof(bool)); //(Deletion status)
    memcpy((ph->data)+(fullsize*record->id.slot),tmp,fullsize); //Copy the record into the table
    free(tmp);                                                  //free buffer
    markDirty(td->bm,ph);                                       //Write to 'disk'
    unpinPage(td->bm,ph);
    return RC_OK;
}
RC deleteRecord (RM_TableData *rel, RID id){
    int size = getRecordSize(rel->schema);
    int fullsize = size+sizeof(RID)+sizeof(bool);
    BM_PageHandle *ph = MAKE_PAGE_HANDLE();
    pinPage(rel->mgmtData->bm,ph,id.page);
    int offset = id.slot*fullsize;
    bool d = TRUE;
    memcpy((ph->data)+offset+fullsize-sizeof(bool),&d,sizeof(bool));
    markDirty(rel->mgmtData->bm,ph);
    unpinPage(rel->mgmtData->bm,ph);
    rel->mgmtData->gaps[id.page]++;
    return RC_OK;
}
RC updateRecord (RM_TableData *rel, Record *record){
    int size = getRecordSize(rel->schema);
    int fullsize = size+sizeof(RID)+sizeof(bool);
    BM_PageHandle *ph = MAKE_PAGE_HANDLE();
    pinPage(rel->mgmtData->bm,ph,record->id.page);
    int offset = record->id.slot*fullsize;
    memcpy((ph->data)+offset,record->data,size);
    markDirty(rel->mgmtData->bm,ph);
    unpinPage(rel->mgmtData->bm,ph);
    return RC_OK;
}
RC getRecord (RM_TableData *rel, RID id, Record *record){
    int size = getRecordSize(rel->schema);
    int fullsize = size+sizeof(RID)+sizeof(bool);
    BM_PageHandle *ph = MAKE_PAGE_HANDLE();
    pinPage(rel->mgmtData->bm,ph,id.page);
    int offset = id.slot*fullsize;
    memcpy(record->data,(ph->data)+offset,size);
    memcpy(&record->id,(ph->data)+offset+size,sizeof(RID));
    memcpy(&record->deleted,(ph->data)+offset+size+sizeof(RID),sizeof(bool));
    markDirty(rel->mgmtData->bm,ph);
    unpinPage(rel->mgmtData->bm,ph);
    return RC_OK;
}

// scans
RC startScan (RM_TableData *rel, RM_ScanHandle *scan, Expr *cond){}
RC next (RM_ScanHandle *scan, Record *record){}
RC closeScan (RM_ScanHandle *scan){}

// dealing with schemas
int getRecordSize (Schema *schema){
    int recordSize = 0;
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
    return recordSize;
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