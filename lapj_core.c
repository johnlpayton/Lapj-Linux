/******************************************************************************
* FILE: lapj_core.c
*
*   version 150202
*
* CONTAINS:
*
*
* PURPOSE:
*   Core protocol elements
* 2015-feb-01
*   - Mostly working entering a fine tuning phase now.
*   - State machine transitions between transparent and ecc mode are cumbersome.
*     will have to use it a while to get a feel for how it should work
*   - Fixed the S-Record download in transparent mode.  Lapj needs to copy
*     an input record when accepting it so the caller can reuse the record.
*     The framer has a mode to do this more elegantly.  Will gradually switch
*     over to using it.  Right now, the accept does the copy
*   - Buffered downloads are noticably faster.  It seems the buffering "spoofs"
*     the send and wait delay.  I have to remember the old Codex 6000 stat mux
*     spoofing for the SNA environment.  It works!
*   - we have to add a flush state. Can use the LAPJSTATE_PAUSE entry.  In
*     core, the LAPJCMD_UARTEMPTY handler provides a place to monitor when
*     a transmit buffer has been sent.  We can put a LJfsm_ style call here
*     to handle a flush style shut down.  The main issue is when to invoke the
*     flush vs an abrubt one.  Our current shutdowns are mostly IRQ7 and LAPJ OFF
*     initiated.
*   -
*
* 2015-jan-16
*   - Working with some bugs.  The main data flow is fine, state changes can be
*     buggy (race conditions??)
*   - Some issues with disconnect.  The thing sometimes get locked into a state
*     where the evb is hung in an event loop sending data and the IRQ7
*     from a ecc PC won't work.  From a direct station it does.  This seems
*     to be a case where the evb LAPJFLAG_TXBYPASS is set and TryEccTxAccept
*     writes to the UART but the state variables are not 0 causing TryEccTxData
*     to attempt writing frames (that don't get acked).
*   - Attempts to comment out stuff did not work.  Rreezinf this version
*   - The call to ReInitialiseECC in LJA_toDisc was causing crashes.  It is still
*     commented out. This problem may be related to the previous.
*
* 2014-dec-25 jlp  Transmit unframed working
*   - Long battle but unframed ecc is working on the transmitter.  There is
*     a possibility that a bug remains where the last mvsp buffer is not sent.
*     Not sure because it is apparently ok right now.  Will pull the plug
*     and try resetting in a minute.  But first, the API is shaping up
*     in the tx.
*       mvsp->ecc
*        DoLapj(LAPJCMD_TXNEWDATA, buf, len) transmit a buffer
*        DoLapj(LAPJCMD_UARTEMPTY, NULL, NULL) forward the uart signal
*        CanEccTxAccept(pEcc) query if we can write
*       ecc->uart
*         isTxDevEmpty(pEcc->DiD) query if the uart is empty
*         try_writeDev(pEcc->DiD, pPkt->pData, pPkt->len) write to uart
*         (framed mode, this calls a bunch of stuff and writes directly into the buffer)
*         try_writeFrame(did,jskbuf,pecc)
*   - Ok, tried a power cycle and it still working, I'll freeze this then start
*     creating a modular verson with an API
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

extern int lapjStuffAFrame(t_LAPJFRAMER* fp,int bypass);
extern int try_writeFrame(int did, t_lapj_buf* pB, t_SIMECC* pEcc);
extern int UpdateVa(int Nr, t_SIMECC* pEcc);
extern int DoLogLapj(t_SIMECC* pEcc, int msg, int a1, int a2, int a3);
extern int lapjMonitorBuffer(t_SIMECC* pEcc, U8* pBufin, int nBuf);
extern int EnqueueCtlPkt(int type, t_SIMECC* pEcc, U8* pPayload, int nPayload);
extern int FlushSendQueue(t_SIMECC* pEcc);
extern int FlushRcvQueue(t_SIMECC* pEcc);
extern int FlushSvQueue(t_SIMECC* pEcc);
extern int StartT401(t_SIMECC* pEcc);
extern int StopT401(t_SIMECC* pEcc);
extern int LJA_ResetReconnect(struct S_SIMECC* pEcc, int Kin);

//======== FSM externs
extern t_FSMEVT LJfsm_LCONNECT;
extern t_FSMEVT LJfsm_n401;
extern t_FSMEVT LJfsm_n402;
extern t_FSMEVT LJfsm_n403;
extern t_FSMEVT LJfsm_n405;
extern t_FSMEVT LJfsm_RXConn;
extern t_FSMEVT LJfsm_RXUA;
extern t_FSMEVT LJfsm_LDISC;
extern t_FSMEVT LJfsm_RXDisc;
extern t_FSMEVT LJfsm_RXSeqChk;

/************************************************************
 * End of Temporary to compile
************************************************************/
int jlpdebug1 = 0;
/*
**************************************************************************
*
*  FUNCTION: int InitialiseECC(t_SIMECC* pEcc)
*
*  PURPOSE: Rebuild the internal buffers (sans pointers)
*
*  ARGUMENTS:
*
*  RETURNS:
*    Type
*
*  COMMENTS:
*   Called on an Init and a transition to disconnect, build the variables
**************************************************************************
*/
int InitialiseECC(t_SIMECC* pEcc)
{
    t_DQue* pQ;
    int     k;

//    pEcc->DiD       = DiD;
    pEcc->id        = 0;

    //======== Set flags for no LAPJ
    pEcc->flgs      =   LAPJFLAG_AUTORESP * 1       // respond to a SABM request
                      + LAPJFLAG_CONMODE2 *0        // na
                      + LAPJFLAG_CONMODE4 *0        // na
                      + LAPJFLAG_CONRETRY * 0       // retry connect
                      + LAPJFLAG_NOMON * 0          // Monitor is active
                      + LAPJFLAG_UNONLY * 1         // Accept unnumbered frames only
                      + LAPJFLAG_RELEASE * 0        // toss rx released packets
                      + LAPJFLAG_RXRAW * 1          // forward raw packets
                      + LAPJFLAG_TXBYPASS * 1       // transmit bypass
                      + LAPJFLAG_NOFRAME * 0        // debug of buffering off
                    ;

    //======== clear lapj state
    pEcc->Va        = 0;
    pEcc->Vr        = 0;
    pEcc->Vs        = 0;
    pEcc->Vc        = 0;
    pEcc->lapjstate = LAPJSTATE_DISC;
    pEcc->M         = MAXMOD42;
    pEcc->W         = 6 + 0*LAPJ_WINDOW;

    //======== reset timers
    pEcc->TicTimer         = 1;
    pEcc->sParam.T401      = 5;                     // 500 ms retransmission
    pEcc->sParam.T402      = 0;                     // disable auto retry
    pEcc->sParam.T403      = 10*60*2;               // 2 minutes
    pEcc->sParam.T405      = 0;                     // Idle time before a reconnect
    pEcc->sParam.N404      = LAPJDATAMAX;           // Set our data to max
    pEcc->sParam.N400      = 120;                   // Retransmissions (60 sec)

    //======== Flush queues
    FlushSvQueue(pEcc);
    FlushSendQueue(pEcc);
    FlushRcvQueue(pEcc);

    //========= Free any framer jsk buffer
    if(pEcc->pRxDf)
    {
        JskFreeBuf(pEcc->pJskMem, pEcc->pRxDf);
        pEcc->pRxDf = 0;
    }

    //========= Clear the framers
    memset(&pEcc->sF,0, sizeof(t_LAPJFRAMER));
    memset(&pEcc->sFRX,0, sizeof(t_LAPJFRAMER));

    //======== set up timers for auto retry
    pEcc->n400      = 0;
    pEcc->n401      = 0;
    pEcc->n402      = 10;         // first connect is fast
    pEcc->n403      = pEcc->TicTimer + pEcc->sParam.T403;
    pEcc->n405      = 0;   // 0* disconect
#if (EASYWINDEBUG>0) && (LAPJDEBUG>1) && 0
printf("InitialiseECC.end\r\n");
#endif
#if (EASYWINDEBUG>0) && (LAPJDEBUG>1)  && 0
{
if(jlpdebug1)
{
printf("InitialiseECC.end.2\r\n");
}
}
#endif

    return 0;
}

