#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "record_mgr.h"
#include "storage_mgr.h"
#include "buffer_mgr.h"
#include "expr.h"
#include "dberror.h"

head* tableList;

// table and manager
RC initRecordManager (void *mgmtData){
    initStorageManager();
    tableList = malloc(sizeof(head*));
    link *td = malloc(sizeof(link*));
    td->next = NULL;
    td->data = mgmtData;
    tableList->first = td;
    tableList->length = 0;
    return RC_OK;
}
RC shutdownRecordManager (){
    while(tableList->first){                //Empty the table list
        delete(tableList,tableList->first);
    }
    while(dir->first){                      //Empty the directory
        delete(dir,dir->first);
    }
    free(tableList);                        //free both lists
    free(dir);
    return RC_OK;
}
RC createTable (char *name, Schema *schema){
    RM_TableData *newTable = malloc(sizeof(RM_TableData));  //Allocate space for the table
    newTable->name=name;                                    //initialize its values
    newTable->schema=schema;
    RID lt = {0,0};
    tData *td = malloc(sizeof(tData));                      //Allocate space for the table's metadata
    int *gaps = malloc(sizeof(int)*1024);   //couldnt find a way to dynamically track this easily, so i just set a hard cap at 1024 pages per table, should be enough :_(
    for(int i=0;i<1024;i++){gaps[i]=0;}                     //initialize metadata
    td->latest=lt;                                          
    td->gaps=gaps;
    td->maxRecords = PAGE_SIZE/(sizeof(RID)+sizeof(bool)+getRecordSize(schema)); //Calculate the maximum amount of records per page.
    newTable->mgmtData=td;
    
    createPageFile(name);                                       //Create a page file to hold the table
    SM_FileHandle* tableFile = malloc(sizeof(SM_FileHandle));   //Allocate
    openPageFile(name,tableFile);                               //And open that file
    BM_BufferPool* bm = malloc(sizeof(BM_BufferPool));          //Allocate
    initBufferPool(bm,name,5,RS_LRU,NULL);                      //And initialize a buffer pool to manage that file
    td->bm = bm;                                                //put that into the metadata

    link *tableEntry = malloc(sizeof(link*));   //Allocate a link for the table
    tableEntry->next = NULL;                    //initialize it
    tableEntry->data = newTable;
    return append(tableList,tableEntry);        //Finally, put the table on the global table list
}
RC openTable (RM_TableData *relo, char *name){
    link *t = tableList->first->next;       
    for (t;t;t=t->next){                    //Search through the table list
        RM_TableData *rel = t->data;        //get the table for that link
        if (strcmp(rel->name,name)==0){     //if the names match
            relo->name=rel->name;           //initialize the output relation with the stored one's values
            relo->mgmtData=rel->mgmtData;
            relo->schema=rel->schema;
            return RC_OK;
        }
    }
    return RC_FILE_NOT_FOUND;
}
RC closeTable (RM_TableData *rel){
    return RC_OK; //Every time i tried to free anything here i caused a malloc-related crash. That must mean there's nothing to free! (sarcasm)
}
RC deleteTable (char *name){
    for (link *t = tableList->first->next;t;t=t->next){ //go through the table list
        RM_TableData *rel = t->data;                    //get the table for that link
        if (strcmp(rel->name,name)==0){                 //if the names match
            destroyPageFile(rel->name);                 //delete the table's data
            shutdownBufferPool(rel->mgmtData->bm);      //shutdown the buffer that managed that table
            free(rel->mgmtData->gaps);
            free(rel->mgmtData);                        //free associated metadata
            return delete(tableList,t);                 //and remove the table from the list
        }
    }
    return RC_FILE_NOT_FOUND;
}
int getNumTuples (RM_TableData *rel){
    tData *td = rel->mgmtData;
    int pnum = td->latest.page;
    int snum = td->latest.slot-1;               //must subtract one because the latest slot is not yet filled
    int maxcount = (pnum*td->maxRecords)+snum;  //calculate the maximum amount based on number of pages and records per page info

    int deadcount=0;                            //now that we have a theoretical max
    for (int i=0;i<256;i++){
        deadcount+=td->gaps[i];                 //find the number of deleted tuples
    }
    return maxcount-deadcount;                  //subtract that off of the max
}

