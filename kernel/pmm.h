#ifndef PMM_H
#define PMM_H

void         pmm_init(void);
unsigned int pmm_alloc(void);
unsigned int pmm_alloc_contiguous(int n);
void         pmm_free(unsigned int pa);
unsigned int pmm_total(void);        /* total managed frames          */
unsigned int pmm_count_used(void);   /* number of allocated frames    */

#endif /* PMM_H */
