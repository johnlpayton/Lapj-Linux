/******************************************************************************
* FILE: GenFrame.c
*
* CONTAINS:
*
* PURPOSE:
*   Framing and I/O routines for lapj  (the mux-has its own framers)
*   lapj is stream oriented.  It is given a buffer containing bytes and
*   places framing around the bytes.
*   for output to the UART, the buffers are set up by the window and ARQ
*   mechanism. Lapj is given a buffer of bytes (in a t_lapj_buf).  It must
*       1) do the CRC (data bytes only)
*       2) do the character stuffing transparency
*       3) add the framing bytes
*       4) send the packet
*   On input, a lapj deframer is constantly running on each received buffer.
*   when a frame is detected it
*       0) does destuffing (part of the monitor)
*       1) does a CRC check
*           1a) on faliure the framer is reset
*       2) enqueues a t_lapj_buf to the core
*   Some other functions in the receiver are added.  If in disconnscted,
*   the raw data has to be sent. The monitor will create a t_lapj_buf on
*   all bytes in the disconnected state and ship them to the core with a code.
*   In connected state, data between lapj frames is discarded quietly (the
*   "unframed channel" is a mux function that des not exist when lapj is
*   connected,  That channel goes to jbug anyway).  Inside a frame, bytes are
*   not delivered until a framw end is detected.  Some kind of detection for a
*   physical disconnect (PC-mxvsp is rebooted) may be hacked in
*
*
******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32_WINNT

    #include "bctype.h"
    #include <stdlib.h>
    extern t_DQue* k_enqueue(t_DQue* q, t_DQue* elem);
    extern t_DQue* k_dequeue(t_DQue* elem);

#else

    #include "kernel.h"
    #include "k_iproto.h"
    #include "kuart5235.h"

#endif

#include "jsk_buff.h"
#include "lapj.h"

//======== Frame for structure
//  First character is the DLE
//  Second character is the STX
//  Third character is the ETX
typedef struct S_GENFRAMER{
    U8*     Bufin;      // input buffer
    int     szBufin;    // size of input
    int     cBufin;     // current index
    U8*     Bufout;     // output buffer
    int     szBufOut;   // size of output (max)
    int     cBufout;    // current index
    U8*     pFrmchar;   // Pointer to the escape table
    U8*     pSubst;     // Pointer to the substitution tabls
    U8      nEsc;       // Number in the tables
    U8      pad[2];
    U8      inframe;    // in frame or not
}t_GENFRAMER;


extern U8 BlockCRC8(U8* src, int n, U8* pCRC8);
extern int SendRXjsktomux(t_lapj_buf* pB, t_SIMECC* pEcc);
extern int SendRXjsktolapm(t_lapj_buf* pB, t_SIMECC* pEcc);
extern int TryEccRx(t_lapj_buf* pB, t_SIMECC* pEcc);
extern void dumpFramer(char*Header, t_LAPJFRAMER* pF);
extern int DoLogLapj(t_SIMECC* pEcc, int msg, int a1, int a2, int a3);
static int TestForUnframed(t_SIMECC* pEcc, t_LAPJFRAMER* pF, int locEtx);
extern t_FSMEVT LJfsm_LDISC;

/*
*****************************************************************************
*  int lapjStuffAFrame(struct S_framer* fp)
*
*   From an input buffer, make a transmit frame with the frame bytes and
*   DLE character stuffing for transparency. All characters in the
*   buffer are assembled and framed as a single frame.
*   This routine adds a start flag, any stuffing bytes, the CRC and the
*   end flag.  Protocol headers are in the block (for stuffing) and the CRC is
*   stuffed and put at the end.
*   The normal use is to start the buffer at the frame top (fp->cBufin=0) , set
*   up the pointers and call the routine.  The output will be a block that
*   can be given to the uart for transmition.  Note, the layer 1 XON/XOFF&abort
*   stuff cannot use this call.  So the actual send to the uart is done outside of this.
*
*   This is adapted from mvsp. You need to set up the framer
*   structure correctly
*       fp->Bufin is the data buffer
*       fp->szBufin is the size of the buffer (normally the character count)
*       fp->cBufin is the first character (usually 0)
*       fp->Bufout is the output data buffer
*       fp->szBufOut is the memory size of the output buffer
*       fp->cBufout is set to 0 my the stuff and contains the total after framing
*           Bad things can happen if the outbuffer is too small.
*           Checks and recovery logic is not yet in place, Make sure
*           szBufOut >= 2*szBufin + 4
*       fp->CRC8 is set but more important in deframing
*       other U8 variables are used  in the deframer
*
* 2015-jan-31
*   Adjusted some counting
*
*****************************************************************************
*/
// stuff 1 byte
static void genStuffHelper(U8 c, t_GENFRAMER* fp)
{
    int i;
    for(i=0; i<fp->nEsc; i++)
    {
        if(c == fp->pFrmchar[i])
        {
            fp->Bufout[fp->cBufout++] = fp->pSubst[0];
            fp->Bufout[fp->cBufout++] = fp->pSubst[i];
            return
        }
    }
    fp->Bufout[fp->cBufout++]=c;
    return;
}
int genStuffAFrame(t_GENFRAMER* fp, int bypass)
{
    int     kin;
    int     kout;
    char    c;
    U8      crc;

    kin = fp->szBufin - fp->cBufin;

    if(kin <= 0)               // no bytes to send
    {
        return(RTNC);
    }

    //========
    if(bypass)
    {
        memcpy(fp->Bufout,fp->Bufin,kin);
        fp->cBufout = kin;
    }
    else
    {
        crc = 0xff;                                 // init crc
        BlockCRC8(&fp->Bufin[fp->cBufin],kin,&crc); // compute crc on unstuffed frame

        fp->cBufout = 0;                            // start a new frame
        fp->Bufout[fp->cBufout++] = fp->pSubst[1];  // Frame STX

        while(fp->cBufin < fp->szBufin)             // for all characters in the buffer
        {
            lapjStuffHelper(fp->Bufin[fp->cBufin++],fp);// next char
            if(fp->cBufout >= (fp->szBufOut - 3) )  // leave three bytes for crc (possibly stuffed) and flag
                break;                              // OOps memory error
        }
        lapjStuffHelper(crc,fp);                        // insert (stuffed) crc
        fp->CRC8 = crc;
        fp->Bufout[fp->cBufout++]=fp->pSubst[2];         // insert the ETX
    }

    return(fp->cBufout);
}

