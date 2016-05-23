#ifndef LAPJ_H
#define JAPJ_H
/*
**************************************************************************
* FILE: lapj.h
*
* PURPOSE: insert file for lapj
*
* COMMENTS:
*
**************************************************************************
*/

// for debug

#define JLPOLDRX 0
#define JLPOLDTX 0

// jskbuffer owners
enum {
    OWNER_simTxCreatePkt=1,         // 1
    OWNER_ProcessRxInput,           // 2
    OWNER_lapjMonitorBuffer1,       // 3
    OWNER_lapjMonitorBuffer2,       // 4
    OWNER_EnqueueCtlPkt,            // 5
    OWNER_MSVPScan,                 // 6
    OWNER_TryEccTxAccept,           // 7
    OWNER_NEW                       // 8
};

//======== jsk buffer Flags
#define LAPJ_BUF_KEEP 1
#define NLAPJDHROFFSET 1        // lapj offset for header before the data

//======== Error Codes
#define ERRCODE_NOERROR 0

// dispatcher
enum {
    LAPJCMD_INIT,           //  0  Initialize Lapj
    LAPJCMD_KILL,           //  1  Kill lapj
    LAPJCMD_RESTART,        //  2  Restart Lapj (does this work?)
    LAPJCMD_UARTEMPTY,      //  3  Uart can accept data (a signal to retry sending)
    LAPJCMD_TXNEWDATA,      //  4  Please send this data burrer
    LAPJCMD_RXNEWDATA,      //  5  Here is received data
    LAPJCMD_TIMER,          //  6  Call this (durrently 100ms)
    LAPJCMD_CANACCEPTTX,    //  7  Query if Lap can accept new buffer
    LAPJCMD_CONNECT,        //  8  Send a SABM and attempt to connect
    LAPJCMD_DISC,           //  9  Send a DISC and disconnect (currently trashes any pending data)

    LAPJCMD_PAUSE,          // not implemented
    LAPJCMD_RESUME,         // not impleented
    LAPJCMD_CLRSTATUS,      // read and clear status (thread safe)
    LAPJCMD_READSTATUS,     // read status (thread safe)
    LAPJCMD_SETPARAMS,      // not implemented yet, compile time set
    LAPJCMD_GETPARAMS,      // not implemented yet, compile time set
    LAPJCMD_SENDUI,         // not implemented Send a ui packet
    LAPJCMD_RXJSK,          // Accept a JSK packet from network (framer)

    LAPCMD_LAST // place holder
};

//======== lapj states
#define LAPJSTATESMAX   6

#define LAPJSTATE_DISC      0
#define LAPJSTATE_DATA      1
#define LAPJSTATE_TRYCON    2
#define LAPJSTATE_TRYDISC   3
#define LAPJSTATE_REJ       4
#define LAPJSTATE_PAUSE     5


// protocol frame definitions
enum {
    FRAMETYPE_DATA  =   1,              // Data frame (Ns,Nr)
    FRAMETYPE_RR    =   0xb0,           // Receive ready (0xb,Nr)
    FRAMETYPE_RNR   =   0xc0,           // Receive not ready (0xb,Nr)
    FRAMETYPE_REJ   =   0xd0,           // Reject (0xd,Nr)
    FRAMETYPE_SREJ  =   0xe0,           // Selective reject (two forms in V.42) (na)
    FRAMETYPE_CONN  =   0xf1,           // Connnect (SABM) (0xf,0x1)
    FRAMETYPE_DISC,                     // 0xf2 Disconnect (0xf,0x2)
    FRAMETYPE_DM,                       // 0xf3 Disconnected mode (reply) (0xf,0x3)
    FRAMETYPE_UI,                       // 0xf4 Unumbered information (send break signal) (0xf,4)
    FRAMETYPE_UA,                       // 0xf5 Unumbered Ack (0xf,0x5) Response to FRAMETYPE_CONN
    FRAMETYPE_FMMR,                     // 0xf6 Frame reject ((0xf,0x6))
    FRAMETYPE_XID,                      // 0xf7 Change window?  (na)
    FRAMETYPE_TEST                      // 0xf8 Test (na)
};

