#include "VirtualMemory.h"
#include "PhysicalMemory.h"

uint64_t getPhysicalAddress(uint64_t virtualAddress);

word_t find_unused_frame(uint64_t virtualAddress, word_t untouchableFrame);

void find_empty_frame_dfs(word_t currFrame, word_t *emptyFrame,
                          word_t currPathFrame, uint64_t *emptyFrameParentPointer, int depth);

void findMaxFrameNum(word_t currFrame, word_t *maxFrameNum, int depth);

void findAndEvictFrame(word_t currFrame, uint64_t pageSwappedIn, word_t *maxFrame,
                       uint64_t *maxFrameParentPointer, uint64_t *maxVal, int depth, uint64_t *evictedPage,
                       word_t evicted);

void clearFrame(word_t frameIndex);

/**
 * Initialize the virtual memory.
 */
void VMinitialize() {
    clearFrame(0);
}

/**
 * Reads a word from the given virtual address
 * and puts its content in *value.
 *
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
 * @param virtualAddress full virtual address
 * @param value variable to hold the word in the given address
 * @return 1 on success 0 otherwise
 */
int VMread(uint64_t virtualAddress, word_t *value) {
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE) {
        return 0;
    }
    uint64_t address = getPhysicalAddress(virtualAddress);
    PMread(address, value);
    return 1;
}

/**
 * Writes a word to the given virtual address.
 *
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
 * @param virtualAddress full virtual address
 * @param value variable to hold the word to be put in the given address
 * @return 1 on success 0 otherwise
 */
int VMwrite(uint64_t virtualAddress, word_t value) {
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE) {
        return 0;
    }
    uint64_t address = getPhysicalAddress(virtualAddress);
    PMwrite(address, value);
    return 1;
}

/**
 * Helper function for finding the physical address of a given
 * virtual address
 * @param virtualAddress given virtual address
 * @return exact physical address
 */
uint64_t getPhysicalAddress(uint64_t virtualAddress) {
    word_t currFrame = 0;
    word_t nextFrame;
    bool useNewFrame = false;
    for (int i = 0; i < TABLES_DEPTH; ++i) {
        uint64_t shiftAddressBy = OFFSET_WIDTH * (TABLES_DEPTH - i);
        uint64_t address = virtualAddress >> shiftAddressBy;
        address = address & (PAGE_SIZE - 1);
        PMread(currFrame * PAGE_SIZE + address, &nextFrame);
        if (nextFrame == 0) {
            useNewFrame = true;
            nextFrame = find_unused_frame(virtualAddress, currFrame);
            if (i < TABLES_DEPTH - 1) { //clears frame if it isn't an actual page
                clearFrame(nextFrame);
            }
            PMwrite(currFrame * PAGE_SIZE + address, nextFrame);
        }
        currFrame = nextFrame;
    }
    if (useNewFrame) {
        PMrestore(currFrame, virtualAddress >> OFFSET_WIDTH);
    }
    return currFrame * PAGE_SIZE + (virtualAddress & (PAGE_SIZE - 1));
}

/**
 * Helper function that finds an unused frame
 * or evicts a frame.
 * @param virtualAddress full virtual address
 * @param untouchableFrame a frame that shouldn't be used as it's part of
 * the current path
 * @return index of the found frame
 */
word_t find_unused_frame(uint64_t virtualAddress, word_t untouchableFrame) {

    //Trying to find empty frame (all zeroes frame)
    uint64_t emptyFrameParentPointer = 0;
    word_t emptyFrame = 0;
    find_empty_frame_dfs(0, &emptyFrame,
                         untouchableFrame, &emptyFrameParentPointer, 0);
    if (emptyFrame != 0) {
        PMwrite(emptyFrameParentPointer, 0);
        return emptyFrame;
    }

    //In case there are no empty frames, trying to find an unused frame
    word_t maxFrameNum = 0;
    findMaxFrameNum(0, &maxFrameNum, 0);
    if (maxFrameNum + 1 < NUM_FRAMES) {
        return maxFrameNum + 1;
    }

    word_t maxFrame = 0;
    uint64_t maxFrameParentPointer = 0;
    uint64_t maxVal = 0;
    uint64_t evictedPage = 0;
    uint64_t pageSwappedIn = virtualAddress >> OFFSET_WIDTH;
    findAndEvictFrame(0, pageSwappedIn, &maxFrame,
                      &maxFrameParentPointer, &maxVal, 0, &evictedPage, 0);
    PMevict(maxFrame, evictedPage);
    PMwrite(maxFrameParentPointer, 0);
    return maxFrame;
}

