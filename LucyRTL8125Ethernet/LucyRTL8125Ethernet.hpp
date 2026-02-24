/* LucyRTL8125Ethernet.hpp -- RTL8125 driver class definition.
*
* Copyright (c) 2020 Laura Müller <laura-mueller@uni-duesseldorf.de>
* All rights reserved.
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the Free
* Software Foundation; either version 2 of the License, or (at your option)
* any later version.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
* more details.
*
* Driver for Realtek RTL8125 PCIe 2.5GB ethernet controllers.
*
* This driver is based on Realtek's r8125 Linux driver (9.003.04).
*/

#include "LucyRTL8125Linux-900501.hpp"
#include "RTL8125LucyRxPool.hpp"

#ifdef DEBUG
#define DebugLog(args...) IOLog(args)
#else
#define DebugLog(args...)
#endif

#define    RELEASE(x)    if(x){(x)->release();(x)=NULL;}

#define super IOEthernetController

enum
{
    MEDIUM_INDEX_AUTO = 0,
    MEDIUM_INDEX_10HD,
    MEDIUM_INDEX_10FD,
    MEDIUM_INDEX_100HD,
    MEDIUM_INDEX_100FD,
    MEDIUM_INDEX_100FDFC,
    MEDIUM_INDEX_1000FD,
    MEDIUM_INDEX_1000FDFC,
    MEDIUM_INDEX_100FDEEE,
    MEDIUM_INDEX_100FDFCEEE,
    MEDIUM_INDEX_1000FDEEE,
    MEDIUM_INDEX_1000FDFCEEE,
    MEDIUM_INDEX_2500FD,
    MEDIUM_INDEX_2500FDFC,
    MEDIUM_INDEX_COUNT
};

#define MBit 1000000ULL

enum {
    kSpeed2500MBit = 2500*MBit,
    kSpeed1000MBit = 1000*MBit,
    kSpeed100MBit = 100*MBit,
    kSpeed10MBit = 10*MBit,
};

enum {
    kFlowControlOff = 0,
    kFlowControlOn = 0x01
};

enum {
    kEEETypeNo = 0,
    kEEETypeYes = 1,
    kEEETypeCount
};

#define kMacHdrLen      14
#define kIPv4HdrLen     20
#define kIPv6HdrLen     40

struct ip4_hdr_be {
    UInt8 hdr_len;
    UInt8 tos;
    UInt16 tot_len;
    UInt16 id;
    UInt16 frg_off;
    UInt8 ttl;
    UInt8 prot;
    UInt16 csum;
    UInt16 addr[4];
};

struct ip6_hdr_be {
    UInt32 vtc_fl;
    UInt16 pay_len;
    UInt8 nxt_hdr;
    UInt8 hop_l;
    UInt16 addr[16];
};

struct tcp_hdr_be {
    UInt16 src_prt;
    UInt16 dst_prt;
    UInt32 seq_num;
    UInt32 ack_num;
    UInt8 dat_off;
    UInt8 flags;
    UInt16 wnd;
    UInt16 csum;
    UInt16 uptr;
};


enum RtlStateFlags {
    __ENABLED = 0,      /* driver is enabled */
    __LINK_UP = 1,      /* link is up */
    __PROMISC = 2,      /* promiscuous mode enabled */
    __M_CAST = 3,       /* multicast mode enabled */
    __POLL_MODE = 4,    /* poll mode is active */
    __POLLING = 5,      /* poll routine is polling */
};

enum RtlStateMask {
    __ENABLED_M = (1 << __ENABLED),
    __LINK_UP_M = (1 << __LINK_UP),
    __PROMISC_M = (1 << __PROMISC),
    __M_CAST_M = (1 << __M_CAST),
    __POLL_MODE_M = (1 << __POLL_MODE),
    __POLLING_M = (1 << __POLLING),
};

/* RTL8125's Rx descriptor. */
typedef union RtlRxDesc {
    struct {
        UInt32 opts1;
        UInt32 opts2;
        UInt64 addr;
    } cmd;
    struct {
        UInt64 blen;
        UInt64 addr;
    } buf;
} RtlRxDesc;

