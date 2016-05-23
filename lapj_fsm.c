/******************************************************************************
* FILE: lapj_fsm.c
*
* CONTAINS: State machine functions for Lapj
*
* PURPOSE:
*   Consolodate state machine functions into descriptor for an event
*   that has little tiny functions for each state.  This is like a state
*   matrix each row corresponds to an event.  Each collumn contains a function
*   pointer indexed by state.
*
*   The idea is that lapj_core does a computation to produce an input.  Then
*   the event descriptor is called to make a state transition based on the input.
*   We pass integers so inputs can be a bit more complex than a boolean but
*   the goal is to keep things simpilier
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

extern int EnqueueCtlPkt(int type, t_SIMECC* pEcc, U8*pPayload, int nPayload);
extern int InitialiseECC(t_SIMECC* pEcc);
extern int FlushSendQueue(t_SIMECC* pEcc);
extern int FlushRcvQueue(t_SIMECC* pEcc);
extern int FlushSvQueue(t_SIMECC* pEcc);
extern void ResetStateV(t_SIMECC* pEcc);
extern int StartT401(t_SIMECC* pEcc);
extern int StopT401(t_SIMECC* pEcc);
extern int TryEccTx(t_SIMECC* pEcc);

extern int jlpdebug1;
#if 1

/*
**************************************************************************
*
*  FUNCTION: int doLapjFSM(struct S_SIMECC* pEcc, struct S_FSMEVT* pTab, int Kin, int dbgId)
*
*  PURPOSE: Call to execute a FSM table entry
*
*  ARGUMENTS:
*  pEcc         Pointer to the Ecc
*  pTab         Pointer to the table for the event
*  Kin          Integer argument (seldom used)
*  dbgId        Identifier for debug
*
*  RETURNS:
*    Event dependent.  A loose convention is to return
*       1 on a transition
*       0 no transition
*
*  COMMENTS:
*   Called from the core.  Loosley, lapj_core handles data flow and lapj_fsm
*   governs state transitions.  There are exceptions.  Of note, the
*   LJA_xxx routines provide both test and initialization
*
**************************************************************************
*/
int doLapjFSM(struct S_SIMECC* pEcc, struct S_FSMEVT* pTab, int Kin, int dbgId)
{
#if (EASYWINDEBUG>0) && (LAPJDEBUG>0) && 1
{
printf("doLapjFSM 0x%06x Tics= %d lapjstate=%d dbgId=%d\r\n",pTab, pEcc->TicTimer, pEcc->lapjstate,dbgId);
}
#endif
    return( (pTab->pF[pEcc->lapjstate])(pEcc,Kin));
}

//========
// NOP 0
//========
int LJA_NOP0(struct S_SIMECC* pEcc, int Kin)
{
#if (EASYWINDEBUG>0) && (LAPJDEBUG>0) && 1
{
printf("LJA_NOP0 Kin=%d\r\n",Kin);
}
#endif
    return 0;
}

//========
// NOP 1
//========
int LJA_NOP1(struct S_SIMECC* pEcc, int Kin)
{
#if (EASYWINDEBUG>0) && (LAPJDEBUG>0) && 1
{
printf("LJA_NOP1 Kin=%d\r\n",Kin);
}
#endif
    return 1;
}

//========
// Reset Reconnect This really doe nothing
//========
int LJA_ResetReconnect(struct S_SIMECC* pEcc, int Kin)
{
#if (EASYWINDEBUG>0) && (LAPJDEBUG>0) && 1
{
printf("LJA_ResetReconnect Kin=%d\r\n",Kin);
}
#endif
    pEcc->n405 = (pEcc->TicTimer + pEcc->sParam.T405);   // 0* disconect
    return 1;
}

