/*
 * fat16.c - FAT16 read/write filesystem driver
 *
 * Supports: root directory files (8.3 names), read, write/create, listdir,
 *           subdirectories (mkdir, cd, delete, rename).
 * Does NOT support: long filenames, timestamps, extending directory clusters.
 *
 * Disk access: calls ata_read_sector() / ata_write_sector() from kernel.c
 */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

/* Provided by kernel.c */
extern int ata_read_sector(unsigned int lba, unsigned short *buf);
extern int ata_write_sector(unsigned int lba, const unsigned short *buf);

/* ============================================================
 * LE integer helpers (byte-array <-> integer)
 * ============================================================ */

static u16 rd16(const u8 *p)
{
    return (u16)p[0] | ((u16)p[1] << 8);
}

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void wr16(u8 *p, u16 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static void wr32(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

/* ============================================================
 * Filesystem state (populated by fat16_init)
 * ============================================================ */

static u8  g_spc;           /* sectors per cluster                        */
static u16 g_fat_lba;       /* LBA of first FAT                           */
static u16 g_fat_size;      /* sectors per FAT                            */
static u8  g_num_fats;      /* number of FAT copies                       */
static u16 g_root_lba;      /* LBA of root directory                      */
static u16 g_root_count;    /* max number of root directory entries       */
static u16 g_root_sectors;  /* sectors occupied by root directory         */
static u16 g_data_lba;      /* LBA of data area (cluster 2 starts here)  */
static int g_initialized = 0;
static u16 g_cwd_cluster = 0;   /* 0 = root directory */

/* ============================================================
 * Sector I/O buffers (512 bytes = 256 x u16 each)
 *
 * g_sec0: FAT operations and data reads/writes
 * g_sec1: directory scan and directory entry writes
 * (kept separate so FAT ops don't clobber directory buffer)
 * ============================================================ */

static u16 g_sec0[256];
static u16 g_sec1[256];

/* ============================================================
 * fat16_init — parse BPB from sector 0 of IDE disk
 * Returns 0 on success, -1 on error.
 * ============================================================ */

int fat16_init(void)
{
    g_initialized = 0;
    g_cwd_cluster = 0;

    if (ata_read_sector(0, g_sec0) < 0)
        return -1;

    u8 *bpb = (u8 *)g_sec0;

    /* Validate boot sector signature */
    if (bpb[510] != 0x55 || bpb[511] != 0xAA)
        return -1;

    u16 bps      = rd16(bpb + 11);  /* bytes per sector     */
    u8  spc      = bpb[13];         /* sectors per cluster  */
    u16 reserved = rd16(bpb + 14);  /* reserved sectors     */
    u8  num_fats = bpb[16];         /* number of FATs       */
    u16 root_cnt = rd16(bpb + 17);  /* root entry count     */
    u16 fat_size = rd16(bpb + 22);  /* sectors per FAT      */

    if (bps != 512 || spc == 0 || num_fats == 0 || fat_size == 0)
        return -1;

    g_spc          = spc;
    g_num_fats     = num_fats;
    g_root_count   = root_cnt;
    g_fat_size     = fat_size;
    g_fat_lba      = reserved;
    g_root_lba     = (u16)(g_fat_lba + g_num_fats * g_fat_size);
    g_root_sectors = (u16)((root_cnt * 32 + 511) / 512);
    g_data_lba     = (u16)(g_root_lba + g_root_sectors);

    g_initialized = 1;
    return 0;
}

/* ============================================================
 * FAT entry access
 * ============================================================ */

/* Read FAT entry for cluster c.  Returns 0xFFFF on I/O error. */
static u16 fat_get(u16 cluster)
{
    u16 sector_off = (u16)(cluster / 256);
    u16 entry_idx  = (u16)(cluster % 256);
    u32 lba = (u32)(g_fat_lba + sector_off);

    if (ata_read_sector(lba, g_sec0) < 0)
        return 0xFFFF;

    return rd16((u8 *)g_sec0 + entry_idx * 2);
}

/* Write val into FAT entry for cluster c (updates every FAT copy). */
static int fat_set(u16 cluster, u16 val)
{
    u16 sector_off = (u16)(cluster / 256);
    u16 entry_idx  = (u16)(cluster % 256);

    for (u8 fat = 0; fat < g_num_fats; fat++) {
        u32 lba = (u32)(g_fat_lba + (u32)fat * g_fat_size + sector_off);
        if (ata_read_sector(lba, g_sec0) < 0) return -1;
        wr16((u8 *)g_sec0 + entry_idx * 2, val);
        if (ata_write_sector(lba, g_sec0) < 0) return -1;
    }
    return 0;
}

/*
 * Scan the FAT for a free cluster (entry == 0x0000).
 * Marks it as end-of-chain (0xFFFF) and returns its number.
 * Returns 0 on failure (cluster 0 is never a valid data cluster).
 */
static u16 fat_alloc(void)
{
    for (u16 s = 0; s < g_fat_size; s++) {
        u32 lba = (u32)(g_fat_lba + s);
        if (ata_read_sector(lba, g_sec0) < 0)
            return 0;

        u8 *p = (u8 *)g_sec0;
        for (int i = 0; i < 256; i++) {
            u16 entry   = rd16(p + i * 2);
            u16 cluster = (u16)(s * 256 + i);
            if (entry == 0x0000 && cluster >= 2) {
                if (fat_set(cluster, 0xFFFF) < 0)
                    return 0;
                return cluster;
            }
        }
    }
    return 0;  /* disk full */
}

/* Free the cluster chain starting at cluster (mark all entries as 0x0000). */
static void free_cluster_chain(u16 cluster)
{
    while (cluster >= 2 && cluster < 0xFFF0) {
        u16 next = fat_get(cluster);
        fat_set(cluster, 0x0000);
        cluster = next;
    }
}

/* ============================================================
 * Directory entry attribute flags
 * ============================================================ */

#define FAT_ATTR_ARCHIVE  0x20
#define FAT_ATTR_DIR      0x10
#define FAT_ATTR_VOLUME   0x08
#define FAT_ATTR_LFN      0x0F  /* long filename meta-entry — skip it */

/* ============================================================
 * Directory access helpers
 * ============================================================ */

#define DIR_MAX_SECTORS 256

/*
 * Return the LBA of the n-th sector in directory 'cluster' (0 = root).
 * For root: sectors g_root_lba .. g_root_lba + g_root_sectors - 1.
 * For subdirs: follows the FAT cluster chain.
 * Returns 0 when out of range or chain exhausted.
 */
static u32 dir_sector_lba(u16 cluster, u32 n)
{
    if (cluster == 0) {
        if (n >= g_root_sectors) return 0;
        return (u32)(g_root_lba + n);
    }

    /* Walk FAT chain to find the right cluster */
    u32 spc     = g_spc;
    u32 cl_idx  = n / spc;
    u32 sec_in  = n % spc;
    u16 cl      = cluster;

    for (u32 i = 0; i < cl_idx; i++) {
        cl = fat_get(cl);
        if (cl < 2 || cl >= 0xFFF0) return 0;
    }

    return (u32)(g_data_lba + (u32)(cl - 2) * spc + sec_in);
}

/*
 * Return 1 if directory cluster is empty (contains only . and .. entries),
 * 0 if non-empty, 0 on I/O error.
 */
static int dir_is_empty(u16 cluster)
{
    for (u32 s = 0; s < DIR_MAX_SECTORS; s++) {
        u32 lba = dir_sector_lba(cluster, s);
        if (!lba) return 1;
        if (ata_read_sector(lba, g_sec0) < 0) return 0;

        u8 *p = (u8 *)g_sec0;
        for (int e = 0; e < 16; e++) {
            u8 *ent = p + e * 32;
            if (ent[0] == 0x00) return 1;      /* end of entries */
            if (ent[0] == 0xE5) continue;      /* deleted */
            u8 attr = ent[11];
            if (attr == FAT_ATTR_LFN || (attr & FAT_ATTR_VOLUME)) continue;
            /* Skip . and .. (both start with '.') */
            if (ent[0] == '.') continue;
            return 0;   /* found a real entry */
        }
    }
    return 1;
}

/* ============================================================
 * 8.3 filename helpers
 * ============================================================ */

/*
 * Convert 11-byte FAT name (e.g. "HELLO   TXT") to a C string
 * (e.g. "hello.txt").  dst must have room for at least 13 bytes.
 * Always lowercases — the on-disk bytes are uppercase (FAT spec),
 * but we display everything lowercase.
 */
static void fat83_to_str(const u8 *fat_name, char *dst)
{
    int i, j = 0;

    for (i = 0; i < 8 && fat_name[i] != ' '; i++) {
        u8 c = fat_name[i];
        if (c >= 'A' && c <= 'Z') c = (u8)(c + 32);
        dst[j++] = (char)c;
    }

    if (fat_name[8] != ' ') {
        dst[j++] = '.';
        for (i = 8; i < 11 && fat_name[i] != ' '; i++) {
            u8 c = fat_name[i];
            if (c >= 'A' && c <= 'Z') c = (u8)(c + 32);
            dst[j++] = (char)c;
        }
    }
    dst[j] = '\0';
}

/*
 * Convert a C filename string (e.g. "HELLO.TXT") to an 11-byte FAT
 * name (e.g. "HELLO   TXT"), uppercased.
 * out must be an 11-byte buffer; filled with spaces first.
 */
static void str_to_fat83(const char *name, u8 *out)
{
    int i, j;

    for (i = 0; i < 11; i++)
        out[i] = ' ';

    i = 0;
    j = 0;
    while (name[i] && name[i] != '.' && j < 8) {
        u8 c = (u8)name[i++];
        if (c >= 'a' && c <= 'z') c = (u8)(c - 32);
        out[j++] = c;
    }
    if (name[i] == '.') {
        i++;
        j = 8;
        while (name[i] && j < 11) {
            u8 c = (u8)name[i++];
            if (c >= 'a' && c <= 'z') c = (u8)(c - 32);
            out[j++] = c;
        }
    }
}

/* Return 1 if two 11-byte FAT names are equal, 0 otherwise. */
static int fat83_match(const u8 *a, const u8 *b)
{
    for (int i = 0; i < 11; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

/* ============================================================
 * fat16_listdir — iterate cwd, call cb for each file and directory.
 * Directories are passed with is_dir=1; . and .. are skipped.
 * Returns 0 on success, -1 on I/O error.
 * ============================================================ */

int fat16_listdir(void (*cb)(const char *name, unsigned int size, int is_dir))
{
    if (!g_initialized) return -1;

    for (u32 s = 0; s < DIR_MAX_SECTORS; s++) {
        u32 lba = dir_sector_lba(g_cwd_cluster, s);
        if (!lba) return 0;
        if (ata_read_sector(lba, g_sec0) < 0) return -1;

        u8 *p = (u8 *)g_sec0;
        for (int e = 0; e < 16; e++) {
            u8 *ent = p + e * 32;

            if (ent[0] == 0x00) return 0;   /* end of directory */
            if (ent[0] == 0xE5) continue;   /* deleted entry    */

            u8 attr = ent[11];
            if (attr == FAT_ATTR_LFN)      continue;
            if (attr & FAT_ATTR_VOLUME)    continue;

            char name_str[13];
            fat83_to_str(ent, name_str);

            if (attr & FAT_ATTR_DIR) {
                /* Skip . and .. */
                if (name_str[0] == '.' &&
                    (name_str[1] == '\0' || (name_str[1] == '.' && name_str[2] == '\0')))
                    continue;
                cb(name_str, 0, 1);
            } else {
                cb(name_str, (unsigned int)rd32(ent + 28), 0);
            }
        }
    }
    return 0;
}

/* ============================================================
 * fat16_read — read a named file from the cwd into buf.
 * Returns number of bytes read, or -1 on error / file not found.
 * ============================================================ */

int fat16_read(const char *filename, unsigned char *buf, unsigned int max_bytes)
{
    if (!g_initialized) return -1;

    u8 fat_name[11];
    str_to_fat83(filename, fat_name);

    /* Search cwd for the file */
    u16 first_cluster = 0;
    u32 file_size     = 0;
    int found         = 0;

    for (u32 s = 0; !found; s++) {
        u32 lba = dir_sector_lba(g_cwd_cluster, s);
        if (!lba) break;
        if (ata_read_sector(lba, g_sec0) < 0) return -1;

        u8 *p = (u8 *)g_sec0;
        for (int e = 0; e < 16; e++) {
            u8 *ent = p + e * 32;

            if (ent[0] == 0x00) goto read_not_found;
            if (ent[0] == 0xE5) continue;

            u8 attr = ent[11];
            if (attr == FAT_ATTR_LFN)                 continue;
            if (attr & (FAT_ATTR_DIR | FAT_ATTR_VOLUME)) continue;

            if (fat83_match(ent, fat_name)) {
                first_cluster = rd16(ent + 26);
                file_size     = rd32(ent + 28);
                found = 1;
                break;
            }
        }
    }

read_not_found:
    if (!found)            return -1;
    if (first_cluster < 2) return 0;   /* empty file */

    /* Follow cluster chain and copy data */
    unsigned int bytes_read = 0;
    u16 cluster = first_cluster;

    while (cluster >= 2 && cluster < 0xFFF0 && bytes_read < max_bytes) {
        u32 clus_lba = (u32)(g_data_lba + (u32)(cluster - 2) * g_spc);

        for (u8 si = 0; si < g_spc && bytes_read < max_bytes; si++) {
            if (ata_read_sector(clus_lba + si, g_sec0) < 0) return -1;

            u8 *p = (u8 *)g_sec0;
            unsigned int to_copy = 512;
            if (bytes_read + to_copy > max_bytes)  to_copy = max_bytes  - bytes_read;
            if (bytes_read + to_copy > file_size)  to_copy = (unsigned int)(file_size - bytes_read);

            for (unsigned int i = 0; i < to_copy; i++)
                buf[bytes_read + i] = p[i];
            bytes_read += to_copy;
        }
        cluster = fat_get(cluster);
    }

    return (int)bytes_read;
}

/* ============================================================
 * fat16_write — create or overwrite a named file in the cwd.
 * Returns 0 on success, -1 on error.
 * ============================================================ */

int fat16_write(const char *filename, const unsigned char *data, unsigned int size)
{
    if (!g_initialized) return -1;

    u8 fat_name[11];
    str_to_fat83(filename, fat_name);

    /*
     * Scan cwd using g_sec1 (keeps it clear of FAT ops in g_sec0).
     * We look for:
     *   - An existing entry with the same name  → reuse it (free old clusters)
     *   - The first free slot (0x00 or 0xE5)    → use it if no existing entry
     */
    int dir_sec  = -1;
    int dir_ent  = -1;
    int free_sec = -1;
    int free_ent = -1;

    for (u32 s = 0; s < DIR_MAX_SECTORS; s++) {
        u32 lba = dir_sector_lba(g_cwd_cluster, s);
        if (!lba) break;
        if (ata_read_sector(lba, g_sec1) < 0) return -1;

        u8 *p = (u8 *)g_sec1;
        for (int e = 0; e < 16; e++) {
            u8 *ent = p + e * 32;

            if (ent[0] == 0x00 || ent[0] == 0xE5) {
                if (free_sec < 0) { free_sec = (int)s; free_ent = e; }
                if (ent[0] == 0x00) goto dir_scan_done;  /* no more entries */
                continue;
            }

            u8 attr = ent[11];
            if (attr == FAT_ATTR_LFN)                    continue;
            if (attr & (FAT_ATTR_DIR | FAT_ATTR_VOLUME)) continue;

            if (fat83_match(ent, fat_name)) {
                /* Free existing cluster chain before overwriting */
                free_cluster_chain(rd16(ent + 26));
                dir_sec = (int)s;
                dir_ent = e;
                goto dir_scan_done;
            }
        }
    }

dir_scan_done:
    if (dir_ent < 0) {
        /* No existing entry — use a free slot */
        if (free_sec < 0) return -1;  /* directory is full */
        dir_sec = free_sec;
        dir_ent = free_ent;
    }

    /* Allocate clusters and write data */
    u16 first_cluster = 0;
    u16 prev_cluster  = 0;
    unsigned int written = 0;

    while (written < size) {
        u16 c = fat_alloc();
        if (c == 0) return -1;  /* disk full */

        if (first_cluster == 0) first_cluster = c;
        if (prev_cluster  != 0) fat_set(prev_cluster, c);  /* link chain */
        /* fat_alloc already wrote FAT[c] = 0xFFFF (end of chain) */

        u32 clus_lba = (u32)(g_data_lba + (u32)(c - 2) * g_spc);

        for (u8 si = 0; si < g_spc; si++) {
            u8 *p = (u8 *)g_sec0;

            /* Zero-fill buffer, then copy remaining data (partial last sector) */
            for (int i = 0; i < 512; i++) p[i] = 0;
            if (written < size) {
                unsigned int to_copy = size - written;
                if (to_copy > 512) to_copy = 512;
                for (unsigned int i = 0; i < to_copy; i++)
                    p[i] = data[written + i];
                written += to_copy;
            }

            if (ata_write_sector(clus_lba + si, g_sec0) < 0) return -1;
        }
        prev_cluster = c;
    }

    /* Write directory entry (re-read sector so FAT ops don't alias g_sec1) */
    u32 lba = dir_sector_lba(g_cwd_cluster, (u32)dir_sec);
    if (ata_read_sector(lba, g_sec1) < 0) return -1;

    u8 *ent = (u8 *)g_sec1 + dir_ent * 32;
    for (int i = 0; i < 32; i++) ent[i] = 0;

    for (int i = 0; i < 11; i++) ent[i] = fat_name[i];
    ent[11] = FAT_ATTR_ARCHIVE;
    wr16(ent + 26, first_cluster);
    wr32(ent + 28, size);

    if (ata_write_sector(lba, g_sec1) < 0) return -1;
    return 0;
}

/* ============================================================
 * fat16_delete — delete a file or empty directory from the cwd.
 * Returns  0 on success,
 *         -1 if not found or I/O error,
 *         -2 if directory is not empty.
 * ============================================================ */

int fat16_delete(const char *name)
{
    if (!g_initialized) return -1;

    u8 fat_name[11];
    str_to_fat83(name, fat_name);

    for (u32 s = 0; s < DIR_MAX_SECTORS; s++) {
        u32 lba = dir_sector_lba(g_cwd_cluster, s);
        if (!lba) return -1;
        if (ata_read_sector(lba, g_sec1) < 0) return -1;

        u8 *p = (u8 *)g_sec1;
        for (int e = 0; e < 16; e++) {
            u8 *ent = p + e * 32;

            if (ent[0] == 0x00) return -1;   /* end of dir, not found */
            if (ent[0] == 0xE5) continue;

            u8 attr = ent[11];
            if (attr == FAT_ATTR_LFN || (attr & FAT_ATTR_VOLUME)) continue;
            if (ent[0] == '.') continue;      /* skip . and .. */

            if (!fat83_match(ent, fat_name)) continue;

            /* Found — check if it's a non-empty directory */
            if (attr & FAT_ATTR_DIR) {
                u16 dc = rd16(ent + 26);
                if (dc >= 2 && !dir_is_empty(dc)) return -2;
            }

            u16 cluster = rd16(ent + 26);
            ent[0] = 0xE5;  /* mark as deleted */
            if (ata_write_sector(lba, g_sec1) < 0) return -1;
            if (cluster >= 2) free_cluster_chain(cluster);
            return 0;
        }
    }
    return -1;
}

/* ============================================================
 * fat16_mkdir — create a subdirectory in the cwd.
 * Returns 0 on success, -1 on error (already exists, disk full, etc.).
 * ============================================================ */

int fat16_mkdir(const char *name)
{
    if (!g_initialized) return -1;

    u8 fat_name[11];
    str_to_fat83(name, fat_name);

    /* Scan cwd: check name doesn't exist, find free slot */
    int free_sec = -1, free_ent = -1;

    for (u32 s = 0; s < DIR_MAX_SECTORS; s++) {
        u32 lba = dir_sector_lba(g_cwd_cluster, s);
        if (!lba) break;
        if (ata_read_sector(lba, g_sec1) < 0) return -1;

        u8 *p = (u8 *)g_sec1;
        for (int e = 0; e < 16; e++) {
            u8 *ent = p + e * 32;

            if (ent[0] == 0x00 || ent[0] == 0xE5) {
                if (free_sec < 0) { free_sec = (int)s; free_ent = e; }
                if (ent[0] == 0x00) goto mkdir_scan_done;
                continue;
            }
            u8 attr = ent[11];
            if (attr == FAT_ATTR_LFN || (attr & FAT_ATTR_VOLUME)) continue;
            if (fat83_match(ent, fat_name)) return -1;  /* already exists */
        }
    }

mkdir_scan_done:
    if (free_sec < 0) return -1;  /* directory is full */

    /* Allocate a cluster for the new directory */
    u16 new_cluster = fat_alloc();
    if (!new_cluster) return -1;

    /* Write . and .. entries into first sector of the new cluster */
    u32 new_lba = (u32)(g_data_lba + (u32)(new_cluster - 2) * g_spc);
    u8 *p = (u8 *)g_sec0;
    for (int i = 0; i < 512; i++) p[i] = 0;

    /* . entry: points to itself */
    for (int i = 0; i < 11; i++) p[i] = ' ';
    p[0] = '.';
    p[11] = FAT_ATTR_DIR;
    wr16(p + 26, new_cluster);

    /* .. entry: points to parent */
    u8 *dotdot = p + 32;
    for (int i = 0; i < 11; i++) dotdot[i] = ' ';
    dotdot[0] = '.'; dotdot[1] = '.';
    dotdot[11] = FAT_ATTR_DIR;
    wr16(dotdot + 26, g_cwd_cluster);

    if (ata_write_sector(new_lba, g_sec0) < 0) {
        fat_set(new_cluster, 0x0000);
        return -1;
    }

    /* Zero out remaining sectors in the cluster */
    for (int i = 0; i < 512; i++) p[i] = 0;
    for (u8 si = 1; si < g_spc; si++) {
        if (ata_write_sector(new_lba + si, g_sec0) < 0) return -1;
    }

    /* Write directory entry in cwd (re-read sector after cluster alloc) */
    u32 dir_lba = dir_sector_lba(g_cwd_cluster, (u32)free_sec);
    if (ata_read_sector(dir_lba, g_sec1) < 0) return -1;

    u8 *ent = (u8 *)g_sec1 + free_ent * 32;
    for (int i = 0; i < 32; i++) ent[i] = 0;
    for (int i = 0; i < 11; i++) ent[i] = fat_name[i];
    ent[11] = FAT_ATTR_DIR;
    wr16(ent + 26, new_cluster);

    if (ata_write_sector(dir_lba, g_sec1) < 0) return -1;
    return 0;
}

/* ============================================================
 * fat16_rename — rename a file or directory within the cwd.
 * Cross-directory moves are not supported.
 * Returns 0 on success, -1 on error (not found, dst exists, etc.).
 * ============================================================ */

int fat16_rename(const char *src, const char *dst)
{
    if (!g_initialized) return -1;

    u8 src_name[11], dst_name[11];
    str_to_fat83(src, src_name);
    str_to_fat83(dst, dst_name);

    int src_s = -1, src_e = -1;

    for (u32 s = 0; s < DIR_MAX_SECTORS; s++) {
        u32 lba = dir_sector_lba(g_cwd_cluster, s);
        if (!lba) break;
        if (ata_read_sector(lba, g_sec1) < 0) return -1;

        u8 *p = (u8 *)g_sec1;
        for (int e = 0; e < 16; e++) {
            u8 *ent = p + e * 32;

            if (ent[0] == 0x00) goto rename_scan_done;
            if (ent[0] == 0xE5) continue;

            u8 attr = ent[11];
            if (attr == FAT_ATTR_LFN || (attr & FAT_ATTR_VOLUME)) continue;
            if (ent[0] == '.') continue;   /* skip . and .. */

            if (fat83_match(ent, dst_name)) return -1;   /* dst already exists */
            if (fat83_match(ent, src_name)) { src_s = (int)s; src_e = e; }
        }
    }

rename_scan_done:
    if (src_s < 0) return -1;  /* src not found */

    /* Re-read the sector containing src and update the name in-place */
    u32 lba = dir_sector_lba(g_cwd_cluster, (u32)src_s);
    if (ata_read_sector(lba, g_sec1) < 0) return -1;
    u8 *ent = (u8 *)g_sec1 + src_e * 32;
    for (int i = 0; i < 11; i++) ent[i] = dst_name[i];
    if (ata_write_sector(lba, g_sec1) < 0) return -1;
    return 0;
}

/* ============================================================
 * fat16_chdir — change the current working directory.
 * name: directory name, ".." to go up, "/" to go to root.
 * Returns 0 on success, -1 if not found or not a directory.
 * ============================================================ */

int fat16_chdir(const char *name)
{
    if (!g_initialized) return -1;
    if (!name || !name[0]) return 0;

    /* cd / → root */
    if (name[0] == '/' && !name[1]) {
        g_cwd_cluster = 0;
        return 0;
    }

    /* cd .. → parent */
    if (name[0] == '.' && name[1] == '.' && !name[2]) {
        if (g_cwd_cluster == 0) return 0;  /* already at root */
        /* Read the .. entry (second 32-byte entry in first sector) */
        u32 lba = dir_sector_lba(g_cwd_cluster, 0);
        if (!lba) return -1;
        if (ata_read_sector(lba, g_sec1) < 0) return -1;
        u8 *dotdot = (u8 *)g_sec1 + 32;
        g_cwd_cluster = rd16(dotdot + 26);
        return 0;
    }

    /* cd . → no-op */
    if (name[0] == '.' && !name[1]) return 0;

    /* Regular name: find directory entry */
    u8 fat_name[11];
    str_to_fat83(name, fat_name);

    for (u32 s = 0; s < DIR_MAX_SECTORS; s++) {
        u32 lba = dir_sector_lba(g_cwd_cluster, s);
        if (!lba) return -1;
        if (ata_read_sector(lba, g_sec1) < 0) return -1;

        u8 *p = (u8 *)g_sec1;
        for (int e = 0; e < 16; e++) {
            u8 *ent = p + e * 32;

            if (ent[0] == 0x00) return -1;   /* not found */
            if (ent[0] == 0xE5) continue;

            u8 attr = ent[11];
            if (attr == FAT_ATTR_LFN || (attr & FAT_ATTR_VOLUME)) continue;
            if (!(attr & FAT_ATTR_DIR)) continue;

            if (fat83_match(ent, fat_name)) {
                g_cwd_cluster = rd16(ent + 26);
                return 0;
            }
        }
    }
    return -1;
}

/* ============================================================
 * fat16_read_from_root — like fat16_read but always searches the
 * root directory, regardless of cwd.  Used by the program loader
 * so that `run prog` works from any subdirectory.
 * ============================================================ */

int fat16_read_from_root(const char *filename, unsigned char *buf, unsigned int max_bytes)
{
    u16 saved = g_cwd_cluster;
    g_cwd_cluster = 0;
    int r = fat16_read(filename, buf, max_bytes);
    g_cwd_cluster = saved;
    return r;
}

/* ============================================================
 * fat16_read_from_bin — read a file from the /bin subdirectory.
 * Temporarily changes cwd to /bin, reads the file, restores cwd.
 * ============================================================ */

int fat16_read_from_bin(const char *name, unsigned char *buf, unsigned int max_bytes)
{
    u16 saved = g_cwd_cluster;
    g_cwd_cluster = 0;
    int r = fat16_chdir("bin");
    if (r < 0) { g_cwd_cluster = saved; return -1; }
    int n = fat16_read(name, buf, max_bytes);
    g_cwd_cluster = saved;
    return n;
}
