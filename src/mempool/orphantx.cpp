#include "orphantx.h"
#include "chaincontrol/validation.h"
#include "sbtccore/transaction/policy.h"
#include "sbtccore/block/blockencodings.h"
#include "mempoolcomponent.h"

SET_CPP_SCOPED_LOG_CATEGORY(CID_TX_MEMPOOL);

COrphanTxMgr::COrphanTxMgr()
{

}

COrphanTxMgr::~COrphanTxMgr()
{

}


bool COrphanTxMgr::AddOrphanTx(const CTransactionRef &tx, int64_t peer)
{
    const uint256 &hash = tx->GetHash();
    if (m_mapOrphanTransactions.count(hash))
        return false;

    // Ignore big transactions, to avoid a
    // send-big-orphans memory exhaustion attack. If a peer has a legitimate
    // large transaction with a missing parent then we assume
    // it will rebroadcast it later, after the parent transaction(s)
    // have been mined or received.
    // 100 orphans, each of which is at most 99,999 bytes big is
    // at most 10 megabytes of orphans and somewhat more byprev index (in the worst case):
    unsigned int sz = GetTransactionWeight(*tx);
    if (sz >= MAX_STANDARD_TX_WEIGHT)
    {
        WLogFormat( "ignoring large orphan tx (size: %u, hash: %s)", sz, hash.ToString());
        return false;
    }

    auto ret = m_mapOrphanTransactions.emplace(hash, OrphanTx{tx, peer, GetTime() + ORPHAN_TX_EXPIRE_TIME});
    assert(ret.second);
    for (const CTxIn &txin : tx->vin)
    {
        m_mapOrphanTransactionsByPrev[txin.prevout].insert(ret.first);
    }

    AddToCompactExtraTransactions(tx);

    NLogFormat("stored orphan tx %s (mapsz %u outsz %u)", hash.ToString(),
             m_mapOrphanTransactions.size(), m_mapOrphanTransactionsByPrev.size());
    return true;
}

int COrphanTxMgr::EraseOrphanTx(uint256 hash)
{
    auto it = m_mapOrphanTransactions.find(hash);
    if (it == m_mapOrphanTransactions.end())
        return 0;
    for (const CTxIn &txin : it->second.tx->vin)
    {
        auto itPrev = m_mapOrphanTransactionsByPrev.find(txin.prevout);
        if (itPrev == m_mapOrphanTransactionsByPrev.end())
            continue;
        itPrev->second.erase(it);
        if (itPrev->second.empty())
            m_mapOrphanTransactionsByPrev.erase(itPrev);
    }
    m_mapOrphanTransactions.erase(it);
    return 1;
}

unsigned int COrphanTxMgr::LimitOrphanTxSize(unsigned int nMaxOrphans)
{
    unsigned int nEvicted = 0;
    static int64_t nNextSweep;
    int64_t nNow = GetTime();
    if (nNextSweep <= nNow)
    {
        // Sweep out expired orphan pool entries:
        int nErased = 0;
        int64_t nMinExpTime = nNow + ORPHAN_TX_EXPIRE_TIME - ORPHAN_TX_EXPIRE_INTERVAL;
        auto iter = m_mapOrphanTransactions.begin();
        while (iter != m_mapOrphanTransactions.end())
        {
            auto maybeErase = iter++;
            if (maybeErase->second.nTimeExpire <= nNow)
            {
                nErased += EraseOrphanTx(maybeErase->second.tx->GetHash());
            } else
            {
                nMinExpTime = std::min(maybeErase->second.nTimeExpire, nMinExpTime);
            }
        }
        // Sweep again 5 minutes after the next entry that expires in order to batch the linear scan.
        nNextSweep = nMinExpTime + ORPHAN_TX_EXPIRE_INTERVAL;
        if (nErased > 0)
            NLogFormat("Erased %d orphan tx due to expiration", nErased);
    }
    while (m_mapOrphanTransactions.size() > nMaxOrphans)
    {
        // Evict a random orphan:
        uint256 randomhash = GetRandHash();
        auto it = m_mapOrphanTransactions.lower_bound(randomhash);
        if (it == m_mapOrphanTransactions.end())
            it = m_mapOrphanTransactions.begin();
        EraseOrphanTx(it->first);
        ++nEvicted;
    }
    return nEvicted;
}

int COrphanTxMgr::EraseOrphansFor(int64_t peer)
{
    int nErased = 0;
    std::map<uint256, OrphanTx>::iterator iter = m_mapOrphanTransactions.begin();
    while (iter != m_mapOrphanTransactions.end())
    {
        auto maybeErase = iter++; // increment to avoid iterator becoming invalid
        if (maybeErase->second.fromPeer == peer)
        {
            nErased += EraseOrphanTx(maybeErase->second.tx->GetHash());
        }
    }
    if (nErased > 0)
        NLogFormat("Erased %d orphan tx from peer=%d", nErased, peer);

    return nErased;
}

void COrphanTxMgr::Clear()
{
    //LOCK(cs_main);
    m_mapOrphanTransactions.clear();
    m_mapOrphanTransactionsByPrev.clear();
}

bool COrphanTxMgr::Exists(uint256 hash)
{
    return  (m_mapOrphanTransactions.count(hash) != 0);
}

int COrphanTxMgr::FindOrphanTransactionsByPrev(const COutPoint& op, ITBYPREV& itByPrev)
{
    itByPrev = m_mapOrphanTransactionsByPrev.find(op);
    if (itByPrev == m_mapOrphanTransactionsByPrev.end())
    {
        return 0;
    }
    return 1;
}