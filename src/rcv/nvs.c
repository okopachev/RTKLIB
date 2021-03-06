/*------------------------------------------------------------------------------
* nvs.c : NVS receiver dependent functions
*
*    Copyright (C) 2012-2013 by M.BAVARO and T.TAKASU, All rights reserved.
*    Copyright (C) 2014 by T.TAKASU, All rights reserved.
*
*     [1] Description of BINR messages which is used by RC program for RINEX
*         files accumulation, NVS
*     [2] NAVIS Navis Standard Interface Protocol BINR, NVS
*
* version : $Revision:$ $Date:$
* history : 2012/01/30 1.0  first version by M.BAVARO
*           2012/11/08 1.1  modified by T.TAKASU
*           2013/02/23 1.2  fix memory access violation problem on arm
*           2013/04/24 1.3  fix bug on cycle-slip detection
*                           add range check of gps ephemeris week
*           2013/09/01 1.4  add check error of week, time jump, obs data range
*           2014/08/26 1.10 fix bug on iode in glonass ephemeris
*-----------------------------------------------------------------------------*/
#include "rtklib.h"

#define NVSSYNC     0x10        /* nvs message sync code 1 */
#define NVSENDMSG   0x03        /* nvs message sync code 1 */
#define NVSCFG      0x06        /* nvs message cfg-??? */

#define ID_XF5RAW   0xf5        /* nvs msg id: raw measurement data */
#define ID_X4AIONO  0x4a        /* nvs msg id: gps ionospheric data */
#define ID_X4BTIME  0x4b        /* nvs msg id: GPS/GLONASS/UTC timescale data */
#define ID_XF7EPH   0xf7        /* nvs msg id: subframe buffer */
#define ID_XE5BIT   0xe5        /* nvs msg id: bit information */
#define ID_X88PVT   0x88        /* nvs msg id: PVT vector data */
#define ID_X40ALM   0x40        /* nvs msg id: almanac data */

#define ID_XD7ADVANCED 0xd7     /* */
#define ID_X02RATEPVT  0x02     /* */
#define ID_XF4RATERAW  0xf4     /* */
#define ID_XD7SMOOTH   0xd7     /* */
#define ID_XD5BIT      0xd5     /* */

static const char rcsid[]="$Id: nvs.c,v 1.0 2012/01/30 00:05:05 MBAVA Exp $";

/* get fields (little-endian) ------------------------------------------------*/
#define U1(p) (*((unsigned char *)(p)))
#define I1(p) (*((char *)(p)))
static unsigned short U2(unsigned char *p) {unsigned short u; memcpy(&u,p,2); return u;}
static unsigned int   U4(unsigned char *p) {unsigned int   u; memcpy(&u,p,4); return u;}
static short          I2(unsigned char *p) {short          i; memcpy(&i,p,2); return i;}
static int            I4(unsigned char *p) {int            i; memcpy(&i,p,4); return i;}
static float          R4(unsigned char *p) {float          r; memcpy(&r,p,4); return r;}
static double         R8(unsigned char *p) {double         r; memcpy(&r,p,8); return r;}

