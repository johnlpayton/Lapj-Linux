/******************************************************************************
* FILE: jskcore.c
*
* CONTAINS:
*
* PURPOSE:
*   interface routines
*
******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32_WINNT
    #include "bctype.h"
    extern t_DQue* k_enqueue(t_DQue* q, t_DQue* elem);
    extern t_DQue* k_dequeue(t_DQue* elem);
    extern char* GetLapPktString(int nPkt);
#endif
#ifdef _LINUX
    #include "lxtype.h"
    extern t_DQue* k_enqueue(t_DQue* q, t_DQue* elem);
    extern t_DQue* k_dequeue(t_DQue* elem);
    extern char* GetLapPktString(int nPkt);
#endif
#ifdef _EVB
    #include "kernel.h"
    #include "k_iproto.h"
    #include "kuart5235.h"
#endif


#include "jsk_buff.h"
#include "lapj.h"



/*
**************************************************************************
*  FUNCTION: void rebuildJskBuf(struct S_LAPJBUFSTRUCT* pJsk, void* x)
*
*  PURPOSE: Build the jsk buffer pool
*
*  ARGUMENTS:
*   pJsk     structure
*   x       memory
*
*  RETURN:
*
*  COMMENTS:
*   sets up the various pointers
*   this rebuilds a jsk structture by chunking out memory
*   Memory allocation is done before we get here
*   see next function for the allocation
*   struct{
*   char bufs[NLAPJBUFPOOL][pJ->maxbufsize]
*   char [2*(pJ->maxbufsize)]
*   }
*
**************************************************************************
*/

void rebuildJskBuf(struct S_LAPJBUFSTRUCT* pJsk, void* x)
{
    int k,n;
    t_lapj_buf* pB;
    U8*     pU8;

    //======== Empty the free queue
    pJsk->freeQHdr.next = &(pJsk->freeQHdr);
    pJsk->freeQHdr.prev = &(pJsk->freeQHdr);
    pJsk->freeQHdr.val = 0;
    pJsk->nAlloc = 0;
    pJsk->id = 0;

    pU8 = (U8*)(pJsk->bulkMemory);

    //======== Initialize the memory blocks
    for(k=0; k<NLAPJBUFPOOL; k+=1)
    {
        t_DQue* pTmpQ = &(pJsk->BufHeaders[k].lnk);  // point to the queue entry
        pTmpQ->next=pTmpQ;                  // make it point to itself
        pTmpQ->prev=pTmpQ;
        n = k*(pJsk->maxbufsize);             // here is some memory
        pTmpQ->val = (int)(&(pU8[n]));

        pB = (t_lapj_buf*)pTmpQ;            // type convert )
        JskPrepBuf(pB);                     // prep the entry
        pB->id = k;                         // for debug, give it an id
        JskFreeQP(pJsk,pTmpQ);              // Free enqueues it
    }
    n = NLAPJBUFPOOL*(pJsk->maxbufsize);
    pJsk->pTxTmpBuf = (U8*)&(pU8[n]);
}
/*
**************************************************************************
*  FUNCTION: void initJskBuf(struct S_LAPJBUFSTRUCT* pJsk)
*
*  PURPOSE: Initialize the JSK buffer scheme
*
*  ARGUMENTS:
*   pJsk     structure
*
*  RETURN:
*
*  COMMENTS:
*   sets up the various pointers
*
**************************************************************************
*/
struct S_LAPJBUFSTRUCT* AllocJskStruct(struct S_LAPJBUFSTRUCT* pJ, int maxbufsize, void* (*eAlloc)(int n))
{
    int     kalloc;

    //======== memory allocation This should go away for a static
    if(!pJ){
#if (EASYWINDEBUG>0) && (LAPJDEBUG>0) && _WIN32_WINNT && 0
//  JLPBUG
printf("initJskBuf: eAlloc= 0x%06x \r\n",eAlloc);
#endif
        pJ = (struct S_LAPJBUFSTRUCT*)eAlloc(sizeof(struct S_LAPJBUFSTRUCT));
//  JLPBUG
#if (EASYWINDEBUG>0) && (LAPJDEBUG>0) && _WIN32_WINNT && 0
printf("initJskBuf: pJ= 0x%06x \r\n",pJ);
#endif
        if(!pJ){
            perror("AllocJskStruct A");
            return pJ;
        }
        memset(pJ,0 ,sizeof(struct S_LAPJBUFSTRUCT));
    }

