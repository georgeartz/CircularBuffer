//
//  CircularBuffer.cpp
//
//  CircularBuffer command line utility
//
//  Created by George Artz on 3/25/20.
//
//  Implement circular buffer with support for
//  add and delete of variable sized blocks.
//  Return buf stats on add and delete requests.
//  Originally intended to use C, but used C++ to
//  use std::map instead of doing a C red-black tree.
//
//  Public interfaces:
//  1. class CircularBuffer(bufSize)
//  1. addBlockToBuf(byte *block, size_t blockSize, BufInfo &bufInfo)
//  2. deleteBlockToBuf(byte *block, BufInfo &bufInfo)
//  3. printBuf()
//
//  Deletes of blocks are handled lazily to minimize movement
//  of data in the circular buffer. Deleted blocks are compacted out
//  when 1) space is needed for an add block request and 2) printBuf operation
//
//  Uses google test
//
//  Dev Env:
//  MacBook Pro, MacOS Catalina 10.15.3, XCode 11.4
//

// #include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <gtest/gtest.h>

typedef size_t uint_t;  // typedef for max unsigned int
typedef uint8_t byte;

enum ResultCode : int {
    RC_SUCCESS                       = 0,
    RC_BLOCK_SIZE_EXCEEDS_FREE_SPACE = -1,
    RC_BLOCK_ADD_FAILED_DUPLICATE    = -2,
    RC_BLOCK_ADD_FAILED_MAP_ERR      = -3,
    RC_BLOCK_NOT_FOUND               = -4,
    RC_BLOCK_MAP_ERASE_ERR           = -5
};

struct BufInfo
{
    uint_t bufSize;
    uint_t bufBytesFree;
    uint_t bufBytesUsed;
};

struct BufBlock
{
    BufBlock *next;
    BufBlock *prev;
    byte     *blockInBuf;
    byte     *blockMapKey;
    uint_t    blockSize;
    bool      blockDeletePending;
};

struct CircularBuffer
{
public:
    CircularBuffer(uint_t bufSz)
    {
        initBuf(bufSz);
    }
    
    ~CircularBuffer()
    {
        if (buf) free(buf);
    }

private:
    void initBuf(uint_t bufSz)
    {
        buf            = (byte*) malloc(bufSz);
        bufSize        = (buf == 0) ? 0 : bufSz;
        bufStart       = buf;
        bufEnd         = bufStart;
        bufBytesFree   = bufSize;
        bufLastPlusOne = buf + bufSize;
        bufBlockHead   = 0;
        bufBlockTail   = 0;
        bufBytesDeletePending = 0;
        return;
    }
        
    void setBufInfo(BufInfo *bi)
    {
        bi->bufSize = bufSize;
        bi->bufBytesFree = bufBytesFree + bufBytesDeletePending;
        bi->bufBytesUsed = (bufSize - bufBytesFree) - bufBytesDeletePending;
    }
    
    void removeBlockFromList(BufBlock *bufBlock)
    {
        if (bufBlock == bufBlockHead)
        {
            BufBlock *next = bufBlock->next;
            bufBlockHead = next;
            if (next)
                next->prev = 0;
            else
                bufBlockTail = 0;
            return;
        }
        if (bufBlock == bufBlockTail)
        {
            BufBlock *prev = bufBlock->prev;
            bufBlockTail = prev;
            if (prev)
                prev->next = 0;
            else
                bufBlockHead = 0;
            return;
        }
        BufBlock *prev = bufBlock->prev;
        BufBlock *next = bufBlock->next;
        prev->next = next;
        next->prev = prev;
    }
    
    void removeBlockFromBuf(byte *blk, uint_t blksz)
    {
        byte *dst, *src;
        dst = blk;
        src = blk + blksz;
        if (src >= bufLastPlusOne)
        {
            uint_t len = src - bufLastPlusOne;
            src = buf + len;
        }
        uint_t bytesToMove;
        bytesToMove = (src <= bufEnd) ? (bufEnd - src) : ((bufLastPlusOne - src) +                                                        (bufEnd - buf));
        if (blk == bufBlockHead->blockInBuf)
        {
            bufStart = src;
            bytesToMove = 0;
        }
        if (blk == bufBlockTail->blockInBuf)
        {
            bufEnd = dst;
            bytesToMove = 0;
        }

        bool case1 = (src > dst);
        while (bytesToMove)
        {
            // Case1: buf...dst..src...bufLastPlusOne
            // Max length of first move is limited to length from src to end of buf
            //
            // Case2: buf...src...dst...bufLastPlusOne
            // Max length of first move is limited to length from dst to end of buf
            //
            uint_t len1 = bufLastPlusOne - ((case1) ? src : dst);
            if (len1 >= bytesToMove)
            {
                // Case1A: buf...dst...src...bufEnd......bufLastPlusOne
                // Case2A: buf...src...budEnd...dst......bufLastPlusOne
                memmove(dst, src, bytesToMove);
                bufEnd = dst + bytesToMove;
                break;
            }
            // Case1B:  buf...bufEnd...dst...src...bufLastPlusOne wrap!!
            // Case2B:  buf...src.......bufEnd...dst...bufLastPlusOne wrap!!
            memmove(dst, src, len1);
            bytesToMove -= len1;
            dst += len1;
            if (case1)
            {
                uint_t len2 = bufLastPlusOne - dst;
                src = buf;
                memmove(dst, src, len2);
                bytesToMove -= len2;
                src += len2;
            }
            else
                src += len1;
            dst = buf;
            memmove(dst, src, bytesToMove);
            bufEnd = dst + bytesToMove;
            break;
        }
                    
        bufBytesDeletePending -= blksz;
        bufBytesFree += blksz;
    }
    