/* ura values (ref [3] 20.3.3.3.1.1) -----------------------------------------*/
static const double ura_eph[]={
    2.4,3.4,4.85,6.85,9.65,13.65,24.0,48.0,96.0,192.0,384.0,768.0,1536.0,
    3072.0,6144.0,0.0
};
/* ura value (m) to ura index ------------------------------------------------*/
static int uraindex(double value)
{
    int i;
    for (i=0;i<15;i++) if (ura_eph[i]>=value) break;
    return i;
}
/* decode NVS xf5-raw: raw measurement data ----------------------------------*/
static int decode_xf5raw(raw_t *raw)
{
    gtime_t time;
    double tadj=0.0,toff=0.0,tn;
    int dTowInt;
    double dTowUTC, dTowGPS, dTowFrac, L1, P1, D1;
    double gpsutcTimescale;
    unsigned char rcvTimeScaleCorr, sys, carrNo, type, code;
    int i,j,prn,sat,n=0,nsat,week,index=0,number;
    unsigned char *p=raw->buff+2;
    char *q,tstr[32],flag,flag2;
    
    trace(4,"decode_xf5raw: len=%d\n",raw->len);
    
    /* time tag adjustment option (-TADJ) */
    if ((q=strstr(raw->opt,"-tadj"))) {
        sscanf(q,"-TADJ=%lf",&tadj);
    }
    dTowUTC =R8(p);
    week = U2(p+8);
    gpsutcTimescale = R8(p+10);
    /* glonassutcTimescale = R8(p+18); */
    rcvTimeScaleCorr = I1(p+26);
    
    /* check gps week range */
    if (week>=4096) {
        trace(2,"nvs xf5raw obs week error: week=%d\n",week);
        return -1;
    }
    week=adjgpsweek(week);
    
    if ((raw->len - 31)%30) {
        
        /* Message length is not correct: there could be an error in the stream */
        trace(2,"nvs xf5raw len=%d seems not be correct\n",raw->len);
        return -1;
    }
    nsat = (raw->len - 31)/30;
    
    dTowGPS = dTowUTC + gpsutcTimescale;
    
    /* Tweak pseudoranges to allow Rinex to represent the NVS time of measure */
    dTowInt  = 10.0*floor((dTowGPS/10.0)+0.5);
    dTowFrac = dTowGPS - (double) dTowInt;
    time=gpst2time(week, dTowInt*0.001);
    
    /* time tag adjustment */
    if (tadj>0.0) {
        tn=time2gpst(time,&week)/tadj;
        toff=(tn-floor(tn+0.5))*tadj;
        time=timeadd(time,-toff);
    }
    /* check time tag jump */
    if (raw->time.time&&fabs(timediff(time,raw->time))>86400.0) {
        time2str(time,tstr,3);
        trace(2,"nvs xf5raw time tag jump error: time=%s\n",tstr);
        return 0;
    }
    if (fabs(timediff(time,raw->time))<=1e-3) {
        time2str(time,tstr,3);
        trace(2,"nvs xf5raw time tag duplicated: time=%s\n",tstr);
        return 0;
    }
    raw->obs.n=0;
    for (i=0,p+=27;(i<nsat) && (n<MAXOBS); i++,p+=30) {

        flag2=0;
        type=U1(p);
        if(type==2 || type==34 || type==50 || type==66|| type==82 || type==130 || type==162 || type==194)
          sys=SYS_GPS;
        else if(type==1 || type==3 || type==5 || type==6 || type==17 || type==33 || type==49 || type==65 || type==81 || type==129 || type==161 || type==193)
          sys=SYS_GLO;
        else if(type==4 || type==68 || type==84)
          sys=SYS_SBS;
        else
          sys=SYS_NONE;
        prn = U1(p+1);
        if (sys == SYS_SBS) prn += 120; /* Correct this */
        if (!(sat=satno(sys,prn))) {
            trace(2,"nvs xf5raw satellite number error: sys=%d prn=%d\n",sys,prn);
            continue;
        }
        carrNo = I1(p+2);
        L1 = R8(p+ 4);
        P1 = R8(p+12);
        D1 = R8(p+20);
        
        /* check range error */
        if (L1<-1E10||L1>1E10||P1<-1E10||P1>1E10||D1<-1E5||D1>1E5) {
            trace(2,"nvs xf5raw obs range error: sat=%2d L1=%12.5e P1=%12.5e D1=%12.5e\n",
                  sat,L1,P1,D1);
            continue;
        }

        switch(type)
        {
          case 1:
          case 2:
          case 4:
          case 5:
          case 17:
          case 129:
          case 130:
            index=0;
            break;
          case 3:
          case 6:
          case 33:
          case 34:
          case 49:
          case 50:
          case 161:
          case 162:
            index=1;
            break;
          case 66:
          case 68:
          case 82:
          case 84:
          case 193:
          case 194:
            index=2;
            break;
          default:
            index=0;
            break;
        }

        switch(type)
        {
          case 1:
          case 2:
            code = CODE_L1C;
            break;
          case 5:
          case 17:
            code = CODE_L1P;
            break;
          case 3:
          case 33:
          case 34:
          case 50:
            code = CODE_L2C;
            break;
          case 6:
          case 49:
            code = CODE_L2P;
            break;
          case 66:
          case 68:
          case 82:
          case 84:
          case 193:
          case 194:
            code = CODE_L3I;
            break;
          default:
            code = CODE_L1C;
        }

        number=n;
        for(j=0; j<n; j++)
          if(raw->obs.data[j].sat==sat)
          {
            number=j;
            flag2=1;
            break;
          }

        if(flag2==0)
          for (j=0;j<NFREQ+NEXOBS;j++) {
              raw->obs.data[number].L[j]=raw->obs.data[number].P[j]=0.0;
              raw->obs.data[number].D[j]=0.0;
              raw->obs.data[number].SNR[j]=raw->obs.data[number].LLI[j]=0;
              raw->obs.data[number].code[j]=CODE_NONE;
          }
        else;
          if ((code==CODE_L1C || code==CODE_L2C) && (raw->obs.data[number].code[index]==CODE_L1P || raw->obs.data[number].code[index]==CODE_L2P))
            continue;

        raw->obs.data[number].time  = time;
        raw->obs.data[number].SNR[index]=(unsigned char)(I1(p+3)*4.0+0.5);

        if (sys==SYS_GLO) {
            raw->obs.data[number].L[index]  =  L1 - toff*(FREQ1_GLO+DFRQ1_GLO*carrNo);
        } else {
            raw->obs.data[number].L[index]  =  L1 - toff*FREQ1;
        }
        raw->obs.data[number].P[index]    = (P1-dTowFrac)*CLIGHT*0.001 - toff*CLIGHT; /* in ms, needs to be converted */
        raw->obs.data[number].D[index]    =  (float)D1;
        
        raw->obs.data[number].code[index] = code;

        /* set LLI if meas flag 4 (carrier phase present) off -> on */
        flag=U1(p+28);
        raw->obs.data[number].LLI[index]=(flag&0x08)&&!(raw->halfc[sat-1][index]&0x08)?1:0;
        raw->halfc[sat-1][index]=flag;
        
#if 0
        if (raw->obs.data[number].SNR[index] > 160) {
            time2str(time,tstr,3);
            trace(2,"%s, obs.data[%d]: SNR=%.3f  LLI=0x%02x\n",  tstr,
                n, (raw->obs.data[number].SNR[index])/4.0, U1(p+28) );
        }
#endif

        raw->obs.data[number].sat = sat;
        
        if(flag2==0)
          n++;
    }
    raw->time=time;
    raw->obs.n=n;
    return 1;
}
/* decode ephemeris ----------------------------------------------------------*/
static int decode_gpsephem(int sat, raw_t *raw)
{
    eph_t eph={0};
    unsigned char *puiTmp = (raw->buff)+2;
    unsigned short week;
    double toc;
    
    trace(4,"decode_ephem: sat=%2d\n",sat);
    
    eph.crs    = R4(&puiTmp[  2]);
    eph.deln   = R4(&puiTmp[  6]) * 1e+3;
    eph.M0     = R8(&puiTmp[ 10]);
    eph.cuc    = R4(&puiTmp[ 18]);
    eph.e      = R8(&puiTmp[ 22]);
    eph.cus    = R4(&puiTmp[ 30]);
    eph.A      = pow(R8(&puiTmp[ 34]), 2);
    eph.toes   = R8(&puiTmp[ 42]) * 1e-3;
    eph.cic    = R4(&puiTmp[ 50]);
    eph.OMG0   = R8(&puiTmp[ 54]);
    eph.cis    = R4(&puiTmp[ 62]);
    eph.i0     = R8(&puiTmp[ 66]);
    eph.crc    = R4(&puiTmp[ 74]);
    eph.omg    = R8(&puiTmp[ 78]);
    eph.OMGd   = R8(&puiTmp[ 86]) * 1e+3;
    eph.idot   = R8(&puiTmp[ 94]) * 1e+3;
    eph.tgd[0] = R4(&puiTmp[102]) * 1e-3;
    toc        = R8(&puiTmp[106]) * 1e-3;
    eph.f2     = R4(&puiTmp[114]) * 1e+3;
    eph.f1     = R4(&puiTmp[118]);
    eph.f0     = R4(&puiTmp[122]) * 1e-3;
    eph.sva    = uraindex(I2(&puiTmp[126]));
    eph.iode   = I2(&puiTmp[128]);
    eph.iodc   = I2(&puiTmp[130]);
    eph.code   = I2(&puiTmp[132]);
    eph.flag   = I2(&puiTmp[134]);
    week       = I2(&puiTmp[136]);
    eph.fit    = 0;
    
    if (week>=4096) {
        trace(2,"nvs gps ephemeris week error: sat=%2d week=%d\n",sat,week);
        return -1;
    }
    if (raw->time.time==0) return 0;

    eph.week=adjgpsweek(week);
    eph.toe=gpst2time(eph.week,eph.toes);
    eph.toc=gpst2time(eph.week,toc);
    eph.ttr=raw->time;
    
    if (!strstr(raw->opt,"-EPHALL")) {
        if (eph.iode==raw->nav.eph[sat-1].iode) return 0; /* unchanged */
    }
    eph.sat=sat;
    raw->nav.eph[sat-1]=eph;
    raw->ephsat=sat;
    return 2;
}
/* adjust daily rollover of time ---------------------------------------------*/
static gtime_t adjday(gtime_t time, double tod)
{
    double ep[6],tod_p;
    time2epoch(time,ep);
    tod_p=ep[3]*3600.0+ep[4]*60.0+ep[5];
    if      (tod<tod_p-43200.0) tod+=86400.0;
    else if (tod>tod_p+43200.0) tod-=86400.0;
    ep[3]=ep[4]=ep[5]=0.0;
    return timeadd(epoch2time(ep),tod);
}
/* decode gloephem -----------------------------------------------------------*/
static int decode_gloephem(int sat, raw_t *raw)
{
    geph_t geph={0};
    unsigned char *p=(raw->buff)+2;
    int prn,tk,tb;
    
    if (raw->len>=93) {
        prn        =I1(p+ 1);
        geph.frq   =I1(p+ 2);
        geph.pos[0]=R8(p+ 3);
        geph.pos[1]=R8(p+11);
        geph.pos[2]=R8(p+19);
        geph.vel[0]=R8(p+27) * 1e+3;
        geph.vel[1]=R8(p+35) * 1e+3;
        geph.vel[2]=R8(p+43) * 1e+3;
        geph.acc[0]=R8(p+51) * 1e+6;
        geph.acc[1]=R8(p+59) * 1e+6;
        geph.acc[2]=R8(p+67) * 1e+6;
        tb = R8(p+75) * 1e-3;
        tk = tb;
        geph.gamn  =R4(p+83);
        geph.taun  =R4(p+87) * 1e-3;
        geph.age   =I2(p+91);
    }
    else {
        trace(2,"nvs NE length error: len=%d\n",raw->len);
        return -1;
    }
    if (!(geph.sat=satno(SYS_GLO,prn))) {
        trace(2,"nvs NE satellite error: prn=%d\n",prn);
        return -1;
    }
    if (raw->time.time==0) return 0;
    
    geph.iode=(tb/900)&0x7F;
    geph.toe=utc2gpst(adjday(raw->time,tb-10800.0));
    geph.tof=utc2gpst(adjday(raw->time,tk-10800.0));
#if 0
    /* check illegal ephemeris by toe */
    tt=timediff(raw->time,geph.toe);
    if (fabs(tt)>3600.0) {
        trace(3,"nvs NE illegal toe: prn=%2d tt=%6.0f\n",prn,tt);
        return 0;
    }
#endif
#if 0
    /* check illegal ephemeris by frequency number consistency */
    if (raw->nav.geph[prn-MINPRNGLO].toe.time&&
        geph.frq!=raw->nav.geph[prn-MINPRNGLO].frq) {
        trace(2,"nvs NE illegal freq change: prn=%2d frq=%2d->%2d\n",prn,
              raw->nav.geph[prn-MINPRNGLO].frq,geph.frq);
        return -1;
    }
    if (!strstr(raw->opt,"-EPHALL")) {
        if (fabs(timediff(geph.toe,raw->nav.geph[prn-MINPRNGLO].toe))<1.0&&
            geph.svh==raw->nav.geph[prn-MINPRNGLO].svh) return 0;
    }
#endif
    raw->nav.geph[prn-1]=geph;
    raw->ephsat=geph.sat;
    
    return 2;
}
/* decode NVS epehemerides in clear ------------------------------------------*/
static int decode_xf7eph(raw_t *raw)
{
    int prn,sat,sys;
    unsigned char *p=raw->buff;
    
    trace(4,"decode_xf7eph: len=%d\n",raw->len);
    
    if ((raw->len)<93) {
        trace(2,"nvs xf7eph length error: len=%d\n",raw->len);
        return -1;
    }
    sys = (U1(p+2)==1)?SYS_GPS:((U1(p+2)==2)?SYS_GLO:SYS_NONE);
    prn = U1(p+3);
    if (!(sat=satno(sys==1?SYS_GPS:SYS_GLO,prn))) {
        trace(2,"nvs xf7eph satellite number error: prn=%d\n",prn);
        return -1;
    }
    if (sys==SYS_GPS) {
        return decode_gpsephem(sat,raw);
    }
    else if (sys==SYS_GLO) {
        return decode_gloephem(sat,raw);
    }
    return 0;
}
/* decode NVS rxm-sfrb: subframe buffer --------------------------------------*/
static int decode_xe5bit(raw_t *raw)
{
    int prn;
    int iBlkStartIdx, iExpLen, iIdx;
    unsigned int words[10];
    unsigned char uiDataBlocks, uiDataType;
    unsigned char *p=raw->buff;
    
    trace(4,"decode_xe5bit: len=%d\n",raw->len);
    
    p += 2;         /* Discard preamble and message identifier */
    uiDataBlocks = U1(p);
    
    if (uiDataBlocks>=16) {
        trace(2,"nvs xf5bit message error: data blocks %u\n", uiDataBlocks);
        return -1;
    }
    iBlkStartIdx = 1;
    for (iIdx = 0; iIdx < uiDataBlocks; iIdx++) {
        iExpLen = (iBlkStartIdx+10);
        if ((raw->len) < iExpLen) {
            trace(2,"nvs xf5bit message too short (expected at least %d)\n", iExpLen);
            return -1;
        }
        uiDataType = U1(p+iBlkStartIdx+1);
        
        switch (uiDataType) {
            case 1: /* Glonass */
                iBlkStartIdx += 19;
                break;
            case 2: /* GPS */
                iBlkStartIdx += 47;
                break;
            case 4: /* SBAS */
                prn = U1(p+(iBlkStartIdx+2)) + 120;
                
                /* sat = satno(SYS_SBS, prn); */
                /* sys = satsys(sat,&prn); */
                memset(words, 0, 10*sizeof(unsigned int));
                for (iIdx=0, iBlkStartIdx+=7; iIdx<10; iIdx++, iBlkStartIdx+=4) {
                    words[iIdx]=U4(p+iBlkStartIdx);
                }
                words[7] >>= 6;
                return sbsdecodemsg(raw->time,prn,words,&raw->sbsmsg) ? 3 : 0;
            default:
                trace(2,"nvs xf5bit SNS type unknown (got %d)\n", uiDataType);
                return -1;
        }
    }
    return 0;
}
/* decode NVS x4aiono --------------------------------------------------------*/
static int decode_x4aiono(raw_t *raw)
{
    unsigned char *p=raw->buff+2;
    
    trace(4,"decode_x4aiono: len=%d\n", raw->len);
    
    raw->nav.ion_gps[0] = R4(p   );
    raw->nav.ion_gps[1] = R4(p+ 4);
    raw->nav.ion_gps[2] = R4(p+ 8);
    raw->nav.ion_gps[3] = R4(p+12);
    raw->nav.ion_gps[4] = R4(p+16);
    raw->nav.ion_gps[5] = R4(p+20);
    raw->nav.ion_gps[6] = R4(p+24);
    raw->nav.ion_gps[7] = R4(p+28);
    
    return 9;
}
/* decode NVS x4btime --------------------------------------------------------*/
static int decode_x4btime(raw_t *raw)
{
    unsigned char *p=raw->buff+2;
    
    trace(4,"decode_x4btime: len=%d\n", raw->len);
    
    raw->nav.utc_gps[1] = R8(p   );
    raw->nav.utc_gps[0] = R8(p+ 8);
    raw->nav.utc_gps[2] = I4(p+16);
    raw->nav.utc_gps[3] = I2(p+20);
    raw->nav.leaps = I1(p+22);
    
    return 9;
}