/*
**************************************************************************
*
*  FUNCTION: int FlushSendQueue(t_SIMECC* pEcc)
*
*  PURPOSE: Free all send packets
*
*  ARGUMENTS:
*
*  RETURNS:
*    Type
*
*  COMMENTS:
*   Called on an Init and a transition to disconnect, build the variables
**************************************************************************
*/
int FlushSendQueue(t_SIMECC* pEcc)
{
    int k;
    for(k=0; k<MAXMOD42; k++)
    {
        if(pEcc->aSend[k])
        {
            JskFreeBuf(pEcc->pJskMem, pEcc->aSend[k]);
            pEcc->aSend[k] = 0;
        }
    }
    return 0;
}
/*
**************************************************************************
*
*  FUNCTION: int FlushRcvQueue(t_SIMECC* pEcc)
*
*  PURPOSE: Free all Rcv packets
*
*  ARGUMENTS:
*
*  RETURNS:
*    Type
*
*  COMMENTS:
*   Called on an Init and a transition to disconnect, build the variables
**************************************************************************
*/
int FlushRcvQueue(t_SIMECC* pEcc)
{
    int k;
    for(k=0; k<MAXMOD42; k++)
    {
        if(pEcc->aRcv[k])
        {
            JskFreeBuf(pEcc->pJskMem, pEcc->aRcv[k]);
            pEcc->aRcv[k] = 0;
        }
    }
    return 0;
}
/*
**************************************************************************
*
*  FUNCTION: int FlushSvQueue(t_SIMECC* pEcc)
*
*  PURPOSE: Free all Sv packets
*
*  ARGUMENTS:
*
*  RETURNS:
*    Type
*
*  COMMENTS:
*   Called on an Init and a transition to disconnect, build the variables
**************************************************************************
*/
int FlushSvQueue(t_SIMECC* pEcc)
{
    t_DQue* pQ;

    pQ = &(pEcc->svHdrQ);
    if(!pQ->next)
    {
        pQ->next = pQ->prev = pQ;
    }
    while((t_DQue*)k_dequeue(pQ->next));
    return 0;
}

/*
**************************************************************************
*
*  FUNCTION: int ResetStateV(t_SIMECC* pEcc)
*
*  PURPOSE: Reset the state variables
*
*  ARGUMENTS:
*
*  RETURNS:
*    Type
*
*  COMMENTS:
*   Called on an Init and a transition to disconnect, build the variables
**************************************************************************
*/
void ResetStateV(t_SIMECC* pEcc)
{
    pEcc->Vs = 0;
    pEcc->Vr = 0;
    pEcc->Va = 0;
    pEcc->Venq = 0;
    pEcc->Vc = 0;
}

/*
**************************************************************************
*
*  FUNCTION: int ClearLapjStats(t_SIMECC* pEcc)
*
*  PURPOSE: Clear the statistocs
*
*  ARGUMENTS:
*
*  RETURNS:
*    Type
*
*  COMMENTS:
*
**************************************************************************
*/
int ClearLapjStats(t_SIMECC* pEcc)
{
    memset(&(pEcc->sStatus), 0, sizeof(struct S_LAPJSTATUS));
    return 1;
}

//======== (x+y) modM this is overkill
 int AddMod(int x, int y, int M)
{
    int retval;
    if(M < 0)
        M = -M;
    retval = x+y;
    while(retval < 0)
        retval += M;
    while(retval >= M)
        retval -= M;
    return retval;
}
//======== (x-y) modM so is this
 int SubMod(int x, int y, int M)
{
    int retval;
    if(M < 0)
        M = -M;
    retval = x-y;
    while(retval < 0)
        retval += M;
    while(retval >= M)
        retval -= M;
    return retval;
}

/*
**************************************************************************
*  FUNCTION: int try_writeFrame(int did, t_lapj_buf* pB, t_SIMECC* pEcc)
*
*  PURPOSE: Try to write a device
*
*  ARGUMENTS:
*   did     device
*   pB      jsk buffer
*
*  RETURN:
*   1 success
*   0 Fail
*
*  COMMENTS:
*   This routine will check the device (UART0) buffer for empty
*   if true, it will frame the packet and write it into the actual
*   uart buffer.  The t_lapj_buf must have had the header (byte)
*   allready written to pData-NLAPFDHROFFSET
**************************************************************************
*/
#if (EASYWINDEBUG>0) && (LAPJDEBUG>1) && 1
static void scanframe(U8*pBuf, int nBuf)
{
    int k;
    for(k=0; k<nBuf;k++)
    {
        if(pBuf[k] == 0x1c)
        {
jlpDumpFrame("scanframe: bad",pBuf, nBuf);
        }
    }
}
#endif
extern struct S_CHARQUEUE* GetVspOutQ(int);

int try_writeFrame(int did, t_lapj_buf* pB, t_SIMECC* pEcc)
{
    int     k;
    int     savesr;
    int     retval = 0;

#if (EASYWINDEBUG>0) && (LAPJDEBUG>3)  && 0
{
printf("try_writeFrame.1 \n");
}
#endif
    k = pEcc->IsTxUartEmpty(pEcc->idTxUart);
#if (EASYWINDEBUG>0) && (LAPJDEBUG>3)  && 0
{
printf("try_writeFrame.2 %d \n",k);
}
#endif
    if(k)
    {
        //======== Frame the packet into the output buffer
        pEcc->sF.Bufin = &(pB->pData[-NLAPJDHROFFSET]);  // include the lapj header
        pEcc->sF.szBufin = NLAPJDHROFFSET + pB->len;  // adjust the count
        pEcc->sF.cBufin = 0;                    // start input
        pEcc->sF.Bufout = pEcc->pJskMem->pTxTmpBuf;      // output to the uart
        pEcc->sF.szBufOut = 2*NLAPJBUFSIZE;             // this is how big it is
        pEcc->sF.cBufout = 0;                   // start at 0
        lapjStuffAFrame(&(pEcc->sF), pEcc->flgs&LAPJFLAG_NOFRAME); // run the framer

#if _EVB && (LAPJDEBUG>1) && 0 // Good tx before frame
DoLogLapj(pEcc, 210, (pEcc->sF.Bufin[0]), (pEcc->sF.Bufin[1]), (pEcc->sF.Bufin[2]));
#endif
        //======== Callback to the Uart
        pEcc->WriteToUart(pEcc->idTxUart, pEcc->sF.Bufout, pEcc->sF.cBufout);
        pEcc->sStatus.nTxTotal += pEcc->sF.cBufout;         // inc the byte count

#if _EVB && (LAPJDEBUG>1) && 1 // Good tx after frame  211 CTL, LEN, CRC
DoLogLapj(pEcc, 211, (pEcc->sF.Bufout[1]), (pEcc->sF.cBufout), (pEcc->sF.Bufout[pEcc->sF.cBufout-2]));
#endif

#if (EASYWINDEBUG>0) && (LAPJDEBUG>1)  && 1
{
U8 c=(U8)pEcc->sF.Bufout[1];
U8 crc;

if(c==LAPJXDLE)                             // escape for debug
    c=(U8)(0x20^pEcc->sF.Bufout[2]);
crc = (pEcc->sF.Bufout[pEcc->sF.cBufout-2]);
printf("try_writeFrame: type:%s Len:%d CRC 0x%02x\r\n",GetLapPktString(c),pEcc->sF.cBufout,  crc);
scanframe(pEcc->sF.Bufout, pEcc->sF.cBufout);
}
#endif
#if (EASYWINDEBUG>0) && (LAPJDEBUG>4)  && 0
{
jlpDumpFrame("try_writeFrame",pEcc->sF.Bufout, pEcc->sF.cBufout);
}
#endif

        if( ! (pB->flgs & LAPJ_BUF_KEEP))
            JskFreeBuf(pEcc->pJskMem,pB);
        retval = 1; // 0 length writes must still return true
    }
    else
    {
        retval = 0;
    }

    return retval;
}

