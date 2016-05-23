

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "kernel.h"
#include "kuart5235.h"
#include "k_iproto.h"
#include "jsk_buff.h"
#include "lapj.h"

typedef struct {
    U8*     nextMDAddress;
    int     defMDCount;
    char    TxtPrint[128];
}T_DBUGVARS;
T_DBUGVARS debugvars;

#define TEXTOUT(f,s) k_write(1,s,strlen(s))

extern struct S_mxvspmux muxVar_store;

struct S_msgstr{
    int     code;
    char*   str;
};
struct S_msgstr msgTable[] = {
    {SYSMSG_CREATE, "SYSMSG_CREATE",},
    {SYSMSG_INIT,"SYSMSG_INIT",},
    {SYSMSG_CONTINUE,"SYSMSG_CONTINUE",},
    {SYSMSG_RESET,"SYSMSG_RESET",},
    {SYSMSG_KILL,"SYSMSG_KILL",},
    {SYSMSG_CLOSE,"SYSMSG_CLOSE",},
    {SYSMSG_SHDOWN,"SYSMSG_SHDOWN",},
    {SYSMSG_NVUPDATE,"SYSMSG_NVUPDATE",},
    {SYSMSG_DEVIRQ,"SYSMSG_DEVIRQ",},
    {SYSMSG_DEVIWDOG,"SYSMSG_DEVIWDOG",},
    {SYSMSG_CHARIN,"SYSMSG_CHARIN",},
    {SYSMSG_CHAROUT,"SYSMSG_CHAROUT",},
    {SYSMSG_10MS,"SYSMSG_10MS",},
    {SYSMSG_100MS,"SYSMSG_100MS",},
    {SYSMSG_1000MS,"SYSMSG_1000MS",},
    {SYSMSG_SMXENAB,"SYSMSG_SMXENAB",},
    {SYSMSG_SMXREAD,"SYSMSG_SMXREAD",},
    {SYSMSG_SMXWRITE,"SYSMSG_SMXWRITE",},
    {SYSMSG_SMXTIMER,"SYSMSG_SMXTIMER",},
    {SYSMSG_ECCSTATE,"SYSMSG_ECCSTATE",},
    {SYSMSG_ECCTXRDY,"SYSMSG_ECCTXRDY",},
    {SYSMSG_ECCRXRDY,"SYSMSG_ECCRXRDY",},
    {SYSMSG_SMXSTATE,"SYSMSG_SMXSTATE",},
    {ELOG_ACTIVATE,"ELOG_ACTIVATE",},
    {ELOG_DEVWAIT,"ELOG_DEVWAIT",},
    {ELOG_DEVWAKEUP,"ELOG_DEVWAKEUP",},
    {ELOG_MSGWAIT,"ELOG_MSGWAIT",},
    {ELOG_SEMWAIT,"ELOG_SEMWAIT",},
    {ELOG_SLEEP,"ELOG_SLEEP",},
    {ELOG_MUTEXWAIT,"ELOG_MUTEXWAIT",},
    {SYSMSG_LAST+1,"SMXPOLL",},

    {0,"",},
};

const char AddrFormat[] ="\r\n%08x: ";
const char ByteFormat[] =" %02x";
const char ShortFormat[]=" %04x";
const char LongFormat[] =" %08x";

extern char MemRamDisk[];



/*
*=====================================================================================
*
* _write, was a test
*
*=====================================================================================
*/
int k_write(int DiD, void *buf, int n)
{
    int k;
    char* pChar = (char*)buf;

    for(k=0; k<n; k++)
    {
        putchar(pChar[k]);
    }
    return n;
}
int PrintCRLF()
{
    return k_write(1,"\r\n",2);
}
/******************************************************************************
* taken fron the debugger
*****************************************************************************/