/* RTL8125's Tx descriptor. */
typedef struct RtlTxDesc {
    UInt32 opts1;
    UInt32 opts2;
    UInt64 addr; /*
    UInt32 reserved0;
    UInt32 reserved1;
    UInt32 reserved2;
    UInt32 reserved3; */
} RtlTxDesc;

/* RTL8125's statistics dump data structure */
typedef struct RtlStatData {
    UInt64 txPackets;
    UInt64 rxPackets;
    UInt64 txErrors;
    UInt32 rxErrors;
    UInt16 rxMissed;
    UInt16 alignErrors;
    UInt32 txOneCollision;
    UInt32 txMultiCollision;
    UInt64 rxUnicast;
    UInt64 rxBroadcast;
    UInt32 rxMulticast;
    UInt16 txAborted;
    UInt16 txUnderun;
    /* new since RTL8125 */
    UInt64 txOctets;
    UInt64 rxOctets;
    UInt64 rxMulticast64;
    UInt64 txUnicast64;
    UInt64 txBroadcast64;
    UInt64 txMulticast64;
    UInt32 txPauseOn;
    UInt32 txPauseOff;
    UInt32 txPauseAll;
    UInt32 txDeferred;
    UInt32 txLateCollision;
    UInt32 txAllCollision;
    UInt32 txAborted32;
    UInt32 alignErrors32;
    UInt32 rxFrame2Long;
    UInt32 rxRunt;
    UInt32 rxPauseOn;
    UInt32 rxPauseOff;
    UInt32 rxPauseAll;
    UInt32 rxUnknownOpcode;
    UInt32 rxMacError;
    UInt32 txUnderrun32;
    UInt32 rxMacMissed;
    UInt32 rxTcamDropped;
    UInt32 tdu;
    UInt32 rdu;
} RtlStatData;

#define kTransmitQueueCapacity  1024

/* With up to 40 segments we should be on the save side. */
#define kMaxSegs 40

/* The number of descriptors must be a power of 2. */
#define kNumTxDesc    1024    /* Number of Tx descriptors */
#define kNumRxDesc    512    /* Number of Rx descriptors */
#define kTxLastDesc    (kNumTxDesc - 1)
#define kRxLastDesc    (kNumRxDesc - 1)
#define kTxDescMask    (kNumTxDesc - 1)
#define kRxDescMask    (kNumRxDesc - 1)
#define kTxDescSize    (kNumTxDesc*sizeof(struct RtlTxDesc))
#define kRxDescSize    (kNumRxDesc*sizeof(union RtlRxDesc))
#define kRxBufArraySize (kNumRxDesc * sizeof(struct rtlRxBufferInfo))
#define kTxBufArraySize (kNumTxDesc * sizeof(mbuf_t))

/* Numbers of IOMemoryDescriptors and IORanges for tx */
#define kNumTxMemDesc       (kNumTxDesc / 2)
#define kTxMemDescMask      (kNumTxMemDesc - 1)
#define kNumTxRanges        (kNumTxDesc + kMaxSegs)
#define kTxRangeMask        kTxDescMask
#define kTxMapMemSize       sizeof(struct rtlTxMapInfo)

/* Numbers of IOMemoryDescriptors and batch size for rx */
#define kRxMemBaseShift 4
#define kNumRxMemDesc   (kNumRxDesc >> kRxMemBaseShift)
#define kRxMapMemSize   (sizeof(struct rtlRxMapInfo))
#define kRxMemBatchSize (kNumRxDesc / kNumRxMemDesc)
#define kRxMemDescMask  (kRxMemBatchSize - 1)
#define kRxMemBaseMask  ~kRxMemDescMask


/* This is the receive buffer size (must be large enough to hold a packet). */
#define kRxBufferSize   PAGE_SIZE

#define kMCFilterLimit  32
#define kMaxMtu 9000
#define kMaxPacketSize (kMaxMtu + ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN)

/* statitics dump delay in ns. */
#define kStatDelayTime 1000000UL    /* 1ms */

/* RealtekRxPool capacities */
#define kRxPoolClstCap   100    /* mbufs with 4k cluster*/
#define kRxPoolMbufCap   50     /* mbufs without clusters */

/* Treshhold value to wake a stalled queue */
#define kTxQueueWakeTreshhold (kNumTxDesc / 10)