/* check for big/little endian */
int is_big_endian()
{
    static const union
    {
        int i;
        char c;
    } u = {0x12345678};

    return u.c == 0x12;
}

/* convert 10-byte floating point number to double */
double convert_R10(unsigned short part1, unsigned int part2, unsigned int part3)
{
    double res;
    unsigned int bit;
    unsigned int resPart1=0, resPart2=0;
    short ex;
    int i;

    if(is_big_endian())
        bit=((short)1<<15)&part1;
    else
        bit=(1<<31)&part3;

    if(bit)
        resPart1=resPart1|(1<<31);

    if(is_big_endian())
        memcpy(&ex, &part1, sizeof(short));
    else
        memcpy(&ex, ((void*)&part3)+2, sizeof(short));

    ex -= 15360;

    for(i=0;i<11;i++)
    {
        bit = ((short)1<<i)&ex;
        if(bit)
            resPart1=resPart1|(1<<(i+20));
    }

    if(is_big_endian())
    {
        for(i=0;i<20;i++)
        {
            bit=(1<<(i+11))&part2;
            if(bit)
                resPart1=resPart1|(1<<i);
        }
        for(i=0;i<11;i++)
        {
            bit=(1<<i)&part2;
            if(bit)
                resPart2=resPart2|(1<<(i+21));
        }
        for(i=0;i<21;i++)
        {
            bit=(1<<(i+11))&part3;
            if(bit)
                resPart2=resPart2|(1<<i);
        }
        memcpy(&res, &resPart1, sizeof(unsigned int));
        memcpy(((void*)&res)+4, &resPart2, sizeof(unsigned int));

    }
    else
    {
        for(i=0;i<15;i++)
        {
            bit = (1<<i)&part3;
            if(bit)
                resPart1=resPart1|(1<<(i+5));
        }
        for(i=0;i<5;i++)
        {
            bit = (1<<(i+27))&part2;
            if(bit)
                resPart1=resPart1|(1<<i);
        }
        for(i=0;i<27;i++)
        {
            bit = (1<<i)&part2;
            if(bit)
                resPart2=resPart2|(1<<(i+5));
        }
        for(i=0;i<5;i++)
        {
            bit = ((unsigned short)1<<(i+11))&part1;
            if(bit)
                resPart2=resPart2|(1<<i);
        }
        memcpy(((void*)&res)+4, &resPart1, sizeof(unsigned int));
        memcpy(&res, &resPart2, sizeof(unsigned int));
    }
    return res;
}

