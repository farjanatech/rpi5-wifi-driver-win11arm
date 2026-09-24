static void WireHeader(UCHAR *p,unsigned len,unsigned channel,unsigned seq)
{
    CywPut16(p,(uint16_t)len);CywPut16(p+2,(uint16_t)~len);
    p[4]=(UCHAR)seq;p[5]=(UCHAR)channel;p[7]=12;p[9]=32;
}
static void InitGlom(void)
{
    Init();ScriptOn=1;ScriptBytes=576;TestNetwork.RxPending=TRUE;TestNetwork.RxBatch=TRUE;
    TestAdapter.RxGlomEnabled=1;
    WireHeader(RxScript,18,0x83,9);
    CywPut16(RxScript+12,76);CywPut16(RxScript+14,64);CywPut16(RxScript+16,64);
    WireHeader(RxScript+64,204,3,10);RxScript[64+9]=35;
    WireHeader(RxScript+76,64,2,10);RxScript[76+9]=255;RxScript[76+8]=255;
    WireHeader(RxScript+140,64,1,11);
    WireHeader(RxScript+204,64,2,12);
}
static void TestPerformanceRx(void)
{
    unsigned i,n;UCHAR payload[1518];CYW_RX_GLOM g;
    memset(payload,0x20,sizeof(payload));
    /* A firmware-provided next length removes the separate 64-byte header
     * read. Check alignment/length before dispatch, never guess from MTU. */
    Init();ScriptOn=1;ScriptBytes=1600;TestNetwork.RxPending=TRUE;TestNetwork.RxBatch=TRUE;
    WireHeader(RxScript,64,2,0);RxScript[6]=96;
    WireHeader(RxScript+64,1530,2,1);
    CHECK(Poll()==0 && TestNetwork.RxNextLength==1536);
    CHECK(Poll()==0 && Delivered==2 && FifoCalls==2 && ScriptAt==1600 && TestAdapter.RxReadAhead==1);
    for(i=0;i<3;++i) {
        Init();ScriptOn=1;ScriptBytes=128;TestNetwork.RxPending=TRUE;TestNetwork.RxBatch=TRUE;
        TestNetwork.RxNextLength=128;WireHeader(RxScript,i==0?96:128,i==1?0:2,1);
        if(i==2)FailFifo=1;
        CHECK(!NT_SUCCESS(Poll()) && !Delivered && Aborts==1 && Terms==1 && !TestNetwork.RxNextLength);
    }
    Init();ScriptOn=1;ScriptBytes=64;TestNetwork.RxPending=TRUE;TestNetwork.RxNextLength=1536;
    WireHeader(RxScript,64,0,1);
    CHECK(Poll()==0 && ScriptAt==64 && !TestAdapter.RxReadAhead); /* control always header-first */
    for(n=0;n<256;++n) {
        memset(Rx,0,64);Rx[6]=(UCHAR)n;
        CHECK(CywRxNextLength(Rx)==(n>=4 && n<=128?n*16:0));
    }
    InitGlom();CHECK(Poll()==0 && TestNetwork.RxGlom.Count==3 && !Delivered);
    CHECK(Poll()==0 && Delivered==1 && Events==0 && TestAdapter.RxGlomGroups==1);
    CHECK(TestNetwork.TxMax==35 && TestNetwork.TxFlow==0 && FifoCalls==2);
    /* TX/control status traffic cannot overwrite the retained RX superframe. */
    CHECK(CywSendFrame(&TestAdapter,2,payload,4)==0);
    CHECK(Poll()==0 && Events==1 && FifoCalls==3);
    CHECK(Poll()==0 && Delivered==2 && !TestNetwork.RxGlom.Count && TestAdapter.RxGlomFrames==3);
    CHECK(!TestAdapter.Transport.SequenceMismatches && TestAdapter.Transport.SequenceExpected==13);
    /* The LAST child's error must prevent delivery of the first child too. */
    for(i=0;i<8;++i) {
        InitGlom();
        if(i==0)RxScript[204+2]^=1; /* checksum */
        if(i==1)RxScript[204+7]=65; /* offset beyond length */
        if(i==2)RxScript[204+5]=0; /* control forbidden in aggregate */
        if(i==3)RxScript[204+5]=0x82; /* nested descriptor */
        if(i==4)RxScript[64+5]=0x83; /* outer descriptor flag */
        if(i==5)RxScript[64+7]=70; /* first slot cannot contain child header */
        if(i==6) {CywPut16(RxScript+64,513);CywPut16(RxScript+66,(uint16_t)~513u);}
        if(i==7)FailFifo=2;
        CHECK(Poll()==0);
        CHECK(!NT_SUCCESS(Poll()) && !Delivered && !Events && !TestNetwork.RxGlom.Count);
        CHECK(Aborts==1 && Terms==1 && TestAdapter.RxGlomErrors==1 && !TestAdapter.RxGlomGroups);
    }
    InitGlom();CHECK(Poll()==0);TestNetwork.Stop=1;
    CHECK(Poll()==STATUS_CANCELLED && FifoCalls==1 && !Delivered);
    /* Descriptors are untrusted lengths, never allocation requests. */
    memset(&g,0,sizeof(g));memset(Rx,0,128);
    CHECK(!CywRxGlomDescriptor(&g,Rx,0));CHECK(!CywRxGlomDescriptor(&g,Rx,3));
    CHECK(!CywRxGlomDescriptor(&g,Rx,66));
    CywPut16(Rx,20);CHECK(!CywRxGlomDescriptor(&g,Rx,2));
    CywPut16(Rx,25);CHECK(!CywRxGlomDescriptor(&g,Rx,2));
    CywPut16(Rx,65532);CywPut16(Rx+2,12);CHECK(!CywRxGlomDescriptor(&g,Rx,4));
    for(i=0;i<32;++i)CywPut16(Rx+2*i,64);
    CHECK(CywRxGlomDescriptor(&g,Rx,64) && g.Count==32 && g.Bytes==2048);
    /* A single descriptor entry is valid; it still has an outer and child header. */
    InitGlom();WireHeader(RxScript,14,0x83,9);CywPut16(RxScript+12,76);
    WireHeader(RxScript+64,76,3,10);
    CHECK(Poll()==0 && Poll()==0 && Delivered==1 && !TestNetwork.RxGlom.Count);
    Init();TestAdapter.FifoBlockReady=1;
    CHECK(CywSendFrame(&TestAdapter,2,payload,sizeof(payload))==0 && LastWriteLength==1536);
    for(i=0;i<1536;++i)CHECK(!Tx[i]); /* sensitive buffer scrubbed after send */
    puts("Checked actual RX read-ahead, complete-before-delivery glom validation, retained-buffer fairness, malformed/fault/stop paths and zero-padded TX.");
}