/*
*****************************************************************************
*  static int lapjUnStuffBuffer(struct S_framer* fp)
*
*   A buffer may have parts of a frame and also unframed date.
*   This routine will unstuff any DLE sequences and return
*   on a frame boundry with perphaps leftover characters.
*   it is up to the higher level caller to assemble frames
*   and route them
*
*****************************************************************************
*/
static int lapjUnStuffBuffer(t_GENFRAMER* fp)
{
    int kin;
    int kout;
    int i;
    U8  c;
    U8  cDLE = fp->pSubst[0];
    U8  cSTX = fp->pSubst[1];
    U8  cETX = fp->pSubst[2];

    while(fp->cBufin < fp->szBufin)
    {
        //========
        if((fp->cBufout >= fp->szBufOut))       // No room in output
            return RTNO;

        c=fp->Bufin[fp->cBufin++];              // Get the input char

        if(c == cSTX)                           // Begin frame
            return fp->pFrmchar[1];
        else if(c == cETX)                      // end frame
            return fp->pFrmchar[2];
        else if(fp->state == 1)                 // previous was a DLE
        {
            for(i=0; i<fp->nEsc; i++)           // substitute from the table
            {
                if(c == fp->pSubst[i])
                {
                    fp->Bufout[fp->cBufout++] = fp->pFrmchar[i];
                }
            }
            fp->state = 0;                      // reset flag
        }
        else if(c==cDLE)                        // is this a DLE
            fp->state =1;                       // yep, get next
        else
            fp->Bufout[fp->cBufout++]=c;        // nope, use raw char
    }

    return(RTNC);                          // ran out of buffer
}

