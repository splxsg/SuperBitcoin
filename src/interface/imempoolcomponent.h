#pragma once

#include <vector>
#include "componentid.h"
#include "exchangeformat.h"
#include "sbtccore/streams.h"
#include "p2p/bloom.h"
#include "framework/component.hpp"
#include "mempool/txmempool.h"

class ITxMempoolComponent : public appbase::TComponent<ITxMempoolComponent>
{
public:
    virtual ~ITxMempoolComponent()
    {
    }

    enum
    {
        ID = CID_TX_MEMPOOL
    };

    virtual int GetID() const override
    {
        return ID;
    }

    virtual bool ComponentInitialize() = 0;

    virtual bool ComponentStartup() = 0;

    virtual bool ComponentShutdown() = 0;

    virtual CTxMemPool &GetMemPool() = 0;

    virtual bool NetRequestTxData(ExNode *xnode, uint256 txHash, bool witness, int64_t timeLastMempoolReq) = 0;

    virtual bool NetReceiveTxData(ExNode *xnode, CDataStream &stream, uint256 &txHash) = 0;

    virtual bool NetRequestTxInventory(ExNode *xnode, bool sendMempool, int64_t minFeeFilter, CBloomFilter *txFilter,
                                       std::vector<uint256> &toSendTxHashes,
                                       std::vector<uint256> &haveSentTxHashes) = 0;

    virtual void AddToCompactExtraTransactions(const CTransactionRef &tx) = 0;
    //add other interface methods here ...

};

#define GET_TXMEMPOOL_INTERFACE(ifObj) \
    auto ifObj = appbase::CApp::Instance().FindComponent<ITxMempoolComponent>()