/*
**************************************************************************
*
*  FUNCTION:  int ParseInputPkt(t_lapj_buf* pB)
*
*  PURPOSE: Parse a raw packet
*
*  ARGUMENTS:
*
*  RETURNS:
*    Type
*
*  COMMENTS:
*   Give a packet that has passed length and CRC checks, compute elements
*   into the t_lapj_buf structure for use by the receiver.  This is dependent on
*   proper alignment
**************************************************************************
*/
 int ParseInputPkt(t_lapj_buf* pB)
{

    U8  frmh;

    frmh = pB->pData[-NLAPJDHROFFSET];          // extract the header
//    pB->len = pB->len - 0;                      // Length was adjusted before arrival

    if(frmh < FRAMETYPE_RR)
    {
        pB->type = FRAMETYPE_DATA;              // data frame
        pB->Ns = 0x0f & (frmh>>4);
        pB->Nr = 0x0f & (frmh);
    }
    else if(frmh < FRAMETYPE_CONN)              // numbered supervisory
    {
        pB->type = 0xf0 & frmh;
        pB->Nr = 0x0f & (frmh);
        pB->Ns = 0x0f;                          // illegal
    }
    else                                        // Unumbered
    {
        pB->type = frmh;
        pB->Nr = 0x0f;
        pB->Ns = 0x0f;
    }

    return pB->type;
}
/*
**************************************************************************
*
*  FUNCTION:  int StartT401(t_SIMECC* pEcc)
*
*  PURPOSE: Start T401
*
*  ARGUMENTS:
*
*  RETURNS:
*    Type
*
*  COMMENTS:
*   Used mostly for documentation ourposes
**************************************************************************
*/
int StartT401(t_SIMECC* pEcc)
{
    pEcc->n401 = pEcc->TicTimer + pEcc->sParam.T401; // start the timer
    pEcc->n400 = 0;                                  // clear retry count

#if (EASYWINDEBUG>0) && (LAPJDEBUG>0)  && 1
{
printf("StartT401: \r\n");
}
#endif
    return 1;
}
/*
**************************************************************************
*
*  FUNCTION:  int StopT401(t_SIMECC* pEcc)
*
*  PURPOSE: Stop T401
*
*  ARGUMENTS:
*
*  RETURNS:
*    Type
*
*  COMMENTS:
*   Used mostly for documentation ourposes
**************************************************************************
*/
int StopT401(t_SIMECC* pEcc)
{
    pEcc->n401 = 0;
#if (EASYWINDEBUG>0) && (LAPJDEBUG>0)  && 1
{
printf("StopT401: \r\n");
}
#endif
    return 1;
}

/*
**************************************************************************
*
*  FUNCTION:  int MakeDataPkt(t_lapj_buf* pPkt)
*
*  PURPOSE: Place a data header on a packet
*
*  ARGUMENTS:
*
*  RETURNS:
*    Type
*
*  COMMENTS:
*   This places a data header on the packet.  The start of the packet
*   remains the same but when sending, the packet has to start one byte
*   earlier and have one byte added.  The reason the paket itself is not
*   adjusted is because retransmitions reuse a t_lapj_buf.
*   A data header is of the form
*       ssssrrrr
*   where ssss < 1011
**************************************************************************
*/
 int MakeDataPkt(t_lapj_buf* pPkt)
{
    U8* pH = pPkt->pData-1;
    *pH = (pPkt->Ns<<4) +  (pPkt->Nr & 0x0f);
    return *pH;
}

/*
**************************************************************************
*
*  FUNCTION:  int MakeSupervPkt(t_lapj_buf* pPkt, int S)
*
*  PURPOSE: Place a supervisory header on a packet
*
*  ARGUMENTS:
*
*  RETURNS:
*    Type
*
*  COMMENTS:
*   This places a supervisory header on the packet.  The start of the packet
*   remains the same but when sending, the packet has to start one byte
*   earlier and have one byte added.  The reason the paket itself is not
*   adjusted is because retransmitions reuse a t_lapj_buf.
*   A supervisory header is of the form
*       ssssrrrr
*   where ssss ==
*       10110000 FRAMETYPE_RR
*       11000000 FRAMETYPE_RNR
*       11010000 FRAMETYPE_REJ
*       11100000 FRAMETYPE_SREJ (not implemented)
**************************************************************************
*/
 int MakeSupervPkt(t_lapj_buf* pPkt, int S)
{
    U8* pH = pPkt->pData-1;
    *pH = (S & 0xf0) +  (pPkt->Nr & 0x0f);
    return *pH;
}
/*
**************************************************************************
*
*  FUNCTION:  int MakeUnPkt(t_lapj_buf* pPkt, int S)
*
*  PURPOSE: Place a unnumbered header on a packet
*
*  ARGUMENTS:
*
*  RETURNS:
*    Type
*
*  COMMENTS:
*   This places a unnumbered header on the packet.  The start of the packet
*   remains the same but when sending, the packet has to start one byte
*   earlier and have one byte added.  The reason the paket itself is not
*   adjusted is because retransmitions reuse a t_lapj_buf.
*   A supervisory header is of the form
*       1111uuuu
*   where uuuu ==
*       0001 FRAMETYPE_CONN
*       0010 FRAMETYPE_DISC
*       0011 FRAMETYPE_DM
*       other codes are not implemented
**************************************************************************
*/
 int MakeUnPkt(t_lapj_buf* pPkt, int S)
{
    U8* pH = pPkt->pData-1;
    *pH = 0xf0 | S;                         // debug force UNum frame
    return *pH;
}

