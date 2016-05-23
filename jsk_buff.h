#ifndef JSK_BUFF_H
#define JSK_BUFF_H
/*
**************************************************************************
* FILE: jsk_buff.h
*
* PURPOSE: insert file for protocol buffers
*
* COMMENTS:
*   Serves the same functionality as Linux:sk_buff.h
*   This is a reduced version of the Linux sk_buff.h.  It defines
*   structures and macros for the protocol buffer of lapj.  There
*   are hooks for enhanced protocols.  The main use right now is to force
*   the communication and program flow along the lines of Linux
*
*   The basic idea is along the lines of our mxvsp ctl packets where
*   a control packet.  A control buffer contains
*       lnk
*
**************************************************************************
*/
typedef int jskatomic_t;

typedef struct S_lapj_buf {
    t_DQue      lnk;
    U8          id;             // for debug
    U8          own;
    U8          pad[2];

    U16         maxlen;         // length of the memory of this t_lapj_buf
    U16         len;            // length of data only
    U8          Ns;             // sequence number of this packet
    U8          Nr;             // expected sequence (not really important)
    U8          type;           // packet type
    U8          flgs;           // various flags

    U8*         pTop;           // pointer to top of this t_lapj_buf
    U8*         pData;          // Pointer to data   (data)
    U8*         pTail;          // pointer to the tail (packet end)
   jskatomic_t  users;          // User reference count remopve for display
}t_lapj_buf;

#define MAXMOD42 11             // max window for Lapj
#define LAPJ_WINDOW MAXMOD42-2
#define NLAPJBUFPOOL (MAXMOD42 + 2 + 4) // total buffers to allocate
#define LAPJBUFOFFSET 4

//#define LAPJDATAMAX 1500  // etherbuf
#define LAPJDATAMAX (((136*3)/4)*4)
#define NLAPJBUFSIZE (LAPJDATAMAX + LAPJBUFOFFSET+8) //


//======== Queue header definition
#define JSKFREEQUEIDX 0 // Free queue
#define JSKCTL 1        // Input control queue
#define JSKTXPEND 2
#define JSKTXSENT 3     // Sent but not acknowledged
#define JSKRX 4         // Received

//======== jsk buffers
typedef struct S_LAPJBUFSTRUCT {
    t_DQue      freeQHdr;
    S16         nAlloc; // for display
    U8          id;
    U8          pad;
    int         maxbufsize;     // maximum buffer size to allocate
    U8*         bulkMemory;
    U8*         pTxTmpBuf;              // a buffer in the bulk memory for transmit framing use
    t_lapj_buf  BufHeaders[NLAPJBUFPOOL];           // buffer headers
//    U8          txTmpBuf[2*NLAPJBUFSIZE];           // temporary for transmitter
//    U8          buf[NLAPJBUFSIZE*NLAPJBUFPOOL];     // bulk memory
}t_LAPJBUFSTRUCT;

#if 0
#ifndef _WIN32_WINNT
//======== Frame for multiplexor
struct S_framer{
    char*   Bufin;      // input buffer
    int     szBufin;    // size of input
    int     cBufin;     // current index
    char*   Bufout;     // output buffer
    int     szBufOut;   // size of output (max)
    int     cBufout;    // current index
    U8      id;         // if for a frame
    U8      seq;        // sequence count for a frame
    U8      state;      // state machine variable
    U8      inframe;    // in frame or not
};
#endif
#endif

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

#ifdef XSTX
#undef XSTX
#endif
#define XSTX 0x02

#ifdef XETX
#undef XETX
#endif
#define XETX 0x03

#ifdef XDLE
#undef XDLE
#endif
#define XDLE 0x10

#ifdef RTNC
#undef RTNC
#endif
#define RTNC -1

extern U8 BlockCRC8(U8* src, int n, U8* pCRC8);
extern struct S_LAPJBUFSTRUCT* initJskBuf(struct S_LAPJBUFSTRUCT* pJ, void* (*eAlloc)(int n));
extern void rebuildJskBuf(struct S_LAPJBUFSTRUCT* pJsk, void* x);
extern t_DQue* JskFreeQP(struct S_LAPJBUFSTRUCT* pJsk,t_DQue* pQ);
extern t_DQue* JskGetQP(struct S_LAPJBUFSTRUCT* pJsk);
extern void JskPrepBuf(t_lapj_buf* pJsk);
extern t_lapj_buf* JskAllocBuf(struct S_LAPJBUFSTRUCT* pJsk, int own);
extern t_lapj_buf* JskFreeBuf(struct S_LAPJBUFSTRUCT* pJsk, t_lapj_buf* pB);
extern void prjskmem(struct S_LAPJBUFSTRUCT* pJsk);

#endif