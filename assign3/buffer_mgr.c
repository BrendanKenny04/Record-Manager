#include "buffer_mgr.h"
#include "storage_mgr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


//Helper function
int nextSlot(BM_BufferPool *bm,PageNumber pn){
    mData *md = bm->mgmtData;
    for(int i=0;i<bm->numPages;i++){
        if (!md->map[i].data){  //If slot is open (initialized to 0)
            return i;
        }else if(bm->strategy==RS_FIFO)md->age[i]+=1; //If slot is not open, increase FIFO age
    }
    switch (bm->strategy){  //Eviction
        case RS_FIFO://Can fall-through because both FIFO and LRU pick the highest age. 
        //Age is calculated differently for each, and that is accounted for in other areas of code.
        case RS_LRU:{
            int max = -1;
            int slot = NO_PAGE;
            for(int i=0;i<bm->numPages;i++){
                 if(md->age[i]>max&&md->fixcount[i]==0){
                      max = md->age[i];
                      slot = i;
                 }
            }
            if(md->dirty[slot]) forcePage(bm,&(md->map[slot])); //Force writeback if unlucky page is dirty
            return slot;
        }
        default: return RC_READ_NON_EXISTING_PAGE;
    }
}

// Buffer Manager Interface Pool Handling
RC initBufferPool(BM_BufferPool *const bm, char *const pageFileName, const int numPages, RS strategy, void *stratData){
    char *pfn = calloc(1,strlen(pageFileName));
    strncpy(pfn,pageFileName,strlen(pageFileName));
    bm->numPages=numPages;
    bm->pageFile=pageFileName;
    bm->strategy=strategy;
    mData *md = calloc(1,sizeof(mData));
    md->age = calloc(numPages,sizeof(int));
    md->dirty = calloc(numPages,sizeof(bool));
    md->fixcount = calloc(numPages,sizeof(int));
    md->map = calloc(numPages,sizeof(BM_PageHandle));
    for(int i=0;i<numPages;i++){
        md->map[i].pageNum=NO_PAGE;
    }
    md->sinf = stratData;
    md->pf = calloc(1,sizeof(SM_FileHandle));
    openPageFile(pfn,md->pf);
    free(pfn);
    bm->mgmtData = md;
    return RC_OK;
}
RC shutdownBufferPool(BM_BufferPool *const bm){
    mData* md = bm->mgmtData;
    free(md->age);
    free(md->dirty);
    free(md->fixcount);
    for(int i=0;i<bm->numPages;i++){
        if(md->map[i].data) free(md->map[i].data);
    }
    free(md->map);
    free(md);
    return RC_OK;
}
RC forceFlushPool(BM_BufferPool *const bm){
    mData *md = bm->mgmtData;
    RC err=0;
    for(int i=0;i<bm->numPages;i++){
        if(md->dirty[i]){
            md->IO[1]++;
            err+=writeBlock(md->map[i].pageNum,md->pf,md->map[i].data);
            md->dirty[i]=FALSE;
        }
    }
    return err;
}

// Buffer Manager Interface Access Pages
RC markDirty (BM_BufferPool *const bm, BM_PageHandle *const page){
    mData *md = bm->mgmtData;
    bool good=FALSE;
    for(int i=0;i<bm->numPages;i++){
        if(md->map[i].pageNum==page->pageNum){      //If page numbers match
            md->dirty[i]=TRUE;                      //Set dirty bit
            good=TRUE;                              //Used to force loop through whole buffer
        }
    }
    if(good) return RC_OK;
    else return RC_READ_NON_EXISTING_PAGE;
}
RC unpinPage (BM_BufferPool *const bm, BM_PageHandle *const page){
    mData *md = bm->mgmtData;
    RC err=0;
    for(int i=0;i<bm->numPages;i++){
        if (md->map[i].pageNum==page->pageNum){
            if(md->dirty[i]){
                err=forcePage(bm,page);
                //md->dirty[i]=FALSE;
                md->IO[1]--;
            }
            md->fixcount[i]--;
            return err;
        }
    }
    return RC_READ_NON_EXISTING_PAGE;
}
RC forcePage (BM_BufferPool *const bm, BM_PageHandle *const page){
    mData *md = bm->mgmtData;
    md->IO[1]++;
    return writeBlock(page->pageNum,md->pf,page->data);
}
RC pinPage (BM_BufferPool *const bm, BM_PageHandle *const page, const PageNumber pageNum){
    mData *md = bm->mgmtData;
    if(pageNum>=((head*)(md->pf->mgmtInfo))->length)appendEmptyBlock(md->pf);
    page->pageNum=pageNum;
    bool good=FALSE;
    for(int i=0;i<bm->numPages;i++){
        if (md->map[i].pageNum==pageNum){ //Lucky day!
            page->data = md->map[i].data;
            md->fixcount[i]++;
            if(bm->strategy==RS_LRU)md->age[i]=0;
            good=TRUE;
        }if(bm->strategy==RS_LRU)md->age[i]++;
    }
    if(good)return RC_OK;
    int i = nextSlot(bm,pageNum);
    if(i<0)return RC_READ_NON_EXISTING_PAGE;
    md->age[i]=0;
    md->map[i].pageNum=pageNum;
    if(md->map[i].data)free(md->map[i].data);
    md->map[i].data=calloc(1,PAGE_SIZE);
    page->data = md->map[i].data;
    md->fixcount[i]++;
    md->IO[0]++;
    return readBlock(pageNum,md->pf,md->map[i].data);
}


// Statistics Interface
PageNumber *getFrameContents (BM_BufferPool *const bm){
    PageNumber *pn = calloc(1,sizeof(PageNumber)*bm->numPages);
    mData *md = bm->mgmtData;
    for(int i=0;i<bm->numPages;i++){
        pn[i]=md->map[i].pageNum;
    }
    return pn;
}
bool *getDirtyFlags (BM_BufferPool *const bm){return ((mData*)(bm->mgmtData))->dirty;}
int *getFixCounts (BM_BufferPool *const bm){return ((mData*)(bm->mgmtData))->fixcount;}
int getNumReadIO (BM_BufferPool *const bm){return ((mData*)(bm->mgmtData))->IO[0];}
int getNumWriteIO (BM_BufferPool *const bm){return ((mData*)(bm->mgmtData))->IO[1];}