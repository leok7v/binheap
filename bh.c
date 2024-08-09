
#ifdef _MSC_VER // /Wall is very useful but yet a bit overreaching:
#pragma warning(disable: 4820) // 'bytes' bytes padding added after construct 'name'
#pragma warning(disable: 5045) // Spectre mitigation for memory load
#pragma warning(disable: 4710) // function not inlined
#pragma warning(disable: 4711) // function selected for automatic inline expansion
#endif

#undef NEED_WIN32_API

#if defined(_MSC_VER) && defined(NEED_WIN32_API)
#define STRICT // as opposite is easy, gentle or sloppy?
#define WIN32_LEAN_AND_MEAN // exclude stuff which no one needs/wants
#define VC_EXTRALEAN        // exclude even more stuff
#include <Windows.h>
#endif

#include <stdio.h>
#include "rt.h"

#pragma push_macro("assert")
#pragma push_macro("printf")

// both IntelliSense and ChatGPT understand and use assert() and printf()

#define assert(...) rt_assert(__VA_ARGS__)
#define printf(...) rt_println(__VA_ARGS__)

// https://en.wikipedia.org/wiki/Binary_heap
// J. W. J. Williams 1964

enum { alphabet  = 32 }; // > 2 and power of two
enum { max_nodes = alphabet * 2 };

static_assert(alphabet > 2 && (alphabet & (alphabet - 1)) == 0,
              "alphabet must be 2^n");

typedef struct node_s {
    int64_t freq;
    int32_t sym;
} node_t;

typedef struct binheap_s {
    node_t  ns[max_nodes]; // nodes
    int32_t sx[alphabet];  // symbol -> ix
    int32_t nc;            // node count
} binheap_t;

static void binheap_init(binheap_t* t) {
    t->nc = 0; // no nodes
    for (int32_t i = 0; i < rt_countof(t->ns); i++) {
        t->ns[i].freq = 0;
        t->ns[i].sym = -1;
    }
    for (int32_t i = 0; i < rt_countof(t->sx); i++) { t->sx[i] = -1; }
}

static inline bool binheap_is_leaf(binheap_t* t, int32_t ix) {
    return t->ns[ix].sym >= 0;
}

static int32_t binheap_find(binheap_t* t, int32_t sym, uint64_t *path, int32_t *bits) {
    *path = 0;
    *bits = 0;  // Start from the root
    const int32_t ix = t->sx[sym];
    if (ix >= 0) {
        assert(0 <= ix && ix < t->nc);
        int32_t i = ix;
        while (i > 0) {
            int32_t parent = (i - 1) / 2;
            *path <<= 1;  // Shift left to make space for the next bit
            if (i == parent * 2 + 2) { *path |= 1; } // Right child
            i = parent;
            (*bits)++;
        }
    } else {
        assert(ix == -1, "%d symbol not found", ix);
    }
    return ix;
}

