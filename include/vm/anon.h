#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"

#define SECTOR_SIZE 512

struct page;
enum vm_type;

struct anon_page {
    int bit_idx;
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif
