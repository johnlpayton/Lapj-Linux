/******************************************************************************
* FILE: lapj_frame.c
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
*   This file should be a layer below lapj.  The framing and ecc handling
*   needs to be associated with the boundry between the device and lapj.
*   The implementation here kind of blurs that distinction. In theory,
*   ecc should see only blocks of data without framing. A failed ecc will
*   toss the block (or call into the special macsbg)
*   lapjMonitorBuffer and  lapjStuffAFrame aret the two major entry points.
*   They should be converted to indirect calls
*   Additionally some outbound calls need to adjusted
*       TestForUnframed : Macsbug test on inframed data
*       SendRXjsktolapm : send a buffer to lapj
*       doLapjFSM(pEcc, &LJfsm_LABORT, 96, 96); : abort call ?
*       SendRXjsktolapm(pEcc->pRxDf, pEcc);  see above
*   And calling structures, jsk memory allocation
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


extern U8 BlockCRC8(U8* src, int n, U8* pCRC8);
extern int SendRXjsktomux(t_lapj_buf* pB, t_SIMECC* pEcc);
static int SendRXjsktolapm(t_lapj_buf* pB, t_SIMECC* pEcc);
extern int TryEccRx(t_lapj_buf* pB, t_SIMECC* pEcc);
extern void dumpFramer(char*Header, t_LAPJFRAMER* pF);
extern int DoLogLapj(t_SIMECC* pEcc, int msg, int a1, int a2, int a3);
static int TestForUnframed(t_SIMECC* pEcc, t_LAPJFRAMER* pF, int locEtx);
extern t_FSMEVT LJfsm_LDISC;
extern t_FSMEVT LJfsm_LABORT;

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
*   Note, the CRC is computed on data before framing and stuffing.  However, the
*   CRC result itself is run through the character stuffer before being appended.
*   Logically, the order is [DATA|CRC] -> [STUFFING] -> [TRANSMISSION] Bit errors
*   in [TRANSMISSION] are detected because the CRC is cyclic and propagates.  A
*   sync like LAPB does the same, bit level stuffing is after the CRC.
*
* 2015-jan-31
*   Adjusted some counting.  When the output buffer could overflow (szBufOut-3),
*   a new CRC is computed and lapjStuffAFrame exits.  This is automatic and quiet.
*   The caller needs to check if cBufin < szBufin if checking the overflow
*   is desirable.  In LapjEVB, we send short frames and don't check.
*   This might cause some weird problems later where long frames are truncated.
*   The user needs to stop trying to send long frames.
*   Scrambling could reduce the probability of a long frame (eg repeated escapes)
*****************************************************************************
*/
// stuff 1 byte
static void lapjStuffHelper(U8 c, t_LAPJFRAMER* fp)
{
    switch(c)
    {

        case XIRQ7:                             // mvsp has removed these but we can look
        case (XIRQ7+0X80):
        case XON:
        case XOFF:
        case LAPJXSTX:                          // these are new
        case LAPJXETX:
        case LAPJXDLE:
            fp->Bufout[fp->cBufout++]=LAPJXDLE;
            fp->Bufout[fp->cBufout++] = c^0x20;
        break;

        default:
            fp->Bufout[fp->cBufout++]=c;
    }

}
int lapjStuffAFrame(t_LAPJFRAMER* fp, int bypass)
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
        fp->Bufout[fp->cBufout++]=LAPJXSTX;         // Frame SYN

        while(fp->cBufin < fp->szBufin)             // for all characters in the buffer
        {
            lapjStuffHelper(fp->Bufin[fp->cBufin++],fp);// next char
            //========
            if(fp->cBufout >= (fp->szBufOut - 3) )  // leave three bytes for crc (possibly stuffed) and flag
            //========
            {
                crc = 0xff;                                 // recompute the CRC
                BlockCRC8(&fp->Bufin[fp->cBufin],fp->cBufin,&crc);
                break;                              // OOps out of memory, ship what we have
            }
        }
        lapjStuffHelper(crc,fp);                        // insert (stuffed) crc
        fp->CRC8 = crc;
        fp->Bufout[fp->cBufout++]=LAPJXETX;         // insert the ETX
    }

    return(fp->cBufout);
}