/* transmitter deadlock treshhold in seconds. */
#define kTxDeadlockTreshhold 6
#define kTxCheckTreshhold (kTxDeadlockTreshhold - 1)
#define kTimerPeriod    1000000000UL

/* MSS value position */
#define MSSShift_8125 18

/* This definitions should have been in IOPCIDevice.h. */
enum
{
    kIOPCIPMCapability = 2,
    kIOPCIPMControl = 4,
};

enum
{
    kIOPCIELinkCapability = 12,
    kIOPCIELinkControl = 16,
};

enum
{
    kIOPCIELinkCtlASPM = 0x0003,    /* ASPM Control */
    kIOPCIELinkCtlL0s = 0x0001,     /* L0s Enable */
    kIOPCIELinkCtlL1 = 0x0002,      /* L1 Enable */
    kIOPCIELinkCtlClkPM = 0x0004,   /* Clock PM Enable */
    kIOPCIELinkCtlCcc = 0x0040,     /* Common Clock Configuration */
    kIOPCIELinkCtlClkReqEn = 0x100, /* Enable clkreq */
};

enum
{
    kPowerStateOff = 0,
    kPowerStateOn,
    kPowerStateCount
};

#define kParamName "Driver Parameters"
#define kEnableEeeName "enableEEE"
#define kEnableCSO6Name "enableCSO6"
#define kEnableTSO4Name "enableTSO4"
#define kEnableTSO6Name "enableTSO6"
#define kPollInt2500Name "µsPollInt2500"
#define kDisableASPMName "disableASPM"
#define kDriverVersionName "Driver Version"
#define kFallbackName "fallbackMAC"
#define kNameLenght 64

#define kEnableRxPollName "rxPolling"

extern const struct RTLChipInfo rtl_chip_info[];

/*
 * Indicates if a tx IOMemoryDescriptor is in the prepared
 * (active) or completed state (inactive).
 */
enum
{
    kIOMemoryInactive = 0,
    kIOMemoryActive = 1
};

typedef struct rtlTxMapInfo {
    UInt16 txNextMem2Use;
    UInt16 txNextMem2Free;
    SInt16 txNumFreeMem;
    IOMemoryDescriptor *txMemIO[kNumTxMemDesc];
    IOAddressRange txMemRange[kNumTxRanges];
    IOAddressRange txSCRange[kMaxSegs];
} rtlTxMapInfo;

typedef struct rtlRxMapInfo {
    IOMemoryDescriptor *rxMemIO[kNumRxMemDesc];
    IOAddressRange rxMemRange[kNumRxDesc];
} rtlRxMapInfo;

typedef struct rtlRxBufferInfo {
    mbuf_t mbuf;
    IOPhysicalAddress64 phyAddr;
} rtlRxBufferInfo;



/**
 *  Known kernel versions
 */
enum KernelVersion {
    Tiger         = 8,
    Leopard       = 9,
    SnowLeopard   = 10,
    Lion          = 11,
    MountainLion  = 12,
    Mavericks     = 13,
    Yosemite      = 14,
    ElCapitan     = 15,
    Sierra        = 16,
    HighSierra    = 17,
    Mojave        = 18,
    Catalina      = 19,
    BigSur        = 20,
    Monterey      = 21,
    Ventura       = 22,
    Sonoma        = 23,
    Sequoia       = 24,
    Tahoe         = 25,
};

/**
 *  Kernel version major
 */
extern const int version_major;

class LucyRTL8125 : public super
{
    
    OSDeclareDefaultStructors(LucyRTL8125)
    
public:
    /* IOService (or its superclass) methods. */
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
    virtual bool init(OSDictionary *properties) override;
    virtual void free() override;
    