/*
*****************************************************************************
*   int lapjMonitorBuffer(t_lapj_buf* pB, t_SIMECC* pEcc)
*
*   This is a residant monitor for lapj
*   It is called each time a uart buffer arrives.  To process the buffer
*   it calls into lapjUnStuffBuffer with the residant framer.  lapjUnStuffBuffer
*   will return in 3 cases
*       a) The input buffer is exhausted.  We want to wait for a new input
*       b) The opuput buffer is full,  This is an exception condition
*       c) stx (or etx) was found to mark a frame
*   Fr right now, we will code the lapj logic then review to see how DM mode
*   will fit in.  The basic plan is to have two jsk buffers, one for the input,
*   one for the deframed. we will forward the appropiate buffer based on the state
*   (DM or CONN). We're not quite sure how we will forward frames to the demux layer yet.
*
*   Let us assume the read of the uart has placed the data into a t_lapj_buf
*   starting at pData and with len set to the count.  We'll force the actual read
*   to be outside for porting.
*
*   ETX is a bit tricky, we need to know that the framer is putting the packet
*   into t_lapj_buf->pData[-NLAPJDHROFFSET] and the count is adjusted by NLAPJDHROFFSET
*   we don't change the jsk buffer parameters but place the data itself at pData
*   This is so we can switch between ECC and transparent modes.  User data always
*   starts at pData.
*
*   The allocation is also tricky.  We have to know who frees the buffers.
*   In this case, we'll have to change the demux so he frees whatever
*   buffer he gets for the end users.  This is a change to devmvsp.c
*
* 2014=dec-26 jlp
*   change to monitor a U8 buffer
*
*****************************************************************************
*/
int lapjMonitorBuffer(t_SIMECC* pEcc, U8* pBufin, int nBuf)
{
    int     retval;
    int     k;
    int     rtc;
    U8      crc;
    int     locEtx;
    t_LAPJFRAMER* pF = &(pEcc->sFRX);

    retval = RTNC;

    //======== check for an empty buffer
    if(nBuf <=0)
        return retval;

    //======== Monitor disabled
    if(((pEcc->flgs & LAPJFLAG_NOMON) == LAPJFLAG_NOMON))
        goto skipmon;

    //======== Check for a valid output jsk buffer
    if(!(pEcc->pRxDf))
    {
        t_lapj_buf* pC;

        pC = JskAllocBuf(pEcc->pJskMem,OWNER_lapjMonitorBuffer1);
        pEcc->pRxDf = pC;
        pF->Bufout = &(pC->pData[-NLAPJDHROFFSET]); // offset for header
        pF->szBufOut = pC->maxlen - LAPJBUFOFFSET + NLAPJDHROFFSET;
        pF->cBufout = 0;
        pF->state = 0;
        pF->inframe = 0;
    }

    pEcc->sStatus.nRxTotal += nBuf;             // inc the byte count

   //======== put the Data into the deframer
    pF->Bufin = pBufin;                         // include the lapj header
    pF->szBufin = nBuf;                         // adjust the count
    pF->cBufin = 0;                             // start input
    locEtx = 0;                                 // Initial location is 0

#if EASYWINDEBUG && (LAPJDEBUG>4)  && 0
{
jlpDumpFrame("lapjMonitorBuffer\r\n",pF->Bufin,pF->szBufin);
}
#endif
    while(1)
    {
        rtc = lapjUnStuffBuffer(pF);
#if EASYWINDEBUG && (LAPJDEBUG>3) && 1
{
printf("lapjMonitorBuffer rtc=%d pF->cBufout=%d Left %d\r\n",rtc,pF->cBufout, (pF->szBufin-pF->cBufin) );
}
#endif
        switch(rtc)
        {
            //========
            case RTNC:  // exhausted input
            //========
            {
                goto getout;
            }
//            break;

            //========
            case LAPJXSTX: // STX, set up lapj
            //========
            {
#if EASYWINDEBUG && (LAPJDEBUG>4) && 1
{
printf("lapjMonitorBuffer: LapSTX\r\n");
}
#endif
                if(pF->inframe)
                    pEcc->sStatus.nRxFRM += 1;

                //======== Test before a stx
                k = TestForUnframed(pEcc, pF, locEtx);
                if(k)
                {
                    doLapjFSM(pEcc, &LJfsm_LDISC, 96, 96);
                    return RTNC;
                }

                //======== Reset output pointers
                pF->cBufout = 0;                // reset count
                pF->state = 0;
                pF->inframe = 1;
                locEtx = pF->cBufin;
            }
            break;

            //========
            case LAPJXETX: // ETX try to send to lapj
            //========
            {
//======== Generate errors
#if EASYWINDEBUG && LAPJDEBUG && 0
{
    int m1,m2;
    m1 = random(1000);
    if(m1<50)
    {
        pF->Bufout[2] ^= 1;
    }
}
#endif
#if EASYWINDEBUG && (LAPJDEBUG>4) && 1
{
printf("lapjMonitorBuffer: LapETX\r\n");
}
#endif
               if(! (pF->inframe))
                    pEcc->sStatus.nRxFRM += 1;

               locEtx = pF->cBufin;            // Here is our new marker

               //======== Checksum the raw frame
                crc = 0xff;
                if(pF->cBufout >= 2)
                    k=BlockCRC8(&(pF->Bufout[0]),pF->cBufout,&crc);  // compute CRC
                else
                    k=1;                        // fail CRC

                //========
                if(k == 0)  // Now call outwards
                //========
                {
                    t_lapj_buf* pC;

                    //======== Send the frame up
                    pEcc->pRxDf->len = pF->cBufout-2;
#if EASYWINDEBUG && (LAPJDEBUG>4) && 0
{
jlpDumpFrame("lapjMonitorBuffer:CRC-Ok\r\n",&(pF->Bufout[0]),pF->cBufout);
}
#endif
#if EASYWINDEBUG && (LAPJDEBUG>3) && 1
{
printf("lapjMonitorBuffer:CRC-Ok\r\n");
}
#endif
#if _EVB && (LAPJDEBUG>1) && 1 // Good rx buffer 201, hdr, len, crc
DoLogLapj(pEcc, 200, (pF->Bufout[0]),pF->cBufout,crc);
#endif
                    SendRXjsktolapm(pEcc->pRxDf, pEcc);

                    //======== Get and initialize a new frame
                    pC = JskAllocBuf(pEcc->pJskMem,OWNER_lapjMonitorBuffer2);
                    pEcc->pRxDf = pC;
                    pF->Bufout = &(pC->pData[-NLAPJDHROFFSET]); // offset for header
                    pF->szBufOut = pC->maxlen - LAPJBUFOFFSET + NLAPJDHROFFSET;
                    pF->cBufout = 0;
                    pF->state = 0;
                    pF->inframe = 0;
                }
                //========
                else        // trash the frame (we don't forward bad frames)
                //========
                {
#if EASYWINDEBUG && (LAPJDEBUG>1) && 1
{
printf("lapjMonitorBuffer:CRC-Bad\r\n");
}
#endif
#if _EVB && (LAPJDEBUG>1) && 1 // Bad CRC 201, hdr, len, crc
DoLogLapj(pEcc, 201, (pF->Bufout[0]),pF->cBufout,crc);
#endif
                    pEcc->sStatus.rRxCRC += 1;
                    pF->cBufout = 0;
                    pF->state = 0;
               }

            }
            break;

            //========
            case RTNO:  // output full
            //========
            {
#if EASYWINDEBUG && (LAPJDEBUG>1) && 1
{
if( (pEcc->lapjstate == LAPJSTATE_DATA) || (pEcc->lapjstate == LAPJSTATE_REJ) )
jlpDumpFrame("lapjMonitorBuffer:BufOV\r\n",&(pF->Bufout[0]),pF->cBufout);
else
printf("lapjMonitorBuffer:BufOV\r\n");
}
#endif
                pF->cBufout = 0;                // trash the frame
                pF->state = 0;
            }
            break;

        }
    }

    getout:

    //======== Any leftover unframed data?
    k = TestForUnframed(pEcc, pF, locEtx);
    if(k)
    {
        doLapjFSM(pEcc, &LJfsm_LDISC, 97, 97);
        return RTNC;
    }

/*
//==== This might be replaced by the LAPJFLAG_RXRAW switch
    //======== In a lapj data state, return
    if( (pEcc->lapjstate==LAPJSTATE_DATA) ||
        (pEcc->lapjstate==LAPJSTATE_REJ)
        )
    {
        return(retval);
    }
*/
    skipmon:
    //======== Forward the raw packet
    if((pEcc)->flgs & LAPJFLAG_RXRAW)
        pEcc->WriteToMvsp(pEcc->idMvsp, pBufin, nBuf);
    return(retval);
}