/*
**************************************************************************
*
*  FUNCTION:  int EnqueueCtlPkt(int type, t_SIMECC* pEcc)
*
*  PURPOSE: Create and enqueue a control packet
*
*  ARGUMENTS:
*
*  RETURNS:
*   0 if packet is not sent
*
*  COMMENTS:
*   We keep a linked list  of packets for convience. At most there should only
*   a single outstanding packet so we can borrow a full t_lapj_buf. If there
*   is more than one, it probably happens during the conn/disc or other
*   transient cases.  We should have enough buffers to pass through it.
*   The type of the packet is recorded
*
**************************************************************************
*/
int EnqueueCtlPkt(int type, t_SIMECC* pEcc, U8*pPayload, int nPayload)
{
    t_lapj_buf* pB;
    t_DQue* e = NULL;

    pB = JskAllocBuf(pEcc->pJskMem,OWNER_EnqueueCtlPkt);
    if(!pB)
        return -1;

    pB->Nr = pEcc->Vr;
    pB->Ns = pEcc->Vs;
    pB->type = type;
    memcpy(pB->pData, pPayload, nPayload);
    pB->len = nPayload;
#if (EASYWINDEBUG>0) && (LAPJDEBUG>1)  && 1
{
    U8 tmpC;
    tmpC = type;
printf("EnqueueCtlPkt: Basetype=%s\r\n",GetLapPktString(tmpC));
}
#endif

    e = &(pEcc->svHdrQ);

#if (EASYWINDEBUG>0) && (LAPJDEBUG>4)  && 0
{
printf("EnqueueCtlPkt Before pQ: 0x%06x &(pB->lnk) 0x%0x6\r\n",e,&(pB->lnk) );
jlpFunction12(NULL,NULL,NULL,NULL);
}
#endif
    e = (t_DQue*)k_enqueue(e,&(pB->lnk));
#if (EASYWINDEBUG>0) && (LAPJDEBUG>4)  && 0
{
printf("EnqueueCtlPkt After pQ: 0x%06x\r\n",e);
jlpFunction12(NULL,NULL,NULL,NULL);
}
#endif
    return 0;
}
/*
**************************************************************************
*
*  FUNCTION:  int TryEccTxSuperv(t_SIMSIDE* pSide, t_SIMECC* pEcc)
*
*  PURPOSE: Try to send A supervisory packet
*
*  ARGUMENTS:
*
*  RETURNS:
*   0 if packet is not sent
*
*  COMMENTS:
*   We keep a linked list  of packets for convience. At most there should only
*   a single outstanding packet so we can borrow a full t_lapj_buf
*
**************************************************************************
*/
 int TryEccTxSuperv(t_SIMECC* pEcc)
{
    int     retval;
    int     tryvar;
    t_lapj_buf* pB;
    t_DQue* pQ = &(pEcc->svHdrQ);

    pQ = (t_DQue*)k_dequeue(pQ->next);

    pB = (t_lapj_buf*)pQ;
    if(pB)
    {

        pB->Nr = pEcc->Vr;                      // Update Nr before sending   8.4.3.1
        if(pB->type <= (FRAMETYPE_CONN-1))      // type of packet
        {
            MakeSupervPkt(pB,pB->type);
            pEcc->Vc = 0;                       // reset any pending sequence number
        }
        else
        {
            MakeUnPkt(pB,pB->type);
        }
#if (EASYWINDEBUG>0) && (LAPJDEBUG>1)  && 1
{
printf("TryEccTxSuperv pQ: 0x%06x type:%s\r\n",pQ,GetLapPktString(pB->pData[-1]));
}
#endif
        pB->flgs &= ~LAPJ_BUF_KEEP;             // will free after sending
        tryvar = try_writeFrame(pEcc->DiD, pB, pEcc);

        retval = 1;
    }
    else
        retval = 0;

    return retval;
}

/*
**************************************************************************
*
*  FUNCTION:  int TryEccTxAck(t_SIMSIDE* pSide, t_SIMECC* pEcc)
*
*  PURPOSE: Test if we should sent an Ack (RR or RNR)
*
*  ARGUMENTS:
*
*  RETURNS:
*   0 if packet is not sent
*
*  COMMENTS:
*   Called when there is no data to send
**************************************************************************
*/
 int TryEccTxAck(t_SIMECC* pEcc)
{
    int     retval;

    if(1 && (pEcc->Vc))
    {

        EnqueueCtlPkt(FRAMETYPE_RR, pEcc, "_RR", 3);
        pEcc->Vc = 0;                   // clear any pending sequence number
        retval = 1;
    }
    else
        retval = 0;

    return retval;
}

/*
**************************************************************************
*
*  FUNCTION:  int TryEccTxData(t_SIMECC* pEcc)
*
*  PURPOSE: Try to send from our queue
*
*  ARGUMENTS:
*
*  RETURNS:
*   0 if packet is not sent
*
*  COMMENTS:
*
**************************************************************************
*/
int TryEccTxData(t_SIMECC* pEcc)
{
    int     retval = 0;
    int     tryvar = 0;
    int     ourW;
    int     ourQ;

    //======== Check the enable
    if(pEcc->flgs & LAPJFLAG_TXNODATA)
    {
        return 0;
    }

    //======== Can we send a packet?
    ourQ = SubMod(pEcc->Venq, pEcc->Vs, pEcc->M); // Queued but not sent
    ourW = SubMod(pEcc->Vs, pEcc->Va, pEcc->M);   // window 8.4.1
    if((ourQ>0) && (ourW < pEcc->W))
    {
        int     n = pEcc->Vs;
        t_lapj_buf* pPkt = pEcc->aSend[n];      // We want to send this one

        //======== is framing disabled? Left from debug
        if( (pEcc->flgs & LAPJFLAG_NOFRAME) == LAPJFLAG_NOFRAME)
        {
            tryvar = pEcc->WriteToUart(pEcc->idTxUart, pPkt->pData, pPkt->len);
DoLogLapj(pEcc, 102, tryvar, n, 0);
#if (EASYWINDEBUG>0) && (LAPJDEBUG>1)  && 1
printf("TryEccTxData.noFrame:tryvar %08x\n",tryvar);
#endif
#if _EVB && (LAPJDEBUG>1) && 1
        DoLogLapj(pEcc, 101, ourQ, ourW, tryvar);
#endif
            if(tryvar)
            {

                //======== Increment our sent packet
                pEcc->Vs = AddMod(pEcc->Vs, 1, pEcc->M);    //8.4.1

                //======== Ack Ourselves
                pEcc->Vr = pEcc->Vs;

                //======== update
                UpdateVa(pEcc->Vr, pEcc);

                pEcc->LapjCanSend(pEcc->idLapj,1);
                retval = 1;
            }
        }
        else  // nope, do lapj framing
        {
            pPkt->Nr = pEcc->Vr;            // Update Nr before sending   8.4.3.1
            MakeDataPkt(pPkt);              // make the frame
            pPkt->flgs |= LAPJ_BUF_KEEP;    // will free after an ack
            tryvar = try_writeFrame(pEcc->DiD, pPkt, pEcc);
#if (EASYWINDEBUG>0) && (LAPJDEBUG>3)  && 0
{
jlpDumpFrame("TryEccTxData.Framed",pPkt->pData,pPkt->len);
}
#endif
#if _EVB && (LAPJDEBUG>1) && 1
        DoLogLapj(pEcc, 103, ourQ, ourW, tryvar);
#endif

            if(tryvar)
            {
                //======== Start Timer T401
                StartT401(pEcc);
                pEcc->Vc = 0;                   // sent any pending sequence number ?

                //======== Increment our sent packet
                pEcc->Vs = AddMod(pEcc->Vs, 1, pEcc->M);    //8.4.1

                retval = 1;
            }
        }
    }
    else
    {
        retval = 0;
    }

    return retval;
}