    //======== reallocate and build memory
    if(pJ->bulkMemory){
        free(pJ->bulkMemory); // oops, evb has problems with this
        pJ->bulkMemory = NULL;
    }

    //======== This is the dynamic allocation
    pJ->maxbufsize = ((maxbufsize/8)+1)*8;  // align on a 64 bit boundry
    kalloc  = NLAPJBUFPOOL*(pJ->maxbufsize) // Buffers
            + 2*(pJ->maxbufsize)+ 16;       // transmit temporaty buffer

    pJ->bulkMemory = (U8*)eAlloc(kalloc);
    if(pJ->bulkMemory == NULL){
        perror("AllocJskStruct B");
        return NULL;
    }

    rebuildJskBuf(pJ, pJ);
#if (EASYWINDEBUG>0) && (LAPJDEBUG>4) && _WIN32_WINNT && 0
prjskmem(pJ);
printf("start: 0x%06x,size 0x%06x\n",pJ->bulkMemory,kalloc);
#endif

    return(pJ);
}
/*
**************************************************************************
*  FUNCTION: void initJskBuf(struct S_LAPJBUFSTRUCT* pJsk)
*
*  PURPOSE: Initialize the JSK buffer scheme
*
*  ARGUMENTS:
*   pJsk     structure
*
*  RETURN:
*
*  COMMENTS:
*   sets up the various pointers
*
**************************************************************************
*/
struct S_LAPJBUFSTRUCT* initJskBuf(struct S_LAPJBUFSTRUCT* pJ, void* (*eAlloc)(int n))
{
    AllocJskStruct(pJ, LAPJDATAMAX, eAlloc);
}

/*
**************************************************************************
*  FUNCTION: void JskFreeQP(struct S_LAPJBUFSTRUCT* pJsk,t_DQue* pQ)
*
*  PURPOSE: Free a Jsk buffer
*
*  ARGUMENTS:
*   pJsk     structure
*   pQ      Pointer to the buffer
*
*  RETURN:
*
*  COMMENTS:
*   Unlinks the buffer then links it into free
*
**************************************************************************
*/
t_DQue* JskFreeQP(struct S_LAPJBUFSTRUCT* pJsk,t_DQue* pQ)
{
    t_DQue* e = NULL;
    if(pQ) // development test
    {
        e = k_dequeue(pQ);                    // unlink him
        e = k_enqueue(&(pJsk->freeQHdr),pQ);  // put into free q
    }
    return e;
}
/*
**************************************************************************
*  FUNCTION: t_DQue* JskGetQP(struct S_LAPJBUFSTRUCT* pJsk)
*
*  PURPOSE: Gets a buffer
*
*  ARGUMENTS:
*   pJsk     structure
*
*  RETURN:
*
*  COMMENTS:
*   uunlinks a buffer from the free list
*   The user routine needs to cast the val pointer to a U8 or char
*   before using the pointer itself
*
**************************************************************************
*/
t_DQue* JskGetQP(struct S_LAPJBUFSTRUCT* pJsk)
{
    t_DQue* pQ = &(pJsk->freeQHdr);
    pQ = (t_DQue*)k_dequeue(pQ->next);
    return pQ;
}
/*
**************************************************************************
*  FUNCTION: U8* JskPrepBuf(struct S_LAPJBUFSTRUCT* pJsk)
*
*  PURPOSE: Prepares a buffer (format) and returns a data pointer
*
*  ARGUMENTS:
*   pJsk     structure
*
*  RETURN:
*
*  COMMENTS:
*   utility to initialize some fields.
*   see rebuildJskBuf(...)
*   (U8*)(pBuf->lnk.val) points to the allocated memory
*   The memory is of size NLAPJBUFSIZE
**************************************************************************
*/
void JskPrepBuf(t_lapj_buf* pBuf)
{
    pBuf->pTop = (U8*)(pBuf->lnk.val)+0;               // min pointer
//    pBuf->pTail = (U8*)(pBuf->lnk.val)+NLAPJBUFSIZE-1;  // max pointer
    pBuf->pData = (U8*)(pBuf->lnk.val)+LAPJBUFOFFSET;   // User data starts here
    pBuf->len   = 0;                                    // zero packet boundries
    pBuf->pTop  = pBuf->pData;
    pBuf->pTail = pBuf->pData;
    pBuf->maxlen = (U16)(NLAPJBUFSIZE);                 // max length of this buffer
    pBuf->flgs = 0x80;                                  // marker for endian debug
}
/*
**************************************************************************
*  FUNCTION: t_lapj_buf* JskAllocBuf(struct S_LAPJBUFSTRUCT* pJsk)
*
*  PURPOSE: Allocate and format a buffer
*
*  ARGUMENTS:
*   pJsk     structure
*
*  RETURN:
*  t_lapj_buf*  pointer to a newly allocated and formatted buffer
*
*  COMMENTS:
*
**************************************************************************
*/
t_lapj_buf* JskAllocBuf(struct S_LAPJBUFSTRUCT* pJsk, int own)
{
    t_lapj_buf* retval;
    retval = (t_lapj_buf*)JskGetQP(pJsk);
    if(retval)
    {
        pJsk->nAlloc += 1;
        JskPrepBuf(retval);
        retval->own = own;
    }
    else
    {
//    printf("JskAllocBuf: Empty\r\n");
    }
#if (EASYWINDEBUG>0) && (LAPJDEBUG>4) && _WIN32_WINNT && 0
prjskmem(pJsk);
#endif
    return retval;

}
/*
**************************************************************************
*  FUNCTION: void JskFreeQP(struct S_LAPJBUFSTRUCT* pJsk,t_DQue* pQ)
*
*  PURPOSE: Free a Jsk buffer
*
*  ARGUMENTS:
*   pJsk     structure
*   pQ      Pointer to the buffer
*
*  RETURN:
*
*  COMMENTS:
*   Unlinks the buffer then links it into free
*
**************************************************************************
*/
t_lapj_buf* JskFreeBuf(struct S_LAPJBUFSTRUCT* pJsk,t_lapj_buf* pA)
{
    t_lapj_buf* pB = NULL;
    if(pA)
    {
        pJsk->nAlloc -= 1;
        pA->own = -1;
        pB = (t_lapj_buf*)JskFreeQP(pJsk,(t_DQue*)pA);
    }
    else
    {
//    printf("JskFreeBuf: Empty\r\n");
    }
    return pB;
}