/**
 * Helper function to find an empty frame, a frame
 * containing a table of zeroes.
 * @param currFrame current frame index
 * @param emptyFrame empty frame
 * @param currPathFrame current frame in path
 * @param emptyFrameParentPointer pointer to empty frame parent
 * @param depth current depth in tree
 */
void find_empty_frame_dfs(word_t currFrame, word_t *emptyFrame,
                          word_t currPathFrame,
                          uint64_t *emptyFrameParentPointer, int depth) {
    if (depth == TABLES_DEPTH || *emptyFrame != 0 || currFrame >= NUM_FRAMES) {
        return;
    }
    bool doNotChange = false;
    word_t frame;
    for (int i = 0; i < PAGE_SIZE; ++i) {
        PMread(currFrame * PAGE_SIZE + i, &frame);
        // found non-zero entry -> recursively calling function on next table
        if (frame != 0) {
            *emptyFrameParentPointer = currFrame * PAGE_SIZE + i;
            doNotChange = true;
            find_empty_frame_dfs(frame, emptyFrame,
                                 currPathFrame, emptyFrameParentPointer, ++depth);
            depth--;

        }
        if (*emptyFrame != 0 && *emptyFrame != currPathFrame) {
            return;
        }
    }
    if (!doNotChange && currFrame != currPathFrame) {
        *emptyFrame = currFrame;
    }
}

/**
 * Helper function that counts the maximal page index
 * that is contained in one of the tables and updates
 * the frame index of the frame holding that page
 * @param currFrame current frame
 * @param maxFrameNum index of the frame that holds the max page
 * @param depth depth from root table
 */
void findMaxFrameNum(word_t currFrame, word_t *maxFrameNum, int depth) {
    if (depth == TABLES_DEPTH + 1 || currFrame >= NUM_FRAMES || *maxFrameNum + 1 == NUM_FRAMES) {
        return;
    }
    if (currFrame > *maxFrameNum) {
        *maxFrameNum = currFrame;
    }
    word_t frame = 0;
    for (int i = 0; i < PAGE_SIZE; ++i) {
        PMread(currFrame * PAGE_SIZE + i, &frame);
        if (frame) {
            findMaxFrameNum(frame, maxFrameNum, ++depth);
            depth--;
        }
    }
}

/**
 * Helper function that helps find the frame to evict,
 * based on it's entries cyclical distance from the page
 * swapped in.
 * @param currFrame current frame
 * @param pageSwappedIn address of page swapped in
 * @param maxFrame frame that holds the maximal value
 * @param maxFrameParentPointer max frame parent address
 * @param maxVal the maximal cyclical distance from all page up
 * until current moment
 * @param depth depth from root table
 * @param evictedPage the page to evict
 * @param evicted update page to evict
 */
void findAndEvictFrame(word_t currFrame, uint64_t pageSwappedIn, word_t *maxFrame,
                       uint64_t *maxFrameParentPointer, uint64_t *maxVal,
                       int depth, uint64_t *evictedPage,
                       word_t evicted) {
    word_t frame;
    for (int i = 0; i < PAGE_SIZE; ++i) {
        PMread(currFrame * PAGE_SIZE + i, &frame);
        if (frame) {
            evicted = (evicted << OFFSET_WIDTH) + i;
            if (depth + 1 == TABLES_DEPTH) {
                uint64_t diff =
                        (pageSwappedIn < (uint64_t) frame) ? (uint64_t) frame - pageSwappedIn :
                        pageSwappedIn - (uint64_t) frame;
                uint64_t min = (diff < (NUM_PAGES - diff)) ? diff : NUM_PAGES - diff;
                if (*maxVal < min) {
                    *maxVal = min;
                    *maxFrame = frame;
                    *maxFrameParentPointer = currFrame * PAGE_SIZE + i;
                    *evictedPage = evicted;
                }
                return;
            }
            findAndEvictFrame(frame, pageSwappedIn, maxFrame,
                              maxFrameParentPointer, maxVal,
                              ++depth, evictedPage, evicted);
            evicted = evicted >> OFFSET_WIDTH;
            depth--;
        }
    }
}