static bool binheap_verify(binheap_t* t) {
    // Verify the max-heap property and the correct setup of nodes
    // assert() decays to nothing in release build but
    // binheap_verify() will return false to help debug the release build
    for (int32_t ix = 0; ix < t->nc; ix++) {
        int32_t left  = 2 * ix + 1;
        int32_t right = 2 * ix + 2;
        // Verify max-heap property: Parent frequency >= child frequencies
        if (left < t->nc) {
            assert(t->ns[ix].freq >= t->ns[left].freq, "Max-heap violation:"
                      " parent [%d] freq: %lld < left child [%d] freq: %lld",
                      ix, t->ns[ix].freq, left, t->ns[left].freq);
            if (t->ns[ix].freq < t->ns[left].freq) { return false; }
        }
        if (right < t->nc) {
            assert(t->ns[ix].freq >= t->ns[right].freq, "Max-heap violation:"
                      " parent [%d] freq: %lld < right child [%d] freq: %lld",
                      ix, t->ns[ix].freq, right, t->ns[right].freq);
            if (t->ns[ix].freq < t->ns[right].freq) { return false; }
        }
        // Verify that the sx array correctly maps symbols to heap indices
        if (t->ns[ix].sym >= 0) {  // This is a leaf node
            int32_t sym = t->ns[ix].sym;
            assert(t->sx[sym] == ix, "Symbol index mismatch:"
                      " sym: %d, t->sx[%d]: %d, expected ix: %d",
                      sym, sym, t->sx[sym], ix);
            if (t->sx[sym] != ix) { return false; }
        }
        // For leaf nodes, verify the path and ensure it's correct
        if (t->ns[ix].sym >= 0) {  // This is a leaf node
            const int32_t sym = t->ns[ix].sym;
            uint64_t path = 0;
            int32_t bits = 0;
            int32_t i = binheap_find(t, sym, &path, &bits);
            assert(i == ix, "Path verification failed:"
                               " expected ix: %d, found ix: %d", ix, i);
            if (i != ix) { return false; }
            const bool in_range = 0 <= t->sx[sym] && t->sx[sym] < max_nodes;
            assert(in_range, "Invalid symbol index in sx: sym: %d, sx: %d",
                      sym, t->sx[sym]);
            if (!in_range) { return false; }
        }
    }
    // Verify that sx array only contains valid indices
    for (int32_t sym = 0; sym < alphabet; sym++) {
        if (t->sx[sym] != -1) {
            const int32_t ix = t->sx[sym];
            const bool in_range = 0 <= ix && ix < t->nc;
            assert(in_range, "Invalid index in sx array: sx[sym:%d]: %d",
                      sym, ix);
            if (!in_range) { return false; }
            assert(t->ns[ix].sym == sym, "Symbol mismatch in sx array:"
                      " ns[sx[sym:%d]].sym: %d", sym, t->ns[ix].sym);
            if (t->ns[ix].sym != sym) { return false; }
        }
    }
    return true;
}

static inline void binheap_swap(binheap_t* t, int32_t ix0, int32_t ix1) {
    node_t* a = &t->ns[ix0];
    node_t* b = &t->ns[ix1];
    const int32_t sa = a->sym;
    const int32_t sb = b->sym;
    if (sa >= 0 && sb >= 0) { // 2 leaves
        const int32_t ix = t->sx[sa]; t->sx[sa] = t->sx[sb]; t->sx[sb] = ix;
    } else if (sa >= 0) {
        t->sx[sa] = ix1;
    } else if (sb >= 0) {
        t->sx[sb] = ix0;
    }
    const node_t swap = *a; *a = *b; *b = swap;
    assert(binheap_verify(t));
}

static int binheap_heapify_up(binheap_t* t, int32_t ix) {
    while (ix > 0) {
        int32_t parent = (ix - 1) / 2;
        if (t->ns[ix].freq > t->ns[parent].freq) {
            binheap_swap(t, ix, parent);
            ix = parent;
        } else {
            break;
        }
    }
    t->sx[t->ns[ix].sym] = ix;
    return ix;
}

static int32_t binheap_heapify_down(binheap_t* t, int32_t ix) {
    int32_t largest  = ix;
    int32_t left  = 2 * ix + 1;
    int32_t right = 2 * ix + 2;
    if (left < t->nc && t->ns[left].freq > t->ns[largest].freq) {
        largest = left;
    }
    if (right < t->nc && t->ns[right].freq > t->ns[largest].freq) {
        largest = right;
    }
    if (largest != ix) {
        binheap_swap(t, ix, largest);
        // tail recursion is shallow and optimizable
        ix = binheap_heapify_down(t, largest);
    }
    t->sx[t->ns[ix].sym] = ix;
    return ix;
}

static int32_t binheap_add(binheap_t* t, int32_t sym) {
    assert(0 <= sym && sym < alphabet);
    assert(t->nc < max_nodes);
    if (t->nc < 2) {
        // Simple case: add the node without merging if there are fewer than 2 nodes.
        t->ns[t->nc] = (node_t){.freq = 1, .sym = sym};
        t->sx[sym] = t->nc;
        int32_t ix = binheap_heapify_up(t, t->nc);
        t->nc++;
        assert(binheap_verify(t));
        return ix;
    } else {
        // Merge the two lowest-frequency nodes
        int32_t smallest = t->nc - 2;
        int32_t second_smallest = t->nc - 1;
        assert(t->nc < rt_countof(t->ns));
        // Create an internal node `i` with the combined frequency
        // of the two smallest nodes
        const int32_t i = t->nc;
        t->ns[i].freq = t->ns[smallest].freq + t->ns[second_smallest].freq;
        t->ns[i].sym = -1; // Internal
        // Increase node count before heapify operations
        // Perform heapify operations to maintain the heap property
        t->nc++;
        binheap_heapify_down(t, smallest);
        binheap_heapify_down(t, second_smallest);
        binheap_heapify_up(t, i);
        // Place the new node into the heap
        assert(t->nc < rt_countof(t->ns));
        const int32_t new = t->nc;
        // Now add the new leaf node (the actual symbol)
        t->ns[new] = (node_t){.freq = 1, .sym = sym};
        t->sx[sym] = t->nc;
        t->nc++;
        int32_t leaf = binheap_heapify_up(t, new);
        assert(binheap_verify(t));
        return leaf;
    }
}