int dbg_MD_B(void* pMem, int nCnt)
{
    int k;
    U32 m,n;
    int c,pflag;
    U8 *pChar,*sChar;
    T_DBUGVARS *dvars;

    dvars=(T_DBUGVARS *)&debugvars;
    n = nCnt;
    pChar=(U8*)pMem;


    memset(dvars->TxtPrint,' ',80);
    dvars->TxtPrint[79]=0;
    k=( (int) pChar);
    sprintf(dvars->TxtPrint,AddrFormat,k);
    dvars->TxtPrint[12]=' ';
    sChar=pChar;
    pflag=0;

    for(m=0;m<n;m++)
    {
        k=(( (int)pChar) -(int)sChar)/sizeof(char);                  // get offset
        if(k>=16)                                        // time for a newline
        {
            TEXTOUT(1,dvars->TxtPrint);
            memset(dvars->TxtPrint,' ',80);
            dvars->TxtPrint[79]=0;
            sprintf(dvars->TxtPrint,AddrFormat,(U32)pChar);
            dvars->TxtPrint[12]=' ';
            sChar=pChar;
            pflag=0;
            k=0;
        }
//        if(!CheckAccess(pChar,MEMACCESSRD))return(-2);
        c=*pChar&0xff;
        sprintf(&(dvars->TxtPrint[k*3+12])," %02x",c);
        dvars->TxtPrint[k*3+12+3]=' ';
        if((c<' ') || (c>0x7e))c='.';
        dvars->TxtPrint[k+62]=c;
        pChar +=1;
        pflag=1;

    }
    if(pflag)
    {
        TEXTOUT(1,dvars->TxtPrint);
        PrintCRLF();
    }
    dvars->nextMDAddress=pChar;
    dvars->defMDCount=n;

    return 0;
}

int dbg_MD_W(void* pMem, int nCnt)
{
    int k;
    U32 m,n;
    int c,cTmp;
    short *pChar, *sChar;
    int pflag;
    T_DBUGVARS *dvars;

    dvars=(T_DBUGVARS *)&debugvars;
    n = nCnt;
    pChar=(short*)pMem;


    memset(dvars->TxtPrint,' ',80);
    dvars->TxtPrint[79]=0;
    k=( (int) pChar);
    sprintf(dvars->TxtPrint,AddrFormat,k);
    dvars->TxtPrint[12]=' ';
    sChar=pChar;
    pflag=0;

    for(m=0;m<n;m++)
    {
        k=(( (int)pChar) -(int)sChar)/sizeof(short);                  // get offset
        if(k>=8)                                        // time for a newline
        {
            k_write(1,dvars->TxtPrint,strlen(dvars->TxtPrint));
            memset(dvars->TxtPrint,' ',80);
            dvars->TxtPrint[79]=0;
            sprintf(dvars->TxtPrint,AddrFormat,(U32)pChar);
            dvars->TxtPrint[12]=' ';
            sChar=pChar;
            pflag=0;
            k=0;
        }
//        if(!CheckAccess(pChar,MEMACCESSRD))return(-2);
        c=*pChar&0xffff;
        sprintf(&(dvars->TxtPrint[k*5+12]),ShortFormat,c);
        dvars->TxtPrint[k*5+12+5]=' ';

        cTmp=0xff&( c>>8 );
        if((cTmp<' ') || (cTmp>0x7e))cTmp='.';
        dvars->TxtPrint[2*k+62]=cTmp;
        cTmp=0xff&(c);
        if((cTmp<' ') || (cTmp>0x7e))cTmp='.';
        dvars->TxtPrint[2*k+1+62]=cTmp;

        pChar +=1;
        pflag=1;

    }
    if(pflag)
    {
        k_write(1,dvars->TxtPrint,strlen(dvars->TxtPrint));
        PrintCRLF();
    }
    dvars->nextMDAddress= (U8*)pChar;
    dvars->defMDCount=n;

    return 0;
}