    // Update addresses of blocks in circular buffer after compaction
    void updateBufBlockList()
    {
        byte *block = bufStart;
        BufBlock *bufBlock = bufBlockHead;
        while(bufBlock)
        {
            bufBlock->blockInBuf = block;
            block = block + bufBlock->blockSize;
            if (block >= bufLastPlusOne)
                block = (block - bufLastPlusOne) + buf;
            bufBlock = bufBlock->next;
        }
    }
    
    // Compact blocks that are pending delete.
    // Move trailing space to replace deleted blocks
    void compactBuf()
    {
        if (bufBytesDeletePending == 0)
            return;
        BufBlock *bufBlock, *bufBlockPrev;
        
        // Process deleted block from tail to head
        // (most recent added block first)
        bufBlock = bufBlockTail;
        while (bufBlock)
        {
            if (bufBlock->blockDeletePending == false)
            {
                bufBlock = bufBlock->prev;
                continue;
            }
            
            removeBlockFromBuf(bufBlock->blockInBuf, bufBlock->blockSize);
            removeBlockFromList(bufBlock);
            deleteBlockFromMap(bufBlock->blockMapKey);
            bufBlockPrev = bufBlock->prev;
            free(bufBlock);
            bufBlock = bufBlockPrev;
        }
        
        updateBufBlockList();
        
        return;
    }
    
    int checkSize(uint_t blockSize)
    {
        if (blockSize <= bufBytesFree)
            return RC_SUCCESS;
        
        if (blockSize > (bufBytesFree + bufBytesDeletePending))
            return RC_BLOCK_SIZE_EXCEEDS_FREE_SPACE;
            
        compactBuf();
        
        return (blockSize <= bufBytesFree) ? RC_SUCCESS : RC_BLOCK_SIZE_EXCEEDS_FREE_SPACE;
    }

    int findBlockInMap(byte *block, BufBlock **ppBufBlock=NULL)
    {
        auto it = blockMap.find(block);
        if (it == blockMap.end())
            return RC_BLOCK_NOT_FOUND;
        if (ppBufBlock) *ppBufBlock = it->second;
        return RC_SUCCESS;
    }

    int addBlockToMap(byte *block, BufBlock *bufBlock)
    {
        blockMapPair = std::make_pair(block, bufBlock);
        std::pair<std::map<byte *, BufBlock *>::iterator,bool> ret;
        ret = blockMap.insert(blockMapPair);
        return (ret.second) ? RC_SUCCESS : RC_BLOCK_ADD_FAILED_MAP_ERR;
    }
    
    int deleteBlockFromMap(byte *block)
    {
        auto count = blockMap.erase(block);
        return (count) ? RC_SUCCESS : RC_BLOCK_MAP_ERASE_ERR;
    }

    int appendBlockToBuf(byte *block, uint_t blockSize)
    {
        int rc = checkSize(blockSize);
        if (rc != RC_SUCCESS)
            return rc;
        uint_t len = blockSize;
        byte  *src = block;
        if (bufEnd > bufStart)
        {
            uint_t len1 = bufLastPlusOne - bufEnd;
            if (blockSize > len1)
            {
                memmove(bufEnd, src, len1);
                len -= len1;
                src += len1;
                bufEnd = buf;
            }
        }
        memmove(bufEnd, src, len);
        bufEnd += len;
        if (bufEnd >= bufLastPlusOne)
            bufEnd = buf;
        bufBytesFree -= blockSize;
        return RC_SUCCESS;
    }

public:
    int addBlockToBuf(byte *block, uint_t blockSize, BufInfo *bufInfo)
    {
        // TBD: acquire lock  BUGBUG
        BufBlock *bufBlock;
        int rc = findBlockInMap(block, &bufBlock);
        if (rc == RC_SUCCESS)
        {
            if (bufBlock->blockDeletePending)
                compactBuf();
            else
            {
                setBufInfo(bufInfo);
                return RC_BLOCK_ADD_FAILED_DUPLICATE;
            }
        }
        byte *blockAddrInBuf = bufEnd;
        rc = appendBlockToBuf(block, blockSize);
        if (rc == RC_SUCCESS)
        {
            BufBlock *bufBlock = new(BufBlock);
            bufBlock->next = 0;
            bufBlock->prev = bufBlockTail;
            if (bufBlockTail) bufBlockTail->next = bufBlock;
            bufBlock->blockInBuf = blockAddrInBuf;
            bufBlock->blockSize = blockSize;
            bufBlock->blockMapKey = block;
            bufBlock->blockDeletePending = false;
            if (bufBlockHead == 0) bufBlockHead = bufBlock;
            bufBlockTail = bufBlock;
            rc = addBlockToMap(block, bufBlock);
        }
            
        setBufInfo(bufInfo);
        
        // TBD: free lock
        return rc;
    }
    