/* decode NVS x88: PVT vector data ----------------------------------------*/
int decode_x88pvt(raw_t *raw)
{
    double latitude, longitude, height;
    float stdCoord;
    unsigned short timePart1;
    unsigned int timePart2, timePart3;
    short week;
    double velLatitude, velLongitude, velHeight;
    float deviation;
    unsigned char status;
    short prev, sol2d, diff_used, raim, diff_flag;

    unsigned char *p=raw->buff+2;

    double time = 0;

    trace(4,"decode_x88pvt: len=%d\n",raw->len);

    latitude = R8(p);
    longitude = R8(p+8);
    height = R8(p+16);
    stdCoord = R4(p+24);
    timePart1 = U2(p+28);
    timePart2 = U4(p+30);
    timePart3 = U4(p+34);
    week = I2(p+38);
    velLatitude = R8(p+40);
    velLongitude = R8(p+48);
    velHeight = R8(p+56);
    deviation = R4(p+64);
    status = U1(p+68);

    /* check gps week range */
    if (week>=4096) {
        trace(2,"nvs xf88pvt week error: week=%d\n",week);
        return -1;
    }
    week=adjgpsweek(week);

    raw->pvt.pos[0] = latitude;
    raw->pvt.pos[1] = longitude;
    raw->pvt.pos[2] = height;
    raw->pvt.vel[0] = velLatitude;
    raw->pvt.vel[1] = velLongitude;
    raw->pvt.vel[2] = velHeight;

    raw->pvt.std = stdCoord;
    raw->pvt.dev = deviation;

    raw->pvt.prev=status&(1<<7)!=0;
    raw->pvt.sol2d=status&(1<<6)!=0;
    raw->pvt.diff_used=status&(1<<4)!=0;
    raw->pvt.raim=status&(1<<3)!=0;
    raw->pvt.diff_flag=status&(1<<2)!=0;

    time=convert_R10(timePart1, timePart2, timePart3);

    raw->pvt.time = gpst2time(week, time*0.001);

    return 4;
}