int dbg_MD_L(void* pMem, int nCnt)
{
    int k;
    U32 m,n;
    int c,cTmp;
    int *pChar,*sChar;
    int pflag;
    T_DBUGVARS *dvars;

    dvars=(T_DBUGVARS *)&debugvars;
    n = nCnt;
    pChar=(int*)pMem;


    memset(dvars->TxtPrint,' ',80);
    dvars->TxtPrint[79]=0;
    sChar=pChar;
    k=( (int) pChar);
    sprintf(dvars->TxtPrint,AddrFormat,k);
    dvars->TxtPrint[12]=' ';
    pflag=0;


    for(m=0;m<n;m++)
    {
        k=(( (int)pChar) -(int)sChar)/sizeof(long);                  // get offset
        if(k>=4)                                        // time for a newline
        {
            TEXTOUT(1,dvars->TxtPrint);
            memset(dvars->TxtPrint,' ',80);
            dvars->TxtPrint[79]=0;
            sprintf(dvars->TxtPrint,AddrFormat,(U32)pChar);
            dvars->TxtPrint[12]=' ';
            sChar=pChar;
            pflag=0;
            k=0;
        }
//        if(!CheckAccess(pChar,MEMACCESSRD))return(-2);
        c=*pChar;
        sprintf(&(dvars->TxtPrint[k*9+12]),LongFormat,c);
        dvars->TxtPrint[k*9+12+9]=' ';

        cTmp=0xff&( c>>24 );
        if((cTmp<' ') || (cTmp>0x7e))cTmp='.';
        dvars->TxtPrint[4*k+62]=cTmp;
        cTmp=0xff&(c>>16);
        if((cTmp<' ') || (cTmp>0x7e))cTmp='.';
        dvars->TxtPrint[4*k+1+62]=cTmp;
        cTmp=0xff&( c>>8 );
        if((cTmp<' ') || (cTmp>0x7e))cTmp='.';
        dvars->TxtPrint[4*k+2+62]=cTmp;
        cTmp=0xff&(c);
        if((cTmp<' ') || (cTmp>0x7e))cTmp='.';
        dvars->TxtPrint[4*k+3+62]=cTmp;

        pChar +=1;
        pflag=1;

    }
    if(pflag)
    {
        TEXTOUT(1,dvars->TxtPrint);
        PrintCRLF();
    }
    dvars->nextMDAddress=(U8*)pChar;
    dvars->defMDCount=n;

    return 0;
}


/*
*=====================================================================================
*
* t_LAPJBUFSTRUCT getJskMemP(void)
*   het pointer to jskMemory structure
*
*=====================================================================================
*/
t_LAPJBUFSTRUCT* getJskMemP(void)
{
    struct S_mxvspmux* pmuxVar_store = (struct S_mxvspmux*)&muxVar_store;
    struct S_LAPJBUFSTRUCT* pJsk;

    pJsk = pmuxVar_store->pJskMem;
    return pJsk;
}

/*
*=====================================================================================
*
* t_lapj_buf getJskBufP(int n)
*   het pointer to n'th jsk buffer
*
*=====================================================================================
*/
t_lapj_buf* getJskBufP(int n)
{
    struct S_LAPJBUFSTRUCT* pJsk;
    t_lapj_buf* pB;

    pJsk = getJskMemP();
    pB = &(pJsk->BufHeaders[n]);
    return pB;
}
/*
*=====================================================================================
*
* char* Msg2Str(int msg)
*   look up a message a string table
*
*=====================================================================================
*/
static char defmsgstr[] = "<???>";
char* Msg2Str(int msg)
{
    int k;
    char* pChar;

    msg = msg & 0xffff;
    for(k=0; k<100; k++)
    {
        pChar = msgTable[k].str;
        if(!pChar)
            break;
        if(msgTable[k].code == msg)
            return(pChar);
    }
    return defmsgstr;
}

/*
*=====================================================================================
*
* Trace messages for task
*
*=====================================================================================
*/
int PrTaskMsgQ(int tid)
{
    t_TASKINFO* pTsk;
    MESSAGEATOM* pMsg;
    MESSAGEQUEUE* pMsgQ;
    char* pChar;
    int k,n;

    pTsk = &k_tasktbl[tid];             // Select the task
    pMsgQ = pTsk->mq;                   // His queue pointer
    n = pMsgQ->put;                     // the put place
    for(k=0; k<pMsgQ->sz; k++)
    {
        pMsg = &(pMsgQ->buf[n]);
        pChar = Msg2Str(pMsg->msg);
        printf("%2d: 0x%06x (0x%06x,0x%06x)  %s\r\n",
            (pMsgQ->sz-k),pMsg->msg,pMsg->param1,pMsg->param2,pChar);
        n = (n+1) % pMsgQ->sz;                         // get the most recent
    }
    return 0;
}