/*
*****************************************************************************
*   int lapjMonitorBuffer(t_lapj_buf* pB, t_SIMECC* pEcc)
*
*   Test for Unframed.  This a little bit of messy code to send data between
*   <lapjetx> .. <lapjstx> packets and residual data at the bebinning and end
*   of frames.
*
*   Normally the data is simply discarded.  But in the evbPC (msvsp) we want
*   to send the data to a detector for MACSBUG breaks.  So we have to do the
*   test in 3 places; beginning, between, and end.  This is a common routine
*   to do the logic.
*
*   The return value of 1 means a detection wants to abort lapj and do to disconnect
*   0 means detection wants to continue
*
*****************************************************************************
*/
static int TestForUnframed(t_SIMECC* pEcc, t_LAPJFRAMER* pF, int locEtx)
{
    int k;
    int retval = 0;

    //========= Do we have a test function?
    if(!pEcc->DoUnframed)
        return 0;

    //========= Are we already disconnected
    if( (pEcc->lapjstate == LAPJSTATE_DISC) )
        return 0;

    //========= Are we in a frame or just junk?
    if(! (pF->inframe))
    {
        k = pF->cBufin-1 - locEtx;              // is there data to send?
        if(k > 0)
        {
            retval = pEcc->DoUnframed(pEcc->idMvsp,&(pF->Bufin[locEtx]),k);
        }
    }
    return retval;
}


#if 1
void dumpFramer(char*Header, t_LAPJFRAMER* pF)
{
    printf("%s\r\n",Header);
    printf("Bufin: 0x%06x szBufin: %d cBufin: %d ", pF->Bufin, pF->szBufin, pF->cBufin);
    printf("Bufout: 0x%06x szBufOut: %d cBufout: %d\r\n", pF->Bufout, pF->szBufOut, pF->cBufout);

}
#endif
