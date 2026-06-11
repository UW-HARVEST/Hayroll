#include <stdint.h>
#include <string.h>

#include "../include/params.h"

int return_spx_n(void)
{
    return SPX_N;
}

void prf_addr(unsigned char *out, const uint32_t addr[8])
{
    /* Since SPX_N may be smaller than 32, we need temporary buffers. */
    unsigned char outbuf[32];
    unsigned char buf[64] = {0};

    memcpy(buf, addr, SPX_ADDR_BYTES);
    memcpy(out, outbuf, SPX_N);
}

/**
 * Computes the message hash using R, the public key, and the message.
 * Outputs the message digest and the index of the leaf. The index is split in
 * the tree index and the leaf index, for convenient copying to an address.
 */
void hash_message(unsigned char *digest, uint64_t *tree, uint32_t *leaf_idx,
                  const unsigned char *R, const unsigned char *pk,
                  const unsigned char *m, unsigned long long mlen)
{
#define SPX_TREE_BITS (SPX_TREE_HEIGHT * (SPX_D - 1))
#define SPX_TREE_BYTES ((SPX_TREE_BITS + 7) / 8)
#define SPX_LEAF_BITS SPX_TREE_HEIGHT
#define SPX_LEAF_BYTES ((SPX_LEAF_BITS + 7) / 8)
#define SPX_DGST_BYTES (SPX_FORS_MSG_BYTES + SPX_TREE_BYTES + SPX_LEAF_BYTES)

    unsigned char buf[SPX_DGST_BYTES];
    unsigned char *bufp = buf;
    uint8_t s_inc[65];

    int n = SPX_DGST_BYTES;

    memcpy(digest, bufp, SPX_FORS_MSG_BYTES);
    bufp += SPX_FORS_MSG_BYTES;

#if SPX_ADDR_BYTES > 64
    #error For given height and depth, 64 bits cannot represent all subtrees
#endif

    if (SPX_D == 1) {
	*tree = 0;
    } else {
        *tree = SPX_TREE_BYTES;
        *tree &= (~(uint64_t)0) >> (64 - SPX_TREE_BITS);
    }
    bufp += SPX_TREE_BYTES;

    *leaf_idx = (uint32_t)SPX_LEAF_BYTES;
    *leaf_idx &= (~(uint32_t)0) >> (32 - SPX_LEAF_BITS);
}