    /* Power Management Support */
    virtual IOReturn registerWithPolicyMaker(IOService *policyMaker) override;
    virtual IOReturn setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker ) override;
    virtual void systemWillShutdown(IOOptionBits specifier) override;

    /* IONetworkController methods. */
    virtual IOReturn enable(IONetworkInterface *netif) override;
    virtual IOReturn disable(IONetworkInterface *netif) override;
    
    virtual IOReturn outputStart(IONetworkInterface *interface, IOOptionBits options ) override;
    virtual IOReturn setInputPacketPollingEnable(IONetworkInterface *interface, bool enabled) override;
    virtual void pollInputPackets(IONetworkInterface *interface, uint32_t maxCount, IOMbufQueue *pollQueue, void *context) override;
    
    virtual void getPacketBufferConstraints(IOPacketBufferConstraints *constraints) const override;
    
    virtual IOOutputQueue* createOutputQueue() override;
    
    virtual const OSString* newVendorString() const override;
    virtual const OSString* newModelString() const override;
    
    virtual IOReturn selectMedium(const IONetworkMedium *medium) override;
    virtual bool configureInterface(IONetworkInterface *interface) override;
    
    virtual bool createWorkLoop() override;
    virtual IOWorkLoop* getWorkLoop() const override;
    
    /* Methods inherited from IOEthernetController. */
    virtual IOReturn getHardwareAddress(IOEthernetAddress *addr) override;
    virtual IOReturn setHardwareAddress(const IOEthernetAddress *addr) override;
    virtual IOReturn setPromiscuousMode(bool active) override;
    virtual IOReturn setMulticastMode(bool active) override;
    virtual IOReturn setMulticastList(IOEthernetAddress *addrs, UInt32 count) override;
    virtual IOReturn getChecksumSupport(UInt32 *checksumMask, UInt32 checksumFamily, bool isOutput) override;
    virtual IOReturn setWakeOnMagicPacket(bool active) override;
    virtual IOReturn getPacketFilters(const OSSymbol *group, UInt32 *filters) const override;
    
    virtual UInt32 getFeatures() const override;
    virtual IOReturn getMaxPacketSize(UInt32 * maxSize) const override;
    virtual IOReturn setMaxPacketSize(UInt32 maxSize) override;
    
private:
    bool initPCIConfigSpace(IOPCIDevice *provider);
    static IOReturn setPowerStateWakeAction(OSObject *owner, void *arg1, void *arg2, void *arg3, void *arg4);
    static IOReturn setPowerStateSleepAction(OSObject *owner, void *arg1, void *arg2, void *arg3, void *arg4);
    void getParams();
    bool setupMediumDict();
    bool initEventSources(IOService *provider);
    
    void interruptOccurred(OSObject *client, IOInterruptEventSource *src, int count);
    UInt32 rxInterrupt(IONetworkInterface *interface, uint32_t maxCount, IOMbufQueue *pollQueue, void *context);
    void txInterrupt();

    bool setupRxResources();
    bool setupTxResources();
    bool setupStatResources();
    void freeRxResources();
    void freeTxResources();
    void freeStatResources();
    
    static IOReturn refillAction(OSObject *owner, void *arg1, void *arg2, void *arg3, void *arg4);

    void clearRxTxRings();
    void discardPacketFragment();
    void checkLinkStatus();
    void setLinkUp();
    void setLinkDown();
    bool txHangCheck();

    /* Hardware initialization methods. */
    IOReturn identifyChip();
    bool initRTL8125();
    void initMacAddr(struct rtl8125_private *tp);
    void enableRTL8125();
    void disableRTL8125();
    void setupRTL8125();
    void setOffset79(UInt8 setting);
    void restartRTL8125();
    void setPhyMedium();
    UInt8 csiFun0ReadByte(UInt32 addr);
    void csiFun0WriteByte(UInt32 addr, UInt8 value);
    void enablePCIOffset99();
    void disablePCIOffset99();
    void initPCIOffset99();
    void setPCI99_180ExitDriverPara();
    void hardwareD3Para();
    UInt16 getEEEMode();
    void exitOOB();
    void powerDownPLL();
    void configPhyHardware();
    void configPhyHardware8125a1();
    void configPhyHardware8125a2();
    void configPhyHardware8125b1();
    void configPhyHardware8125b2();

    void rtl812xDumpTallyCounter(struct rtl8125_private *tp);
    static void runStatUpdateThread(thread_call_param_t param0);
    void statUpdateThread();

    /* Descriptor related methods. */
    void getChecksumResult(mbuf_t m, UInt32 status1, UInt32 status2);
    
    /* AppleVTD support methods*/
    bool setupRxMap();
    void freeRxMap();
    bool setupTxMap();
    void freeTxMap();

    void    interruptOccurredVTD(OSObject *client, IOInterruptEventSource *src, int count);
    UInt32  rxInterruptVTD(IONetworkInterface *interface, uint32_t maxCount,
                           IOMbufQueue *pollQueue, void *context);
    UInt32  txMapPacket(mbuf_t packet, IOPhysicalSegment *vector, UInt32 maxSegs);
    void    txUnmapPacket();
    UInt16  rxMapBuffers(UInt16 index, UInt16 count);

    /* Watchdog timer method. */
    void timerActionRTL8125(IOTimerEventSource *timer);