/*
**************************************************************************
*
*  FUNCTION: int CanEccTxAccept(t_SIMECC* pEcc)
*
*  PURPOSE: See if we can accept a packet from the interface
*
*  ARGUMENTS:
*
*  RETURNS:
*   0 if packet is not accepted
*
*  COMMENTS:
*
**************************************************************************
*/
int CanEccTxAccept(t_SIMECC* pEcc)
{
    int     retval;
    int     tryvar;
    int     ourW;
    int     ourQ;

#if (EASYWINDEBUG>0) && (LAPJDEBUG>4)  && 0
{
printf("CanEccTxAccept flgs=0x%04x\r\n",pEcc->flgs);
}
#endif
    //======== Can we accept a packet?
    if((pEcc->flgs & LAPJFLAG_TXBYPASS) || (pEcc->flgs & LAPJFLAG_TXNODATA))
    {
        retval = pEcc->IsTxUartEmpty(pEcc->idTxUart);
    }
    else
    {
        ourW = SubMod(pEcc->Venq, pEcc->Va, pEcc->M);  // window
        if((ourW <= pEcc->W))
        {
            retval = 1;
        }
        else
            retval = 0;
    }
    return retval;
}
/*
**************************************************************************
*
*  FUNCTION: int TryEccTxAccept(t_SIMECC* pEcc, U8* pBuf, int nbuf)
*
*  PURPOSE: Allocate a t_lapj_buf and copy the data
*
*  ARGUMENTS:
*
*  RETURNS:
*   0 if packet is not accepted
*
*  COMMENTS:
*   Note, the transparent mode is a spagetti right now. Win98 seems to want
*   the buffer to be constant while sending, WinXP and muxevb don't care.
**************************************************************************
*/
int TryEccTxAccept(t_SIMECC* pEcc, U8* pBuf, int nBuf)
{
    int     retval;
    int     tryvar;
    int     ourW;
    int     ourQ;
    t_lapj_buf* pB;


    //======== Can we accept a packet?
    if(CanEccTxAccept(pEcc))
    {
        //========
        if((pEcc->flgs & LAPJFLAG_TXBYPASS) || (pEcc->flgs & LAPJFLAG_TXNODATA)) // bypass mode
        //========
        {
            //========== Copy HACK for transparent mode quicj fix/test
            memcpy(pEcc->pJskMem->pTxTmpBuf,pBuf,nBuf);
            retval = pEcc->WriteToUart(pEcc->idTxUart, pEcc->pJskMem->pTxTmpBuf, nBuf);
        }
        //========
        else  // buffered mode
        //========
        {
            //======== Limit our packet size
            if(nBuf >= LAPJDATAMAX)
                nBuf = LAPJDATAMAX;

            //======== Allocate a buffer
            pB = JskAllocBuf(pEcc->pJskMem,OWNER_TryEccTxAccept);
            memcpy(pB->pData, pBuf, nBuf);
            pB->len = nBuf;
DoLogLapj(pEcc, 106, (int)pB, nBuf, 0);

            //======== accept and Number the packet
            pEcc->aSend[pEcc->Venq] = pB;
            pB->Ns = pEcc->Venq;                 // Send sequence number for this packet

            //======== Increment our look ahead
            pEcc->Venq = AddMod(pEcc->Venq, 1, pEcc->M);
            retval = nBuf;
        }
    }
    else
        retval = 0;

    return retval;
}
/*
**************************************************************************
*
*  FUNCTION: int TryEccTx(t_SIMSIDE* pSide, t_SIMECC* pEcc)
*
*  PURPOSE: Simulate Tx processing a new packet
*
*  ARGUMENTS:
*
*  RETURNS:
*   0 if packet is not sent
*
*  COMMENTS:
*   This is for new packets. We haven't got to retransmitting
*   or control packets yet.
*
*   This is like a polling approach. Every call we compute
*   what the parameters are for the call and determine what
*   (if any) to send. Not the most elegent way, but it might work for now
*   and also unccover logic hidden by a differenc approach
*
**************************************************************************
*/
int TryEccTx(t_SIMECC* pEcc)
{
    int     retval=0;

    //======== Can we send anything
    if(pEcc->IsTxUartEmpty(pEcc->idTxUart))
    {

        //======== Is there a supervisory packet
        retval = TryEccTxSuperv(pEcc);
        if(retval)
            goto getout;

        //======== Try a data packet ... need to do RNR check here
        retval = TryEccTxData(pEcc);
        if(retval)
            goto getout;

        //======== Do we need to Ack
        retval = TryEccTxAck(pEcc);
        if(retval)
            goto getout;

    }
    else
    {
        DoLogLapj(pEcc, 100, 0, 0, 0);
    }

getout:
    return retval;
}