    int deleteBlockFromBuf(byte *block, BufInfo *bufInfo)
    {
        // TBD: acquire lock  BUGBUG
        
        // Mark block for deletion. Block will be removed when buf space needed
        // for next block add
        BufBlock *bufBlock;
        int rc = findBlockInMap(block, &bufBlock);
        if (rc == RC_SUCCESS)
        {
            bufBytesDeletePending += bufBlock->blockSize;
            bufBlock->blockDeletePending = true;
        }
        
        setBufInfo(bufInfo);
    
        // TBD: free lock
        return RC_SUCCESS;
    }
    
    void printBuf()
    {
        compactBuf();       // compact delete pending blocks
        uint_t bytesToPrint = bufSize - bufBytesFree;    // bytes used in buf
        int precision;
        
        if (bytesToPrint <= (bufLastPlusOne - bufStart))
        {
            precision = (int) bytesToPrint;
            printf("%.*s", precision, bufStart);
            return;
        }
            
        // Bytes in buf wrap end of buffer so do two prints
        precision = (int) (bufLastPlusOne - bufStart);
        printf("%.*s", precision, bufStart);
        precision = ((int) bytesToPrint) - precision;
        printf("%.*s", precision, buf);
    }

private:
    byte   *buf;
    byte   *bufLastPlusOne;
    byte   *bufStart;
    byte   *bufEnd;
    uint_t  bufSize;
    uint_t  bufBytesFree;   // buf bytes free. Does not include pending deletes
    uint_t  bufBytesDeletePending;
    // Linked list of blocks in buf. List in order block added.
    // Tail BufBlock is for last block added to buf.
    BufBlock *bufBlockHead;
    BufBlock *bufBlockTail;
    std::map<byte *, BufBlock *>  blockMap;
    std::pair<byte *, BufBlock *> blockMapPair;
};

TEST(CircularBuffer, BasicTwoMsgs)
{
    CircularBuffer buf(100);
    BufInfo bufInfo;
        
    char msg1[] = "Buf msg1\n";
    char msg2[] = "Buf msg2\n";
    int rc;
    
    rc = buf.addBlockToBuf((byte*) msg1, strlen(msg1), &bufInfo);
    EXPECT_EQ(rc, RC_SUCCESS);

    rc = buf.addBlockToBuf((byte*) msg2, strlen(msg2), &bufInfo);
    EXPECT_EQ(rc, RC_SUCCESS);

    buf.printBuf();
}

TEST(CircularBuffer, DeleteBlock)
{
    CircularBuffer buf(18);
    BufInfo bufInfo;
        
    char msg3[] = "Buf msg3\n";
    char msg4[] = "Buf msg4\n";
    char msg5[] = "Buf msg5\n";
    
    uint_t msg3len = strlen(msg3);
    uint_t msg4len = strlen(msg4);
    uint_t msg5len = strlen(msg5);

    int rc;
    uint_t bytesInBuf = 0;
    
    bytesInBuf += msg3len;
    rc = buf.addBlockToBuf((byte*) msg3, msg3len, &bufInfo);
    EXPECT_EQ(rc, RC_SUCCESS);
    EXPECT_EQ(bufInfo.bufBytesUsed, bytesInBuf);

    bytesInBuf += msg4len;
    rc = buf.addBlockToBuf((byte*) msg4, msg4len, &bufInfo);
    EXPECT_EQ(rc, RC_SUCCESS);
    EXPECT_EQ(bufInfo.bufBytesUsed, bytesInBuf);

    printf("Expecting msg3, msg4:\n");
    buf.printBuf();
    
    bytesInBuf -= msg3len;
    rc = buf.deleteBlockFromBuf((byte *) msg3, &bufInfo);
    EXPECT_EQ(rc, RC_SUCCESS);
    EXPECT_EQ(bufInfo.bufBytesUsed, bytesInBuf);
    
    bytesInBuf += msg3len;
    rc = buf.addBlockToBuf((byte*) msg3, msg3len, &bufInfo);
    EXPECT_EQ(rc, RC_SUCCESS);
    EXPECT_EQ(bufInfo.bufBytesUsed, bytesInBuf);

    printf("Expecting msg4, msg3:\n");
    buf.printBuf();

    bytesInBuf -= msg4len;
    rc = buf.deleteBlockFromBuf((byte *) msg4, &bufInfo);
    EXPECT_EQ(rc, RC_SUCCESS);
    EXPECT_EQ(bufInfo.bufBytesUsed, bytesInBuf);

    
    bytesInBuf += msg5len;
    rc = buf.addBlockToBuf((byte*) msg5, msg5len, &bufInfo);
    EXPECT_EQ(rc, RC_SUCCESS);
    EXPECT_EQ(bufInfo.bufBytesUsed, bytesInBuf);
    
    printf("Expecting msg3, msg5:\n");
    buf.printBuf();
}