/*
*=====================================================================================
*
* Printf the Log queue
* After some confusion (pilot errors), the top will be the oldest
* and the bottom the most recent
*=====================================================================================
*/
int PrLogQ(void)
{
//    t_TASKINFO* pTsk;
    MESSAGEATOM* pMsg;
    MESSAGEQUEUE* pMsgQ;
    char* pChar;
    int k,n;

    pMsgQ = (MESSAGEQUEUE*)MemRamDisk;                   // His queue pointer
    n = pMsgQ->put;                     // the put place
    for(k=0; k<pMsgQ->sz; k++)
    {
        pMsg = &(pMsgQ->buf[n]);
        pChar = Msg2Str(pMsg->msg);
        printf("%2d: 0x%06x (0x%06x,0x%06x)  %s\r\n",
            (pMsgQ->sz-k),pMsg->msg,pMsg->param1,pMsg->param2,pChar);
        n = (n+1) % pMsgQ->sz;                         // get the most recent
    }
    return 0;
}
/*
*=====================================================================================
*
* Printf out a chatacher queue
*
* int PrCharQ(char* str, CHARQUEUE* q)
*   str:    Title
*   p:      Pointer to the character queue
*
* Prints in characters
* Note the buffer will appear reversed
*=====================================================================================
*/
int PrCharQ(char* str, CHARQUEUE* q)
{
    int k,n;
    int c;

    n = q->put - q->get;
    if(n < 0)
        n += q->sz;
    printf("%s  Contains %d characters\r\n",str,n);
    n = q->put;                     // the put place
    for(k=0; k<q->sz; k++)
    {
        n -= 1;                         // get the most recent
        if(n<0)
            n = q->sz-1;
        c = 0xff & q->buf[n];
        if(c<0x20)
            c='.';
        if(c==0x7f)
            c='.';
        if( (k%72) == 0)
        printf("\r\n");
        printf("%c",c);
    }
    printf("\r\n");
    return 0;
}

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
void prjskmem(void)
{
    int k,n;
//    DEVSTRUCT* pDev;
    struct S_mxvspmux* pmuxVar_store = (struct S_mxvspmux*)&muxVar_store;
    struct S_LAPJBUFSTRUCT* pJsk;
    t_DQue* pQ;
    t_lapj_buf* pB;
    char strN[16];
    char strP[16];

    // derererence to get pJsk
    pJsk = pmuxVar_store->pJskMem;
    printf("pJsk 0x%06x\r\n",pJsk);
    pQ = &(pJsk->freeQHdr);
    printf("freeQHdr (0x%06x): n:0x%06x p:0x%06x v:0x%06x\r\n",
        pQ,pQ->next,pQ->prev,pQ->val);
    for(k=0; k< NLAPJBUFPOOL; k++)
    {
        pB = &(pJsk->BufHeaders[k]);
        pQ = &(pB->lnk);

        //======== is next in the table?
        if( (n=isInJskTab(pJsk,pQ->next)) >= 0)
            sprintf(strN,"[%6d]",n);
        else
            sprintf(strN,"0x%06x",pQ->next);
        //======== is prev in the table
        if( (n=isInJskTab(pJsk,pQ->prev)) >= 0)
            sprintf(strP,"[%6d]",n);
        else
            sprintf(strP,"0x%06x",pQ->prev);
#if 1
        printf("%2d(0x%06x): n:%s p:%s v:0x%06x H:0x%06x own:%d\r\n",
            k,pQ,strN,strP,pQ->val,pB->pData,pB->own);
#else
        printf("%2d(0x%06x): n:0x%06x p:0x%06x v:0x%06x H:0x%06x own:%d\r\n",
            k,pQ,pQ->next,pQ->prev,pQ->val,pB->pData,pB->own);
#endif
    }
}

/*
*=====================================================================================
*
* void prmuxVar_store(void)
*
*  print the muxVar_store table
*
*=====================================================================================
*/
void prmuxVar_store(void)
{
    struct S_mxvspmux* pMux = (struct S_mxvspmux*)&muxVar_store;
//    struct S_LAPJBUFSTRUCT* pJsk;

    printf("pMux 0x%06x\r\n",pMux);
    printf("dev: 0x%02x pit: 0x%02x scanstate: %2d scancnt: %2d vsp: 0x%02x\r\n",
        pMux->dev,
        pMux->pit,
        pMux->scanstate,
        pMux->scancnt,
        pMux->vsp);
    printf("pJskMem: 0x%06x bufpTxFpkt: 0x%06x bufpRxFrame: 0x%06x\r\n",
        pMux->pJskMem,pMux->bufpTxFpkt, pMux->bufpRxFrame);

}