/*
**************************************************************************
*
*  FUNCTION: int UpdateVa(t_SIMSIDE* pSide, t_SIMECC* pEcc, int Nr)
*
*  PURPOSE: Logic to update Va and start timer T401
*
*  ARGUMENTS:
*
*  RETURNS:
*    Count of released packets
*
*  COMMENTS:
*   Nr is an ack for all frames local has transmitted up to and including Nr-1.
All I frames and supervisory frames contain N(R), the expected send sequence number of the next
received I frame. At the time that a frame of the above types is designated for transmission, the
value of N(R) is set equal to V(R). N(R) indicates that the error-correcting entity transmitting the
N(R) has correctly received all I frames numbered up to and including N(R) - 1
*   That means the remote has not received Nr but is expecting it.
*
*   Each connection shall have an associated V(A) when using I frame commands and supervisory
frame commands/responses. V(A) identifies the last frame that has been acknowledged by its peer
(V(A) - 1 equals the N(S) of the last acknowledged I frame). V(A) can take on the value 0 through
n minus 1. The value of V(A) shall be updated by the valid N(R) values received from its peer (see
8.2.3.2.6). A valid N(R) value is one that is in the range V(A) = N(R) = V(S).
*   That means Va is the next frame we expect to be acknowledged
*   so we have to release all packets up to but not including Nr
*
*   On receipt of a valid I frame or an RR, RNR, or REJ supervisory frame, even in the own-receiver
busy or the timer recovery condition, the error-correcting entity shall treat the N(R) contained in
this frame as an acknowledgement for all the I frames it has transmitted with an N(S) up to and
including the received N(R) - 1. V(A) shall be set to N(R). The error-correcting entity shall stop the
timer T401 on receipt of a valid I frame or an RR, RNR, or REJ supervisory frame with the N(R)
higher than V(A) (actually acknowledging some I frames), or an REJ frame with an N(R) equal
to V(A). The error-correcting entity shall stop the timer T401 on receipt of an SREJ supervisory
frame with an N(R) equal to or higher than V(A), even though there is no acknowledgement
function associated with the N(R) contained in the SREJ frame.

**************************************************************************
*/
int UpdateVa(int Nr, t_SIMECC* pEcc)
{
    int k;

#if JLPOLDTX && 0
    k=SubMod(Nr,pEcc->Va,pEcc->M);
    if(k != 0)  // test actually Gt
    {
        StopT401(pEcc);                     // 8.4.3.2
    }
    pEcc->Va = Nr;                          // 8.4.3.2
#else
    //========
    if(Nr == pEcc->Va) // Check if our copy is the same?
    //========
    {
        goto UpdateVa_out;
    }

    //======== release Acked packets
    while(pEcc->Va != Nr)                   // Until Va reaches Nr but not including
    //========
    {
        k = pEcc->Va;                       // release the packet
        if((pEcc->aSend[k]))                // if it exists
        {
DoLogLapj(pEcc, 104, k, (int)pEcc->aSend[k], Nr);
            JskFreeBuf(pEcc->pJskMem, pEcc->aSend[k]);
            pEcc->aSend[k] = 0;
        }
        else
        {
DoLogLapj(pEcc, 105, k, pEcc->Va, Nr);
        }
        pEcc->Va = AddMod(pEcc->Va, 1, pEcc->M); // inc the Va
    }

UpdateVa_out:

    StopT401(pEcc);
    //======== Do we hace room to accept new data
    if(CanEccTxAccept(pEcc))
    {
        pEcc->LapjCanSend(pEcc->idLapj,1);
    }

    return 0;
#endif
}
/*
**************************************************************************
*
*  FUNCTION:  int TryEccRxDM(t_SIMSIDE* pSide, t_SIMECC* pEcc)
*
*  PURPOSE: Simulate Rx processing a DM
*
*  ARGUMENTS:
*
*  RETURNS:
*    Count of released packets
*
*  COMMENTS:
*   The remote is disconnected
*
**************************************************************************
*/
 int TryEccRxDM(t_lapj_buf* pB, t_SIMECC* pEcc)
{
    int     retval;

    retval = 1;
    return retval;
}
/*
**************************************************************************
*
*  FUNCTION:  int TryEccRxDISC(t_SIMSIDE* pSide, t_SIMECC* pEcc)
*
*  PURPOSE: Simulate Rx processing a DISC
*
*  ARGUMENTS:
*
*  RETURNS:
*    Count of released packets
*
*  COMMENTS:
*   DisConnect if connected
*
**************************************************************************
*/
 int TryEccRxDISC(t_lapj_buf* pB, t_SIMECC* pEcc)
{
    int     retval;

    doLapjFSM(pEcc, &LJfsm_RXDisc, FRAMETYPE_DISC, FRAMETYPE_DISC);

    retval = 1;
    return retval;
}
/*
**************************************************************************
*
*  FUNCTION:  int TryEccRxCONN(t_SIMSIDE* pSide, t_SIMECC* pEcc)
*
*  PURPOSE: Simulate Rx processing a CONN (SABM)
*
*  ARGUMENTS:
*
*  RETURNS:
*    Count of released packets
*
*  COMMENTS:
*   Connect if disconnected
*
**************************************************************************
*/
 int TryEccRxCONN(t_lapj_buf* pB, t_SIMECC* pEcc)
{
    int     retval;

#if (EASYWINDEBUG>0) && (LAPJDEBUG>0)  && 1
{
printf("***********************RXCONN\r\n");
}
#endif
    doLapjFSM(pEcc, &LJfsm_RXConn, FRAMETYPE_CONN, FRAMETYPE_CONN);

    retval = 1;
    return retval;
}
/*
**************************************************************************
*
*  FUNCTION:  int TryEccRxCONN(t_SIMSIDE* pSide, t_SIMECC* pEcc)
*
*  PURPOSE: Simulate Rx processing a CONN (SABM)
*
*  ARGUMENTS:
*
*  RETURNS:
*    Count of released packets
*
*  COMMENTS:
*   Connect if disconnected
*
**************************************************************************
*/
 int TryEccRxUA(t_lapj_buf* pB, t_SIMECC* pEcc)
{
    int     retval;

#if (EASYWINDEBUG>0) && (LAPJDEBUG>0)  && 1
{
printf("***********************UA\r\n");
}
#endif
    doLapjFSM(pEcc, &LJfsm_RXUA, FRAMETYPE_UA, FRAMETYPE_UA);

    retval = 1;
    return retval;
}
/*
**************************************************************************
*
*  FUNCTION:  int TryEccRxREJ(t_SIMSIDE* pSide, t_SIMECC* pEcc)
*
*  PURPOSE: Simulate Rx processing a REJ
*
*  ARGUMENTS:
*
*  RETURNS:
*    Count of released packets
*
*  COMMENTS:
*   An REJ is an nack to use go-back-N retransmission
*   We just set the tx back but remain in data
*
**************************************************************************
*/
 int TryEccRxREJ(t_lapj_buf* pB, t_SIMECC* pEcc)
{
    int     retval;
    int     n;
    int     k;

    pEcc->Vs = pEcc->Va = pB->Nr;     // 8.4.4 a
    pEcc->sStatus.nRxREJ += 1;
    pEcc->sStatus.nRetrans += 1;
    retval = 1;
    return retval;
}
/*
**************************************************************************
*
*  FUNCTION:  int TryEccRxSREJ(t_SIMSIDE* pSide, t_SIMECC* pEcc)
*
*  PURPOSE: Simulate Rx processing a SREJ
*
*  ARGUMENTS:
*
*  RETURNS:
*    Count of released packets
*
*  COMMENTS:
*   An SREJ is a request for a single frame retransmission.  In V.42
*   there is an optional multiframe that we don't plan to use.  The single
*   frame mode is attractive fo our application where the UART in the PC
*   can occasionally loose characters.
*
**************************************************************************
*/
 int TryEccRxSREJ(t_lapj_buf* pB, t_SIMECC* pEcc)
{
    int     retval;

    retval = 1;
    return retval;
}
/*
**************************************************************************
*
*  FUNCTION:  int TryEccRxRNR(t_SIMSIDE* pSide, t_SIMECC* pEcc)
*
*  PURPOSE: Simulate Rx processing a RNR
*
*  ARGUMENTS:
*
*  RETURNS:
*    Count of released packets
*
*  COMMENTS:
*   An RR is an ack that sets the remote flow on
*
**************************************************************************
*/
 int TryEccRxRNR(t_lapj_buf* pB, t_SIMECC* pEcc)
{
    int     retval;
    int     n;
    int     k;

    UpdateVa(pB->Nr, pEcc);

    retval = 1;
    return retval;
}
/*
**************************************************************************
*
*  FUNCTION:  int TryEccRxRR(t_SIMSIDE* pSide, t_SIMECC* pEcc)
*
*  PURPOSE: Simulate Rx processing a RR
*
*  ARGUMENTS:
*
*  RETURNS:
*    Count of released packets
*
*  COMMENTS:
*   An RR is an ack that releases the remote flow off
*
**************************************************************************
*/
 int TryEccRxRR(t_lapj_buf* pB, t_SIMECC* pEcc)
{
    int     retval;
    int     n;
    int     k;

    UpdateVa(pB->Nr, pEcc);

    retval = 1;
    return retval;
}
/*
**************************************************************************
*
*  FUNCTION:  int TryEccRxReleasePkt(t_SIMSIDE* pSide, t_SIMECC* pEcc, t_DATASIGNAL* pPkt)
*
*  PURPOSE: Release a packet to the rxtask
*
*  ARGUMENTS:
*
*  RETURNS:
*    Count of released packets
*
*  COMMENTS:
*   This implementation assumes an infinatly fast data sink
*   Later, we have to consider flow control
*   Note: we are releasing a packet here.  But there is a future possibility
*   that we will add an additional output buffer.  Hence, "JskFreeBuff"
*   is scattered into each handler
*
**************************************************************************
*/
 int TryEccRxReleasePkt(t_lapj_buf* pB, t_SIMECC* pEcc)
{
    int     retval;
    if((pEcc->flgs & LAPJFLAG_RELEASE))
        pEcc->WriteToMvsp(pEcc->idMvsp, pB->pData, pB->len);
    JskFreeBuf(pEcc->pJskMem,pB);
    retval = 1;
    return retval;
}
/*
**************************************************************************
*
*  FUNCTION:  int TryEccRxData(t_SIMSIDE* pSide, t_SIMECC* pEcc)
*
*  PURPOSE: Simulate Rx processing a Datapacket
*
*  ARGUMENTS:
*
*  RETURNS:
*    Count of released packets
*
*  COMMENTS:
*   Called from the chain that gets uart rx packets. This process a data
*   packet
*
**************************************************************************
*/
 int TryEccRxData(t_lapj_buf* pB, t_SIMECC* pEcc)
{
    int     retval;
    int     n;
    int     k;
    int     ourW;

    //======== If this pkt is in our expected window, store it for later
    n = pB->Ns;
    ourW = SubMod(n, pEcc->Vr, pEcc->M);
    {
        if(ourW < pEcc->W)
            pEcc->aRcv[n] = pB;
    }

#if 1  // This logic is more efficient done here

    //======== This logic is easier done here
    if(pEcc->Vr == pB->Ns)                          // 8.2.3.2.5  The main test
    //========
    {
        TryEccRxReleasePkt((pEcc->aRcv[pEcc->Vr]),pEcc);
        pEcc->lapjstate = LAPJSTATE_DATA;
        pEcc->Vr = AddMod(pEcc->Vr,1,pEcc->M);      // 8.4.2
        pEcc->Vc = 1;                               // Set flag for RR
    }
    //========
    else if( (pEcc->lapjstate == LAPJSTATE_DATA))   // transition out of data mode
    //========
    {
        pEcc->lapjstate = LAPJSTATE_REJ;            // rej
        EnqueueCtlPkt(FRAMETYPE_REJ, pEcc, "_REJ1", 5); // tell remote
        pEcc->sStatus.nTxREJ += 1;                          // statistics
        pEcc->n400 = 0;                             // clear retry counter
        JskFreeBuf(pEcc->pJskMem,pB);               // conflict with use of aRcv (future)
   }
   else  // ignore the packet
   {
        JskFreeBuf(pEcc->pJskMem,pB);
   }

#endif

#if _EVB && (LAPJDEBUG>1) && 0
DoLogLapj(pEcc, 9, pEcc->lapjstate, pB->Ns, pEcc->Vr);
#endif
   //======== LAPM
    {
        UpdateVa(pB->Nr, pEcc);  // Nr the NEXT packet he expects to get
    }

    retval = 1;
    return retval;
}
/*
**************************************************************************
*
*  FUNCTION:  int TryEccRx(t_SIMSIDE* pSide, t_SIMECC* pEcc)
*
*  PURPOSE: Simulate Rx processing a packet
*
*  ARGUMENTS:
*
*  RETURNS:
*    Count of released packets
*
*  COMMENTS:
*   There is a packet available. We need to check the header and select
*   which processing routine to call
*
**************************************************************************
*/
 int TryEccRx(t_lapj_buf* pB, t_SIMECC* pEcc)
{
    int     retval = 1;
    int     n;
    int     k;


    n = ParseInputPkt(pB);
#if (EASYWINDEBUG>0) && (LAPJDEBUG>1)  && 1
{
printf("TryEccRx: lapjstate=%d type=%s\r\n",pEcc->lapjstate, GetLapPktString(pB->pData[-1]));
}
#endif
#if _EVB && (LAPJDEBUG>1) && 0
DoLogLapj(pEcc, 8, pEcc->lapjstate, n, 0);
#endif

    switch(n)
    {
        //========
        case FRAMETYPE_DATA:  // Data packet
        //========
        {
            if( !(pEcc->flgs & LAPJFLAG_UNONLY))
                retval = TryEccRxData(pB, pEcc);
            else
                JskFreeBuf(pEcc->pJskMem,pB);
        }
        break;

        //========
        case FRAMETYPE_RR:  // Usually an ACK
        //========
        {
            if( !(pEcc->flgs & LAPJFLAG_UNONLY))
                retval = TryEccRxRR(pB, pEcc);
            JskFreeBuf(pEcc->pJskMem,pB);
        }
        break;

        //========
        case FRAMETYPE_RNR: // Flow OFF
        //========
        {
            if( !(pEcc->flgs & LAPJFLAG_UNONLY))
                retval = TryEccRxRNR(pB, pEcc);
            JskFreeBuf(pEcc->pJskMem,pB);
        }
        break;

        //========
        case FRAMETYPE_REJ: // V42= go back N
        //========
        {
            if( !(pEcc->flgs & LAPJFLAG_UNONLY))
                retval = TryEccRxREJ(pB, pEcc);
            JskFreeBuf(pEcc->pJskMem,pB);
        }
        break;

        //========
        case FRAMETYPE_SREJ: // V42 can be single or multiple
        //========
        {
            if( !(pEcc->flgs & LAPJFLAG_UNONLY))
                retval = TryEccRxSREJ(pB, pEcc);
            JskFreeBuf(pEcc->pJskMem,pB);
        }
        break;

        //========
        case FRAMETYPE_CONN: // v42 sabme
        //========
        {
            retval = TryEccRxCONN(pB, pEcc);
            JskFreeBuf(pEcc->pJskMem,pB);
        }
        break;

        //========
        case FRAMETYPE_UA: // v42 UA
        //========
        {
            retval = TryEccRxUA(pB, pEcc);
            JskFreeBuf(pEcc->pJskMem,pB);
        }
        break;

        //========
        case FRAMETYPE_DISC: //
        //========
        {
            retval = TryEccRxDISC(pB, pEcc);
            JskFreeBuf(pEcc->pJskMem,pB);
        }
        break;

        //========
        case FRAMETYPE_DM: // Other side is disconnected
        //========
        {
            retval = TryEccRxDM(pB, pEcc);
            JskFreeBuf(pEcc->pJskMem,pB);
        }
        break;

        //========
        default:
        //========
            retval = 0;
            JskFreeBuf(pEcc->pJskMem,pB);
            break;
    }

    return retval;
}
/*
**************************************************************************
*
*  FUNCTION:  int doEccCom(t_SIMSIDE* pSide, t_SIMECC* pEcc)
*
*  PURPOSE: Simulate Counter and common processing a packet
*
*  ARGUMENTS:
*
*  RETURNS:
*    1
*
*  COMMENTS:
*
*   Timer managenent:
*   SysTimer ticks each time this is called
*   if a timer is not 0, it is checked
*   T401  Wait for a positive ack (data mode)
*   T402  Retry for connect (disc mode)
*   T403  Disconnect while waiting (trycon,rej) (usually = N*T402)
*
**************************************************************************
*/
 int doEccCom(t_SIMECC* pEcc)
{
    int retval = 1;
    //======== Increment the timer: wired to 100ms right bow
    pEcc->TicTimer += 1;

    //======== n401 processing retransmit timer

    if((pEcc->n401>0) && (pEcc->TicTimer > pEcc->n401))
    {
        pEcc->n401 = 0;
        doLapjFSM(pEcc, &LJfsm_n401, 41, 401);
    }


    //======== n402 Disconnect repeat attempt
    if((pEcc->n402>0) && (pEcc->TicTimer > pEcc->n402))
    {
        pEcc->n402 = 0;
        doLapjFSM(pEcc, &LJfsm_n402, 42, 402);
    }


    //======== n403 disconnect
    if((pEcc->n403>0) && (pEcc->TicTimer > pEcc->n403))
    {
        pEcc->n403 = 0;
        doLapjFSM(pEcc, &LJfsm_n403, 43, 403);
    }

    //======== n405 reconnect
    if((pEcc->n405>0) && (pEcc->TicTimer > pEcc->n405))
    {
        pEcc->n405 = 0;
        doLapjFSM(pEcc, &LJfsm_n405, 45, 405);
    }
    return retval;
}