static int32_t binheap_inc_freq(binheap_t* t, int32_t sym) {
    assert(0 <= sym && sym < rt_countof(t->sx));
    int32_t ix = t->sx[sym];
    assert(0 <= ix && ix < rt_countof(t->ns), "ix: %d cannot be root", ix);
    // Increment the frequency of the current node
    t->ns[ix].freq++;
    assert(binheap_is_leaf(t, ix));
    // Perform heapify operations to maintain the max-heap property
    ix = binheap_heapify_down(t, ix); // Heapify down first to push down if necessary
    ix = binheap_heapify_up(t, ix);   // Then heapify up to correct position
    assert(t->sx[sym] == ix);
    // After heapify, check the parent frequencies and adjust them if needed
    int32_t p = ix;
    while (p > 0) {
        int32_t parent = (p - 1) / 2;
        // Parent should be updated with the correct frequency sum
        t->ns[parent].freq = t->ns[2 * parent + 1].freq + t->ns[2 * parent + 2].freq;
        p = parent;
        parent = (p - 1) / 2;
    }
    // Ensure the tree is still a valid heap
    assert(binheap_verify(t));
    return ix;
}

static void binheap_print(binheap_t* t) {
    assert(binheap_verify(t));
    for (int32_t ix = 0; ix < t->nc; ix++) {
        int32_t left  = 2 * ix + 1;
        int32_t right = 2 * ix + 2;
        if (t->ns[ix].sym >= 0) {
            const int32_t sym = t->ns[ix].sym;
            uint64_t path = 0;
            int32_t bits = 0;
            int32_t i = binheap_find(t, sym, &path, &bits);
            assert(i == ix && t->sx[sym] == i, "i: %d ix: %d t->sx[%d]: %d",
                   i, ix, sym, t->sx[sym]);
            assert(0 <= t->sx[sym] &&t->sx[sym] < max_nodes, "sx[%d]: %d",
                   sym, t->sx[sym]);
            assert(t->ns[i].sym == sym, "sx[%d]: %d t->ns[%d].sym: %d",
                   sym, t->sx[sym], t->ns[i].sym);
            char bin[65]  = {0};
            char turn[65] = {0};
            uint64_t p = path;
            for (int32_t b = 0; b < bits; b++) {
                bin[b] = '0' + (p & 0x1);
                turn[b] = (p & 0x1) ? 'L' : 'R';
                p >>= 1;
            }
            printf("[%2d] freq: %6lld sym: %4d left: %3d right: %3d bits: %d %s %s\n",
                    i, t->ns[ix].freq, sym, left, right, bits, bin, turn);
        } else {
            printf("[%2d] freq: %6lld sym: none left: %3d right: %3d\n",
                    ix, t->ns[ix].freq, left, right);
        }
    }
}

static int binheap_test(void) {
    static binheap_t bh;
    binheap_init(&bh);
    for (int32_t sym = 0; sym < alphabet; sym++) {
        binheap_add(&bh, sym);
        assert(binheap_verify(&bh));
    }
    binheap_print(&bh);
    printf("\n");
    for (int32_t i = 0; i < alphabet * alphabet * alphabet; i++) {
        int32_t sym = rand() * alphabet / RAND_MAX;
        assert(0 <= sym && sym < alphabet, "sym: %d", sym);
        int32_t freq = rand() * 16 / RAND_MAX;
        for (int32_t k = 0; k < freq; k++) {
            binheap_inc_freq(&bh, sym);
            assert(binheap_verify(&bh));
        }
    }
    binheap_print(&bh);
    return 0;
}

int main(void) {
    return binheap_test();
}

#pragma pop_macro("assert")
#pragma pop_macro("printf")