//======== Frame for lapj
typedef struct S_LAPJFRAMER{
    U8*   Bufin;      // input buffer
    int     szBufin;    // size of input
    int     cBufin;     // current index
    U8*   Bufout;     // output buffer
    int     szBufOut;   // size of output (max)
    int     cBufout;    // current index
    U8      CRC8;       //
    U8      ctl;        // control byte for a frame
    U8      state;      // state machine variable
    U8      inframe;    // in frame or not
}t_LAPJFRAMER;

//======== ECC
#define LAPJFLAG_AUTORESP   0x0001      // 1 respond to a SABM
#define LAPJFLAG_CONMODE2    0x0002      //
#define LAPJFLAG_CONMODE4    0x0004      //
#define LAPJFLAG_CONRETRY   0x0008      // 1 retry connection periodically
#define LAPJFLAG_UNONLY     0x0010      // receiver Unnumbered flags only
#define LAPJFLAG_NOMON      0x0020      // receiver No Monitor
#define LAPJFLAG_RELEASE    0x0040      // receiver Release a Lapj packet
#define LAPJFLAG_RXRAW      0x0080      // receiver Forward Raw packets
#define LAPJFLAG_TXBYPASS   0x0100      // transmitter Bypass buffering
#define LAPJFLAG_TXNODATA   0x0200      // transmitter No data, allow unframed
#define LAPJFLAG_NOFRAME    0x8000      // transmitter Buffered with No framing (debug)

//======== Combinations
#define LAPJFLAG_DMASK (LAPJFLAG_UNONLY+LAPJFLAG_NOMON+LAPJFLAG_RELEASE+LAPJFLAG_RXRAW+LAPJFLAG_TXBYPASS+LAPJFLAG_TXNODATA)
#define LAPJFLAG_QPROTECT (LAPJFLAG_NOMON | LAPJFLAG_TXBYPASS)
#define LAPJFLAG_QUEUEDDATA (LAPJFLAG_RELEASE)
#define LAPJFLAG_MONITORUF (LAPJFLAG_UNONLY+LAPJFLAG_RXRAW+LAPJFLAG_TXBYPASS)

//======== Parameters
struct S_LAPJPARAMS{
    U16     T401;               // Acknowledgement timer
    U16     T402;               // Retry for connect (disc mode)

    U16     T403;               // Disconnect while waiting (trycon,rej) (usually = N*T402)
    U16     T405;               // Tics in idle before attempting a reconnect

    U16     T406;
    U16     N400;               // Max Retransmissions

    U16     N404;               // Max octets in a frame
    U16     SWN0Lapj;           // switches not in Lapj

    U16     SWYesLapj;          // switches in Lapj
    U16     Window;             // Window size

    U16     Buflen;             // Buffer length
    U16     pad16;
};

struct S_LAPJSTATUS{
    U32     LapState;           // Copy for Interface
    U32     nTxREJ;             // Sent this many rejs
    U32     nRxREJ;             // received this many rejs
    U32     rRxCRC;             // Bad CrC checks
    U32     nRxFRM;             // Framing violations
    U32     nTxTotal;           // total bytes written (incl frame)
    U32     nRxTotal;           // total bytes read (incl frame)
    U32     neT401;             // This many T401 timeouts
    U32     nRetrans;           // This many retransmission s
    U8      DiscReason;
    U8      pad[3];
};