/*
**************************************************************************
*
*  FUNCTION: int DoLapj(t_SIMECC* pEcc, intptr_t p1. intptr_t p2)
*
*  PURPOSE: Interface
*
*  ARGUMENTS:
*
*  RETURNS:
*    1
*
*  COMMENTS:
*
*
**************************************************************************
*/
#if _EVB && (LAPJDEBUG) && 1
struct S_LAPLOG* pJskLog=NULL;
int jskLogCnt;
#endif

typedef void* (*eAlloc)(int n);
typedef void* (*eFree)(void* p);

int DoLapj(t_SIMECC* pEcc, int msg, intptr_t p1, intptr_t p2)
{
    int retval;

#if (EASYWINDEBUG>0) && (LAPJDEBUG>1) && 1
if(! ( (msg==LAPJCMD_TIMER) || (msg==LAPJCMD_CANACCEPTTX)) )
{
printf("DoLapj:msg=%d\r\n",msg);
}
#endif

#if _EVB && (LAPJDEBUG>1) && 0
if(! ( (msg==LAPJCMD_TIMER) || (msg==LAPJCMD_CANACCEPTTX)) )
{
DoLogLapj(pEcc, 255, msg, 0, 0);
}
#endif

    switch(msg)
    {
        default:
            retval = 0;
        break;

        //========
        case LAPJCMD_INIT: // 0
        //========
        {
#if _EVB && (LAPJDEBUG) && 1
 jskLogCnt = MAXLOGLAPJ*sizeof(struct S_LAPLOG);
 pJskLog=(struct S_LAPLOG*)k_malloc(k_gettid(),jskLogCnt);
 memset(pJskLog,0,jskLogCnt);
 jskLogCnt = 0;
#endif

            //======== Get memory buffer block
            pEcc->pJskMem = initJskBuf(pEcc->pJskMem, (eAlloc)p1);
            InitialiseECC(pEcc);
            pEcc->n402 = pEcc->TicTimer + 10;         // first connect is fast
            memset(&pEcc->sFRX,0, sizeof(t_LAPJFRAMER));// clear the framer for the monitor

            retval =    0;
        }
        break;

        //========
        case LAPJCMD_KILL:
        //========
        {
            if(pEcc->pJskMem)
            {
                ((eFree)(p1))(pEcc->pJskMem);
                pEcc->pJskMem = NULL;
            }
            retval = 0;
        }
        break;

        //========
        case LAPJCMD_RESTART:
        //========
        {
            retval = 0;
        }
        break;

        //========
        case LAPJCMD_UARTEMPTY:  // from the Uart, forwarding the signal is key
        //========
        {
#if (EASYWINDEBUG>0) && (LAPJDEBUG>4)  && 0
{
//if(jlpdebug1)
{
printf("LAPJCMD_UARTEMPTY\r\n");
}
}
#endif
            //======== In Bypass mode
            if(pEcc->flgs & LAPJFLAG_TXBYPASS)  // echo the message back
            //========
            {
                pEcc->LapjCanSend(pEcc->idLapj,1);  // kick the source of data
                retval = pEcc->IsTxUartEmpty(pEcc->idTxUart); // return UART status
            }
            //========
            else  // (not Bypass) = run lapj
            //========
            {
                int ourW;

                //======== Does lapj have anything to send?
                retval = TryEccTx(pEcc);

                //======== Now ReCheck our window
#if 1
                if(CanEccTxAccept(pEcc))
                {
                    pEcc->LapjCanSend(pEcc->idLapj,1);
                    retval = 1;
                }
                else
                    retval = 0;
#endif

            }
        }
        break;

        //========
        case LAPJCMD_TXNEWDATA:
        //========
        {
            retval = TryEccTxAccept(pEcc,(U8*)p1, (int)p2); // can we accept it
            retval = TryEccTx(pEcc);            // Any Pending
        }
        break;

        //========
        case LAPJCMD_RXNEWDATA:
        //========
        {
            retval = lapjMonitorBuffer(pEcc, (U8*)p1, (int)p2);
        }
        break;

        //========
        case LAPJCMD_RXJSK:
        //========
        {
            retval = TryEccRx((t_lapj_buf*)p1, pEcc);
        }
        break;

        //========
        case LAPJCMD_TIMER:
        //========
        {
            if(doEccCom(pEcc))
                retval = TryEccTx(pEcc);            // Any Pending
            else
                retval = 0;
        }
        break;

        //========
        case LAPJCMD_CANACCEPTTX:
        //========
        {
            retval = CanEccTxAccept(pEcc);
        }
        break;

        //========
        case LAPJCMD_CONNECT: // this might not be thread safe
        //========
        {
            doLapjFSM(pEcc, &LJfsm_LCONNECT, LAPJCMD_CONNECT, LAPJCMD_CONNECT);
            retval = 0;
        }
        break;

        //========
        case LAPJCMD_DISC:  // this might not be thread safe
        //========
        {
            doLapjFSM(pEcc, &LJfsm_LDISC, LAPJCMD_DISC, LAPJCMD_DISC);
            retval = 0;
        }
        break;

        //========
        case LAPJCMD_READSTATUS:   // read status
        //========
        {
            struct S_LAPJSTATUS* pS = (struct S_LAPJSTATUS*)p1;

            pEcc->sStatus.LapState = pEcc->lapjstate; // copy for display
            *pS = pEcc->sStatus;

            retval = 1;
        }
        break;

        //========
        case LAPJCMD_CLRSTATUS:     // read and clear status
        //========
        {
            struct S_LAPJSTATUS* pS = (struct S_LAPJSTATUS*)p1;

            pEcc->sStatus.LapState = pEcc->lapjstate; // copy for display
            *pS = pEcc->sStatus;
            memset(&(pEcc->sStatus), 0, sizeof(struct S_LAPJSTATUS));

            retval = 1;
        }
        break;
    }
    return retval;
}