/*
*=====================================================================================
*
* void prEcc(t_SIMECC* pEcc)
*
*  print the muxVar_store table
*
*=====================================================================================
*/
void prEcc(t_SIMECC* pEcc)
{
    int k;
    t_lapj_buf* pB;

    printf("pEcc 0x%06x\r\n",pEcc);
    printf("Vs %2d: Vr: %2d Va: %2d Vc: %2d Venq: %2d M: %2d W: %2d state: %2d flgs: 0x%04x\r\n",
        pEcc->Vs,
        pEcc->Vr,
        pEcc->Va,
        pEcc->Vc,
        pEcc->Venq,
        pEcc->M,
        pEcc->W,
        pEcc->lapjstate,
        pEcc->flgs);
    printf("svHdrQ: n: 0x%06x p: 0x%06x\r\n",
        pEcc->svHdrQ.next,pEcc->svHdrQ.prev
        );
    for(k=0; k<MAXMOD42; k++)
    {
        pB = pEcc->aSend[k];
        if(pB)
        {
            printf("%2d: 0x%06x own:%2d len:%4d pData:0x%06x\r\n",k,pB,pB->own,pB->len,pB->pData);
        }
        else
        {
            printf("%2d: 0x%06x\r\n",k,pB);
        }
    }
}
/*
*=====================================================================================
*
* void prLogLapj(void)
*
*  print the log
*
*=====================================================================================
*/
struct JASKLOG{
    U8  msg;
    U8  Pad1;
    U8  Pad2;
    U8  Pad3;
    U8  Vs;
    U8  Vr;
    U8  Va;
    U8  Venq;
};
extern struct S_LAPLOG** pJskLog;
#define MAXLOG 100

void prLogLapj(void)
{
    int k;
    t_lapj_buf* pB;
    struct S_LAPLOG* pTmp = pJskLog;

    printf("prLogLapj 0x%06x 0x%06x\r\n",pJskLog, pTmp);

    for(k=0; k<MAXLOG; k++)
    {
        printf("%2d: msg:%4d Vs %2d: Vr: %2d Va: %2d Venq: %2d (%02x, %02x, %02x)\r\n",
            k,
            pTmp->msg,
            pTmp->Vs,
            pTmp->Vr,
            pTmp->Va,
            pTmp->Venq,
            pTmp->U8a1,
            pTmp->U8a2,
            pTmp->U8a3
        );
        pTmp++;
    }
}
/*
*****************************************************************************
* FUNCTION  struct S_DEVSTRUCT *GetVspDPointer( int DiD)
*
* PUTPOSE Get the Device pointer for the vsp
*
* ARGUMENTS
*   DiD  device id
*
* RETURN
*   NULL on some check failures
*   pointer to the device
*
* COMMENTS
*   Useful utility to combing error ehecking and layers of dereference
*
*****************************************************************************
*/
struct S_DEVSTRUCT *GetVspDPointer( int DiD)
{
    struct S_DEVSTRUCT *uDev;   // pointer to the device structure
    struct S_TASKINFO *pTask;   // pointer to the task
    struct S_CHARQUEUE *aq;     // a temp for a queue
    int     itmp;
    int     savSR;

    itmp = DiD & 0xff;                          // mask off any subclass bits
    if( (itmp<0) || (itmp >= MAXDEV) )          // standard check
        return(NULL);

    uDev = &k_devtbl[itmp];                     // get the device

    if(!(uDev->flags & DEV_EXIST))              // check if it exists
        return(NULL);

    if(uDev->tid < 0)                           // No owner for this device
        return(NULL);                           // do we care?

    pTask=&k_tasktbl[uDev->tid];                // get the task pointer
    if((pTask->state == TS_EMPTY)               // Owner is an empty task
     ||(pTask->state == TS_INCLOSE)             // Owner is exiting
     ||(pTask->state == TS_HUNG))               // Owner is alt exiting
        return(NULL);

    return uDev;

}
/*
*****************************************************************************
* FUNCTION  struct S_CHARQUEUE *GetVspOutQ( int DiD)
*
* PUTPOSE Get the Device pointer for the vsp
*
* ARGUMENTS
*   DiD  device id
*
* RETURN
*   NULL on some check failures
*   pointer to the que
*
* COMMENTS
*   Useful utility to combing error ehecking and layers of r=dereference
*
*****************************************************************************
*/
struct S_CHARQUEUE *GetVspOutQ( int DiD)
{
    struct S_DEVSTRUCT *uDev;   // pointer to the device structure
    struct S_TASKINFO *pTask;   // pointer to the task
    struct S_CHARQUEUE *aq;     // a temp for a queue

    uDev = GetVspDPointer(DiD);
    if(!uDev)
        return NULL;

    aq = (struct S_CHARQUEUE *)uDev->outputdata;
    if (!aq)
        return(NULL);                // this device does not support queued writes

    return(aq);
}
