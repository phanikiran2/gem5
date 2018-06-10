#include "arch/power/radixwalk.hh"

#include <memory>

#include "arch/power/miscregs.hh"
#include "arch/power/tlb.hh"
#include "base/bitfield.hh"
#include "cpu/base.hh"
#include "cpu/thread_context.hh"
#include "debug/RadixWalk.hh"
#include "mem/packet_access.hh"
#include "mem/request.hh"

namespace PowerISA {

Fault
RadixWalk::start(ThreadContext * tc, RequestPtr req, BaseTLB::Mode mode)
{
    // prtbe ---> Process table base
    uint64_t prtbe = getRPDEntry(tc);
    // rpds --->  Root Page Directory Base
    uint64_t rpdb = (prtbe & 0x0fffffffffffff00) >> 8;
    uint64_t rpds = prtbe & 0x000000000000001f;
    // PageSize ---> radix tree size
    uint64_t pagesize = ((((prtbe >> 58) & 0x18) | ((prtbe >> 5) & 0x7)) + 31);
    Addr vaddr = req->getVaddr();
    DPRINTF(RadixWalk,"RPDB: %lx\nRPDS: %lx\nPageSize: %lx\n\n"
            ,rpdb,rpds,pagesize);
    Addr paddr = this->walkTree(vaddr,rpdb,rpds,pagesize);
    req->setPaddr(paddr);
    DPRINTF(RadixWalk,"Radix Translated %#x -> %#x",vaddr,paddr);
    return NoFault;
}

uint64_t
RadixWalk::getRPDEntry(ThreadContext * tc)
{
    Ptcr ptcr = tc->readIntReg(INTREG_PTCR);
    DPRINTF(RadixWalk,"PTCR:%lx\n",(uint64_t)ptcr);
    uint32_t lpidr = tc->readIntReg(INTREG_LPIDR);
    DPRINTF(RadixWalk,"LPIDR: %x\n",lpidr);
    //Accessing 2nd double wod of partition table (pate1)
    uint64_t baseaddr = (ptcr.patb << 12)+(lpidr*sizeof(uint64_t)*2)+8;
    uint64_t dataSize = 8;
    Request::Flags flags = Request::PHYSICAL;
    RequestPtr request = new Request(baseaddr, dataSize, flags,
                                     this->masterId);
    Packet *read = new Packet(request, MemCmd::ReadReq);
    read->allocate();
    this->port.sendAtomic(read);
    uint64_t pate1 = read->get<uint64_t>();
    DPRINTF(RadixWalk,"2nd Double word of partition table entry: %lx\n",pate1);
    uint64_t prtb = (pate1 & 0x0ffffffffffff000);
    delete read->req;
    prtb = prtb + (tc->readIntReg(INTREG_PIDR))*sizeof(prtb)*2 ;
    DPRINTF(RadixWalk,"Process table base: %lx\n",prtb);
    flags = Request::PHYSICAL;
    request = new Request(prtb, dataSize, flags,
                                     this->masterId);
    read = new Packet(request, MemCmd::ReadReq);
    read->allocate();
    this->port.sendAtomic(read);
    //Prtbe ---> Process Table Entry
    uint64_t prtbe = read->get<uint64_t>();
    DPRINTF(RadixWalk,"process table entry: %lx\n\n",prtbe);
    delete read->req;
    return prtbe;
}

Addr
RadixWalk::walkTree(Addr vaddr ,uint64_t ptbase ,
                    uint64_t ptsize ,uint64_t pagesize)
{
        uint64_t index;
        uint64_t datasize = 8;
        index = ((vaddr >> (pagesize - ptsize)) & ((1UL << ptsize) - 1));
        uint64_t entryAddr = ptbase + (index * sizeof(uint64_t));
        pagesize = pagesize - ptsize;
        Request::Flags flags = Request::PHYSICAL;
        RequestPtr request = new Request(entryAddr,datasize,
                                         flags,this->masterId);
        Packet *read = new Packet(request, MemCmd::ReadReq);
        read->allocate();
        this->port.sendAtomic(read);
        Rpde rpde = read->get<uint64_t>();
        delete read->req;
        printf("rpde:%lx\n",(uint64_t)rpde);
        if (rpde.leaf == 1)
        {
                uint64_t realpn = rpde & 0x01fffffffffff000;
                uint64_t pageMask = (1UL << pagesize) - 1;
                Addr paddr = (realpn & ~pageMask) | (vaddr & pageMask);
                printf("paddr:%lx\n",paddr);
                return paddr;
        }
        DPRINTF(RadixWalk,"NLB: %lx\n",(uint64_t)rpde.nextLevelBase);
        DPRINTF(RadixWalk,"NLS: %lx\n",(uint64_t)rpde.nextLevelSize);
        DPRINTF(RadixWalk,"PageSize: %lx",(uint64_t)pagesize);
        return walkTree(vaddr ,rpde.nextLevelBase ,
                        rpde.nextLevelSize ,pagesize);
}

void
RadixWalk::RadixPort::recvReqRetry()
{

}

bool
RadixWalk::RadixPort::recvTimingResp(PacketPtr pkt)
{
    return true;
}

BaseMasterPort &
RadixWalk::getMasterPort(const std::string &if_name, PortID idx)
{
    if (if_name == "port")
        return port;
    else{
        return MemObject::getMasterPort(if_name, idx);
    }
}

/* end namespace PowerISA */ }

PowerISA::RadixWalk *
PowerRadixWalkParams::create()
{
    return new PowerISA::RadixWalk(this);
}