//========
// Go into data mode
//========
int LJA_toData(struct S_SIMECC* pEcc, int Kin)
{
     int    k;
#if (EASYWINDEBUG>0) && (LAPJDEBUG>0) && 1
{
printf("LJA_toData %d\r\n", Kin);
}
#endif
    //======== Reset sequence variables
    ResetStateV(pEcc);
    pEcc->sStatus.DiscReason = ERRCODE_NOERROR;

    //======== timers
    pEcc->n401 = 0;                             // disable retransmission
    pEcc->n402 = 0;                             // disable connect/UA
    pEcc->n403 = 0;   // 0* disconect

    //======== Flush queues
    FlushSendQueue(pEcc);
    FlushRcvQueue(pEcc);

    //======== Data flow
    k = pEcc->flgs & (~LAPJFLAG_DMASK);         // keep not modified bits
    pEcc->flgs = k | (LAPJFLAG_RELEASE);

    //======== State variable
    pEcc->lapjstate = LAPJSTATE_DATA;
    EnqueueCtlPkt(FRAMETYPE_UA, pEcc, "_UA", 3);
    return 1;
}

//========
// Go into try data mode
//========
int LJA_toTryConn(struct S_SIMECC* pEcc, int Kin)
{
    int     k;
#if (EASYWINDEBUG>0) && (LAPJDEBUG>0) && 1
{
printf("LJA_toTryConn %d\r\n", Kin);
}
#endif

    //======== Reset sequence variables
    ResetStateV(pEcc);

    //======== Timers
    pEcc->n401 = 0;                             // disable retransmission
    if(pEcc->sParam.T402)
        pEcc->n402 = pEcc->TicTimer + pEcc->sParam.T402;   // Restart the timer
    else
        pEcc->n402 = 0;
    pEcc->n403 = pEcc->TicTimer + pEcc->sParam.T403;   // disconect
    pEcc->lapjstate = LAPJSTATE_TRYCON;

    //======== Flush queues
    FlushSendQueue(pEcc);
    FlushRcvQueue(pEcc);
//    FlushSvQueue(pEcc);

    //======== Data flow
    k = pEcc->flgs & (~LAPJFLAG_DMASK);         // keep not modified bits
    //======== Allow supervisory packets but not data
    pEcc->flgs = k | (LAPJFLAG_UNONLY+LAPJFLAG_RXRAW+LAPJFLAG_TXNODATA);

    pEcc->lapjstate = LAPJSTATE_TRYCON;
    EnqueueCtlPkt(FRAMETYPE_CONN, pEcc, "_Try Conn",9);
    TryEccTx(pEcc);                             // Force a transmission if there is not one in progress

    return 1;
}

//========
// Conditional try to start a connedtion
//========
int LJA_TestToTryConn(struct S_SIMECC* pEcc, int Kin)
{
    int k;
#if (EASYWINDEBUG>0) && (LAPJDEBUG>3) && 1
{
printf("LJA_TestTryConn %d\r\n", Kin);
}
#endif

    //======== Test our condition
    if((pEcc->flgs & LAPJFLAG_CONRETRY))
    //========
    {
        LJA_toTryConn(pEcc,Kin);               // Send a SABM
    }
    else
    {
        if(pEcc->sParam.T402)
            pEcc->n402 = pEcc->TicTimer + pEcc->sParam.T402;   // Restart the timer
        else
            pEcc->n402 = 0;
    }
    return 1;
}

//========
// Conditional try accept a connection
//========
int LJA_TestToConn(struct S_SIMECC* pEcc, int Kin)
{
    int k;
#if (EASYWINDEBUG>0) && (LAPJDEBUG>0) && 1
{
printf("LJA_TestToConn %d\r\n", Kin);
}
#endif

    //======== Test our condition
    if((pEcc->flgs & LAPJFLAG_AUTORESP))
    //========
    {
        LJA_toData(pEcc,Kin);               // Accept the SABM
    }
    else
    {
        if(pEcc->sParam.T402)
            pEcc->n402 = pEcc->TicTimer + pEcc->sParam.T402;   // Restart the timer
        else
            pEcc->n402 = 0;
    }
    return 1;
}

//========
// Go into disc mode
//========
int LJA_toDisc(struct S_SIMECC* pEcc, int Kin)
{
    int     k;
#if (EASYWINDEBUG>0) && (LAPJDEBUG>0) && 1
{
printf("LJA_toDisc %d\r\n", Kin);
jlpdebug1=1;
}
#endif

#if 0
    InitialiseECC(pEcc);  // does not work for some unknown reason
#endif
#if 1
    ResetStateV(pEcc);
    pEcc->lapjstate = LAPJSTATE_DISC;
    pEcc->sStatus.DiscReason = Kin;

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

    //======== clear timers
    pEcc->n400      = 0;
    pEcc->n401      = 0;
    pEcc->n402      = 0;
    pEcc->n403      = 0;
#endif

    //======== Data flow
    k = pEcc->flgs & (~LAPJFLAG_DMASK);         // keep not modified bits
    pEcc->flgs = k | (LAPJFLAG_UNONLY+LAPJFLAG_RXRAW+LAPJFLAG_TXBYPASS);

    return 1;
}

