#ifndef PMM_H
#define PMM_H

void         pmm_init(void);
unsigned int pmm_alloc(void);
unsigned int pmm_alloc_contiguous(int n);
void         pmm_free(unsigned int pa);

#endif /* PMM_H */