// handling records in a table
RC insertRecord (RM_TableData *rel, Record *record){
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
    if(td->latest.slot>=(td->maxRecords-1)){     //If at end of current page,
        record->id=td->latest;                    //Set the record's slot to the next slot
        td->latest.slot=0;                        //Reset slot to 0
        td->latest.page++;                        //And move to the next page
    }else{                                       //Otherwise
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
    int size = getRecordSize(rel->schema);                          //repeat block
    int fullsize = size+sizeof(RID)+sizeof(bool);                   
    BM_PageHandle *ph = MAKE_PAGE_HANDLE();                         
    pinPage(rel->mgmtData->bm,ph,id.page);                          
    int offset = id.slot*fullsize;                                  
    bool d = TRUE;                                                  //get a boolean holding true
    memcpy((ph->data)+offset+fullsize-sizeof(bool),&d,sizeof(bool));//copy in that memory to the record's deleted flag
    markDirty(rel->mgmtData->bm,ph);                                
    unpinPage(rel->mgmtData->bm,ph);                                //writeback
    rel->mgmtData->gaps[id.page]++;                                 //add 1 to the number of gaps on this page
    return RC_OK;                       
}
RC updateRecord (RM_TableData *rel, Record *record){
    int size = getRecordSize(rel->schema);  //repeat block
    int fullsize = size+sizeof(RID)+sizeof(bool);
    BM_PageHandle *ph = MAKE_PAGE_HANDLE();
    pinPage(rel->mgmtData->bm,ph,record->id.page);
    int offset = record->id.slot*fullsize;
    memcpy((ph->data)+offset,record->data,size);    //Metadata remains the same, as it takes up the same slot and has not been deleted.
    markDirty(rel->mgmtData->bm,ph);                //Simply copy in the new data from the provided record
    unpinPage(rel->mgmtData->bm,ph);    //write back
    return RC_OK;
}
RC getRecord (RM_TableData *rel, RID id, Record *record){
    int size = getRecordSize(rel->schema);  //repeat block
    int fullsize = size+sizeof(RID)+sizeof(bool);
    BM_PageHandle *ph = MAKE_PAGE_HANDLE();
    pinPage(rel->mgmtData->bm,ph,id.page);
    int offset = id.slot*fullsize;
    memcpy(record->data,(ph->data)+offset,size);                                //Copy data from the table into the provided
    memcpy(&record->id,(ph->data)+offset+size,sizeof(RID));                     //^^^
    memcpy(&record->deleted,(ph->data)+offset+size+sizeof(RID),sizeof(bool));   //^^^
    markDirty(rel->mgmtData->bm,ph);    //force writeback
    unpinPage(rel->mgmtData->bm,ph);    //unpin
    return RC_OK;
}

// scans
RC startScan (RM_TableData *rel, RM_ScanHandle *scan, Expr *cond){
    scan->rel = rel;                          //simply initialize the scan's table and other metadata
    scan->mgmtData = malloc(sizeof(sData));
    scan->mgmtData->condition = cond;
    scan->mgmtData->currentPos.page=0;
    scan->mgmtData->currentPos.slot=0;
    return RC_OK;
}
RC next (RM_ScanHandle *scan, Record *record){
    sData *sd = scan->mgmtData;
    tData *td = scan->rel->mgmtData;
    int pnum = td->latest.page;                             //get positional information
    int snum = td->latest.slot;                             //about the last entry in the table
    int pcur = sd->currentPos.page;                         //get positional information
    int scur = sd->currentPos.slot;                         //about where we are in the table
    if(pcur>pnum || (scur>=snum&&pnum==pcur)){              //check if we are at the end of the table
        return RC_RM_NO_MORE_TUPLES;                        //if so, signal that.
    }
    Record *r;                                              
    createRecord(&r,scan->rel->schema);                     //make a record
    getRecord(scan->rel,sd->currentPos,r);                  //get the record at the scan's current position
    Value *v;
    evalExpr(r,scan->rel->schema,sd->condition,&v);         //Get the result of the condition for the current record
    freeRecord(r);                                          //as we recurse, this is necessary to not go into infinite memory debt
    if (v->v.boolV){                                        //If the expression is true for the current record,
        freeVal(v);
        getRecord(scan->rel,sd->currentPos,record);          //return the current record and move to the next
        if(sd->currentPos.slot>=(td->maxRecords-1)){         //If at end of current page,
            sd->currentPos.slot=0;                            //Reset slot to 0
            sd->currentPos.page++;                            //And move to the next page
        }else{                                               //Otherwise
            sd->currentPos.slot++;                            //Move to the next slot
        }
        return RC_OK;
    }else{                                                  //If not
        freeVal(v);
        if(sd->currentPos.slot>=(td->maxRecords-1)){         //If at end of current page,
            sd->currentPos.slot=0;                            //Reset slot to 0
            sd->currentPos.page++;                            //And move to the next page
        }else{                                               //Otherwise
            sd->currentPos.slot++;                            //Move to the next slot
        }
        return next(scan,record);                            //recurse, using the next record position
    }
}
RC closeScan (RM_ScanHandle *scan){
    free(scan->mgmtData);   //The only dynamically allocated part of the scan is the metadata container struct
    return RC_OK;
}

// dealing with schemas
int getRecordSize (Schema *schema){
    int recordSize = 0;                             //Calculate the record's size
    for (int i=0;i<schema->numAttr;i++){            //for each attribute
        switch (schema->dataTypes[i]){              //check its type
            case DT_INT:                            //Integer
                recordSize+=sizeof(int);            //add size of int
                break;
            case DT_STRING:                         //String
                recordSize+=schema->typeLength[i];  //add size defined in typeLength
                break;
            case DT_FLOAT:                          //Float
                recordSize+=sizeof(float);          //add size of float
                break;
            case DT_BOOL:                           //Boolean
                recordSize+=sizeof(bool);           //add size of bool
                break;
        }
    }
    return recordSize;
}
Schema *createSchema (int numAttr, char **attrNames, DataType *dataTypes, int *typeLength, int keySize, int *keys){
    Schema *s = malloc(sizeof(Schema));     //Simply allocate and then fill out the schema
    s->attrNames=attrNames;
    s->dataTypes=dataTypes;
    s->keyAttrs=keys;
    s->keySize=keySize;
    s->numAttr=numAttr;
    s->typeLength=typeLength;
    return s;
}
RC freeSchema (Schema *schema){
    free(schema);   //all metadata is statically allocated, only need to free the schema itself
    return RC_OK;   
}

// dealing with records and attribute values
RC createRecord (Record **record, Schema *schema){
    Record *r = malloc(sizeof(Record));         //allocate space for the record
    r->data = malloc(getRecordSize(schema));    //allocate space for the record's data
    r->id.page=0;                               //initialize the record's position
    r->id.slot=0;
    *record = r;                                //effectively return the created record
    return RC_OK;
}
RC freeRecord (Record *record){
    free(record->data);     //free the record's allocated data slot
    free(record);           //free the record's reference
    return RC_OK;
}
RC getAttr (Record *record, Schema *schema, int attrNum, Value **value){
    size_t offset = 0;                                      
    for (int i=0;i<attrNum;i++){                            //Calculate the offset into the record's data
        switch (schema->dataTypes[i]){                      //check the datatypes, in order
            case DT_INT:                                    //if int,
                offset+=sizeof(int);                         //add that to the offset
                break;
            case DT_STRING:                                 //if string
                offset+=schema->typeLength[i];               //check the defined size of the string and add that
                break;
            case DT_FLOAT: //Float                          //if float
                offset+=sizeof(float);                       //add that to the offset
                break;
            case DT_BOOL: //Boolean                         //if bool
                offset+=sizeof(bool);                        //add that to the offset
                break;
        }
    }
    Value *val;
    switch (schema->dataTypes[attrNum]){                                //Depending on data type, use different make_value
        case DT_INT:                                                    //for ints
            MAKE_VALUE(val,DT_INT,*(int*)((record->data)+offset));      //DT_INT and int*
            break;
        case DT_STRING:                                                 //for strings
            MAKE_STRING_VALUE(val,(record->data)+offset);               //different function entirely
            val->v.stringV[schema->typeLength[attrNum]]='\0';           //MUST make sure to cut off string after the defined size!
            break;                                                          //Otherwise, might contain the next attribute by default
        case DT_FLOAT:                                                  //for floats
            MAKE_VALUE(val,DT_FLOAT,*(float*)((record->data)+offset));  //DT_FLOAT and float*
            break;
        case DT_BOOL:                                                   //for bools
            MAKE_VALUE(val,DT_BOOL,*(bool*)((record->data)+offset));    //DT_BOOL and bool*
            break;
    }
    *value = val;       //effectively return the retrieved value
    return RC_OK;
}
RC setAttr (Record *record, Schema *schema, int attrNum, Value *value){
    DataType dt = schema->dataTypes[attrNum];               //Get the datatype of the attribute to be set
    if (dt!=value->dt)                                      //Check for discrepancies
        return RC_RM_COMPARE_VALUE_OF_DIFFERENT_DATATYPE;
    
    size_t offset = 0;                                      
    for (int i=0;i<attrNum;i++){                            //Calculate the offset into the record's data
        switch (schema->dataTypes[i]){                      //check the datatypes, in order
            case DT_INT:                                    //if int,
                offset+=sizeof(int);                         //add that to the offset
                break;
            case DT_STRING:                                 //if string
                offset+=schema->typeLength[i];               //check the defined size of the string and add that
                break;
            case DT_FLOAT: //Float                          //if float
                offset+=sizeof(float);                       //add that to the offset
                break;
            case DT_BOOL: //Boolean                         //if bool
                offset+=sizeof(bool);                        //add that to the offset
                break;
        }
    }
    switch (value->dt){                                                                 //Depending on datatype, need different attribute of v
        case DT_INT:                                                                    //For ints,
            memcpy((record->data)+offset,&(value->v.intV),sizeof(int));                  //copy in v.intV
            break;
        case DT_STRING:                                                                 //For strings
            memcpy((record->data)+offset,(value->v.stringV),schema->typeLength[attrNum]);//copy in v.stringV
            break;
        case DT_FLOAT:                                                                  //For floats
            memcpy((record->data)+offset,&(value->v.floatV),sizeof(float));              //copy in v.floatV
            break;
        case DT_BOOL:                                                                   //For bools
            memcpy((record->data)+offset,&(value->v.boolV),sizeof(bool));                //copy in v.boolV
            break;
    } return RC_OK;
}