//========
// Graceful Go into disc mode
//========
int LJA_toTryDisc(struct S_SIMECC* pEcc, int Kin)
{
    int k;
#if (EASYWINDEBUG>0) && (LAPJDEBUG>0) && 1
{
printf("LJA_toTryDisc %d\r\n", Kin);
}
#endif


    //======== Timers
    pEcc->n401 = 0;                             // disable retransmission
    pEcc->n402 = 0;                             // Retry attemot
    pEcc->n403 = pEcc->TicTimer + 4*pEcc->sParam.T401;   // Give up time
    pEcc->lapjstate = LAPJSTATE_TRYDISC;

    //======== Data flow
    k = pEcc->flgs & (~LAPJFLAG_DMASK);         // keep not modified bits
    pEcc->flgs = k | (LAPJFLAG_RELEASE);

    //======== State variable
    pEcc->lapjstate = LAPJSTATE_TRYDISC;
    EnqueueCtlPkt(FRAMETYPE_DISC, pEcc, "_DISC", 5);

    return 1;
}

//==================== L_CONNECT
const t_FSMEVT LJfsm_LCONNECT =
//   LAPJSTATE_DISC,    LAPJSTATE_DATA, LAPJSTATE_TRYCON,   LAPJSTATE_TRYDISC,  LAPJSTATE_REJ,  LAPJSTATE_PAUSE
    {LJA_toTryConn,     LJA_toData,     LJA_NOP0,           LJA_NOP0,           LJA_toData,     LJA_NOP0};
//====================

//==================== L_DISC
const t_FSMEVT LJfsm_LDISC =
//   LAPJSTATE_DISC,    LAPJSTATE_DATA, LAPJSTATE_TRYCON,   LAPJSTATE_TRYDISC,  LAPJSTATE_REJ,  LAPJSTATE_PAUSE
    {LJA_NOP0,          LJA_toTryDisc,  LJA_toDisc,         LJA_NOP0,           LJA_toTryDisc,  LJA_NOP0};
//====================

//==================== L_ABORT
const t_FSMEVT LJfsm_LABORT =
//   LAPJSTATE_DISC,    LAPJSTATE_DATA, LAPJSTATE_TRYCON,   LAPJSTATE_TRYDISC,  LAPJSTATE_REJ,  LAPJSTATE_PAUSE
    {LJA_NOP0,          LJA_toDisc,     LJA_toDisc,         LJA_toDisc,         LJA_toDisc,     LJA_toDisc};
//====================
//========
// T401 timeout Here, we kick a REJ
//========
int LJA_T401REJ(struct S_SIMECC* pEcc, int Kin)
{
#if (EASYWINDEBUG>0) && (LAPJDEBUG>0) && 1
{
printf("LJA_T401REJ Kin=%d\r\n",Kin);
}
#endif
    pEcc->Vs = pEcc->Va;                        // Go-Back-N
    StartT401(pEcc);

    //========
    if(++(pEcc->n400) > pEcc->sParam.N400)     // count attempts
    //========
    {
        LJA_toDisc(pEcc,50);                  // too many
    }
    //========
    else                                        // tru again
    //========
    {
        pEcc->lapjstate = LAPJSTATE_REJ;
        EnqueueCtlPkt(FRAMETYPE_REJ, pEcc, "_REJ2", 5);
        pEcc->sStatus.nTxREJ += 1;
        pEcc->sStatus.neT401 += 1;
        pEcc->sStatus.nRetrans += 1;
    }

    return 1;
}