/**
 * Helper function that helps clear a given frame,
 * puts 0 in all its entries
 * @param frameIndex the base address of the frame
 */
void clearFrame(word_t frameIndex) {
    for (word_t i = 0; i < PAGE_SIZE; ++i) {
        PMwrite(frameIndex * PAGE_SIZE + i, 0);
    }
}
//#include "VirtualMemory.h"
//#include "PhysicalMemory.h"
//
//uint64_t getPhysicalAddress(uint64_t virtualAddress);
//
//word_t find_unused_frame(uint64_t virtualAddress, word_t untouchableFrame);
//
//void find_empty_frame_dfs(word_t currFrame, word_t *emptyFrame,
//                          word_t currPathFrame, uint64_t *emptyFrameParentPointer, word_t *maxFrameNum,
//                          uint64_t pageSwappedIn, word_t *maxFrame,
//                          uint64_t *maxFrameParentPointer, uint64_t *maxVal, uint64_t *evictedPage,
//                          word_t evicted, int depth);
//
////void findMaxFrameNum(word_t currFrame,, int depth);
////
////void findAndEvictFrame(word_t currFrame, );
//
//void clearFrame(word_t frameIndex);
//
///**
// * Initialize the virtual memory.
// */
//void VMinitialize() {
//    clearFrame(0);
//}
//
///**
// * Reads a word from the given virtual address
// * and puts its content in *value.
// *
// * returns 1 on success.
// * returns 0 on failure (if the address cannot be mapped to a physical
// * address for any reason)
// * @param virtualAddress full virtual address
// * @param value variable to hold the word in the given address
// * @return 1 on success 0 otherwise
// */
//int VMread(uint64_t virtualAddress, word_t *value) {
//    if (virtualAddress >= VIRTUAL_MEMORY_SIZE) {
//        return 0;
//    }
//    uint64_t address = getPhysicalAddress(virtualAddress);
//    PMread(address, value);
//    return 1;
//}
//
///**
// * Writes a word to the given virtual address.
// *
// * returns 1 on success.
// * returns 0 on failure (if the address cannot be mapped to a physical
// * address for any reason)
// * @param virtualAddress full virtual address
// * @param value variable to hold the word to be put in the given address
// * @return 1 on success 0 otherwise
// */
//int VMwrite(uint64_t virtualAddress, word_t value) {
//    if (virtualAddress >= VIRTUAL_MEMORY_SIZE) {
//        return 0;
//    }
//    uint64_t address = getPhysicalAddress(virtualAddress);
//    PMwrite(address, value);
//    return 1;
//}
//
///**
// * Helper function for finding the physical address of a given
// * virtual address
// * @param virtualAddress given virtual address
// * @return exact physical address
// */
//uint64_t getPhysicalAddress(uint64_t virtualAddress) {
//    word_t currFrame = 0;
//    word_t nextFrame;
//    bool useNewFrame = false;
//    for (int i = 0; i < TABLES_DEPTH; ++i) {
//        uint64_t shiftAddressBy = OFFSET_WIDTH * (TABLES_DEPTH - i);
//        uint64_t address = virtualAddress >> shiftAddressBy;
//        address = address & (PAGE_SIZE - 1);
//        PMread(currFrame * PAGE_SIZE + address, &nextFrame);
//        if (nextFrame == 0) {
//            useNewFrame = true;
//            nextFrame = find_unused_frame(virtualAddress, currFrame);
//            if (i < TABLES_DEPTH - 1) { //clears frame if it isn't an actual page
//                clearFrame(nextFrame);
//            }
//            PMwrite(currFrame * PAGE_SIZE + address, nextFrame);
//        }
//        currFrame = nextFrame;
//    }
//    if (useNewFrame) {
//        PMrestore(currFrame, virtualAddress >> OFFSET_WIDTH);
//    }
//    return currFrame * PAGE_SIZE + (virtualAddress & (PAGE_SIZE - 1));
//}
//
///**
// * Helper function that finds an unused frame
// * or evicts a frame.
// * @param virtualAddress full virtual address
// * @param untouchableFrame a frame that shouldn't be used as it's part of
// * the current path
// * @return index of the found frame
// */
//word_t find_unused_frame(uint64_t virtualAddress, word_t untouchableFrame) {
//
//    //Trying to find empty frame (all zeroes frame)
//    uint64_t emptyFrameParentPointer = 0;
//    word_t maxFrameNum = 0;
//    word_t emptyFrame = 0;
//    word_t maxFrame = 0;
//    uint64_t maxFrameParentPointer = 0;
//    uint64_t maxVal = 0;
//    uint64_t evictedPage = 0;
//    uint64_t pageSwappedIn = virtualAddress >> OFFSET_WIDTH;
//    find_empty_frame_dfs(0, &emptyFrame,
//                         untouchableFrame, &emptyFrameParentPointer,
//                         &maxFrameNum, pageSwappedIn, &maxFrame,
//                         &maxFrameParentPointer, &maxVal, &evictedPage, 0, 0);
//    if (emptyFrame != 0) {
//        PMwrite(emptyFrameParentPointer, 0);
//        return emptyFrame;
//    }
//
//    //In case there are no empty frames, trying to find an unused frame
//    if (maxFrameNum + 1 < NUM_FRAMES) {
//        return maxFrameNum + 1;
//    }
//    PMevict(maxFrame, evictedPage);
//    PMwrite(maxFrameParentPointer, 0);
//    return maxFrame;
//}
//
///**
// * Helper function to find an empty frame, a frame
// * containing a table of zeroes.
// * @param currFrame current frame index
// * @param emptyFrame empty frame
// * @param currPathFrame current frame in path
// * @param emptyFrameParentPointer pointer to empty frame parent
// * @param depth current depth in tree
// */
//void find_empty_frame_dfs(word_t currFrame, word_t *emptyFrame,
//                          word_t currPathFrame, uint64_t *emptyFrameParentPointer, word_t *maxFrameNum,
//                          uint64_t pageSwappedIn, word_t *maxFrame,
//                          uint64_t *maxFrameParentPointer, uint64_t *maxVal, uint64_t *evictedPage,
//                          word_t evicted, int depth) {
//    if (depth == TABLES_DEPTH +1|| *emptyFrame != 0 || currFrame >= NUM_FRAMES ) {
//        return;
//    }
//
//    if (currFrame > *maxFrameNum) {
//        *maxFrameNum = currFrame;
//    }
//    bool doNotChange = false;
//    word_t frame;
//    for (int i = 0; i < PAGE_SIZE; ++i) {
//        PMread(currFrame * PAGE_SIZE + i, &frame);
//        // found non-zero entry -> recursively calling function on next table
//        if (frame != 0) {
//            *emptyFrameParentPointer = currFrame * PAGE_SIZE + i;
//            doNotChange = true;
//            evicted = (evicted << OFFSET_WIDTH) + i;
//            if (depth + 1 == TABLES_DEPTH) {
//                uint64_t diff =
//                        (pageSwappedIn < (uint64_t) frame) ? (uint64_t) frame - pageSwappedIn :
//                        pageSwappedIn - (uint64_t) frame;
//                uint64_t min = (diff < (NUM_PAGES - diff)) ? diff : NUM_PAGES - diff;
//                if (*maxVal < min) {
//                    *maxVal = min;
//                    *maxFrame = frame;
//                    *maxFrameParentPointer = currFrame * PAGE_SIZE + i;
//                    *evictedPage = evicted;
//                }
//            }
//            find_empty_frame_dfs(frame, emptyFrame, currPathFrame, emptyFrameParentPointer, maxFrameNum,
//                                 pageSwappedIn, maxFrame,
//                                 maxFrameParentPointer, maxVal, evictedPage,
//                                 evicted, ++depth);
//            evicted = evicted >> OFFSET_WIDTH;
//            depth--;
//
//        }
//        if (*emptyFrame != 0 && *emptyFrame != currPathFrame ) {
//            return;
//
//        }
//    }
//    if (!doNotChange && currFrame != currPathFrame &&  depth < TABLES_DEPTH) {
//        *emptyFrame = currFrame;
//    }
//}
//
///**
// * Helper function that counts the maximal page index
// * that is contained in one of the tables and updates
// * the frame index of the frame holding that page
// * @param currFrame current frame
// * @param maxFrameNum index of the frame that holds the max page
// * @param depth depth from root table
// */
////void findMaxFrameNum(word_t currFrame, word_t *maxFrameNum, int depth) {
////    if (depth == TABLES_DEPTH + 1 || currFrame >= NUM_FRAMES || *maxFrameNum + 1 == NUM_FRAMES) {
////        return;
////    }
////    if (currFrame > *maxFrameNum) {
////        *maxFrameNum = currFrame;
////    }
////    word_t frame = 0;
////    for (int i = 0; i < PAGE_SIZE; ++i) {
////        PMread(currFrame * PAGE_SIZE + i, &frame);
////        if (frame) {
////            findMaxFrameNum(frame, maxFrameNum, ++depth);
////            depth--;
////        }
////    }
////}
//
///**
// * Helper function that helps find the frame to evict,
// * based on it's entries cyclical distance from the page
// * swapped in.
// * @param currFrame current frame
// * @param pageSwappedIn address of page swapped in
// * @param maxFrame frame that holds the maximal value
// * @param maxFrameParentPointer max frame parent address
// * @param maxVal the maximal cyclical distance from all page up
// * until current moment
// * @param depth depth from root table
// * @param evictedPage the page to evict
// * @param evicted update page to evict
// */
////void findAndEvictFrame(word_t currFrame, uint64_t pageSwappedIn, word_t *maxFrame,
////                       uint64_t *maxFrameParentPointer, uint64_t *maxVal,
////                       int depth, uint64_t *evictedPage,
////                       word_t evicted) {
////    word_t frame;
////    for (int i = 0; i < PAGE_SIZE; ++i) {
////        PMread(currFrame * PAGE_SIZE + i, &frame);
////        if (frame) {
////            evicted = (evicted << OFFSET_WIDTH) + i;
////            if (depth + 1 == TABLES_DEPTH) {
////                uint64_t diff =
////                        (pageSwappedIn < (uint64_t) frame) ? (uint64_t) frame - pageSwappedIn :
////                        pageSwappedIn - (uint64_t) frame;
////                uint64_t min = (diff < (NUM_PAGES - diff)) ? diff : NUM_PAGES - diff;
////                if (*maxVal < min) {
////                    *maxVal = min;
////                    *maxFrame = frame;
////                    *maxFrameParentPointer = currFrame * PAGE_SIZE + i;
////                    *evictedPage = evicted;
////                }
////                return;
////            }
////            findAndEvictFrame(frame, pageSwappedIn, maxFrame,
////                              maxFrameParentPointer, maxVal,
////                              ++depth, evictedPage, evicted);
////            evicted = evicted >> OFFSET_WIDTH;
////            depth--;
////        }
////    }
////}
//
///**
// * Helper function that helps clear a given frame,
// * puts 0 in all its entries
// * @param frameIndex the base address of the frame
// */
//void clearFrame(word_t frameIndex) {
//    for (word_t i = 0; i < PAGE_SIZE; ++i) {
//        PMwrite(frameIndex * PAGE_SIZE + i, 0);
//    }
//}
