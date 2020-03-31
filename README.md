# CircularBuffer
CircularBuffer 

Implement circular buffer with support for add and delete of variable sized blocks. Buffer statistics are returned on add and delete requests. Originally intended to write in C but used C++ to use std::map instead of C re-black tree.

Interfaces:
1. class CircularBuffer(bufSize)
2. addBlockToBuf(byte *block, size_t blockSize, BufInfo &bufInfo)
3. deleteBlockToBuf(byte *block, BufInfo &bufInfo)
4. printBuf()

Block delete requests are handled lazily to minimize movement of data in the circular buffer. Deleted blocks are compacted out of the buffer when 1) space is needed for an add block request and 2) printBuf operation

Uses google test

Dev Env:
MacBook Pro, MacOS Catalina 10.15.3, XCode 11.4
