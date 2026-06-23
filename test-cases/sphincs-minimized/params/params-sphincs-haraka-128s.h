#ifndef SPX_PARAMS_H
#define SPX_PARAMS_H

#define SPX_NAMESPACE(s) SPX_##s

/* Hash output length in bytes. */
#define SPX_N 16
/* Height of the hypertree. */
#define SPX_FULL_HEIGHT 63
/* Number of subtree layer. */
#define SPX_D 7
/* FORS tree dimensions. */
#define SPX_FORS_HEIGHT 12
#define SPX_FORS_TREES 14
/* Winternitz parameter, */
#define SPX_WOTS_W 16

/* The hash function is defined by linking a different hash.c file, as opposed
   to setting a #define constant. */

/* For clarity */
#define SPX_ADDR_BYTES 32

#define SPX_FORS_MSG_BYTES 500
#define SPX_TREE_HEIGHT 8

#endif