//========
// T401 timeout Same without a REJ
//========
int LJA_T401RTRYDATA(struct S_SIMECC* pEcc, int Kin)
{
    int retval;
#if (EASYWINDEBUG>0) && (LAPJDEBUG>0) && 1
{
printf("LJA_T401RTRYDATA Kin=%d\r\n",Kin);
}
#endif
    pEcc->Vs = pEcc->Va;                        // Go-Back-N
    StartT401(pEcc);

    //========
    if(++(pEcc->n400) > pEcc->sParam.N400)     // count attempts
    //========
    {
        LJA_toDisc(pEcc,52);                  // too many
        retval = 1;
    }
    //========
    else                                        // tru again
    //========
    {
//        pEcc->lapjstate = LAPJSTATE_REJ;
//        EnqueueCtlPkt(FRAMETYPE_REJ, pEcc, "_REJ2", 5);
        pEcc->sStatus.neT401 += 1;
        pEcc->sStatus.nRetrans += 1;
        retval = 0;
    }

    return retval;
}
//==================== L_n401 Retransmition timeout
const t_FSMEVT LJfsm_n401 =
//   LAPJSTATE_DISC,    LAPJSTATE_DATA, LAPJSTATE_TRYCON,   LAPJSTATE_TRYDISC,  LAPJSTATE_REJ,  LAPJSTATE_PAUSE
    {LJA_NOP0,          LJA_T401RTRYDATA, LJA_NOP0,         LJA_NOP0,           LJA_T401REJ,    LJA_NOP0};
//====================

//==================== L_n402 Retry connection need to review
const t_FSMEVT LJfsm_n402 =
//   LAPJSTATE_DISC,    LAPJSTATE_DATA, LAPJSTATE_TRYCON,   LAPJSTATE_TRYDISC,  LAPJSTATE_REJ,  LAPJSTATE_PAUSE
    {LJA_TestToTryConn, LJA_NOP0,       LJA_TestToTryConn,  LJA_toDisc,         LJA_NOP0,       LJA_NOP0};
//====================

//==================== L_n403 disconnection need to review
const t_FSMEVT LJfsm_n403 =
//   LAPJSTATE_DISC,    LAPJSTATE_DATA, LAPJSTATE_TRYCON,   LAPJSTATE_TRYDISC,  LAPJSTATE_REJ,  LAPJSTATE_PAUSE
    {LJA_NOP0,           LJA_toTryDisc,  LJA_toDisc,        LJA_toDisc,         LJA_toTryDisc,  LJA_NOP0};
//====================

//==================== L_n405 reconnect This should not be used for the EVB
const t_FSMEVT LJfsm_n405 =
//   LAPJSTATE_DISC,    LAPJSTATE_DATA, LAPJSTATE_TRYCON,   LAPJSTATE_TRYDISC,  LAPJSTATE_REJ,  LAPJSTATE_PAUSE
    {LJA_ResetReconnect,LJA_NOP0,       LJA_toDisc,         LJA_NOP0,           LJA_NOP0,       LJA_NOP0};
//====================

//==================== received a remote connect (mod, from data to data we just reset seq logic)
const t_FSMEVT LJfsm_RXConn =
//   LAPJSTATE_DISC,    LAPJSTATE_DATA, LAPJSTATE_TRYCON,   LAPJSTATE_TRYDISC,  LAPJSTATE_REJ,  LAPJSTATE_PAUSE
    {LJA_TestToConn,    LJA_toData,     LJA_toData,         LJA_NOP0,           LJA_toData,     LJA_NOP0};
//====================

//==================== received a remote disconnect
const t_FSMEVT LJfsm_RXDisc =
//   LAPJSTATE_DISC,    LAPJSTATE_DATA, LAPJSTATE_TRYCON,   LAPJSTATE_TRYDISC,  LAPJSTATE_REJ,  LAPJSTATE_PAUSE
    {LJA_NOP0,          LJA_toDisc,     LJA_toDisc,         LJA_toDisc,         LJA_toDisc,     LJA_NOP0};
//====================

//==================== received a remote UA
const t_FSMEVT LJfsm_RXUA =
//   LAPJSTATE_DISC,    LAPJSTATE_DATA, LAPJSTATE_TRYCON,   LAPJSTATE_TRYDISC,  LAPJSTATE_REJ,  LAPJSTATE_PAUSE
    {LJA_NOP0,          LJA_NOP0,        LJA_toData,        LJA_NOP0,           LJA_NOP0,       LJA_NOP0};
//====================


#endif
