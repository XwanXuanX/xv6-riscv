#include "kernel/types.h"
#include "user/user.h"

// Memory allocator by Kernighan and Ritchie,
// The C programming Language, 2nd ed.  Section 8.7.

using align = long;

union header {
    struct {
        header *ptr;
        uint size;
    } s;
    align x;
};

using header = header;

static header base;
static header *freep;

void free(void *ap) {
    header *p;

    header *bp = static_cast<header *>(ap) - 1;
    for (p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr) {
        if (p >= p->s.ptr && (bp > p || bp < p->s.ptr)) {
            break;
        }
    }
    if (bp + bp->s.size == p->s.ptr) {
        bp->s.size += p->s.ptr->s.size;
        bp->s.ptr = p->s.ptr->s.ptr;
    } else {
        bp->s.ptr = p->s.ptr;
    }
    if (p + p->s.size == bp) {
        p->s.size += bp->s.size;
        p->s.ptr = bp->s.ptr;
    } else {
        p->s.ptr = bp;
    }
    freep = p;
}

static header *morecore(uint nu) {
    if (nu < 4096) {
        nu = 4096;
    }
    char *p = sbrk(nu * sizeof(header));
    if (p == SBRK_ERROR) {
        return nullptr;
    }
    const auto hp = reinterpret_cast<header *>(p);
    hp->s.size = nu;
    free(hp + 1);
    return freep;
}

void *malloc(const uint nbytes) {
    header *prevp;

    const uint nunits = (nbytes + sizeof(header) - 1) / sizeof(header) + 1;
    if ((prevp = freep) == nullptr) {
        base.s.ptr = freep = prevp = &base;
        base.s.size = 0;
    }
    for (header *p = prevp->s.ptr;; prevp = p, p = p->s.ptr) {
        if (p->s.size >= nunits) {
            if (p->s.size == nunits) {
                prevp->s.ptr = p->s.ptr;
            } else {
                p->s.size -= nunits;
                p += p->s.size;
                p->s.size = nunits;
            }
            freep = prevp;
            return p + 1;
        }
        if (p == freep) {
            if ((p = morecore(nunits)) == nullptr) {
                return nullptr;
            }
        }
    }
}