void lapjInitFramer(struct S_LAPJFRAMER* fp, int tabl, char* mtab, char* stab)
{};
/*
**************************************************************************
*  FUNCTION: static int SendRXjsktolapm(t_lapj_buf* pB, t_SIMECC* pEcc);
*
*  PURPOSE: Framer call with a lapj buffer
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
static int SendRXjsktolapm(t_lapj_buf* pB, t_SIMECC* pEcc)
{
//    return TryEccRx(pB, pEcc);
    return DoLapj(pEcc, LAPJCMD_RXJSK, (intptr_t)pB, 0);
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
static int lapjUnStuffBuffer(t_LAPJFRAMER* fp)
{
    int kin;
    int kout;
    U8  c;


    while(fp->cBufin < fp->szBufin)
    {
        //========
        if((fp->cBufout >= fp->szBufOut))       // Ro room in output
            return RTNO;

        c=fp->Bufin[fp->cBufin++];              // Get the input char

        if(c == LAPJXSTX)                       // Begin frame
            return LAPJXSTX;
        else if(c == LAPJXETX)                  // end frame
            return LAPJXETX;
        else if(fp->state == 1)                 // previous was a DLE
        {
            fp->Bufout[fp->cBufout++]=c^0x20;   // invert the next
            fp->state = 0;                      // reset flag
        }
        else if(c==LAPJXDLE)                    // is this a DLE
            fp->state =1;                       // yep, get next
        else
            fp->Bufout[fp->cBufout++]=c;        // nope, save the cgar
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

#if (EASYWINDEBUG>0) && (LAPJDEBUG>4)  && 0
{
jlpDumpFrame("lapjMonitorBuffer\r\n",pF->Bufin,pF->szBufin);
}
#endif
    while(1)
    {
        rtc = lapjUnStuffBuffer(pF);
#if (EASYWINDEBUG>0) && (LAPJDEBUG>3) && 1
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
#if (EASYWINDEBUG>0) && (LAPJDEBUG>4) && 1
{
printf("lapjMonitorBuffer: LapSTX\r\n");
}
#endif
                //========
                if(pF->inframe)
                //========
                {
                    pEcc->sStatus.nRxFRM += 1; // hit an error counter
                }

                //======== Test before a stx
                k = TestForUnframed(pEcc, pF, locEtx);
                if(k)
                {
                    doLapjFSM(pEcc, &LJfsm_LABORT, 96, 96);
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
#if (EASYWINDEBUG>0) && LAPJDEBUG && 0
{
    int m1,m2;
    m1 = random(1000);
    if(m1<50)
    {
        pF->Bufout[2] ^= 1;
    }
}
#endif
#if (EASYWINDEBUG>0) && (LAPJDEBUG>4) && 1
{
printf("lapjMonitorBuffer: LapETX\r\n");
}
#endif
               locEtx = pF->cBufin;            // Here is our new marker

                //======== Check for ETX outside of a frame
               if(! (pF->inframe))
               //========
               {
                    pEcc->sStatus.nRxFRM += 1; // hit an error counter
               }

               //======== Checksum the raw frame include CRC
                crc = 0xff;
                if(pF->cBufout >= 2)
                    k=BlockCRC8(&(pF->Bufout[0]),pF->cBufout,&crc);  // compute CRC
                else
                    k=1;                        // fail short frames

                //========
                if(k == 0)  // good CRC, Now call outwards
                //========
                {
                    t_lapj_buf* pC;

                    //======== Send the frame up
                    pEcc->pRxDf->len = pF->cBufout-2;   // adjust length
#if (EASYWINDEBUG>0) && (LAPJDEBUG>4) && 0
{
jlpDumpFrame("lapjMonitorBuffer:CRC-Ok\r\n",&(pF->Bufout[0]),pF->cBufout);
}
#endif
#if (EASYWINDEBUG>0) && (LAPJDEBUG>3) && 1
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
#if (EASYWINDEBUG>0) && (LAPJDEBUG>1) && 1
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
#if (EASYWINDEBUG>0) && (LAPJDEBUG>1) && 1
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
        doLapjFSM(pEcc, &LJfsm_LABORT, 97, 97);
        return RTNC;
    }

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
*   Return 0  To discard the data and continue
*   Return 1  To initiate a Lapj Disconnect
*
*   In the EVB, the return is 0 and it ignores data between valid Lapj frames
*   as perhaps errored of otherwise bad frames.  The retransmit logic will take
*   care of this.
*
*   In the PC, the function does a detection of some characters the Macsbug
*   spits out on a reset or exception.  Lapj wants to exit into transparent
*   mode so we can do some debugging of the problem
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
        k = pF->cBufin - locEtx;              // is there data to send?
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