#if 1
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
    printf("svHdrQ: 0x%06x next 0x%06x prev: 0x%06x\r\n",
        &(pEcc->svHdrQ),pEcc->svHdrQ.next,pEcc->svHdrQ.prev
        );
    printf("TicTimer %d n401: %d n402: %d n403: %d\r\n",pEcc->TicTimer,pEcc->n401,pEcc->n402,pEcc->n403);
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
#endif


/*
**************************************************************************
*
*  FUNCTION: int DoLogLapj(t_SIMECC* pEcc, int cmd, int a1, int a2, int a3)
*
*  PURPOSE: Interface
*
*  ARGUMENTS:
*
*  RETURNS:
*    1
*
*  COMMENTS:
*
*
**************************************************************************
*/
#ifndef _EVB
int DoLogLapj(t_SIMECC* pEcc, int msg, int a1, int a2, int a3)
{
    return 0;
}

#else

int DoLogLapj(t_SIMECC* pEcc, int msg, int a1, int a2, int a3)
{
    struct S_LAPLOG tmplog;

    memset(&tmplog,0,sizeof(struct S_LAPLOG));
    tmplog.msg = msg;
    tmplog.Vs = pEcc->Vs;
    tmplog.Vr = pEcc->Vr;
    tmplog.Va = pEcc->Va;
    tmplog.Venq = pEcc->Venq;
    tmplog.U8a1 = a1;
    tmplog.U8a2 = a2;
    tmplog.U8a3 = a3;
    pJskLog[jskLogCnt++] = tmplog;
    if(jskLogCnt >= MAXLOGLAPJ) jskLogCnt = 0;
    return 0;
}

#endif