#if 1
/*
*=====================================================================================
*
* void prjskmem(void)
*
*   printt basis memory of the structure in raw kex
*
*=====================================================================================
*/
static int isInJskTab(struct S_LAPJBUFSTRUCT* pJsk,t_DQue* pA)
{
    int k;
//    t_DQue* pQ;
    t_lapj_buf* pB;

    for(k=0; k< NLAPJBUFPOOL; k++)
    {
        pB = &(pJsk->BufHeaders[k]);
        if( (void*)pB == (void*)pA)
            return k;
    }
    return -1;
}
static void prJskloc(char*str, struct S_LAPJBUFSTRUCT* pJsk, t_DQue* pQ)
{
    int n;

    if(pQ == &(pJsk->freeQHdr))
    {
        sprintf(str,"[     H]");
    }
    else if( (n=isInJskTab(pJsk,pQ)) >= 0)
    {
        sprintf(str,"[%6d]",n);
    }
    else
        sprintf(str,"0x%06x",pQ);
}
void prjskmem(struct S_LAPJBUFSTRUCT* pJsk)
{
    int k,n;
    t_DQue* pQ;
    t_lapj_buf* pB;
    char strN[16];
    char strP[16];

    pQ = &(pJsk->freeQHdr);
    printf("pJsk 0x%06x freeQHdr (0x%06x) ",pJsk,pQ);
    prJskloc(strN,pJsk,pQ->next);
    prJskloc(strP,pJsk,pQ->prev);
    printf("n:%s p:%s v:0x%08x\r\n", strN, strP, pQ->val);

    for(k=0; k< NLAPJBUFPOOL; k++)
    {
        pB = &(pJsk->BufHeaders[k]);
        pQ = &(pB->lnk);
        prJskloc(strN,pJsk,pQ->next);
        prJskloc(strP,pJsk,pQ->prev);
        printf("%2d(0x%06x): n:%s p:%s v:0x%06x H:0x%06x own:%d\r\n",
            k,pQ,strN,strP,pQ->val,pB->pData,pB->own);
    }
    printf("pTxTmpBuf: 0x%06x\n", pJsk->pTxTmpBuf);
}
#endif