/* decode NVS x40: almanac data ----------------------------------------*/
int decode_x40alm(raw_t *raw)
{
    unsigned char *p=raw->buff+2;
    unsigned char system, prn, health, hn;
    float e, i0, OMGd, OMG0, omg, m0, af0, af1, af01, taun, tn, Tndot;
    double a, t0a, Tn;
    unsigned short wn, na;
    unsigned short timePart1;
    unsigned int timePart2, timePart3;
    double time = 0;

    trace(4,"decode_x40alm: len=%d\n",raw->len);

    if(raw->len < 42)
        return 0;

    system = *p;
    switch(system) {
        case 1: /* GPS */
            prn = *(p+1);
            health = *(p+2);
            e = R4(p+4);
            i0 = R4(p+8);
            OMGd = R4(p+12);
            a = R8(p+16);
            OMG0 = R4(p+24);
            omg = R4(p+28);
            m0 = R4(p+32);
            af0 = R4(p+36);
            af1 = R4(p+40);
            af01 = R4(p+44);
            timePart1 = I2(p+48);
            timePart2 = U4(p+50);
            timePart3 = U4(p+54);
            time = convert_R10(timePart1,timePart2,timePart3);
            if(is_big_endian())
                wn = U2(p+72);
            else
                wn = U2(p+58);

            wn=adjgpsweek(wn);

            raw->nav.alm[prn-1].toa = gpst2time(wn, time*0.001);
            raw->nav.alm[prn-1].toas = time*0.001;

            raw->nav.alm[prn-1].A = a;
            raw->nav.alm[prn-1].M0 = m0;
            raw->nav.alm[prn-1].OMG0 = OMG0;
            raw->nav.alm[prn-1].OMGd = OMGd;
            raw->nav.alm[prn-1].e = e;
            raw->nav.alm[prn-1].f0 = af0;
            raw->nav.alm[prn-1].f1 = af1;
            raw->nav.alm[prn-1].i0 = i0;
            raw->nav.alm[prn-1].omg = omg;
            raw->nav.alm[prn-1].sat = prn;
            raw->nav.alm[prn-1].svh = health;
            raw->nav.alm[prn-1].week = wn;
            break;
        case 2: /* GLONASS */
          #ifdef ENAGLO
            prn = *(p+1);
            health = *(p+2);
            hn = *(p+3);
            taun = R4(p+4);
            OMG0 = R4(p+8);
            i0 = R4(p+12);
            e = R4(p+16);
            omg = R4(p+20);
            tn = R4(p+24);
            Tn = R8(p+28);
            Tndot = R4(p+36);
            if(is_big_endian())
                na = U2(p+54);
            else
                na = U2(p+40);

            raw->nav.galm[prn-1].sat=prn;
            raw->nav.galm[prn-1].svh=health;
            raw->nav.galm[prn-1].Hn=hn;
            raw->nav.galm[prn-1].tau=taun;
            raw->nav.galm[prn-1].lambda=OMG0;
            raw->nav.galm[prn-1].I=i0;
            raw->nav.galm[prn-1].eps=e;
            raw->nav.galm[prn-1].omg=omg;
            raw->nav.galm[prn-1].tn=tn;
            raw->nav.galm[prn-1].Tn=Tn;
            raw->nav.galm[prn-1].Tndot=Tndot;
            raw->nav.galm[prn-1].na=na;
          #endif
            break;
    }
    return 6;
}

