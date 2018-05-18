#include "arch/power/radixwalk.hh"

#include <memory>

#include "arch/power/miscregs.hh"
#include "arch/power/tlb.hh"
#include "base/bitfield.hh"
#include "cpu/base.hh"
#include "cpu/thread_context.hh"
#include "mem/packet_access.hh"
#include "mem/request.hh"

namespace PowerISA {

Fault
RadixWalk::start(ThreadContext * tc, RequestPtr req, BaseTLB::Mode mode)
{
    uint64_t pate = getRPDEntry(tc);
    uint64_t rpdb = pate & 0x0fffffffffffff00;
    uint64_t rpds = pate & 0x000000000000001f;
    uint64_t pagesize = ((((pate >> 58) & 0x18) | ((pate >> 5) & 0x7)) + 31);
    Addr vaddr = req->getVaddr();
    Addr paddr = this->walkTree(vaddr,rpdb,rpds,pagesize);
    req->setPaddr(paddr);
    return NoFault;
}

uint64_t
RadixWalk::getRPDEntry(ThreadContext * tc)
{
    Ptcr ptcr = tc->readIntReg(INTREG_PTCR);
    uint32_t lpidr = tc->readIntReg(INTREG_LPIDR);
    uint64_t baseaddr = ptcr.patb+(lpidr*sizeof(uint64_t)*2)+8;
    uint64_t dataSize = 8;
    Request::Flags flags = Request::PHYSICAL;
    RequestPtr request = new Request(baseaddr, dataSize, flags,
                                     this->masterId);
    Packet *read = new Packet(request, MemCmd::ReadReq);
    read->allocate();
    uint64_t pate1 = read->get<uint64_t>();
    printf("2nd Quad word of partition table entry: %lx\n",pate1);
    uint64_t prtb = (pate1 & 0x0fffffffffffff000)>>12;
    prtb = prtb + (tc->readIntReg(INTREG_PIDR))*sizeof(prtb)*2 ;
    request = new Request(prtb, dataSize, flags,
                                     this->masterId);
    read = new Packet(request, MemCmd::ReadReq);
    read->allocate();
    uint64_t prtbe = read->get<uint64_t>();
    printf("process table entry: %lx\n\n",prtbe);
    return prtbe;
}

Addr
RadixWalk::walkTree(Addr vaddr ,uint64_t ptbase ,
                    uint64_t ptsize ,uint64_t pagesize)
{
        uint64_t index;
        uint64_t datasize = 8;
        index = ((vaddr >> (pagesize - ptsize)) & ((1UL << ptsize) - 1));
        uint64_t entryAddr = ptbase + ( index * sizeof(uint64_t));
        pagesize = pagesize - ptsize;
        Request::Flags flags = Request::PHYSICAL;
        RequestPtr request = new Request(entryAddr,datasize,
                                         flags,this->masterId);
        Packet *read = new Packet(request, MemCmd::ReadReq);
        read->allocate();
        Rpde rpde = read->get<uint64_t>();
        delete read->req;
        if (rpde.leaf == 1)
        {
                uint64_t realpn = rpde & 0x01fffffffffff000;
                uint64_t pageMask = (1UL << pagesize) - 1;
                Addr paddr = (realpn & ~pageMask) | (vaddr & pageMask);
                return paddr;
        }
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
    else
        return MemObject::getMasterPort(if_name, idx);
}

/* end namespace PowerISA */ }

PowerISA::RadixWalk *
PowerRadixWalkParams::create()
{
    return new PowerISA::RadixWalk(this);
}