typedef struct S_SIMECC
{
    //======== Callbacks
    int (*IsTxUartEmpty)(intptr_t idTxUart);    // Query to Uart
    int (*CanAcceptMvsp)(intptr_t idMvsp);      // Query -> Mvsp
    int (*LapjCanSend)(intptr_t idLapj, int n);                     // Signal a TX empty condition
    int (*WriteToUart) (intptr_t idTxUart, U8* pBuf, int nBuf);     // Write -> Uart
    int (*WriteToMvsp) (intptr_t idMvsp, U8* pBuf, int nBuf);       // Write -> Mvsp
    int (*DoUnframed) (intptr_t idMvsp, U8* pBuf, int nBuf);        // Write -> Unframed
    intptr_t     idTxUart;
    intptr_t     idMvsp;
    intptr_t     idLapj;

    //======== Other
    U8      DiD;                // UART did
    U8      id;                 // 0=Forward (A) 1=Reverse (B) ?what is this?
    U16     flgs;               // operation flags
    int     npkts;              // Number of times called

    //======== LAPM 8.2.3.2
    U8      Vs;                 // Send State variable Sequence # of next frame
    U8      Va;                 // Acknowledge variable
    U8      Vr;                 // Receive state variable
    U8      Vc;                 // request to ack a Rx'ed packet

    U8      Venq;               // buffer to enqueue data (usually = Vs)
    U8      Vh;                 // Buffers held but not released
    U8      M;                  // Sequence modulus
    U8      W;                  // Window size

    U8      lapjstate;
    U8      pad2;
    U16     n400;               // Max Retransmissions

    U32     n401;               // Acknowledgement timer
    U32     n402;               // wait for an ack (try to reconect)
    U32     n403;               // inactivity timer (small optional)
    U32     n405;               // Reconnect time (long)
    U32     TicTimer;           // count tics for retransmission purposes

    //======== Parameters
    struct S_LAPJPARAMS sParam;

    //======== Error count
    struct S_LAPJSTATUS sStatus;

   //======== Buffer management
    t_LAPJBUFSTRUCT* pJskMem;   // Duplucate Pointer to memory management structure
    t_DQue  svHdrQ;             // supervisory packet Queue (Use val for the type)
    t_lapj_buf* pRxDf;          // Deframed buffer for RX (working) (could also be in the deframer)
    t_LAPJFRAMER sF;            // Temporary framinb framer in here (put debug)
    t_LAPJFRAMER sFRX;          // Rx deframer, uses jsk buffers, needs to be initialized

    //======== Buffers
    t_lapj_buf* aSend[MAXMOD42];// transmit buffer pointers (use a circuliar table)
    t_lapj_buf* aRcv[MAXMOD42]; // Receive buffer pointers
}t_SIMECC;

//======== State machine
typedef int (*f_LapjFunct)(struct S_SIMECC*, int);
typedef struct S_FSMEVT {f_LapjFunct pF[LAPJSTATESMAX]; } t_FSMEVT;
extern int doLapjFSM(struct S_SIMECC* pEcc, struct S_FSMEVT* pTab, int Kin, int dbgId);

struct S_LAPLOG{
    U8  msg;
    U8  U8a1;
    U8  U8a2;
    U8  U8a3;
    U8  Vs;
    U8  Vr;
    U8  Va;
    U8  Venq;
};
#define MAXLOGLAPJ 200

extern int DoLapj(t_SIMECC* pEcc, int msg, intptr_t p1, intptr_t p2);
extern void prEcc(t_SIMECC* pEcc);

#ifdef XOFF
#undef XOFF
#endif
#define XOFF 0x13

#ifdef XON
#undef XON
#endif
#define XON 0x11

#ifdef XIRQ7
#undef XIRQ7
#endif
#define XIRQ7 0x1c

#ifdef LAPJXSTX
#undef LAPJXSTX
#endif
#define LAPJXSTX 0xae // 1010_1110

#ifdef LAPJXETX
#undef LAPJXETX
#endif
#define LAPJXETX 0xab // 1010_1011

#ifdef LAPJXDLE
#undef LAPJXDLE
#endif
#define LAPJXDLE 0xad // 1010_1101

#ifdef RTNC
#undef RTNC
#endif
#define RTNC -1

#ifdef RTNO
#undef RTNO
#endif
#define RTNO -2

#endif