/* decode NVS raw message ----------------------------------------------------*/
static int decode_nvs(raw_t *raw)
{
    int type=U1(raw->buff+1);
    
    trace(3,"decode_nvs: type=%02x len=%d\n",type,raw->len);

    sprintf(raw->msgtype,"NVS: type=%2d len=%3d",type,raw->len);
    switch (type) {
        case ID_XF5RAW:  return decode_xf5raw (raw);
        case ID_XF7EPH:  return decode_xf7eph (raw);
        case ID_XE5BIT:  return decode_xe5bit (raw);
        case ID_X4AIONO: return decode_x4aiono(raw);
        case ID_X4BTIME: return decode_x4btime(raw);
        case ID_X88PVT:  return decode_x88pvt (raw);
        case ID_X40ALM:  return decode_x40alm (raw);
        default: break;
    }
    return 0;
}
/* input NVS raw message from stream -------------------------------------------
* fetch next NVS raw data and input a message from stream
* args   : raw_t *raw   IO     receiver raw data control struct
*          unsigned char data I stream data (1 byte)
* return : status (-1: error message, 0: no message, 1: input observation data,
*                  2: input ephemeris, 3: input sbas message,
*                  9: input ion/utc parameter)
*
* notes  : to specify input options, set raw->opt to the following option
*          strings separated by spaces.
*
*          -EPHALL    : input all ephemerides
*          -TADJ=tint : adjust time tags to multiples of tint (sec)
*
*-----------------------------------------------------------------------------*/
extern int input_nvs(raw_t *raw, unsigned char data)
{
    trace(5,"input_nvs: data=%02x\n",data);
    
    /* synchronize frame */
    if ((raw->nbyte==0) && (data==NVSSYNC)) {
        
        /* Search a 0x10 */
        raw->buff[0] = data;
        raw->nbyte=1;
        return 0;
    }
    if ((raw->nbyte==1) && (data != NVSSYNC) && (data != NVSENDMSG)) {
        
        /* Discard double 0x10 and 0x10 0x03 at beginning of frame */
        raw->buff[1]=data;
        raw->nbyte=2;
        raw->flag=0;
        return 0;
    }
    /* This is all done to discard a double 0x10 */
    if (data==NVSSYNC) raw->flag = (raw->flag +1) % 2;
    if ((data!=NVSSYNC) || (raw->flag)) {
        
        /* Store the new byte */
        raw->buff[(raw->nbyte++)] = data;
    }
    /* Detect ending sequence */
    if ((data==NVSENDMSG) && (raw->flag)) {
        raw->len   = raw->nbyte;
        raw->nbyte = 0;
        
        /* Decode NVS raw message */
        return decode_nvs(raw);
    }
    if (raw->nbyte == MAXRAWLEN) {
        trace(2,"nvs message size error: len=%d\n",raw->nbyte);
        raw->nbyte=0;
        return -1;
    }
    return 0;
}
/* input NVS raw message from file ---------------------------------------------
* fetch next NVS raw data and input a message from file
* args   : raw_t  *raw   IO     receiver raw data control struct
*          FILE   *fp    I      file pointer
* return : status(-2: end of file, -1...9: same as above)
*-----------------------------------------------------------------------------*/
extern int input_nvsf(raw_t *raw, FILE *fp)
{
    int i,data, odd=0;
    
    trace(4,"input_nvsf:\n");
    
    /* synchronize frame */
    for (i=0;;i++) {
        if ((data=fgetc(fp))==EOF) return -2;
        
        /* Search a 0x10 */
        if (data==NVSSYNC) {
            
            /* Store the frame begin */
            raw->buff[0] = data;
            if ((data=fgetc(fp))==EOF) return -2;
            
            /* Discard double 0x10 and 0x10 0x03 */
            if ((data != NVSSYNC) && (data != NVSENDMSG)) {
                raw->buff[1]=data;
                break;
            }
        }
        if (i>=4096) return 0;
    }
    raw->nbyte = 2;
    for (i=0;;i++) {
        if ((data=fgetc(fp))==EOF) return -2;
        if (data==NVSSYNC) odd=(odd+1)%2;
        if ((data!=NVSSYNC) || odd) {
            
            /* Store the new byte */
            raw->buff[(raw->nbyte++)] = data;
        }
        /* Detect ending sequence */
        if ((data==NVSENDMSG) && odd) break;
        if (i>=4096) return 0;
    }
    raw->len = raw->nbyte;
    if ((raw->len) > MAXRAWLEN) {
        trace(2,"nvs length error: len=%d\n",raw->len);
        return -1;
    }
    /* decode nvs raw message */
    return decode_nvs(raw);
}
/* generate NVS binary message -------------------------------------------------
* generate NVS binary message from message string
* args   : char  *msg   I      message string
*            "RESTART  [arg...]" system reset
*            "CFG-SERI [arg...]" configure serial port property
*            "CFG-FMT  [arg...]" configure output message format
*            "CFG-RATE [arg...]" configure binary measurement output rates
*          unsigned char *buff O binary message
* return : length of binary message (0: error)
* note   : see reference [1][2] for details.
*-----------------------------------------------------------------------------*/
extern int gen_nvs(const char *msg, unsigned char *buff)
{
    unsigned char *q=buff;
    char mbuff[1024],*args[32],*p;
    unsigned int byte;
    int iRate,n,narg=0;
    unsigned char ui100Ms;
    
    trace(4,"gen_nvs: msg=%s\n",msg);
    
    strcpy(mbuff,msg);
    for (p=strtok(mbuff," ");p&&narg<32;p=strtok(NULL," ")) {
        args[narg++]=p;
    }
    *q++=NVSSYNC; /* DLE */
    
    if (!strcmp(args[0],"CFG-PVTRATE")) {
        *q++=ID_XD7ADVANCED;
        *q++=ID_X02RATEPVT;
        if (narg>1) {
            iRate = atoi(args[1]);
            *q++ = (unsigned char) iRate;
        }
    }
    else if (!strcmp(args[0],"CFG-RAWRATE")) {
        *q++=ID_XF4RATERAW;
        if (narg>1) {
            iRate = atoi(args[1]);
            switch(iRate) {
                case 2:  ui100Ms =  5; break;
                case 5:  ui100Ms =  2; break;
                case 10: ui100Ms =  1; break;
                default: ui100Ms = 10; break;
            }
            *q++ = ui100Ms;
        }
    }
    else if (!strcmp(args[0],"CFG-SMOOTH")) {
        *q++=ID_XD7SMOOTH;
        *q++ = 0x03;
        *q++ = 0x01;
        *q++ = 0x00;
    }
    else if (!strcmp(args[0],"CFG-BINR")) {
        for (n=1;(n<narg);n++) {
            if (sscanf(args[n], "%2x",&byte)) {
                *q++=(unsigned char)byte;
                if((unsigned char)byte==NVSSYNC)
                    *q++=(unsigned char)byte;
            }
        }
    }
    else return 0;
    
    n=(int)(q-buff);
    
    *q++=0x10; /* ETX */
    *q=0x03;   /* DLE */
    return n+2;
}