private:
    IOWorkLoop *workLoop;
    IOCommandGate *commandGate;
    IOPCIDevice *pciDevice;
    OSDictionary *mediumDict;
    IONetworkMedium *mediumTable[MEDIUM_INDEX_COUNT];
    IOBasicOutputQueue *txQueue;
    
    IOInterruptEventSource *interruptSource;
    IOTimerEventSource *timerSource;
    IOEthernetInterface *netif;
    IOMemoryMap *baseMap;
    IOMapper *mapper;
    
    /* transmitter data */
    IOBufferMemoryDescriptor *txBufDesc;
    IOPhysicalAddress64 txPhyAddr;
    IODMACommand *txDescDmaCmd;
    struct RtlTxDesc *txDescArray;
    IOMbufNaturalMemoryCursor *txMbufCursor;
    mbuf_t *txMbufArray;
    void *txBufArrayMem;
    rtlTxMapInfo *txMapInfo;
    void *txMapMem;
    UInt64 txDescDoneCount;
    UInt64 txDescDoneLast;
    UInt32 txNextDescIndex;
    UInt32 txDirtyDescIndex;
    UInt32 txTailPtr0;
    UInt32 txClosePtr0;
    SInt32 txNumFreeDesc;

    /* receiver data */
    IOBufferMemoryDescriptor *rxBufDesc;
    IOPhysicalAddress64 rxPhyAddr;
    IODMACommand *rxDescDmaCmd;
    RtlRxDesc *rxDescArray;
    rtlRxBufferInfo *rxBufArray;
    void *rxBufArrayMem;
    void *rxMapMem;
    rtlRxMapInfo *rxMapInfo;
    RTL8125LucyRxPool *rxPool;
    UInt64 multicastFilter;
    mbuf_t rxPacketHead;
    mbuf_t rxPacketTail;
    UInt32 rxConfigReg;
    UInt32 rxConfigMask;
    SInt32 rxPacketSize;
    UInt16 rxNextDescIndex;
    UInt16 rxMapNextIndex;

    /* power management data */
    unsigned long powerState;
    IOByteCount pcieCapOffset;
    IOByteCount pciPMCtrlOffset;

    /* statistics data */
    UInt32 deadlockWarn;
    IONetworkStats *netStats;
    IOEthernetStats *etherStats;
    IOBufferMemoryDescriptor *statBufDesc;
    IOPhysicalAddress64 statPhyAddr;
    IODMACommand *statDescDmaCmd;
    thread_call_t statCall;
    struct RtlStatData *statData;

    UInt32 mtu;
    UInt32 speed;
    UInt32 duplex;
    UInt16 flowCtl;
    UInt16 autoneg;
    UInt16 eeeCap;
    UInt16 eeeMode;
    struct pci_dev pciDeviceData;
    struct rtl8125_private linuxData;
    struct IOEthernetAddress currMacAddr;
    struct IOEthernetAddress origMacAddr;
    struct IOEthernetAddress fallBackMacAddr;

    UInt32 pollInterval2500;
    UInt32 intrMask;
    UInt32 intrMaskRxTx;
    UInt32 intrMaskTimer;
    UInt32 intrMaskPoll;

    UInt64 statDelay;
    UInt64 timerInterval;

    /* flags */
    UInt32 stateFlags;
    
    bool wolCapable;
    bool wolActive;
    bool enableTSO4;
    bool enableTSO6;
    bool useAppleVTD;
    
#ifdef DEBUG_INTR
    UInt32 tmrInterrupts;
    UInt32 lastRxIntrupts;
    UInt32 lastTxIntrupts;
    UInt32 lastTmrIntrupts;
#endif
};
